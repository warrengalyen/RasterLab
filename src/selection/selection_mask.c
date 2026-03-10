#include "selection/selection_mask.h"
#include <cairo.h>
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
 * Create a bounded selection mask covering a sub-region of the document.
 * The buffer is (width × height) pixels; pixel at local (x, y) maps to
 * document position (x + offset_x, y + offset_y).
 */
SelectionMask* selection_mask_new_bounded(int offset_x, int offset_y, int width, int height) {
    SelectionMask* mask = selection_mask_new(width, height);
    if (mask) {
        mask->offset_x = offset_x;
        mask->offset_y = offset_y;
    }
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
    if (!mask || !mask->base_mask)
        return;

    /* Clear authoritative mask */
    memset(mask->base_mask, 0, mask->stride * mask->height);

    /* Clear derived preview if present */
    if (mask->feathered_preview) {
        memset(mask->feathered_preview, 0, mask->stride * mask->height);
    }

    /* Reset active data view to authoritative storage */
    mask->data = mask->base_mask;
    mask->feather_dirty = FALSE;
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
}

/**
 * Check if selection is empty
 */
gboolean selection_mask_is_empty(SelectionMask* mask) {
    /* IMPORTANT:
     * `base_mask` is the authoritative combined hard-edged selection.
     * `data` may point at the derived `feathered_preview` (for preview/display),
     * so checking `data` can incorrectly report "empty" if the preview buffer is
     * stale or has been cleared. */
    if (!mask || !mask->base_mask)
        return TRUE;

    for (int y = 0; y < mask->height; y++) {
        uint8_t* row = mask->base_mask + y * mask->stride;
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

                /* Fill rectangle region in selection's mask — one memset per row. */
                for (int row = y1; row < y2; row++)
                    memset(&sel->mask[row * stride + x1], 255, (size_t)clamped_width);

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

            /* Fill rectangle region in base_mask with 255 — one memset per row. */
            for (int row = y1; row < y2; row++)
                memset(base_mask + row * stride + x1, 255, (size_t)clamped_width);

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

                /* Fill rectangle region in selection's mask — one memset per row. */
                for (int row = y1; row < y2; row++)
                    memset(&sel->mask[row * sel_stride + x1], 255, (size_t)clamped_width);

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

        /* Fill rectangle region in selection's mask — one memset per row. */
        for (int row = y1; row < y2; row++)
            memset(&sel->mask[row * stride + x1], 255, (size_t)clamped_width);

        /* Add selection to list */
        selection_mask_add_selection(mask, sel);
        selection_unref(sel); /* Release our reference (list now owns it) */

        /* Mark affected region as dirty */
        selection_mask_mark_dirty(mask, x1, y1, clamped_width, clamped_height);
    }
}

/* ---------------------------------------------------------------------------
 * Scanline ellipse rasterizer
 *
 * Fills the pixels of an ellipse into `buf` (a flat byte mask, stride bytes
 * per row) whose bounding box in buf-space is [x1,x2) × [y1,y2).
 *
 * Non-AA path  — O(height) sqrt calls + memset per row.
 * AA path      — same for the fully-inside interior; per-pixel math only for
 *                the ~1-pixel-wide transition band on each edge.
 *
 * Pixel col is inside the (hard-edge) ellipse iff:
 *   (col+0.5 - cx)²/rx² + (row+0.5 - cy)²/ry² ≤ 1
 * which rearranges to:
 *   col ∈ [ ceil(cx - x_half - 0.5),  floor(cx + x_half - 0.5) ]
 * where x_half = rx * sqrt(1 - norm_dy²).
 * --------------------------------------------------------------------------- */
static void fill_ellipse_scanline_to_buf(
    uint8_t* buf, int stride,
    int y1, int y2, int x1, int x2,
    double cx, double cy, double rx, double ry,
    SelectionSmoothingMode smoothing, double aa_width) {
    if (rx <= 0.0 || ry <= 0.0)
        return;

    if (smoothing == SELECTION_SMOOTH_ANTIALIASED && aa_width > 0.0) {
        /* Pre-compute squared outer/inner normalized radii. */
        double outer_r2 = (1.0 + aa_width) * (1.0 + aa_width);
        double inner_r2v = (1.0 - aa_width) * (1.0 - aa_width);
        double inner_r2 = (inner_r2v > 0.0) ? inner_r2v : 0.0;

        for (int row = y1; row < y2; row++) {
            double norm_dy = (row + 0.5 - cy) / ry;
            double norm_dy2 = norm_dy * norm_dy;
            if (norm_dy2 >= outer_r2)
                continue; /* Row entirely outside AA zone */

            /* Outer span covers all pixels that receive any non-zero AA value. */
            double outer_half = rx * sqrt(outer_r2 - norm_dy2);
            int outer_left = (int)ceil(cx - outer_half - 0.5);
            int outer_right = (int)floor(cx + outer_half - 0.5);
            if (outer_left < x1)
                outer_left = x1;
            if (outer_right >= x2)
                outer_right = x2 - 1;
            if (outer_left > outer_right)
                continue;

            uint8_t* row_ptr = buf + row * stride;

            if (norm_dy2 < inner_r2) {
                /* This row has an inner zone that is 100% inside → memset. */
                double inner_half = rx * sqrt(inner_r2 - norm_dy2);
                int inner_left = (int)ceil(cx - inner_half - 0.5);
                int inner_right = (int)floor(cx + inner_half - 0.5);
                if (inner_left < x1)
                    inner_left = x1;
                if (inner_right >= x2)
                    inner_right = x2 - 1;

                /* Left AA band */
                for (int col = outer_left; col < inner_left; col++) {
                    double dx = (col + 0.5 - cx) / rx;
                    double dist = sqrt(dx * dx + norm_dy2);
                    double t = (dist - (1.0 - aa_width)) / (2.0 * aa_width);
                    if (t <= 0.0) {
                        row_ptr[col] = 255;
                        continue;
                    }
                    if (t >= 1.0)
                        continue;
                    row_ptr[col] = (uint8_t)((1.0 - smoothstep((float)t)) * 255.0 + 0.5);
                }
                /* Inner fully-opaque zone */
                if (inner_left <= inner_right)
                    memset(row_ptr + inner_left, 255,
                           (size_t)(inner_right - inner_left + 1));
                /* Right AA band */
                for (int col = inner_right + 1; col <= outer_right; col++) {
                    double dx = (col + 0.5 - cx) / rx;
                    double dist = sqrt(dx * dx + norm_dy2);
                    double t = (dist - (1.0 - aa_width)) / (2.0 * aa_width);
                    if (t <= 0.0) {
                        row_ptr[col] = 255;
                        continue;
                    }
                    if (t >= 1.0)
                        continue;
                    row_ptr[col] = (uint8_t)((1.0 - smoothstep((float)t)) * 255.0 + 0.5);
                }
            } else {
                /* Entire visible span is in the AA transition zone (very tall row
                 * or very thin ellipse) — per-pixel for the narrow outer band. */
                for (int col = outer_left; col <= outer_right; col++) {
                    double dx = (col + 0.5 - cx) / rx;
                    double dist = sqrt(dx * dx + norm_dy2);
                    double t = (dist - (1.0 - aa_width)) / (2.0 * aa_width);
                    if (t <= 0.0) {
                        row_ptr[col] = 255;
                        continue;
                    }
                    if (t >= 1.0)
                        continue;
                    row_ptr[col] = (uint8_t)((1.0 - smoothstep((float)t)) * 255.0 + 0.5);
                }
            }
        }
    } else {
        /* Hard-edge (SMOOTH_NONE or SMOOTH_FEATHERED): O(height) scanline + memset. */
        for (int row = y1; row < y2; row++) {
            double norm_dy = (row + 0.5 - cy) / ry;
            double rem = 1.0 - norm_dy * norm_dy;
            if (rem < 0.0)
                continue;
            double x_half = rx * sqrt(rem);
            int x_left = (int)ceil(cx - x_half - 0.5);
            int x_right = (int)floor(cx + x_half - 0.5);
            if (x_left < x1)
                x_left = x1;
            if (x_right >= x2)
                x_right = x2 - 1;
            if (x_left <= x_right)
                memset(buf + row * stride + x_left, 255,
                       (size_t)(x_right - x_left + 1));
        }
    }
}

/**
 * Fill elliptical region - creates a new Selection object with per-selection feathering
 */
void selection_mask_fill_ellipse(
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

    /* Clamp bounding rectangle to bounds */
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + width > mask->width) ? mask->width : (x + width);
    int y2 = (y + height > mask->height) ? mask->height : (y + height);
    int clamped_width = x2 - x1;
    int clamped_height = y2 - y1;

    if (clamped_width <= 0 || clamped_height <= 0)
        return;

    /* Calculate ellipse center and radii from original (unclamped) bounds */
    double cx = x + width / 2.0;
    double cy = y + height / 2.0;
    double rx = width / 2.0;
    double ry = height / 2.0;

    if (direct_modify) {
        /* Direct modification path: modify base_mask directly without creating Selection objects */
        gboolean should_create_selection = (mask->selections == NULL || mask->selections->data == NULL);

        /* Calculate antialiasing transition width in normalized space */
        double aa_width = (rx > 0 && ry > 0) ? (1.0 / fmin(rx, ry)) : 0.0;

        if (should_create_selection) {
            /* Create a Selection object when mask is empty (for proper selection tracking) */
            Selection* sel = selection_new(x1, y1, clamped_width, clamped_height,
                                           SELECTION_COMBINE_NEW,
                                           smoothing,
                                           feather_radius);
            if (sel) {
                /* Allocate mask for this selection */
                int stride = calculate_stride(mask->width);
                sel->mask = g_malloc0(stride * mask->height);

                /* Fill ellipse region — scanline rasterizer replaces per-pixel checks. */
                fill_ellipse_scanline_to_buf(sel->mask, stride, y1, y2, x1, x2,
                                             cx, cy, rx, ry, smoothing, aa_width);

                /* Add selection to list */
                selection_mask_add_selection(mask, sel);
                selection_unref(sel);

                if (!mask->data && mask->base_mask) {
                    mask->data = mask->base_mask;
                }

                selection_mask_mark_dirty(mask, x1, y1, clamped_width, clamped_height);
            }
        } else {
            /* Direct modify path: modify base_mask directly */
            int stride = mask->stride;
            uint8_t* base_mask = mask->base_mask;

            /* Fill ellipse region in base_mask — scanline rasterizer. */
            fill_ellipse_scanline_to_buf(base_mask, stride, y1, y2, x1, x2,
                                         cx, cy, rx, ry, smoothing, aa_width);

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
                                           SELECTION_COMBINE_NEW,
                                           smoothing,
                                           feather_radius);
            if (sel) {
                int sel_stride = calculate_stride(mask->width);
                sel->mask = g_malloc0(sel_stride * mask->height);

                /* Fill ellipse region — scanline rasterizer. */
                fill_ellipse_scanline_to_buf(sel->mask, sel_stride, y1, y2, x1, x2,
                                             cx, cy, rx, ry, smoothing, aa_width);

                selection_mask_add_selection(mask, sel);
                selection_unref(sel);
            }

            mask->data = mask->base_mask;
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

        /* Allocate mask for this selection (full mask size, but only ellipse region is filled) */
        int stride = calculate_stride(mask->width);
        sel->mask = g_malloc0(stride * mask->height);

        /* Calculate antialiasing transition width in normalized space */
        /* Use approximately 1 pixel transition at the edge */
        double aa_width = (rx > 0 && ry > 0) ? (1.0 / fmin(rx, ry)) : 0.0;

        /* Fill ellipse region — scanline rasterizer replaces per-pixel checks. */
        fill_ellipse_scanline_to_buf(sel->mask, stride, y1, y2, x1, x2,
                                     cx, cy, rx, ry, smoothing, aa_width);

        /* Add selection to list */
        selection_mask_add_selection(mask, sel);
        selection_unref(sel);

        /* Mark affected region as dirty */
        selection_mask_mark_dirty(mask, x1, y1, clamped_width, clamped_height);
    }
}

/**
 * Fill polygon from list of points (image space).
 * area_mode: 0=interior, 1=exterior, 2=border (border_width in pixels).
 * curvature: 0=straight sides; >0 curved (0..1, bulge amount).
 */
void selection_mask_fill_polygon(
    SelectionMask* mask,
    const double* points_x,
    const double* points_y,
    int n_points,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius,
    float curvature,
    int area_mode,
    int border_width,
    gboolean direct_modify) {
    (void)direct_modify; /* Always create Selection for polygon */
    if (!mask || !mask->base_mask || n_points < 3)
        return;
    if (!points_x || !points_y)
        return;

    int w = mask->width;
    int h = mask->height;
    int stride = mask->stride;

    /* Bounding box of polygon (expand for curvature bulge) */
    double min_x = points_x[0], max_x = points_x[0];
    double min_y = points_y[0], max_y = points_y[0];
    for (int i = 1; i < n_points; i++) {
        if (points_x[i] < min_x)
            min_x = points_x[i];
        if (points_x[i] > max_x)
            max_x = points_x[i];
        if (points_y[i] < min_y)
            min_y = points_y[i];
        if (points_y[i] > max_y)
            max_y = points_y[i];
    }
    int x1 = (int)floor(min_x);
    int y1 = (int)floor(min_y);
    int x2 = (int)ceil(max_x);
    int y2 = (int)ceil(max_y);
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > w)
        x2 = w;
    if (y2 > h)
        y2 = h;
    int box_w = x2 - x1;
    int box_h = y2 - y1;
    if (box_w <= 0 || box_h <= 0)
        return;

    /* Create selection with full-size mask */
    Selection* sel = selection_new(x1, y1, box_w, box_h, combine, smoothing, feather_radius);
    if (!sel)
        return;
    sel->mask = g_malloc0(stride * (size_t)h);

    /* Rasterize polygon with Cairo A8 surface */
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    if (!surf) {
        g_free(sel->mask);
        sel->mask = NULL;
        selection_unref(sel);
        return;
    }
    cairo_t* cr = cairo_create(surf);
    if (!cr) {
        cairo_surface_destroy(surf);
        g_free(sel->mask);
        sel->mask = NULL;
        selection_unref(sel);
        return;
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, points_x[0], points_y[0]);

    if (curvature > 1e-6f) {
        /* Curved sides: quadratic Bezier per edge, control point bulges outward */
        double cx = 0, cy = 0;
        for (int i = 0; i < n_points; i++) {
            cx += points_x[i];
            cy += points_y[i];
        }
        cx /= n_points;
        cy /= n_points;
        for (int i = 0; i < n_points; i++) {
            int a = i;
            int b = (i + 1) % n_points;
            double ax = points_x[a], ay = points_y[a];
            double bx = points_x[b], by = points_y[b];
            double mx = (ax + bx) * 0.5;
            double my = (ay + by) * 0.5;
            double tx = bx - ax;
            double ty = by - ay;
            double len = sqrt(tx * tx + ty * ty);
            if (len < 1e-9) {
                cairo_line_to(cr, bx, by);
                continue;
            }
            /* Perpendicular pointing outward (away from centroid) */
            double px = -ty / len;
            double py = tx / len;
            double dot = (mx - cx) * px + (my - cy) * py;
            if (dot < 0) {
                px = -px;
                py = -py;
            }
            /* Control point: midpoint + curvature * edge_length * 0.25 outward */
            double k = (double)curvature * len * 0.25;
            double ctrl_x = mx + k * px;
            double ctrl_y = my + k * py;
            /* Quadratic to cubic: P1 = A + (2/3)(C-A), P2 = B + (2/3)(C-B) */
            double p1x = ax + (2.0 / 3.0) * (ctrl_x - ax);
            double p1y = ay + (2.0 / 3.0) * (ctrl_y - ay);
            double p2x = bx + (2.0 / 3.0) * (ctrl_x - bx);
            double p2y = by + (2.0 / 3.0) * (ctrl_y - by);
            cairo_curve_to(cr, p1x, p1y, p2x, p2y, bx, by);
        }
    } else {
        /* Straight sides */
        for (int i = 1; i < n_points; i++)
            cairo_line_to(cr, points_x[i], points_y[i]);
    }
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Copy A8 alpha to sel->mask */
    int cairo_stride = cairo_image_surface_get_stride(surf);
    unsigned char* data = cairo_image_surface_get_data(surf);
    for (int y = 0; y < h; y++) {
        memcpy(sel->mask + (size_t)y * stride, data + (size_t)y * cairo_stride, (size_t)w);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    /* Area mode: 1=exterior (invert), 2=border (dilate then subtract interior) */
    if (area_mode == 1) {
        for (int i = 0; i < h * stride; i++)
            sel->mask[i] = (uint8_t)(255 - sel->mask[i]);
    } else if (area_mode == 2 && border_width > 0) {
        SelectionMask* tmp = selection_mask_new(w, h);
        if (tmp) {
            Selection* interior = selection_ref(sel);
            selection_mask_add_selection(tmp, interior);
            selection_unref(interior);
            selection_mask_border(tmp, border_width, NULL, NULL);
            memcpy(sel->mask, tmp->base_mask, (size_t)stride * (size_t)h);
            selection_mask_free(tmp);
        }
    }

    selection_mask_add_selection(mask, sel);
    selection_unref(sel);
    selection_mask_mark_dirty(mask, x1, y1, box_w, box_h);
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
    gdouble zoom_factor,
    gboolean already_in_image_space) {
    if (!cr || !mask)
        return;

    /* Apply zoom transform only when not already in image space (e.g. preview draws with scale first) */
    cairo_save(cr);
    if (!already_in_image_space && zoom_factor != 1.0) {
        cairo_scale(cr, zoom_factor, zoom_factor);
    }

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
        cairo_restore(cr);
        return;
    }

    /* Calculate dash size in image pixel space to maintain constant visual size on screen
     * When zoom is 2x, we use half the dash size in image space so it appears the same size on screen */
    gdouble dash_size = ANT_DASH_SIZE / zoom_factor;
    if (dash_size < 1.0) {
        dash_size = 1.0; /* Minimum dash size of 1 pixel */
    }

    /* For bounded masks (offset != 0), local coordinates must be translated to
     * document space before drawing.  For full-document masks offset_x/y are 0
     * so there is no change in behaviour for existing callers. */
    const int off_x = mask->offset_x;
    const int off_y = mask->offset_y;

    /* Edge detection and rendering via contour tracing.
     *
     * The naive approach of using (x+y)/dash_size as the pattern index fails at
     * 45-degree outline segments: d(x+y)/ds = 0 there, so the pattern never
     * toggles and those sections appear as one long unbroken dash.
     *
     * Instead we:
     *   Pass 1 – build an edge_state bitmap (0=interior, 1=edge/unvisited,
     *             2=edge/visited) using the same 4-neighbour boundary rule.
     *   Pass 2 – for every unvisited edge pixel, trace the connected contour
     *             with Moore (8-connected) neighbourhood following, accumulating
     *             true pixel-to-pixel arc length.  The dash pattern is assigned
     *             from that arc length, so dashes are uniform at every angle.
     */

    const uint8_t MIN_ALPHA = 1; /* Minimum alpha to consider as "inside" selection */
    const int W = mask->width;
    const int H = mask->height;

    /* Allocate edge state array (1 byte per pixel: 0/1/2) */
    size_t map_size = (size_t)W * H;
    uint8_t* edge_state = g_malloc0(map_size);
    if (!edge_state) {
        cairo_restore(cr);
        return;
    }

    /* Pass 1: mark boundary pixels */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t center = mask_data[y * mask->stride + x];
            if (center < MIN_ALPHA)
                continue;
            uint8_t left   = (x > 0)     ? mask_data[ y      * mask->stride + (x - 1)] : 0;
            uint8_t right  = (x < W - 1) ? mask_data[ y      * mask->stride + (x + 1)] : 0;
            uint8_t top    = (y > 0)     ? mask_data[(y - 1) * mask->stride +  x     ] : 0;
            uint8_t bottom = (y < H - 1) ? mask_data[(y + 1) * mask->stride +  x     ] : 0;
            if (left < MIN_ALPHA || right < MIN_ALPHA || top < MIN_ALPHA || bottom < MIN_ALPHA)
                edge_state[(size_t)y * W + x] = 1; /* unvisited edge */
        }
    }

    /* 8-connected neighbour directions in clockwise screen order:
     * 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE */
    static const int dx8[8] = { 1,  1,  0, -1, -1, -1,  0,  1 };
    static const int dy8[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

    /* Pass 2: trace each connected edge component with Moore neighbourhood.
     *
     * Scan top-left → bottom-right; every unvisited edge pixel is the start of
     * a new contour component.  At each step we search clockwise starting one
     * step past the backtrack direction (the "turn left" / outer-contour rule).
     * Arc length is the sum of pixel-to-pixel Euclidean distances (1.0 for
     * axis-aligned steps, √2 for diagonal steps). */
    for (int sy = 0; sy < H; sy++) {
        for (int sx = 0; sx < W; sx++) {
            if (edge_state[(size_t)sy * W + sx] != 1)
                continue; /* not an unvisited edge pixel */

            int      cx        = sx;
            int      cy        = sy;
            gdouble  arc_len   = 0.0;
            int      prev_x    = -1;
            int      prev_y    = -1;
            int      last_dir  = 0; /* initially assumed to be moving East */

            while (TRUE) {
                edge_state[(size_t)cy * W + cx] = 2; /* mark visited */

                /* Accumulate arc length from previous pixel */
                if (prev_x >= 0) {
                    double ddx = (double)(cx - prev_x);
                    double ddy = (double)(cy - prev_y);
                    arc_len += sqrt(ddx * ddx + ddy * ddy);
                }

                /* Render this pixel using arc-length-based pattern */
                int doc_x   = cx + off_x;
                int doc_y   = cy + off_y;
                int pattern = ((int)(arc_len / dash_size) + dash_phase) % 2;
                cairo_set_source_rgb(cr,
                                     pattern ? 0.0 : 1.0,
                                     pattern ? 0.0 : 1.0,
                                     pattern ? 0.0 : 1.0);
                cairo_rectangle(cr, (gdouble)doc_x, (gdouble)doc_y, 1.0, 1.0);
                cairo_fill(cr);

                prev_x = cx;
                prev_y = cy;

                /* Find next unvisited edge neighbour: search clockwise starting
                 * one step past the backtrack direction so we follow the outer
                 * contour without reversing. */
                int      back_dir = (last_dir + 4) % 8;
                gboolean found    = FALSE;
                for (int d = 1; d <= 8; d++) {
                    int try_dir = (back_dir + d) % 8;
                    int nx      = cx + dx8[try_dir];
                    int ny      = cy + dy8[try_dir];
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
                        edge_state[(size_t)ny * W + nx] == 1) {
                        last_dir = try_dir;
                        cx       = nx;
                        cy       = ny;
                        found    = TRUE;
                        break;
                    }
                }

                if (!found)
                    break; /* end of this contour component */
            }
        }
    }

    g_free(edge_state);
    cairo_restore(cr);
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

/* =================================================================
 * Felzenszwalb-Huttenlocher Exact Euclidean Distance Transform (EDT)
 *
 * Separable O(n) algorithm: one 1-D pass per row, one per column.
 * Produces exact Euclidean distances with no radius cap.
 *
 * Reference: Felzenszwalb, P.F. & Huttenlocher, D.P. (2012).
 *   "Distance Transforms of Sampled Functions."
 *   Theory of Computing, 8(1), 415-428.
 * ================================================================= */

/* Sentinel value for "no seed in this direction".
 * Must exceed the maximum possible squared distance in any image
 * we process.  1e18 covers images up to ~1 billion pixels per side. */
#define EDT_INF 1e18f

/**
 * 1-D lower-envelope Euclidean distance transform (FH algorithm).
 *
 * Computes d[i] = min_{j} { (i-j)^2 + f[j] }
 * where only positions with f[j] < EDT_INF/2 act as "seeds".
 * Seeds have f[j] = 0; non-seeds have f[j] = EDT_INF.
 *
 * The result d[i] is the squared distance to the nearest seed.
 *
 * @param f  Input function (0 at seeds, EDT_INF elsewhere), length n
 * @param d  Output squared distances, length n
 * @param v  Scratch: int array of length n   (parabola centers)
 * @param z  Scratch: float array of length n+1 (parabola boundaries)
 * @param n  Length of f and d
 */
static void edt_1d(const float* f, float* d, int* v, float* z, int n) {
    /* --- Phase 1: build lower envelope of upward parabolas --- */
    int k = 0;
    v[0] = 0;
    z[0] = -EDT_INF; /* sentinel: ensures the loop terminates at k = 0 */
    z[1] = EDT_INF;

    for (int q = 1; q < n; q++) {
        float fq_plus_q2 = f[q] + (float)(q * q);

        /* Pop any parabola whose rightward dominance ends before q.
         * z[0] = -EDT_INF guarantees termination without an explicit k >= 0 check. */
        while (1) {
            int r = v[k];
            float s = (fq_plus_q2 - (f[r] + (float)(r * r))) / (2.0f * (float)(q - r));
            if (s > z[k]) {
                /* New parabola dominates beyond s; add it to the envelope */
                k++;
                v[k] = q;
                z[k] = s;
                z[k + 1] = EDT_INF;
                break;
            }
            k--; /* pop dominated parabola */
        }
    }

    /* --- Phase 2: left-to-right scan, query the lower envelope --- */
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < (float)q)
            k++;
        float diff = (float)(q - v[k]);
        d[q] = diff * diff + f[v[k]];
    }
}

/**
 * 2-D exact Euclidean Distance Transform (separable FH algorithm).
 *
 * Computes the Euclidean distance from every pixel to its nearest
 * "seed" pixel.  @p seed_is_nonzero controls which pixels are seeds:
 *   TRUE  — seeds are pixels where mask[i] > 0 (outside-distance:
 *            distance of unselected pixels to nearest selected pixel)
 *   FALSE — seeds are pixels where mask[i] == 0 (inside-distance:
 *            distance of selected pixels to nearest unselected pixel)
 *
 * Complexity: O(width × height).  No radius cap — results are exact
 * for any feather radius.  A single small workspace of
 * O(max(width, height)) is allocated once and reused for all rows
 * and columns.
 *
 * @param mask            Input binary mask (0 / non-zero)
 * @param dist            Output distances (stride × height floats, caller-allocated)
 * @param width           Image width in pixels
 * @param height          Image height in pixels
 * @param stride          Row stride of both mask and dist (bytes == sizeof(float) * stride)
 * @param seed_is_nonzero Seed definition (see above)
 */
static void compute_edt(const uint8_t* mask, float* dist,
                        int width, int height, int stride,
                        gboolean seed_is_nonzero) {
    int max_dim = (width > height) ? width : height;

    /* Workspace — allocated once, reused for every row and every column */
    float* buf_in = g_malloc(max_dim * sizeof(float));
    float* buf_out = g_malloc(max_dim * sizeof(float));
    int* v = g_malloc(max_dim * sizeof(int));
    float* z = g_malloc((max_dim + 1) * sizeof(float));

    /* ---- Pass 1: 1-D EDT along each row ----
     * Produces squared horizontal distance to the nearest seed in the same row. */
    for (int y = 0; y < height; y++) {
        const uint8_t* row = mask + y * stride;
        for (int x = 0; x < width; x++) {
            int is_seed = seed_is_nonzero ? (row[x] > 0) : (row[x] == 0);
            buf_in[x] = is_seed ? 0.0f : EDT_INF;
        }
        edt_1d(buf_in, buf_out, v, z, width);
        float* drow = dist + y * stride;
        for (int x = 0; x < width; x++) {
            drow[x] = buf_out[x];
        }
    }

    /* ---- Pass 2: 1-D EDT along each column ----
     * Uses the row-pass squared distances as the input function.
     * By the separability property:
     *   min_{y'} { (y-y')^2 + dist_row[y'][x] }
     *   = min_{y'} { (y-y')^2 + min_{x'} { (x-x')^2 } }  [where x' is a seed]
     *   = min_{(x',y') seeds} { (x-x')^2 + (y-y')^2 }
     *   = squared 2-D Euclidean distance to nearest seed.
     * We take sqrt to yield the final distance. */
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            buf_in[y] = dist[y * stride + x];
        }
        edt_1d(buf_in, buf_out, v, z, height);
        for (int y = 0; y < height; y++) {
            float sq = buf_out[y];
            dist[y * stride + x] = (sq < EDT_INF * 0.5f) ? sqrtf(sq) : EDT_INF;
        }
    }

    g_free(buf_in);
    g_free(buf_out);
    g_free(v);
    g_free(z);
}

/**
 * Generate feathered preview for a single Selection using Signed Distance Field (SDF)
 * This applies feathering to the selection's mask if feather_mode == FEATHERED and radius > 0
 * Uses signed distance field for symmetric, Photoshop-like feathering
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

    float feather_radius = sel->feather_radius;
    if (feather_radius < 1.0f) {
        sel->feather_dirty = FALSE;
        return;
    }

    /* Clear preview to transparent */
    memset(sel->feathered_preview, 0, stride * mask_height);

    /* Allocate two distance buffers; signed_dist is computed inline from them. */
    float* dist_outside = g_malloc(stride * mask_height * sizeof(float));
    float* dist_inside = g_malloc(stride * mask_height * sizeof(float));

    /* Exact O(width × height) Euclidean DT — no radius cap, no search loop.
     *   dist_outside[i]: distance of pixel i to the nearest SELECTED pixel
     *   dist_inside[i]:  distance of pixel i to the nearest UNSELECTED pixel
     * Signed distance = dist_outside - dist_inside:
     *   negative  → inside the selection
     *   positive  → outside the selection
     *   ~0        → on the boundary */
    compute_edt(sel->mask, dist_outside, mask_width, mask_height, stride, TRUE);
    compute_edt(sel->mask, dist_inside, mask_width, mask_height, stride, FALSE);

    /* Convert signed distance to alpha using a symmetric smoothstep falloff.
     * At sdf = -feather_radius (deep inside):  alpha = 1  (fully selected)
     * At sdf =  feather_radius (deep outside): alpha = 0  (fully transparent)
     * In between: smooth cubic interpolation. */
    for (int y = 0; y < mask_height; y++) {
        for (int x = 0; x < mask_width; x++) {
            int idx = y * stride + x;
            float sdf = dist_outside[idx] - dist_inside[idx];

            float alpha;
            if (sdf <= -feather_radius) {
                alpha = 1.0f;
            } else if (sdf >= feather_radius) {
                alpha = 0.0f;
            } else {
                /* Map sdf from [-R, R] to t in [0, 1] then apply smoothstep */
                float t = (sdf + feather_radius) / (2.0f * feather_radius);
                t = fmaxf(0.0f, fminf(1.0f, t));
                alpha = 1.0f - smoothstep(t);
            }

            sel->feathered_preview[idx] = (uint8_t)(255.0f * alpha + 0.5f);
        }
    }

    g_free(dist_outside);
    g_free(dist_inside);

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
        /* Data source potentially changed; cached surface must be rebuilt */
        mask->dirty = TRUE;
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
    /* Feathered preview updated; cached surface must be rebuilt */
    mask->dirty = TRUE;
}

/* ============================================================
 * Selection Modification Operations
 * ============================================================ */

/**
 * Morphological dilation - expands selection outward
 */
static void dilate_mask(uint8_t* src_mask, uint8_t* dst_mask, int width, int height, int stride, int radius) {
    /* EDT-based dilation: O(w×h), exact circular kernel.
     * Binarize at threshold 128 first so anti-aliased edge pixels don't act as seeds. */
    int n = stride * height;
    uint8_t* binary = g_malloc(n);
    if (!binary) {
        memset(dst_mask, 0, n);
        return;
    }
    for (int i = 0; i < n; i++)
        binary[i] = (src_mask[i] >= 128) ? 255 : 0;

    float* dist = g_malloc(n * sizeof(float));
    if (!dist) {
        g_free(binary);
        memset(dst_mask, 0, n);
        return;
    }
    /* seed_is_nonzero=TRUE: seeds are pixels with binary > 0 (i.e. src_mask >= 128).
     * dist[i] = distance from pixel i to nearest selected pixel. */
    compute_edt(binary, dist, width, height, stride, TRUE);
    g_free(binary);

    float r = (float)radius;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * stride + x;
            dst_mask[idx] = (dist[idx] <= r) ? 255 : 0;
        }
    }
    g_free(dist);
}

/**
 * Apply Gaussian blur to a mask (used for feather operation)
 */
static void apply_gaussian_blur(uint8_t* src_mask, uint8_t* dst_mask, int width, int height, int stride, int radius) {
    if (!src_mask || !dst_mask || radius < 1) {
        return;
    }

    /* Create a temporary buffer for the blurred result */
    uint8_t* temp_result = g_malloc0(stride * height);
    if (!temp_result) {
        return;
    }

    /* Apply Gaussian-like blur to create feathered edges */
    float sigma = (float)radius / 3.0f; /* Approximate Gaussian sigma */
    int kernel_size = radius * 2 + 1;
    float* kernel = g_malloc(sizeof(float) * kernel_size);
    if (!kernel) {
        g_free(temp_result);
        return;
    }

    /* Create Gaussian kernel */
    float sum = 0.0f;
    for (int i = 0; i < kernel_size; i++) {
        float x = (float)(i - radius);
        kernel[i] = expf(-(x * x) / (2.0f * sigma * sigma));
        sum += kernel[i];
    }
    /* Normalize kernel */
    for (int i = 0; i < kernel_size; i++) {
        kernel[i] /= sum;
    }

    /* Apply horizontal blur */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float value = 0.0f;
            for (int k = 0; k < kernel_size; k++) {
                int px = x + k - radius;
                if (px >= 0 && px < width) {
                    int idx = y * stride + px;
                    value += (float)src_mask[idx] * kernel[k];
                }
            }
            temp_result[y * stride + x] = (uint8_t)(value + 0.5f);
        }
    }

    /* Apply vertical blur */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float value = 0.0f;
            for (int k = 0; k < kernel_size; k++) {
                int py = y + k - radius;
                if (py >= 0 && py < height) {
                    int idx = py * stride + x;
                    value += (float)temp_result[idx] * kernel[k];
                }
            }
            dst_mask[y * stride + x] = (uint8_t)(value + 0.5f);
        }
    }

    g_free(kernel);
    g_free(temp_result);
}

/**
 * Morphological erosion - contracts selection inward
 */
static void erode_mask(uint8_t* src_mask, uint8_t* dst_mask, int width, int height, int stride, int radius) {
    /* EDT-based erosion: O(w×h), exact circular kernel.
     * Binarize at threshold 128 first so anti-aliased edge pixels (0 < value < 128) are
     * treated as "unselected" seeds — otherwise the EDT would skip them and compute
     * distances to the nearest fully-zero pixel, making thin borders disappear.
     * A selected pixel survives iff its distance to the nearest unselected pixel > radius.
     * Strict > matches the old brute-force closed-disk semantics (pixels at exactly
     * distance r are inside the kernel and cause the center to fail). */
    int n = stride * height;
    uint8_t* binary = g_malloc(n);
    if (!binary) {
        memset(dst_mask, 0, n);
        return;
    }
    for (int i = 0; i < n; i++)
        binary[i] = (src_mask[i] >= 128) ? 255 : 0;

    float* dist = g_malloc(n * sizeof(float));
    if (!dist) {
        g_free(binary);
        memset(dst_mask, 0, n);
        return;
    }
    /* seed_is_nonzero=FALSE: seeds are pixels with binary == 0 (i.e. src_mask < 128).
     * dist[i] = distance from pixel i to nearest unselected pixel. */
    compute_edt(binary, dist, width, height, stride, FALSE);
    g_free(binary);

    float r = (float)radius;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * stride + x;
            gboolean at_edge = (x == 0 || y == 0 || x == width - 1 || y == height - 1);
            dst_mask[idx] = (!at_edge && src_mask[idx] >= 128 && dist[idx] > r) ? 255 : 0;
        }
    }
    g_free(dist);
}

/**
 * Grow selection by specified radius (dilate)
 * Processes each selection individually, preserving its feathering parameters
 */
gboolean selection_mask_grow(SelectionMask* mask, gint radius,
                             SelectionOperationProgressCallback progress_callback,
                             gpointer progress_user_data) {
    if (!mask || radius < 1 || radius > 500) {
        return FALSE;
    }

    if (selection_mask_is_empty(mask) || !mask->selections) {
        return FALSE; /* Can't grow empty selection */
    }

    /* Count selections */
    gint total_selections = g_list_length(mask->selections);
    if (total_selections == 0) {
        return FALSE;
    }

    /* Process each selection individually */
    GList* iter;
    gint current = 0;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            current++;
            continue;
        }

        /* Update progress */
        if (progress_callback) {
            if (!progress_callback(current, total_selections, progress_user_data)) {
                return FALSE; /* User cancelled */
            }
        }

        /* Allocate temporary buffer for this selection's mask */
        uint8_t* temp_buffer = g_malloc0(mask->stride * mask->height);
        if (!temp_buffer) {
            current++;
            continue;
        }

        /* Apply dilation to this selection's mask */
        dilate_mask(sel->mask, temp_buffer, mask->width, mask->height, mask->stride, radius);

        /* Copy result back to selection's mask */
        memcpy(sel->mask, temp_buffer, mask->stride * mask->height);
        g_free(temp_buffer);

        /* Mark selection's feathering as dirty so it regenerates */
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
            sel->feathered_preview = NULL;
        }
        sel->feather_dirty = TRUE;

        current++;
    }

    /* Rebuild base_mask from all modified selections */
    selection_mask_rebuild_from_selections(mask);

    /* Mark as dirty */
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
    mask->feather_dirty = TRUE;

    return TRUE;
}

/**
 * Shrink selection by specified radius (erode)
 * Processes each selection individually, preserving its feathering parameters
 */
gboolean selection_mask_shrink(SelectionMask* mask, gint radius,
                               SelectionOperationProgressCallback progress_callback,
                               gpointer progress_user_data) {
    if (!mask || radius < 1 || radius > 500) {
        return FALSE;
    }

    if (selection_mask_is_empty(mask) || !mask->selections) {
        return FALSE; /* Can't shrink empty selection */
    }

    /* Count selections */
    gint total_selections = g_list_length(mask->selections);
    if (total_selections == 0) {
        return FALSE;
    }

    /* Process each selection individually */
    GList* iter;
    gint current = 0;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            current++;
            continue;
        }

        /* Update progress */
        if (progress_callback) {
            if (!progress_callback(current, total_selections, progress_user_data)) {
                return FALSE; /* User cancelled */
            }
        }

        /* Allocate temporary buffer for this selection's mask */
        uint8_t* temp_buffer = g_malloc0(mask->stride * mask->height);
        if (!temp_buffer) {
            current++;
            continue;
        }

        /* Apply erosion to this selection's mask */
        erode_mask(sel->mask, temp_buffer, mask->width, mask->height, mask->stride, radius);

        /* Copy result back to selection's mask */
        memcpy(sel->mask, temp_buffer, mask->stride * mask->height);
        g_free(temp_buffer);

        /* Mark selection's feathering as dirty so it regenerates */
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
            sel->feathered_preview = NULL;
        }
        sel->feather_dirty = TRUE;

        current++;
    }

    /* Rebuild base_mask from all modified selections */
    selection_mask_rebuild_from_selections(mask);

    /* Mark as dirty */
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
    mask->feather_dirty = TRUE;

    return TRUE;
}

/**
 * Create border selection (original minus eroded)
 *
 * Note: This creates an INNER border of the given radius (similar to Photoshop's
 * Select > Modify > Border). This avoids returning an empty selection when the
 * selection already touches the image edges (e.g. Select All).
 * Processes each selection individually, preserving its feathering parameters
 */
gboolean selection_mask_border(SelectionMask* mask, gint radius,
                               SelectionOperationProgressCallback progress_callback,
                               gpointer progress_user_data) {
    if (!mask || radius < 1 || radius > 500) {
        return FALSE;
    }

    if (selection_mask_is_empty(mask) || !mask->selections) {
        return FALSE; /* Can't create border from empty selection */
    }

    /* Count selections */
    gint total_selections = g_list_length(mask->selections);
    if (total_selections == 0) {
        return FALSE;
    }

    /* Process each selection individually */
    GList* iter;
    gint current = 0;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            current++;
            continue;
        }

        /* Update progress */
        if (progress_callback) {
            if (!progress_callback(current, total_selections, progress_user_data)) {
                return FALSE; /* User cancelled */
            }
        }

        /* Allocate temporary buffer */
        uint8_t* temp_buffer = g_malloc0(mask->stride * mask->height);
        if (!temp_buffer) {
            current++;
            continue;
        }

        /* Erode this selection's mask */
        erode_mask(sel->mask, temp_buffer, mask->width, mask->height, mask->stride, radius);

        /* Border = original - eroded (inner border) */
        for (int i = 0; i < mask->height * mask->stride; i++) {
            if (sel->mask[i] >= 128 && temp_buffer[i] < 128) {
                sel->mask[i] = 255; /* Border pixel */
            } else {
                sel->mask[i] = 0; /* Not border */
            }
        }

        g_free(temp_buffer);

        /* Mark selection's feathering as dirty so it regenerates */
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
            sel->feathered_preview = NULL;
        }
        sel->feather_dirty = TRUE;

        current++;
    }

    /* Rebuild base_mask from all modified selections */
    selection_mask_rebuild_from_selections(mask);

    /* Mark as dirty */
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
    mask->feather_dirty = TRUE;

    return TRUE;
}

/**
 * Feather selection edges (modify base_mask with feathering)
 * Processes each selection individually, applying Gaussian blur to each selection's mask
 * Preserves each selection's individual feathering parameters
 */
gboolean selection_mask_feather(SelectionMask* mask, gint radius,
                                SelectionOperationProgressCallback progress_callback,
                                gpointer progress_user_data) {
    if (!mask || radius < 1 || radius > 500) {
        return FALSE;
    }

    if (selection_mask_is_empty(mask) || !mask->selections) {
        return FALSE; /* Can't feather empty selection */
    }

    /* Count selections */
    gint total_selections = g_list_length(mask->selections);
    if (total_selections == 0) {
        return FALSE;
    }

    /* Process each selection individually */
    GList* iter;
    gint current = 0;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            current++;
            continue;
        }

        /* Update progress */
        if (progress_callback) {
            if (!progress_callback(current, total_selections, progress_user_data)) {
                return FALSE; /* User cancelled */
            }
        }

        /* Allocate temporary buffer for this selection's mask */
        uint8_t* temp_buffer = g_malloc0(mask->stride * mask->height);
        if (!temp_buffer) {
            current++;
            continue;
        }

        /* Apply Gaussian blur to this selection's mask */
        apply_gaussian_blur(sel->mask, temp_buffer, mask->width, mask->height, mask->stride, radius);

        /* Copy result back to selection's mask */
        memcpy(sel->mask, temp_buffer, mask->stride * mask->height);
        g_free(temp_buffer);

        /* Mark selection's feathering as dirty so it regenerates */
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
            sel->feathered_preview = NULL;
        }
        sel->feather_dirty = TRUE;

        current++;
    }

    /* Rebuild base_mask from all modified selections */
    selection_mask_rebuild_from_selections(mask);

    /* Mark as dirty */
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
    mask->feather_dirty = TRUE;

    return TRUE;
}

/**
 * Sharpen selection edges (contract then expand to make edges harder)
 * Processes each selection individually, preserving its feathering parameters
 */
gboolean selection_mask_sharpen(SelectionMask* mask, gint radius,
                                SelectionOperationProgressCallback progress_callback,
                                gpointer progress_user_data) {
    if (!mask || radius < 1 || radius > 500) {
        return FALSE;
    }

    if (selection_mask_is_empty(mask) || !mask->selections) {
        return FALSE; /* Can't sharpen empty selection */
    }

    /* Count selections */
    gint total_selections = g_list_length(mask->selections);
    if (total_selections == 0) {
        return FALSE;
    }

    /* Process each selection individually */
    GList* iter;
    gint current = 0;
    for (iter = mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (!sel || !sel->mask) {
            current++;
            continue;
        }

        /* Update progress */
        if (progress_callback) {
            if (!progress_callback(current, total_selections, progress_user_data)) {
                return FALSE; /* User cancelled */
            }
        }

        /* Allocate temporary buffers */
        uint8_t* temp_buffer1 = g_malloc0(mask->stride * mask->height);
        uint8_t* temp_buffer2 = g_malloc0(mask->stride * mask->height);
        if (!temp_buffer1 || !temp_buffer2) {
            if (temp_buffer1)
                g_free(temp_buffer1);
            if (temp_buffer2)
                g_free(temp_buffer2);
            current++;
            continue;
        }

        /* Shrink then grow to sharpen edges */
        /* First shrink */
        erode_mask(sel->mask, temp_buffer1, mask->width, mask->height, mask->stride, radius);

        /* Then grow back */
        dilate_mask(temp_buffer1, temp_buffer2, mask->width, mask->height, mask->stride, radius);

        /* Copy result back to selection's mask */
        memcpy(sel->mask, temp_buffer2, mask->stride * mask->height);
        g_free(temp_buffer1);
        g_free(temp_buffer2);

        /* Mark selection's feathering as dirty so it regenerates */
        if (sel->feathered_preview) {
            g_free(sel->feathered_preview);
            sel->feathered_preview = NULL;
        }
        sel->feather_dirty = TRUE;

        current++;
    }

    /* Rebuild base_mask from all modified selections */
    selection_mask_rebuild_from_selections(mask);

    /* Mark as dirty */
    selection_mask_mark_dirty(mask, 0, 0, mask->width, mask->height);
    mask->feather_dirty = TRUE;

    return TRUE;
}
