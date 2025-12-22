#include "selection/selection_mask.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SELECTION_THRESHOLD 128 /* Alpha value threshold for edge detection */
#define ANT_DASH_SIZE 4.0f      /* Marching ants dash length in pixels */

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
    mask->data = g_malloc0(mask->stride * height);
    mask->temp_data = g_malloc0(mask->stride * height);
    mask->dirty = TRUE; /* Initially dirty */
    mask->surface = NULL;

    return mask;
}

/**
 * Free selection mask
 */
void selection_mask_free(SelectionMask* mask) {
    if (!mask)
        return;

    if (mask->data) {
        g_free(mask->data);
        mask->data = NULL;
    }
    if (mask->temp_data) {
        g_free(mask->temp_data);
        mask->temp_data = NULL;
    }
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
 * Fill rectangular region
 */
void selection_mask_fill_rect(
    SelectionMask* mask,
    int x, int y, int width, int height,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius) {
    if (!mask || !mask->data)
        return;
    if (width <= 0 || height <= 0)
        return;

    /* Create temporary mask for this rectangle */
    SelectionMask* temp = selection_mask_new(mask->width, mask->height);

    /* Clamp rectangle to bounds */
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + width > mask->width) ? mask->width : (x + width);
    int y2 = (y + height > mask->height) ? mask->height : (y + height);

    if (smoothing == SELECTION_SMOOTH_FEATHERED || smoothing == SELECTION_SMOOTH_ANTIALIASED) {
        /* Use antialiased circle for soft edges */
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        float radius = (width < height) ? width / 2.0f : height / 2.0f;

        draw_soft_circle(temp, cx, cy, radius, smoothing, feather_radius);

        /* Fill the rectangle interior with feathering at edges */
        for (int row = y1; row < y2; row++) {
            for (int col = x1; col < x2; col++) {
                float dist_left = col - x;
                float dist_right = (x + width) - col;
                float dist_top = row - y;
                float dist_bottom = (y + height) - row;

                float dist = fminf(fminf(dist_left, dist_right), fminf(dist_top, dist_bottom));

                uint8_t alpha = 255;
                if (smoothing == SELECTION_SMOOTH_FEATHERED && dist < feather_radius) {
                    float t = 1.0f - (dist / feather_radius);
                    alpha = (uint8_t)(255.0f * t * t); /* Quadratic falloff */
                } else if (smoothing == SELECTION_SMOOTH_ANTIALIASED && dist < 1.0f) {
                    alpha = (uint8_t)(255.0f * (1.0f - (1.0f - dist)));
                }

                temp->data[row * temp->stride + col] = alpha;
            }
        }
    } else {
        /* Hard edges - simple fill */
        for (int row = y1; row < y2; row++) {
            for (int col = x1; col < x2; col++) {
                temp->data[row * temp->stride + col] = 255;
            }
        }
    }

    /* Apply temp mask to main mask using combine mode */
    selection_mask_apply(mask, temp, combine);
    selection_mask_free(temp);

    /* Track if feathering was applied */
    if (smoothing == SELECTION_SMOOTH_FEATHERED && feather_radius > 0) {
        mask->has_feathering = TRUE;
    }

    /* Mark affected region as dirty */
    selection_mask_mark_dirty(mask, x1, y1, x2 - x1, y2 - y1);
}

/**
 * Apply one mask to another
 */
void selection_mask_apply(
    SelectionMask* dest,
    SelectionMask* src,
    SelectionCombineMode combine) {
    if (!dest || !src || !dest->data || !src->data)
        return;
    if (dest->width != src->width || dest->height != src->height)
        return;

    for (int i = 0; i < dest->height * dest->stride; i++) {
        uint8_t dst_val = dest->data[i];
        uint8_t src_val = src->data[i];

        switch (combine) {
            case SELECTION_COMBINE_NEW:
                dest->data[i] = src_val;
                break;
            case SELECTION_COMBINE_ADD:
                dest->data[i] = (dst_val > src_val) ? dst_val : src_val;
                break;
            case SELECTION_COMBINE_SUBTRACT:
                dest->data[i] = (uint8_t)((dst_val * (255 - src_val)) / 255);
                break;
            case SELECTION_COMBINE_INTERSECT:
                dest->data[i] = (dst_val < src_val) ? dst_val : src_val;
                break;
        }
    }

    dest->dirty = TRUE;
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
    if (!mask || !mask->data)
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
 * Render animated marching ants outline from mask edges
 */
void selection_mask_render_outline(
    cairo_t* cr,
    SelectionMask* mask,
    int dash_phase,
    gdouble zoom_factor) {
    if (!cr || !mask || !mask->data)
        return;

    (void)zoom_factor; /* Unused parameter - kept for API compatibility */

    /* Trace edges where alpha transitions and render marching ants outline
       Use simple threshold-based edge detection (works for both feathered and non-feathered)
       Feathering is already baked into the alpha gradient of the mask */
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            uint8_t center = selection_mask_get_alpha(mask, x, y);

            /* Skip fully transparent pixels */
            if (center < SELECTION_THRESHOLD)
                continue;

            /* Check neighbors */
            uint8_t left = selection_mask_get_alpha(mask, x - 1, y);
            uint8_t right = selection_mask_get_alpha(mask, x + 1, y);
            uint8_t top = selection_mask_get_alpha(mask, x, y - 1);
            uint8_t bottom = selection_mask_get_alpha(mask, x, y + 1);

            /* Draw at threshold boundary where selection transitions to non-selected */
            if (left < SELECTION_THRESHOLD || right < SELECTION_THRESHOLD ||
                top < SELECTION_THRESHOLD || bottom < SELECTION_THRESHOLD) {
                /* Render marching ants pixel - shift pattern by dash_phase for animation */
                int pattern = ((x + y) / (int)ANT_DASH_SIZE + dash_phase) % 2;

                cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
                cairo_rectangle(cr, (gdouble)x, (gdouble)y, 1.0, 1.0);
                cairo_fill(cr);
            }
        }
    }
}
