#include "selection/selection_undo.h"
#include "command.h"
#include "commands/command_selection.h"
#include "document.h"
#include "selection/selection_mask.h"
#include "ui.h"
#include <glib.h>
#include <lz4.h>
#include <string.h>

/**
 * Internal transaction state
 */
struct _SelectionUndoTransaction {
    SelectionMask* mask;               /* Selection mask being tracked */
    ImageDocument* doc;                /* Document reference */
    gchar* operation_name;             /* Operation name for command */
    SelectionUndoDelta* initial_delta; /* Captures "before" state */
    gint min_region_x, min_region_y;   /* Accumulated bounding box */
    gint max_region_x, max_region_y;
    gboolean has_regions; /* TRUE if any regions registered */
};

/* Forward declarations for serialization functions */
static guint selection_serialize(Selection* sel, int mask_width, int mask_height, int stride, guint8** out_data);
static guint selection_deserialize(const guint8* data, guint data_size, int mask_width, int mask_height, int stride, Selection** out_sel);
static guint selections_list_serialize(GList* selections, int mask_width, int mask_height, int stride, guint8** out_data);
static GList* selections_list_deserialize(const guint8* data, guint data_size, int mask_width, int mask_height, int stride);

/**
 * Create a selection undo delta for a rectangular region
 * Captures the current mask state for the given region
 */
SelectionUndoDelta* selection_undo_delta_create(
    SelectionMask* mask,
    gint region_x,
    gint region_y,
    gint region_width,
    gint region_height) {
    SelectionUndoDelta* delta;
    guint data_size;
    uint8_t* temp_buffer;
    int y;

    if (!mask || region_width <= 0 || region_height <= 0) {
        return NULL;
    }

    /* Bounds check */
    if (region_x < 0 || region_y < 0 ||
        region_x >= mask->width || region_y >= mask->height) {
        return NULL;
    }

    /* Clamp region to mask bounds */
    if (region_x + region_width > mask->width) {
        region_width = mask->width - region_x;
    }
    if (region_y + region_height > mask->height) {
        region_height = mask->height - region_y;
    }

    delta = g_new0(SelectionUndoDelta, 1);
    delta->region_x = region_x;
    delta->region_y = region_y;
    delta->region_width = region_width;
    delta->region_height = region_height;
    delta->is_compressed = FALSE;

    /* Serialize entire selections list as objects (state-based undo for selections) */
    delta->selections_before_size = selections_list_serialize(mask->selections, mask->width, mask->height, mask->stride, &delta->selections_before);

    /* Allocate buffer for mask region data */
    /* Each row is stored contiguously, but we use simple row-by-row copy */
    data_size = region_width * region_height;

    delta->mask_before = g_malloc(data_size);
    delta->mask_after = g_malloc(data_size);

    if (!delta->mask_before || !delta->mask_after) {
        selection_undo_delta_free(delta);
        return NULL;
    }

    /* Copy region from mask */
    temp_buffer = delta->mask_before;
    for (y = 0; y < region_height; y++) {
        uint8_t* src_row = mask->base_mask + (region_y + y) * mask->stride + region_x;
        memcpy(temp_buffer, src_row, region_width);
        temp_buffer += region_width;
    }

    /* mask_after will be filled by caller or during commit */

    return delta;
}

/**
 * Serialize a Selection object to a buffer
 * Format: x(4), y(4), width(4), height(4), combine_mode(4), feather_mode(4), feather_radius(4), mask_size(4), mask_data(mask_size)
 */
static guint selection_serialize(Selection* sel, int mask_width, int mask_height, int stride, guint8** out_data) {
    guint8* buffer;
    guint pos = 0;
    guint mask_size = stride * mask_height;

    if (!sel || !out_data) {
        return 0;
    }

    /* Allocate buffer: header (32 bytes: x,y,width,height,combine_mode,feather_mode,feather_radius,mask_size) + mask data */
    guint total_size = 32 + mask_size;
    buffer = g_malloc(total_size);
    if (!buffer) {
        *out_data = NULL;
        return 0;
    }

    /* Serialize header */
    memcpy(buffer + pos, &sel->x, 4);
    pos += 4;
    memcpy(buffer + pos, &sel->y, 4);
    pos += 4;
    memcpy(buffer + pos, &sel->width, 4);
    pos += 4;
    memcpy(buffer + pos, &sel->height, 4);
    pos += 4;
    gint combine_mode = (gint)sel->combine_mode;
    memcpy(buffer + pos, &combine_mode, 4);
    pos += 4;
    gint feather_mode = (gint)sel->feather_mode;
    memcpy(buffer + pos, &feather_mode, 4);
    pos += 4;
    memcpy(buffer + pos, &sel->feather_radius, 4);
    pos += 4;
    memcpy(buffer + pos, &mask_size, 4);
    pos += 4;

    /* Serialize mask data */
    if (sel->mask) {
        memcpy(buffer + pos, sel->mask, mask_size);
    } else {
        memset(buffer + pos, 0, mask_size);
    }
    pos += mask_size;

    *out_data = buffer;
    return pos;
}

/**
 * Deserialize a Selection object from a buffer
 * Returns the number of bytes consumed, or 0 on error
 */
static guint selection_deserialize(const guint8* data, guint data_size, int mask_width, int mask_height, int stride, Selection** out_sel) {
    guint pos = 0;
    gint x, y, width, height, combine_mode, feather_mode;
    gfloat feather_radius;
    guint mask_size;

    if (!data || data_size < 32 || !out_sel) {
        return 0;
    }

    /* Deserialize header */
    memcpy(&x, data + pos, 4);
    pos += 4;
    memcpy(&y, data + pos, 4);
    pos += 4;
    memcpy(&width, data + pos, 4);
    pos += 4;
    memcpy(&height, data + pos, 4);
    pos += 4;
    memcpy(&combine_mode, data + pos, 4);
    pos += 4;
    memcpy(&feather_mode, data + pos, 4);
    pos += 4;
    memcpy(&feather_radius, data + pos, 4);
    pos += 4;
    memcpy(&mask_size, data + pos, 4);
    pos += 4;

    /* Validate mask size */
    guint expected_mask_size = stride * mask_height;
    if (mask_size != expected_mask_size || pos + mask_size > data_size) {
        return 0;
    }

    /* Create Selection object */
    Selection* sel = selection_new(x, y, width, height,
                                   (SelectionCombineMode)combine_mode,
                                   (SelectionSmoothingMode)feather_mode,
                                   feather_radius);
    if (!sel) {
        return 0;
    }

    /* Allocate and copy mask data */
    sel->mask = g_malloc0(mask_size);
    memcpy(sel->mask, data + pos, mask_size);
    pos += mask_size;

    *out_sel = sel;
    return pos;
}

/**
 * Serialize entire selections list
 */
static guint selections_list_serialize(GList* selections, int mask_width, int mask_height, int stride, guint8** out_data) {
    guint8* buffer;
    guint pos = 0;
    guint count = 0;
    GList* iter;

    if (!out_data) {
        return 0;
    }

    /* First pass: count selections and calculate total size */
    for (iter = selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel) {
            count++;
        }
    }

    /* Allocate buffer: count(4) + serialized selections */
    guint total_size = 4;
    for (iter = selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel) {
            guint sel_expected_size = 32 + (stride * mask_height); /* Header (32 bytes) + mask */
            total_size += sel_expected_size;
        }
    }

    buffer = g_malloc(total_size);
    if (!buffer) {
        *out_data = NULL;
        return 0;
    }

    /* Write count */
    memcpy(buffer + pos, &count, 4);
    pos += 4;

    /* Serialize each selection */
    for (iter = selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel) {
            guint8* sel_data = NULL;
            guint sel_size = selection_serialize(sel, mask_width, mask_height, stride, &sel_data);
            if (sel_size > 0 && sel_data) {
                if (pos + sel_size > total_size) {
                    /* Free buffer and return error */
                    g_free(buffer);
                    *out_data = NULL;
                    if (sel_data) {
                        g_free(sel_data);
                    }
                    return 0;
                }
                memcpy(buffer + pos, sel_data, sel_size);
                pos += sel_size;
            } else {
                /* Free buffer and return error */
                if (buffer) {
                    g_free(buffer);
                }
                *out_data = NULL;
                return 0;
            }
            if (sel_data) {
                g_free(sel_data);
            }
        }
    }

    *out_data = buffer;
    return pos;
}

/**
 * Deserialize entire selections list
 */
static GList* selections_list_deserialize(const guint8* data, guint data_size, int mask_width, int mask_height, int stride) {
    guint pos = 0;
    guint count = 0;
    GList* selections = NULL;

    if (!data || data_size < 4) {
        return NULL;
    }

    /* Read count */
    memcpy(&count, data + pos, 4);
    pos += 4;

    /* Deserialize each selection */
    for (guint i = 0; i < count && pos < data_size; i++) {
        Selection* sel = NULL;
        guint consumed = selection_deserialize(data + pos, data_size - pos, mask_width, mask_height, stride, &sel);
        if (consumed > 0 && sel) {
            selections = g_list_append(selections, sel);
            pos += consumed;
        } else {
            /* Error deserializing - free what we have and return NULL */
            if (selections) {
                for (GList* iter = selections; iter != NULL; iter = iter->next) {
                    Selection* s = (Selection*)iter->data;
                    if (s) {
                        selection_unref(s);
                    }
                }
                g_list_free(selections);
            }
            return NULL;
        }
    }

    return selections;
}

/**
 * Free a selection undo delta and its buffers
 */
void selection_undo_delta_free(SelectionUndoDelta* delta) {
    if (!delta) {
        return;
    }

    if (delta->mask_before) {
        g_free(delta->mask_before);
    }
    if (delta->mask_after) {
        g_free(delta->mask_after);
    }
    if (delta->selections_before) {
        g_free(delta->selections_before);
    }
    if (delta->selections_after) {
        g_free(delta->selections_after);
    }

    g_free(delta);
}

/**
 * Begin a selection undo transaction
 * Captures the current selection mask state (used as "before" for delta)
 */
SelectionUndoTransaction* selection_undo_transaction_begin(
    SelectionMask* mask,
    ImageDocument* doc,
    const gchar* operation_name) {
    SelectionUndoTransaction* transaction;

    if (!mask || !doc || !operation_name) {
        return NULL;
    }

    transaction = g_new0(SelectionUndoTransaction, 1);
    transaction->mask = mask;
    transaction->doc = doc;
    transaction->operation_name = g_strdup(operation_name);
    transaction->has_regions = FALSE;

    /* Initialize bounding box to invalid state */
    transaction->min_region_x = G_MAXINT;
    transaction->min_region_y = G_MAXINT;
    transaction->max_region_x = G_MININT;
    transaction->max_region_y = G_MININT;

    return transaction;
}

/**
 * Register a region as modified in the transaction
 * Merges multiple regions into a single bounding box
 */
gboolean selection_undo_transaction_register_region(
    SelectionUndoTransaction* transaction,
    gint region_x,
    gint region_y,
    gint region_width,
    gint region_height) {
    gint region_right, region_bottom;

    if (!transaction || region_width <= 0 || region_height <= 0) {
        return FALSE;
    }

    /* Clamp to mask bounds */
    if (region_x < 0) {
        region_width += region_x;
        region_x = 0;
    }
    if (region_y < 0) {
        region_height += region_y;
        region_y = 0;
    }

    if (region_width <= 0 || region_height <= 0) {
        return FALSE;
    }

    if (region_x >= transaction->mask->width || region_y >= transaction->mask->height) {
        return FALSE;
    }

    region_right = region_x + region_width;
    region_bottom = region_y + region_height;

    if (region_right > transaction->mask->width) {
        region_right = transaction->mask->width;
    }
    if (region_bottom > transaction->mask->height) {
        region_bottom = transaction->mask->height;
    }

    /* Update bounding box */
    if (!transaction->has_regions) {
        transaction->min_region_x = region_x;
        transaction->min_region_y = region_y;
        transaction->max_region_x = region_right;
        transaction->max_region_y = region_bottom;
        transaction->has_regions = TRUE;

        /* Capture initial "before" state when first region is registered */
        if (!transaction->initial_delta) {
            transaction->initial_delta = selection_undo_delta_create(
                transaction->mask,
                region_x,
                region_y,
                region_right - region_x,
                region_bottom - region_y);
        }
    } else {
        if (region_x < transaction->min_region_x) {
            transaction->min_region_x = region_x;
        }
        if (region_y < transaction->min_region_y) {
            transaction->min_region_y = region_y;
        }
        if (region_right > transaction->max_region_x) {
            transaction->max_region_x = region_right;
        }
        if (region_bottom > transaction->max_region_y) {
            transaction->max_region_y = region_bottom;
        }
    }

    return TRUE;
}

/**
 * Commit a selection undo transaction
 * Captures the "after" state and creates a command for undo/redo
 */
Command* selection_undo_transaction_commit(SelectionUndoTransaction* transaction) {
    SelectionUndoDelta* delta;
    Command* cmd;
    gint region_width, region_height;
    uint8_t* temp_buffer;
    int y;

    if (!transaction || !transaction->has_regions) {
        if (transaction) {
            g_free(transaction->operation_name);
            g_free(transaction);
        }
        return NULL;
    }

    /* Use the initial delta captured when region was registered */
    if (!transaction->initial_delta) {
        g_free(transaction->operation_name);
        g_free(transaction);
        return NULL;
    }

    delta = transaction->initial_delta;
    region_width = transaction->max_region_x - transaction->min_region_x;
    region_height = transaction->max_region_y - transaction->min_region_y;

    /* Copy "after" state from current mask */
    temp_buffer = delta->mask_after;
    for (y = 0; y < region_height; y++) {
        uint8_t* src_row = transaction->mask->base_mask +
                           (transaction->min_region_y + y) * transaction->mask->stride +
                           transaction->min_region_x;
        memcpy(temp_buffer, src_row, region_width);
        temp_buffer += region_width;
    }

    /* Serialize entire selections list after operation (state-based undo for selections) */
    delta->selections_after_size = selections_list_serialize(transaction->mask->selections, transaction->mask->width, transaction->mask->height, transaction->mask->stride, &delta->selections_after);

    /* Check if mask actually changed */
    if (memcmp(delta->mask_before, delta->mask_after, region_width * region_height) == 0) {
        /* No change, free delta and return NULL */
        selection_undo_delta_free(delta);
        g_free(transaction->operation_name);
        g_free(transaction);
        return NULL;
    }

    /* Create command */
    cmd = command_create_selection(transaction->mask, delta, transaction->doc, transaction->operation_name);

    g_free(transaction->operation_name);
    g_free(transaction);

    return cmd;
}

/**
 * Cancel a selection undo transaction without creating a command
 */
void selection_undo_transaction_cancel(SelectionUndoTransaction* transaction) {
    if (!transaction) {
        return;
    }

    if (transaction->operation_name) {
        g_free(transaction->operation_name);
    }

    if (transaction->initial_delta) {
        selection_undo_delta_free(transaction->initial_delta);
    }

    g_free(transaction);
}

/**
 * Apply "before" mask state
 */
gboolean selection_undo_apply_before(SelectionMask* mask, SelectionUndoDelta* delta) {
    guint8* src;
    int y;

    if (!mask || !delta || !delta->mask_before) {
        return FALSE;
    }

    if (delta->region_width <= 0 || delta->region_height <= 0) {
        return FALSE;
    }

    /* Restore selections list as objects (state-based undo for selections) */
    if (mask->selections) {
        GList* iter;
        for (iter = mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(mask->selections);
        mask->selections = NULL;
    }

    if (delta->selections_before && delta->selections_before_size >= 4) {
        /* Check if we have a valid serialized selections list (at least the count field) */
        guint count = 0;
        memcpy(&count, delta->selections_before, 4);

        /* Try to deserialize selections list (only if count > 0, otherwise empty list) */
        GList* restored_selections = NULL;
        if (count > 0) {
            restored_selections = selections_list_deserialize(delta->selections_before, delta->selections_before_size, mask->width, mask->height, mask->stride);
            if (!restored_selections) {
                /* Deserialization failed - fall through to backward compatibility */
                goto fallback_before;
            }
        }
        /* If count == 0, restored_selections stays NULL (empty list is valid) */

        /* Success: restore selections (NULL for empty list is valid) */
        mask->selections = restored_selections;

        if (mask->selections) {
            /* Rebuild base_mask from restored selections */
            selection_mask_rebuild_from_selections(mask);
        } else {
            /* Empty selections list - clear base_mask and feathered preview */
            memset(mask->base_mask, 0, mask->stride * mask->height);
            if (mask->feathered_preview) {
                memset(mask->feathered_preview, 0, mask->stride * mask->height);
            }
            mask->data = mask->base_mask;
            mask->feather_dirty = FALSE;
        }

        /* Destroy cached surface to force rebuild */
        if (mask->surface) {
            cairo_surface_destroy(mask->surface);
            mask->surface = NULL;
        }

        /* Mark entire mask as dirty since selections might cover more than the delta region */
        selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
        return TRUE;
    }

fallback_before:
    /* Backward compatibility: if no selections were stored or deserialization failed */
    if (!mask->selections) {
        /* Fallback: restore base_mask region (region-based undo for pixels, backward compatibility) */
        src = delta->mask_before;
        for (y = 0; y < delta->region_height; y++) {
            uint8_t* dest_row = mask->base_mask + (delta->region_y + y) * mask->stride + delta->region_x;
            memcpy(dest_row, src, delta->region_width);
            src += delta->region_width;
        }
        /* No selections stored - check if base_mask has any selected pixels (backward compatibility) */
        gboolean has_selection = FALSE;
        for (int i = 0; i < mask->height * mask->stride; i++) {
            if (mask->base_mask[i] > 0) {
                has_selection = TRUE;
                break;
            }
        }

        if (has_selection) {
            /* Fallback: create a single selection from base_mask (no feathering in backward compatibility mode) */
            Selection* sel = selection_new(0, 0, mask->width, mask->height,
                                           SELECTION_COMBINE_NEW,
                                           SELECTION_SMOOTH_NONE,
                                           0.0f);
            if (sel) {
                int stride = mask->stride;
                sel->mask = g_malloc0(stride * mask->height);
                memcpy(sel->mask, mask->base_mask, stride * mask->height);
                mask->selections = g_list_append(mask->selections, selection_ref(sel));
                selection_unref(sel);
            }
        } else {
            /* No selection - ensure data pointer is set correctly */
            mask->data = mask->base_mask;
            mask->feather_dirty = FALSE;
        }
    }

    /* Mark mask as dirty for redraw */
    selection_mask_mark_dirty(mask, delta->region_x, delta->region_y,
                              delta->region_width, delta->region_height);

    return TRUE;
}

/**
 * Apply "after" mask state
 */
gboolean selection_undo_apply_after(SelectionMask* mask, SelectionUndoDelta* delta) {
    guint8* src;
    int y;

    if (!mask || !delta || !delta->mask_after) {
        return FALSE;
    }

    if (delta->region_width <= 0 || delta->region_height <= 0) {
        return FALSE;
    }

    /* Restore selections list as objects (state-based undo for selections) */
    if (mask->selections) {
        GList* iter;
        for (iter = mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(mask->selections);
        mask->selections = NULL;
    }

    if (delta->selections_after && delta->selections_after_size >= 4) {
        /* Check if we have a valid serialized selections list (at least the count field) */
        guint count = 0;
        memcpy(&count, delta->selections_after, 4);

        /* Try to deserialize selections list (only if count > 0, otherwise empty list) */
        GList* restored_selections = NULL;
        if (count > 0) {
            restored_selections = selections_list_deserialize(delta->selections_after, delta->selections_after_size, mask->width, mask->height, mask->stride);
            if (!restored_selections) {
                /* Deserialization failed - fall through to backward compatibility */
                goto fallback_after;
            }
        }
        /* If count == 0, restored_selections stays NULL (empty list is valid) */

        /* Success: restore selections (NULL for empty list is valid) */
        mask->selections = restored_selections;

        if (mask->selections) {
            /* Rebuild base_mask from restored selections */
            selection_mask_rebuild_from_selections(mask);
        } else {
            /* Empty selections list - clear base_mask and feathered preview */
            memset(mask->base_mask, 0, mask->stride * mask->height);
            if (mask->feathered_preview) {
                memset(mask->feathered_preview, 0, mask->stride * mask->height);
            }
            mask->data = mask->base_mask;
            mask->feather_dirty = FALSE;
        }

        /* Destroy cached surface to force rebuild */
        if (mask->surface) {
            cairo_surface_destroy(mask->surface);
            mask->surface = NULL;
        }

        /* Mark entire mask as dirty since selections might cover more than the delta region */
        selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
        return TRUE;
    }

fallback_after:
    /* Backward compatibility: if no selections were stored or deserialization failed */
    if (!mask->selections) {
        /* Fallback: restore base_mask region (region-based undo for pixels, backward compatibility) */
        src = delta->mask_after;
        for (y = 0; y < delta->region_height; y++) {
            uint8_t* dest_row = mask->base_mask + (delta->region_y + y) * mask->stride + delta->region_x;
            memcpy(dest_row, src, delta->region_width);
            src += delta->region_width;
        }
        /* No selections stored - check if base_mask has any selected pixels (backward compatibility) */
        gboolean has_selection = FALSE;
        for (int i = 0; i < mask->height * mask->stride; i++) {
            if (mask->base_mask[i] > 0) {
                has_selection = TRUE;
                break;
            }
        }

        if (has_selection) {
            /* Fallback: create a single selection from base_mask (no feathering in backward compatibility mode) */
            Selection* sel = selection_new(0, 0, mask->width, mask->height,
                                           SELECTION_COMBINE_NEW,
                                           SELECTION_SMOOTH_NONE,
                                           0.0f);
            if (sel) {
                int stride = mask->stride;
                sel->mask = g_malloc0(stride * mask->height);
                memcpy(sel->mask, mask->base_mask, stride * mask->height);
                mask->selections = g_list_append(mask->selections, selection_ref(sel));
                selection_unref(sel);
            }
        } else {
            /* No selection - ensure data pointer is set correctly */
            mask->data = mask->base_mask;
            mask->feather_dirty = FALSE;
        }
    }

    /* Mark mask as dirty for redraw */
    selection_mask_mark_dirty(mask, delta->region_x, delta->region_y,
                              delta->region_width, delta->region_height);

    return TRUE;
}

/**
 * Serialize a selection undo delta for disk storage
 * Format:
 *   region_x (int32), region_y (int32), region_width (int32), region_height (int32)
 *   compressed_size_before (uint32), compressed_size_after (uint32)
 *   [compressed mask_before data]
 *   [compressed mask_after data]
 */
gboolean selection_undo_delta_serialize(
    SelectionUndoDelta* delta,
    gint compression_level,
    guint8** out_data,
    guint* out_size) {
    guint8* buffer;
    guint buffer_pos;
    guint region_data_size;
    guint compressed_before_size, compressed_after_size;
    guint8 *compressed_before, *compressed_after;
    int max_compressed_size;

    if (!delta || !out_data || !out_size) {
        return FALSE;
    }

    region_data_size = delta->region_width * delta->region_height;
    max_compressed_size = LZ4_compressBound(region_data_size);

    if (max_compressed_size <= 0) {
        return FALSE;
    }

    /* Compress before data */
    compressed_before = g_malloc(max_compressed_size);
    compressed_before_size = LZ4_compress_fast(
        (const char*)delta->mask_before,
        (char*)compressed_before,
        region_data_size,
        max_compressed_size,
        compression_level);

    if (compressed_before_size == 0) {
        g_free(compressed_before);
        return FALSE;
    }

    /* Compress after data */
    compressed_after = g_malloc(max_compressed_size);
    compressed_after_size = LZ4_compress_fast(
        (const char*)delta->mask_after,
        (char*)compressed_after,
        region_data_size,
        max_compressed_size,
        compression_level);

    if (compressed_after_size == 0) {
        g_free(compressed_before);
        g_free(compressed_after);
        return FALSE;
    }

    /* Allocate output buffer */
    *out_size = 24 + compressed_before_size + compressed_after_size;
    buffer = g_malloc(*out_size);

    /* Write header */
    buffer_pos = 0;
    memcpy(buffer + buffer_pos, &delta->region_x, 4);
    buffer_pos += 4;
    memcpy(buffer + buffer_pos, &delta->region_y, 4);
    buffer_pos += 4;
    memcpy(buffer + buffer_pos, &delta->region_width, 4);
    buffer_pos += 4;
    memcpy(buffer + buffer_pos, &delta->region_height, 4);
    buffer_pos += 4;
    memcpy(buffer + buffer_pos, &compressed_before_size, 4);
    buffer_pos += 4;
    memcpy(buffer + buffer_pos, &compressed_after_size, 4);
    buffer_pos += 4;

    /* Write compressed data */
    memcpy(buffer + buffer_pos, compressed_before, compressed_before_size);
    buffer_pos += compressed_before_size;
    memcpy(buffer + buffer_pos, compressed_after, compressed_after_size);
    buffer_pos += compressed_after_size;

    g_free(compressed_before);
    g_free(compressed_after);

    *out_data = buffer;
    return TRUE;
}

/**
 * Deserialize a selection undo delta from disk storage
 */
gboolean selection_undo_delta_deserialize(
    const guint8* data,
    guint size,
    SelectionUndoDelta** out_delta) {
    SelectionUndoDelta* delta;
    guint buffer_pos;
    guint region_data_size;
    guint compressed_before_size, compressed_after_size;
    guint8 *decompressed_before, *decompressed_after;
    int decompressed_size;

    if (!data || size < 24 || !out_delta) {
        return FALSE;
    }

    delta = g_new0(SelectionUndoDelta, 1);
    buffer_pos = 0;

    /* Read header */
    memcpy(&delta->region_x, data + buffer_pos, 4);
    buffer_pos += 4;
    memcpy(&delta->region_y, data + buffer_pos, 4);
    buffer_pos += 4;
    memcpy(&delta->region_width, data + buffer_pos, 4);
    buffer_pos += 4;
    memcpy(&delta->region_height, data + buffer_pos, 4);
    buffer_pos += 4;
    memcpy(&compressed_before_size, data + buffer_pos, 4);
    buffer_pos += 4;
    memcpy(&compressed_after_size, data + buffer_pos, 4);
    buffer_pos += 4;

    /* Validate sizes */
    if (buffer_pos + compressed_before_size + compressed_after_size != size) {
        g_free(delta);
        return FALSE;
    }

    if (delta->region_width <= 0 || delta->region_height <= 0) {
        g_free(delta);
        return FALSE;
    }

    region_data_size = delta->region_width * delta->region_height;

    /* Decompress before data */
    decompressed_before = g_malloc(region_data_size);
    decompressed_size = LZ4_decompress_safe(
        (const char*)(data + buffer_pos),
        (char*)decompressed_before,
        compressed_before_size,
        region_data_size);

    if (decompressed_size != (int)region_data_size) {
        g_free(decompressed_before);
        g_free(delta);
        return FALSE;
    }

    buffer_pos += compressed_before_size;

    /* Decompress after data */
    decompressed_after = g_malloc(region_data_size);
    decompressed_size = LZ4_decompress_safe(
        (const char*)(data + buffer_pos),
        (char*)decompressed_after,
        compressed_after_size,
        region_data_size);

    if (decompressed_size != (int)region_data_size) {
        g_free(decompressed_before);
        g_free(decompressed_after);
        g_free(delta);
        return FALSE;
    }

    delta->mask_before = decompressed_before;
    delta->mask_after = decompressed_after;
    delta->is_compressed = FALSE; /* Now decompressed in memory */
    delta->compressed_size_before = compressed_before_size;
    delta->compressed_size_after = compressed_after_size;

    *out_delta = delta;
    return TRUE;
}

/**
 * Convenient helpers for standard selection operations
 */

/**
 * Create a command for selecting all
 */
Command* selection_command_create_select_all(SelectionMask* mask, ImageDocument* doc) {
    SelectionUndoTransaction* transaction;
    Command* cmd;

    if (!mask || !doc) {
        return NULL;
    }

    transaction = selection_undo_transaction_begin(mask, doc, command_get_name_string(CMD_NAME_SELECT_ALL));
    if (!transaction) {
        return NULL;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0, mask->width, mask->height);

    /* Perform the operation */
    /* Note: The actual operation (filling with 255) should be done BEFORE commit
     * This is a helper; the caller is responsible for the operation.
     * For consistency with tile_undo_transaction, we could have a separate step. */

    cmd = selection_undo_transaction_commit(transaction);
    return cmd;
}

/**
 * Create a command for deselecting all
 */
Command* selection_command_create_deselect_all(SelectionMask* mask, ImageDocument* doc) {
    SelectionUndoTransaction* transaction;
    Command* cmd;

    if (!mask || !doc) {
        return NULL;
    }

    transaction = selection_undo_transaction_begin(mask, doc, command_get_name_string(CMD_NAME_DESELECT_ALL));
    if (!transaction) {
        return NULL;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0, mask->width, mask->height);

    cmd = selection_undo_transaction_commit(transaction);
    return cmd;
}

/**
 * Create a command for inverting selection
 */
Command* selection_command_create_invert(SelectionMask* mask, ImageDocument* doc) {
    SelectionUndoTransaction* transaction;
    Command* cmd;

    if (!mask || !doc) {
        return NULL;
    }

    transaction = selection_undo_transaction_begin(mask, doc, command_get_name_string(CMD_NAME_INVERT_SELECTION));
    if (!transaction) {
        return NULL;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0, mask->width, mask->height);

    cmd = selection_undo_transaction_commit(transaction);
    return cmd;
}

/**
 * Create a command for feathering selection
 */
Command* selection_command_create_feather(SelectionMask* mask, ImageDocument* doc, float feather_radius) {
    SelectionUndoTransaction* transaction;
    Command* cmd;

    if (!mask || !doc || feather_radius <= 0) {
        return NULL;
    }

    transaction = selection_undo_transaction_begin(mask, doc, command_get_name_string(CMD_NAME_FEATHER_SELECTION));
    if (!transaction) {
        return NULL;
    }

    /* Register entire mask as modified (feathering affects all edges) */
    selection_undo_transaction_register_region(transaction, 0, 0, mask->width, mask->height);

    cmd = selection_undo_transaction_commit(transaction);
    return cmd;
}
