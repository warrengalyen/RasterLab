#include "commands/command_move.h"
#include "document.h"
#include "render/layer.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"

/**
 * Move selected pixels command data structure
 */
typedef struct {
    struct ImageLayer* new_layer;       /* The extracted layer with selected pixels */
    struct ImageLayer* original_layer;  /* The original layer pixels were extracted from */
    struct ImageDocument* doc;          /* Document reference */
    cairo_surface_t* original_snapshot; /* Snapshot of original layer before extraction (to restore on undo) */
    gint initial_offset_x;              /* Initial position of extracted layer */
    gint initial_offset_y;
    gint final_offset_x; /* Final position after moving */
    gint final_offset_y;
    gboolean had_selection;    /* TRUE if selection existed and was cleared */
    uint8_t* selection_backup; /* Backup of base_mask before clearing (NULL if no selection) */
    GList* selections_backup;  /* Backup of selections list before clearing (NULL if no selection) */
} MoveSelectedPixelsCommandData;

/**
 * Move command apply callback (restore to new position)
 */
static void move_command_apply(Command* cmd, struct ImageDocument* doc) {
    MoveCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Apply new position */
    data->layer->offset_x = data->new_offset_x;
    data->layer->offset_y = data->new_offset_y;

    /* Mark composite as dirty and queue redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Move command revert callback (restore to old position)
 */
static void move_command_revert(Command* cmd, struct ImageDocument* doc) {
    MoveCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Restore old position */
    data->layer->offset_x = data->old_offset_x;
    data->layer->offset_y = data->old_offset_y;

    /* Mark composite as dirty and queue redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Move command destroy callback
 */
static void move_command_destroy(Command* cmd) {
    MoveCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MoveCommandData*)cmd->user_data;
    g_free(data);
}

/**
 * Create a move command
 */
Command* command_create_move(struct ImageLayer* layer,
                             gint old_x, gint old_y,
                             gint new_x, gint new_y) {
    Command* cmd;
    MoveCommandData* data;

    if (!layer) {
        return NULL;
    }

    /* Create command data */
    data = (MoveCommandData*)g_malloc(sizeof(MoveCommandData));
    data->layer = layer;
    data->old_offset_x = old_x;
    data->old_offset_y = old_y;
    data->new_offset_x = new_x;
    data->new_offset_y = new_y;

    /* Create command */
    cmd = command_new("Move Layer",
                      COMMAND_MOVE,
                      move_command_apply,
                      move_command_revert,
                      move_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Move selected pixels command apply callback
 * Adds the extracted layer and moves it to final position, and clears selection
 */
static void move_selected_pixels_command_apply(Command* cmd, struct ImageDocument* doc) {
    MoveSelectedPixelsCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveSelectedPixelsCommandData*)cmd->user_data;

    if (!data->new_layer) {
        return;
    }

    /* Check if this is a redo (new_layer not in document) or initial execution (new_layer already in document) */
    gboolean is_redo = !g_list_find(doc->layers, data->new_layer);

    /* Add layer to document if not already there */
    if (is_redo) {
        doc->layers = g_list_append(doc->layers, data->new_layer);
        doc->selected_layer = data->new_layer;
    }

    /* Set final position */
    data->new_layer->offset_x = data->final_offset_x;
    data->new_layer->offset_y = data->final_offset_y;

    /* Restore original layer pixels if needed (for redo after undo) */
    /* Only restore snapshot on redo - on initial execution, pixels are already cleared by extraction */
    if (is_redo && data->original_layer && data->original_snapshot) {
        cairo_t* cr = cairo_create(data->original_layer->surface);
        cairo_set_source_surface(cr, data->original_snapshot, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_mark_dirty(data->original_layer->surface);
        layer_invalidate_cache(data->original_layer);

        /* After restoring snapshot, clear pixels from original layer where selection was active */
        /* This is needed for redo - the snapshot has original pixels, but we need to clear selected area again */
        /* Use selection_build_combined_mask to get feathered mask, just like extract_selection_to_layer does */
        if (data->had_selection && data->selection_backup && doc->selection_mask) {
            int stride = doc->selection_mask->stride;
            int width = doc->selection_mask->width;
            int height = doc->selection_mask->height;

            /* Restore selection backup to selection mask temporarily */
            /* Restore base_mask */
            for (int y = 0; y < height; y++) {
                uint8_t* row = doc->selection_mask->base_mask + y * stride;
                uint8_t* backup_row = data->selection_backup + y * stride;
                for (int x = 0; x < width; x++) {
                    row[x] = backup_row[x];
                }
            }

            /* Restore selections list */
            if (doc->selection_mask->selections) {
                GList* iter;
                for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                    Selection* sel = (Selection*)iter->data;
                    if (sel) {
                        selection_unref(sel);
                    }
                }
                g_list_free(doc->selection_mask->selections);
            }
            doc->selection_mask->selections = NULL;

            if (data->selections_backup) {
                GList* iter;
                for (iter = data->selections_backup; iter != NULL; iter = iter->next) {
                    Selection* sel = (Selection*)iter->data;
                    if (sel) {
                        selection_mask_add_selection(doc->selection_mask, selection_ref(sel));
                    }
                }
            }

            /* Regenerate feathered preview if needed */
            doc->selection_mask->feather_dirty = TRUE;
            selection_mask_regenerate_combined_feather_preview(doc->selection_mask);

            /* Calculate layer region in document coordinates */
            gint layer_x_min = data->original_layer->offset_x;
            gint layer_y_min = data->original_layer->offset_y;
            gint layer_x_max = data->original_layer->offset_x + data->original_layer->width;
            gint layer_y_max = data->original_layer->offset_y + data->original_layer->height;

            /* Clamp to document bounds */
            layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
            layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
            layer_x_max = (layer_x_max > doc->width) ? doc->width : layer_x_max;
            layer_y_max = (layer_y_max > doc->height) ? doc->height : layer_y_max;

            if (layer_x_max > layer_x_min && layer_y_max > layer_y_min) {
                /* Create dirty rect for the layer region */
                DirtyRect layer_rect;
                dirty_rect_set(&layer_rect, layer_x_min, layer_y_min,
                               layer_x_max - layer_x_min, layer_y_max - layer_y_min);

                /* Get feathered selection mask for this region */
                DirtyRect actual_region;
                SelectionMask* region_mask = selection_build_combined_mask(
                    doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

                if (region_mask && region_mask->data) {
                    /* Clear pixels from original layer where selection is active (using feathered mask) */
                    cairo_surface_flush(data->original_layer->surface);
                    guchar* src_data = cairo_image_surface_get_data(data->original_layer->surface);
                    gint src_stride = cairo_image_surface_get_stride(data->original_layer->surface);
                    gint layer_width = data->original_layer->width;
                    gint layer_height = data->original_layer->height;

                    for (gint y = 0; y < layer_height; y++) {
                        gint doc_y = data->original_layer->offset_y + y;
                        gint mask_y = doc_y - actual_region.y;

                        if (mask_y < 0 || mask_y >= region_mask->height) {
                            continue;
                        }

                        for (gint x = 0; x < layer_width; x++) {
                            gint doc_x = data->original_layer->offset_x + x;
                            gint mask_x = doc_x - actual_region.x;

                            if (mask_x < 0 || mask_x >= region_mask->width) {
                                continue;
                            }

                            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
                            if (mask_alpha > 0) {
                                /* Clear pixel in source layer (set to transparent) */
                                /* This matches extract_selection_to_layer behavior */
                                guchar* src_pixel = src_data + y * src_stride + x * 4;
                                src_pixel[0] = 0;
                                src_pixel[1] = 0;
                                src_pixel[2] = 0;
                                src_pixel[3] = 0;
                            }
                        }
                    }

                    cairo_surface_mark_dirty(data->original_layer->surface);
                    selection_mask_free(region_mask);
                }
            }

            /* Clear selection mask that we temporarily restored (selection will be cleared at bottom) */
            for (int y = 0; y < height; y++) {
                uint8_t* row = doc->selection_mask->base_mask + y * stride;
                for (int x = 0; x < width; x++) {
                    row[x] = 0;
                }
            }

            /* Clear selections list */
            if (doc->selection_mask->selections) {
                GList* iter;
                for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                    Selection* sel = (Selection*)iter->data;
                    if (sel) {
                        selection_unref(sel);
                    }
                }
                g_list_free(doc->selection_mask->selections);
                doc->selection_mask->selections = NULL;
            }
        }
    }

    /* Clear selection if it was backed up (meaning it existed) */
    if (data->had_selection && doc->selection_mask && data->selection_backup) {
        int stride = doc->selection_mask->stride;
        int width = doc->selection_mask->width;
        int height = doc->selection_mask->height;
        uint8_t* base_mask = doc->selection_mask->base_mask;

        /* Clear entire base_mask */
        for (int y = 0; y < height; y++) {
            uint8_t* row = base_mask + y * stride;
            for (int x = 0; x < width; x++) {
                row[x] = 0;
            }
        }

        /* Clear selections list */
        if (doc->selection_mask->selections) {
            GList* iter;
            for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    selection_unref(sel);
                }
            }
            g_list_free(doc->selection_mask->selections);
            doc->selection_mask->selections = NULL;
        }

        /* Set data pointer to base_mask (no feathering) */
        doc->selection_mask->data = doc->selection_mask->base_mask;

        /* Mark mask as dirty */
        selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
        doc->selection_mask->feather_dirty = TRUE;
    }

    document_invalidate_composite(doc);
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Move selected pixels command revert callback
 * Removes the extracted layer and restores original layer pixels and selection
 */
static void move_selected_pixels_command_revert(Command* cmd, struct ImageDocument* doc) {
    MoveSelectedPixelsCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveSelectedPixelsCommandData*)cmd->user_data;

    if (!data->new_layer || !data->original_layer) {
        return;
    }

    /* Remove extracted layer from document */
    if (g_list_find(doc->layers, data->new_layer)) {
        doc->layers = g_list_remove(doc->layers, data->new_layer);

        /* Update selected layer if needed */
        if (doc->selected_layer == data->new_layer) {
            doc->selected_layer = data->original_layer;
        }
    }

    /* Restore original layer pixels from snapshot */
    if (data->original_snapshot) {
        cairo_t* cr = cairo_create(data->original_layer->surface);
        cairo_set_source_surface(cr, data->original_snapshot, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_mark_dirty(data->original_layer->surface);
        layer_invalidate_cache(data->original_layer);
    }

    /* Restore selection if it was backed up */
    if (data->had_selection && doc->selection_mask && data->selection_backup) {
        int stride = doc->selection_mask->stride;
        int width = doc->selection_mask->width;
        int height = doc->selection_mask->height;
        uint8_t* base_mask = doc->selection_mask->base_mask;

        /* Restore base_mask from backup */
        for (int y = 0; y < height; y++) {
            uint8_t* row = base_mask + y * stride;
            uint8_t* backup_row = data->selection_backup + y * stride;
            for (int x = 0; x < width; x++) {
                row[x] = backup_row[x];
            }
        }

        /* Restore selections list */
        if (doc->selection_mask->selections) {
            GList* iter;
            for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    selection_unref(sel);
                }
            }
            g_list_free(doc->selection_mask->selections);
        }
        doc->selection_mask->selections = NULL;

        /* Restore selections from backup */
        if (data->selections_backup) {
            GList* iter;
            for (iter = data->selections_backup; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    selection_mask_add_selection(doc->selection_mask, selection_ref(sel));
                }
            }
        }

        /* Set data pointer - will be set by selection_mask_add_selection based on feathering */
        doc->selection_mask->data = doc->selection_mask->base_mask;

        /* Mark mask as dirty */
        selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
        doc->selection_mask->feather_dirty = TRUE;
    }

    document_invalidate_composite(doc);
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Move selected pixels command destroy callback
 */
static void move_selected_pixels_command_destroy(Command* cmd) {
    MoveSelectedPixelsCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MoveSelectedPixelsCommandData*)cmd->user_data;

    /* Free snapshot if exists */
    if (data->original_snapshot) {
        cairo_surface_destroy(data->original_snapshot);
    }

    /* Free selection backup if exists */
    if (data->selection_backup) {
        g_free(data->selection_backup);
    }

    /* Free selections backup if exists */
    if (data->selections_backup) {
        GList* iter;
        for (iter = data->selections_backup; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(data->selections_backup);
    }

    /* Free extracted layer if not in document (undo case) */
    if (data->new_layer) {
        if (!data->doc || !data->doc->layers) {
            /* Document is being freed - don't free layer */
        } else if (!g_list_find(data->doc->layers, data->new_layer)) {
            /* Layer not in document - free it */
            layer_free(data->new_layer);
        }
    }

    g_free(data);
}

/**
 * Create a move selected pixels command
 * This command handles extracting selected pixels to a new layer and moving that layer
 * @param snapshot Optional snapshot of original layer (taken BEFORE extraction). If NULL, creates one.
 */
Command* command_create_move_selected_pixels_with_snapshot(struct ImageDocument* doc,
                                                           struct ImageLayer* new_layer,
                                                           struct ImageLayer* original_layer,
                                                           gint initial_x, gint initial_y,
                                                           gint final_x, gint final_y,
                                                           cairo_surface_t* snapshot) {
    Command* cmd;
    MoveSelectedPixelsCommandData* data;

    if (!doc || !new_layer || !original_layer || !original_layer->surface) {
        if (snapshot) {
            cairo_surface_destroy(snapshot);
        }
        return NULL;
    }

    /* Create snapshot if not provided */
    if (!snapshot) {
        snapshot = cairo_surface_snapshot(original_layer->surface);
        if (!snapshot) {
            return NULL;
        }
    }

    /* Create command data */
    data = (MoveSelectedPixelsCommandData*)g_malloc(sizeof(MoveSelectedPixelsCommandData));
    data->new_layer = new_layer;
    data->original_layer = original_layer;
    data->doc = doc;
    data->original_snapshot = snapshot;
    data->initial_offset_x = initial_x;
    data->initial_offset_y = initial_y;
    data->final_offset_x = final_x;
    data->final_offset_y = final_y;
    data->had_selection = FALSE;
    data->selection_backup = NULL;
    data->selections_backup = NULL;

    /* Backup selection state if selection exists */
    if (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        int stride = doc->selection_mask->stride;
        int width = doc->selection_mask->width;
        int height = doc->selection_mask->height;

        /* Backup base_mask */
        data->selection_backup = g_malloc(stride * height);
        for (int y = 0; y < height; y++) {
            uint8_t* src_row = doc->selection_mask->base_mask + y * stride;
            uint8_t* dst_row = data->selection_backup + y * stride;
            for (int x = 0; x < width; x++) {
                dst_row[x] = src_row[x];
            }
        }

        /* Backup selections list (create references) */
        if (doc->selection_mask->selections) {
            GList* iter;
            for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    data->selections_backup = g_list_append(data->selections_backup, selection_ref(sel));
                }
            }
        }

        data->had_selection = TRUE;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_MOVE_SELECTED_PIXELS),
                      COMMAND_LAYER_EDIT,
                      move_selected_pixels_command_apply,
                      move_selected_pixels_command_revert,
                      move_selected_pixels_command_destroy);

    if (!cmd) {
        cairo_surface_destroy(snapshot);
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a move selected pixels command (creates snapshot automatically)
 * This command handles extracting selected pixels to a new layer and moving that layer
 * NOTE: This creates snapshot AFTER extraction, so pixels are already cleared. Use with_snapshot version instead.
 */
Command* command_create_move_selected_pixels(struct ImageDocument* doc,
                                             struct ImageLayer* new_layer,
                                             struct ImageLayer* original_layer,
                                             gint initial_x, gint initial_y,
                                             gint final_x, gint final_y) {
    /* Create snapshot automatically (but this will be AFTER extraction, so pixels are already cleared) */
    /* This function should only be used when snapshot is taken before extraction */
    return command_create_move_selected_pixels_with_snapshot(
        doc, new_layer, original_layer, initial_x, initial_y, final_x, final_y, NULL);
}