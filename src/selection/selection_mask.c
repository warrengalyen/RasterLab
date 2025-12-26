#include "selection/selection_mask.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SELECTION_THRESHOLD 128 /* Alpha value threshold for edge detection */
#define ANT_DASH_SIZE 4.0f      /* Marching ants dash length in pixels */

/**
 * Smoothstep interpolation function
 * Returns smooth interpolation between 0 and 1 for t in [0, 1]
 * smoothstep(t) = t * t * (3.0f - 2.0f * t) = 3t² - 2t³
 */
static inline float smoothstep(float t) {
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/**
 * Allocate aligned row stride for better cache performance
 */
static int calculate_stride(int width) {
    return ((width + 3) / 4) * 4; /* Align to 4-byte boundary */
}

/**
 * Create a new selection mask
 */
SelectionMask* selection_mask_new(int width, int height) {
    SelectionMask* mask = g_malloc0(sizeof(SelectionMask));

    mask->width = width;
    mask->height = height;
    mask->stride = calculate_stride(width);

    /* Allocate base mask (hard 0/255 selection) - AUTHORITATIVE */
    mask->base_mask = g_malloc0(mask->stride * height);

    /* Initially use base_mask as data */
    mask->data = mask->base_mask;

    mask->temp_data = g_malloc0(mask->stride * height);
    mask->dirty = TRUE; /* Initially dirty */
    mask->surface = NULL;

    /* Initialize selections list */
    mask->selections = NULL;

    /* Feathering preview */
    mask->feathered_preview = NULL;
    mask->feather_dirty = FALSE;

    return mask;
}

/**
 * Free selection mask
 */
void selection_mask_free(SelectionMask* mask) {
    if (!mask)
        return;

    /* Free all selections in the list */
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

    if (mask->base_mask) {
        g_free(mask->base_mask);
        mask->base_mask = NULL;
    }
    if (mask->temp_data) {
        g_free(mask->temp_data);
        mask->temp_data = NULL;
    }
    if (mask->feathered_preview) {
        g_free(mask->feathered_preview);
        mask->feathered_preview = NULL;
    }
    /* Note: data always points to either base_mask or feathered_preview,
       so we don't free it separately to avoid double-free */
    mask->data = NULL;

    if (mask->surface) {
        cairo_surface_destroy(mask->surface);
        mask->surface = NULL;
    }
    g_free(mask);
}

/**
 * Clear selection mask to all zeros
 */
void selection_mask_clear(SelectionMask* mask) {
    if (!mask || !mask->data)
        return;

    memset(mask->data, 0, mask->stride * mask->height);
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
}

/**
 * Check if selection is empty
 */
gboolean selection_mask_is_empty(SelectionMask* mask) {
    if (!mask || !mask->data)
        return TRUE;

    for (int y = 0; y < mask->height; y++) {
        uint8_t* row = mask->data + y * mask->stride;
        for (int x = 0; x < mask->width; x++) {
            if (row[x] != 0)
                return FALSE;
        }
    }
    return TRUE;
}

/**
 * Get alpha value at specific pixel
 */
uint8_t selection_mask_get_alpha(SelectionMask* mask, int x, int y) {
    if (!mask || !mask->data)
        return 0;
    if (x < 0 || x >= mask->width || y < 0 || y >= mask->height)
        return 0;

    return mask->data[y * mask->stride + x];
}

/**
 * Draw a soft circle using antialiasing
 */
static void draw_soft_circle(SelectionMask* mask, float cx, float cy, float radius,
                             SelectionSmoothingMode smoothing, float feather_radius) {
    int x_min = (int)ceil(cx - radius);
    int x_max = (int)floor(cx + radius);
    int y_min = (int)ceil(cy - radius);
    int y_max = (int)floor(cy + radius);

    x_min = (x_min < 0) ? 0 : x_min;
    x_max = (x_max >= mask->width) ? mask->width - 1 : x_max;
    y_min = (y_min < 0) ? 0 : y_min;
    y_max = (y_max >= mask->height) ? mask->height - 1 : y_max;

    for (int y = y_min; y <= y_max; y++) {
        for (int x = x_min; x <= x_max; x++) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = sqrtf(dx * dx + dy * dy);

            float alpha = 0.0f;

            if (smoothing == SELECTION_SMOOTH_FEATHERED) {
                /* Gaussian falloff */
                float feather = feather_radius > 0.0f ? feather_radius : 1.0f;
                if (dist < radius) {
                    alpha = 255.0f;
                } else if (dist < radius + feather) {
                    float t = (dist - radius) / feather;
                    alpha = 255.0f * (1.0f - t * t); /* Quadratic falloff */
                }
            } else if (smoothing == SELECTION_SMOOTH_ANTIALIASED) {
                /* Smooth antialiased edge */
                if (dist < radius - 1.0f) {
                    alpha = 255.0f;
                } else if (dist < radius + 1.0f) {
                    float t = (dist - (radius - 1.0f)) / 2.0f;
                    alpha = 255.0f * (1.0f - t);
                }
            } else {
                /* Hard edges */
                if (dist <= radius) {
                    alpha = 255.0f;
                }
            }

            if (alpha > 0.0f) {
                mask->data[y * mask->stride + x] = (uint8_t)alpha;
            }
        }
    }
}

/**
 * Fill rectangular region - creates a new Selection object with per-selection feathering
 * PHASE 1: Basic per-selection storage (feathering rendering comes in Phase 2)
 */
void selection_mask_fill_rect(
    SelectionMask* mask,
    int x, int y, int width, int height,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius,
    gboolean direct_modify) {
    if (!mask || !mask->base_mask)
        return;
    if (width <= 0 || height <= 0)
        return;

    /* Clamp rectangle to bounds */
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + width > mask->width) ? mask->width : (x + width);
    int y2 = (y + height > mask->height) ? mask->height : (y + height);
    int clamped_width = x2 - x1;
    int clamped_height = y2 - y1;

    if (clamped_width <= 0 || clamped_height <= 0)
        return;

    if (direct_modify) {
        /* Direct modification path: modify base_mask directly without creating Selection objects */
        /* Combine modes are ignored - this is for global selection commands, not tool operations */
        /* EXCEPTION: If no selections exist, create a Selection object for proper tracking */
        gboolean should_create_selection = (mask->selections == NULL || mask->selections->data == NULL);

        if (should_create_selection) {
            /* Create a Selection object when mask is empty (for proper selection tracking) */
            Selection* sel = selection_new(x1, y1, clamped_width, clamped_height,
                                           SELECTION_COMBINE_NEW, /* Always NEW for global commands */
                                           SELECTION_SMOOTH_NONE, /* No smoothing for global commands */
                                           0.0f);
            if (sel) {
                /* Allocate mask for this selection */
                int stride = calculate_stride(mask->width);
                sel->mask = g_malloc0(stride * mask->height);

                /* Fill rectangle region in selection's mask */
                for (int row = y1; row < y2; row++) {
                    for (int col = x1; col < x2; col++) {
                        sel->mask[row * stride + col] = 255;
                    }
                }

                /* Add selection to list */
                selection_mask_add_selection(mask, sel);
                selection_unref(sel); /* Release our reference (list now owns it) */

                /* Ensure mask->data is set (selection_mask_add_selection should do this, but be explicit) */
                if (!mask->data && mask->base_mask) {
                    mask->data = mask->base_mask;
                }

                /* Mark affected region as dirty */
                selection_mask_mark_dirty(mask, x1, y1, clamped_width, clamped_height);
            }
        } else {
            /* Direct modify path: modify base_mask directly, but still create a Selection object */
            /* Ignore combine modes - just fill with 255 for global commands */
            int stride = mask->stride;
            uint8_t* base_mask = mask->base_mask;

            /* Fill rectangle region in base_mask with 255 (ignore combine mode) */
            for (int row = y1; row < y2; row++) {
                uint8_t* row_ptr = base_mask + row * stride;
                for (int col = x1; col < x2; col++) {
                    row_ptr[col] = 255;
                }
            }

            /* Clear existing selections list */
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

            /* Create a new Selection object to represent the full selection */
            Selection* sel = selection_new(x1, y1, clamped_width, clamped_height,
                                           SELECTION_COMBINE_NEW, /* Always NEW for global commands */
                                           SELECTION_SMOOTH_NONE, /* No smoothing for global commands */
                                           0.0f);
            if (sel) {
                /* Allocate mask for this selection */
                int sel_stride = calculate_stride(mask->width);
                sel->mask = g_malloc0(sel_stride * mask->height);

                /* Fill rectangle region in selection's mask */
                for (int row = y1; row < y2; row++) {
                    for (int col = x1; col < x2; col++) {
                        sel->mask[row * sel_stride + col] = 255;
                    }
                }

                /* Add selection to list */
                selection_mask_add_selection(mask, sel);
                selection_unref(sel); /* Release our reference (list now owns it) */
            }

            /* Set data pointer to base_mask (no feathering when direct_modify is TRUE) */
            mask->data = mask->base_mask;

            /* Mark mask as dirty */
            selection_mask_mark_dirty(mask, x1, y1, clamped_width, clamped_height);
            mask->feather_dirty = TRUE;
        }
    } else {
        /* Normal path: create Selection objects (for tool-based operations) */
        /* For NEW combine mode, clear all existing selections */
        if (combine == SELECTION_COMBINE_NEW) {
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
        }

        /* Create new Selection object with per-selection feathering parameters */
        Selection* sel = selection_new(x1, y1, clamped_width, clamped_height,
                                       combine, smoothing, feather_radius);
        if (!sel) {
            return;
        }

        /* Allocate mask for this selection (full mask size, but only rectangle region is filled) */
        int stride = calculate_stride(mask->width);
        sel->mask = g_malloc0(stride * mask->height);

        /* Fill rectangle region in selection's mask (hard 0/255 only) */
        for (int row = y1; row < y2; row++) {
            for (int col = x1; col < x2; col++) {
                sel->mask[row * stride + col] = 255;
            }
        }

        /* Add selection to list */
        selection_mask_add_selection(mask, sel);
        selection_unref(sel); /* Release our reference (list now owns it) */

        /* Mark affected region as dirty */
        selection_mask_mark_dirty(mask, x1, y1, clamped_width, clamped_height);
    }
}

/**
 * Apply one mask to another (operates on base_mask only)
 * Never modifies feathering parameters - only base_mask
 */
void selection_mask_apply(
    SelectionMask* dest,
    SelectionMask* src,
    SelectionCombineMode combine) {
    if (!dest || !src || !dest->base_mask || !src->base_mask)
        return;
    if (dest->width != src->width || dest->height != src->height)
        return;

    for (int i = 0; i < dest->height * dest->stride; i++) {
        uint8_t dst_val = dest->base_mask[i];
        uint8_t src_val = src->base_mask[i];

        switch (combine) {
            case SELECTION_COMBINE_NEW:
                dest->base_mask[i] = src_val;
                break;
            case SELECTION_COMBINE_ADD:
                dest->base_mask[i] = (dst_val > src_val) ? dst_val : src_val;
                break;
            case SELECTION_COMBINE_SUBTRACT:
                dest->base_mask[i] = (uint8_t)((dst_val * (255 - src_val)) / 255);
                break;
            case SELECTION_COMBINE_INTERSECT:
                dest->base_mask[i] = (dst_val < src_val) ? dst_val : src_val;
                break;
        }
    }

    dest->dirty = TRUE;
    dest->feather_dirty = TRUE; /* Feathered preview needs recompute due to base_mask change */
}

/**
 * Mark region as dirty
 */
void selection_mask_mark_dirty(SelectionMask* mask, int x, int y, int width, int height) {
    if (!mask)
        return;
    mask->dirty = TRUE;
}

/**
 * Get or rebuild Cairo surface from mask data
 */
cairo_surface_t* selection_mask_get_surface(SelectionMask* mask) {
    if (!mask || !mask->base_mask)
        return NULL;

    /* Regenerate combined feathered preview if needed (per-selection feathering) */
    if (mask->feather_dirty) {
        selection_mask_regenerate_combined_feather_preview(mask);
    }

    if (!mask->data)
        return NULL;

    if (!mask->dirty && mask->surface) {
        return mask->surface;
    }

    /* Destroy old surface if it exists */
    if (mask->surface) {
        cairo_surface_destroy(mask->surface);
    }

    /* Create new ARGB32 surface */
    mask->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, mask->width, mask->height);
    if (!mask->surface)
        return NULL;

    /* Copy mask data to surface */
    uint32_t* pixels = (uint32_t*)cairo_image_surface_get_data(mask->surface);
    int surface_stride = cairo_image_surface_get_stride(mask->surface) / 4;

    for (int y = 0; y < mask->height; y++) {
        uint8_t* src_row = mask->data + y * mask->stride;
        uint32_t* dst_row = pixels + y * surface_stride;

        for (int x = 0; x < mask->width; x++) {
            uint8_t alpha = src_row[x];
            /* Create ARGB32: white fill with variable alpha */
            dst_row[x] = (alpha << 24) | 0x00FFFFFF;
        }
    }

    cairo_surface_mark_dirty(mask->surface);
    mask->dirty = FALSE;

    return mask->surface;
}

/**
 * Public function to regenerate feathered preview after parameters change
 * Used by undo/redo to rebuild preview with restored feathering parameters
 * Now uses per-selection feathering system
 */
void selection_mask_regenerate_feather_preview(SelectionMask* mask) {
    if (!mask)
        return;

    /* Mark all selections as needing regeneration */
    GList* iter;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel) {
            sel->feather_dirty = TRUE;
        }
    }

    /* Regenerate combined preview with per-selection feathering */
    selection_mask_regenerate_combined_feather_preview(mask);
}
void selection_mask_render_outline(
    cairo_t* cr,
    SelectionMask* mask,
    int dash_phase,
    gdouble zoom_factor) {
    if (!cr || !mask)
        return;

    (void)zoom_factor; /* Unused parameter - kept for API compatibility */

    /* Ensure feathered preview is generated if feathering is active */
    /* Check if any selection has feathering enabled */
    gboolean any_feathering = FALSE;
    GList* iter;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel && sel->feather_mode == SELECTION_SMOOTH_FEATHERED && sel->feather_radius > 0.0f) {
            any_feathering = TRUE;
            break;
        }
    }

    /* Regenerate feathered preview if needed */
    if (any_feathering && mask->feather_dirty) {
        selection_mask_regenerate_combined_feather_preview(mask);
    }

    /* Use feathered preview if available and feathering is active, otherwise base_mask */
    const uint8_t* mask_data;
    if (any_feathering && mask->feathered_preview) {
        mask_data = mask->feathered_preview;
    } else {
        mask_data = mask->base_mask;
    }

    if (!mask_data) {
        return;
    }

    /* Edge detection: detect boundary where selection transitions to non-selected
       For feathered selections, detect the outer edge where feathering ends (mask->0)
       For hard selections, detect the hard edge (255->0) */
    const uint8_t MIN_ALPHA = 1; /* Minimum alpha to consider as "inside" selection */

    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            /* Get center pixel from mask (feathered or hard-edged) */
            uint8_t center = mask_data[y * mask->stride + x];

            /* Skip pixels that are completely outside selection */
            if (center == 0)
                continue;

            /* Check neighbors - get alpha values with bounds checking */
            uint8_t left = (x > 0) ? mask_data[y * mask->stride + (x - 1)] : 0;
            uint8_t right = (x < mask->width - 1) ? mask_data[y * mask->stride + (x + 1)] : 0;
            uint8_t top = (y > 0) ? mask_data[(y - 1) * mask->stride + x] : 0;
            uint8_t bottom = (y < mask->height - 1) ? mask_data[(y + 1) * mask->stride + x] : 0;

            /* Detect edge: pixel has some alpha (inside selection) but has at least one neighbor with alpha=0 (outside)
               This detects the outer boundary for both:
               - Hard edges: center=255, neighbor=0
               - Feathered edges: center>0 (any value in feather), neighbor=0 (end of feather) */
            if (left == 0 || right == 0 || top == 0 || bottom == 0) {
                /* Render marching ants pixel - shift pattern by dash_phase for animation */
                int pattern = ((x + y) / (int)ANT_DASH_SIZE + dash_phase) % 2;

                cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
                cairo_rectangle(cr, (gdouble)x, (gdouble)y, 1.0, 1.0);
                cairo_fill(cr);
            }
        }
    }
}

/* ============================================================
 * Per-Selection API Implementation
 * ============================================================ */

/**
 * Create a new Selection object
 */
Selection* selection_new(int x, int y, int width, int height,
                         SelectionCombineMode combine_mode,
                         SelectionSmoothingMode feather_mode,
                         float feather_radius) {
    Selection* sel = g_malloc0(sizeof(Selection));

    if (!sel) {
        return NULL;
    }

    sel->x = x;
    sel->y = y;
    sel->width = width;
    sel->height = height;
    sel->combine_mode = combine_mode;
    sel->feather_mode = feather_mode;
    sel->feather_radius = feather_radius;
    sel->feather_dirty = TRUE;
    sel->feathered_preview = NULL;
    sel->mask = NULL; /* Will be allocated when needed */
    sel->ref_count = 1;

    return sel;
}

/**
 * Increment reference count
 */
Selection* selection_ref(Selection* sel) {
    if (sel) {
        sel->ref_count++;
    }
    return sel;
}

/**
 * Decrement reference count and free if zero
 */
void selection_unref(Selection* sel) {
    if (!sel) {
        return;
    }

    sel->ref_count--;
    if (sel->ref_count <= 0) {
        if (sel->mask) {
            g_free(sel->mask);
        }
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
        }
        g_free(sel);
    }
}

/**
 * Get list of selections
 */
GList* selection_mask_get_selections(SelectionMask* mask) {
    if (!mask) {
        return NULL;
    }
    return mask->selections;
}

/**
 * Add selection to mask and update base_mask
 */
void selection_mask_add_selection(SelectionMask* mask, Selection* sel) {
    if (!mask || !sel) {
        return;
    }

    /* Add to list (takes ownership) */
    mask->selections = g_list_append(mask->selections, selection_ref(sel));

    /* Rebuild base_mask from all selections (will regenerate preview if needed) */
    selection_mask_rebuild_from_selections(mask);
}

/**
 * Remove selection from mask and update base_mask
 */
void selection_mask_remove_selection(SelectionMask* mask, Selection* sel) {
    if (!mask || !sel) {
        return;
    }

    /* Remove from list */
    GList* link = g_list_find(mask->selections, sel);
    if (link) {
        mask->selections = g_list_remove_link(mask->selections, link);
        selection_unref(sel);
        g_list_free(link);
    }

    /* Rebuild base_mask from remaining selections (will regenerate preview if needed) */
    selection_mask_rebuild_from_selections(mask);
}

/**
 * Rebuild base_mask from all selections
 * Combines all selections according to their combine_mode
 */
void selection_mask_rebuild_from_selections(SelectionMask* mask) {
    if (!mask) {
        return;
    }

    /* Clear base_mask */
    memset(mask->base_mask, 0, mask->stride * mask->height);

    /* Combine all selections according to their combine_mode */
    gboolean is_first = TRUE;
    GList* iter;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            continue;
        }

        /* Apply this selection's mask to base_mask using its combine_mode */
        for (int i = 0; i < mask->height * mask->stride; i++) {
            uint8_t dst_val = mask->base_mask[i];
            uint8_t src_val = sel->mask[i];

            /* First selection always starts fresh (treat as NEW even if mode says otherwise) */
            if (is_first) {
                mask->base_mask[i] = src_val;
            } else {
                switch (sel->combine_mode) {
                    case SELECTION_COMBINE_NEW:
                        /* Shouldn't happen after first, but handle gracefully */
                        mask->base_mask[i] = src_val;
                        break;
                    case SELECTION_COMBINE_ADD:
                        mask->base_mask[i] = (dst_val > src_val) ? dst_val : src_val;
                        break;
                    case SELECTION_COMBINE_SUBTRACT:
                        mask->base_mask[i] = (uint8_t)((dst_val * (255 - src_val)) / 255);
                        break;
                    case SELECTION_COMBINE_INTERSECT:
                        mask->base_mask[i] = (dst_val < src_val) ? dst_val : src_val;
                        break;
                }
            }
        }
        is_first = FALSE;
    }

    /* Check if any selections need feathering */
    gboolean any_feathering = FALSE;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel && sel->feather_mode == SELECTION_SMOOTH_FEATHERED && sel->feather_radius > 0.0f) {
            any_feathering = TRUE;
            break;
        }
    }

    /* If feathering is needed, regenerate combined preview, otherwise use base_mask */
    if (any_feathering) {
        selection_mask_regenerate_combined_feather_preview(mask);
    } else {
        mask->data = mask->base_mask;
    }

    mask->dirty = TRUE;
}

/**
 * Generate feathered preview for a single Selection
 * This applies feathering to the selection's mask if feather_mode == FEATHERED and radius > 0
 */
static void selection_generate_feathered_preview(Selection* sel, int mask_width, int mask_height, int stride) {
    if (!sel || !sel->mask) {
        return;
    }

    /* Only apply feathering if BOTH conditions are true:
       1. Mode is FEATHERED
       2. Radius is greater than 0 */
    gboolean should_feather = (sel->feather_mode == SELECTION_SMOOTH_FEATHERED &&
                               sel->feather_radius > 0.0f);

    if (!should_feather) {
        /* No feathering needed - clear preview if exists */
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
            sel->feathered_preview = NULL;
        }
        sel->feather_dirty = FALSE;
        return;
    }

    /* Allocate feathered preview if needed */
    if (!sel->feathered_preview) {
        sel->feathered_preview = g_malloc0(stride * mask_height);
    }

    /* Copy selection mask to preview (keep hard interior) */
    memcpy(sel->feathered_preview, sel->mask, stride * mask_height);

    int radius = (int)sel->feather_radius;
    if (radius <= 0) {
        sel->feather_dirty = FALSE;
        return;
    }

    /* Two-pass separable distance transform (same algorithm as compute_preview_feather_mask) */
    float* dist = g_malloc(stride * mask_height * sizeof(float));

    /* Initialize: 0 for selected, large value for unselected */
    float large_val = (float)(radius + 1);
    for (int i = 0; i < mask_height * stride; i++) {
        dist[i] = (sel->mask[i] > 0) ? 0.0f : large_val;
    }

    /* Pass 1: Horizontal - find nearest selected pixel in each row */
    for (int y = 0; y < mask_height; y++) {
        /* Forward pass: propagate distance from left */
        for (int x = 1; x < mask_width; x++) {
            if (dist[y * stride + x - 1] < large_val) {
                float candidate = dist[y * stride + x - 1] + 1.0f;
                dist[y * stride + x] = fminf(dist[y * stride + x], candidate);
            }
        }

        /* Backward pass: propagate distance from right */
        for (int x = mask_width - 2; x >= 0; x--) {
            if (dist[y * stride + x + 1] < large_val) {
                float candidate = dist[y * stride + x + 1] + 1.0f;
                dist[y * stride + x] = fminf(dist[y * stride + x], candidate);
            }
        }
    }

    /* Pass 2: Vertical - combine with vertical distance */
    for (int x = 0; x < mask_width; x++) {
        float* temp = g_malloc(mask_height * sizeof(float));
        memset(temp, 0, mask_height * sizeof(float));

        for (int y = 0; y < mask_height; y++) {
            float min_dist = large_val;

            /* Check all pixels within vertical radius */
            for (int dy = -radius; dy <= radius; dy++) {
                int ny = y + dy;
                if (ny >= 0 && ny < mask_height) {
                    float h_dist = dist[ny * stride + x];
                    if (h_dist < large_val) {
                        /* Euclidean distance combining horizontal and vertical offset */
                        float euclidean_sq = h_dist * h_dist + (float)(dy * dy);
                        float euclidean = sqrtf(euclidean_sq);
                        min_dist = fminf(min_dist, euclidean);
                    }
                }
            }

            temp[y] = min_dist;
        }

        /* Copy temp back to dist for this column */
        for (int y = 0; y < mask_height; y++) {
            dist[y * stride + x] = temp[y];
        }

        g_free(temp);
    }

    /* Apply feathering gradient based on distance field */
    for (int y = 0; y < mask_height; y++) {
        for (int x = 0; x < mask_width; x++) {
            int idx = y * stride + x;

            /* Keep interior pixels solid */
            if (sel->mask[idx] > 0)
                continue;

            float d = dist[idx];

            /* Outside feather radius - keep fully transparent */
            if (d >= sel->feather_radius)
                continue;

            /* Normalize distance: 0 at edge, 1 at radius */
            float t = d / sel->feather_radius;
            t = fmaxf(0.0f, fminf(1.0f, t));

            float alpha = 1.0f - smoothstep(t);

            sel->feathered_preview[idx] = (uint8_t)(255.0f * alpha);
        }
    }

    g_free(dist);
    sel->feather_dirty = FALSE;
}

/**
 * Regenerate combined feathered preview from all selections
 * Generates per-selection feathered previews and combines them according to combine_mode
 */
void selection_mask_regenerate_combined_feather_preview(SelectionMask* mask) {
    if (!mask) {
        return;
    }

    /* Check if any selection needs feathering */
    gboolean any_feathering = FALSE;
    GList* iter;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel && sel->feather_mode == SELECTION_SMOOTH_FEATHERED && sel->feather_radius > 0.0f) {
            any_feathering = TRUE;
            break;
        }
    }

    /* If no feathering needed, use base_mask directly */
    if (!any_feathering) {
        if (mask->feathered_preview) {
            g_free(mask->feathered_preview);
            mask->feathered_preview = NULL;
        }
        mask->data = mask->base_mask;
        mask->feather_dirty = FALSE;
        return;
    }

    /* Allocate combined preview if needed */
    if (!mask->feathered_preview) {
        mask->feathered_preview = g_malloc0(mask->stride * mask->height);
    }

    /* Clear preview */
    memset(mask->feathered_preview, 0, mask->stride * mask->height);

    /* Combine all selections with their individual feathering applied */
    gboolean is_first = TRUE;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            continue;
        }

        /* Generate feathered preview for this selection if needed */
        if (sel->feather_dirty) {
            selection_generate_feathered_preview(sel, mask->width, mask->height, mask->stride);
        }

        /* Get the appropriate mask to use (feathered if available, else hard) */
        uint8_t* src_mask = sel->feathered_preview ? sel->feathered_preview : sel->mask;

        /* Combine this selection's mask with combined preview */
        for (int i = 0; i < mask->height * mask->stride; i++) {
            uint8_t dst_val = mask->feathered_preview[i];
            uint8_t src_val = src_mask[i];

            /* First selection always starts fresh */
            if (is_first) {
                mask->feathered_preview[i] = src_val;
            } else {
                switch (sel->combine_mode) {
                    case SELECTION_COMBINE_NEW:
                        /* Shouldn't happen after first, but handle gracefully */
                        mask->feathered_preview[i] = src_val;
                        break;
                    case SELECTION_COMBINE_ADD:
                        /* Union: take maximum */
                        mask->feathered_preview[i] = (dst_val > src_val) ? dst_val : src_val;
                        break;
                    case SELECTION_COMBINE_SUBTRACT:
                        /* Subtract: multiply by inverse */
                        mask->feathered_preview[i] = (uint8_t)((dst_val * (255 - src_val)) / 255);
                        break;
                    case SELECTION_COMBINE_INTERSECT:
                        /* Intersect: take minimum */
                        mask->feathered_preview[i] = (dst_val < src_val) ? dst_val : src_val;
                        break;
                }
            }
        }
        is_first = FALSE;
    }

    mask->data = mask->feathered_preview;
    mask->feather_dirty = FALSE;
}
