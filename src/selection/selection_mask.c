#include "selection/selection_mask.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SELECTION_THRESHOLD 128 /* Alpha value threshold for edge detection */
#define ANT_DASH_SIZE 4.0f      /* Marching ants dash length in pixels */

/* Forward declaration for compute_preview_feather_mask */
static void compute_preview_feather_mask(SelectionMask* mask);

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

    /* Allocate base mask (hard 0/255 selection) */
    mask->base_mask = g_malloc0(mask->stride * height);

    /* Initially use base_mask as data */
    mask->data = mask->base_mask;

    mask->temp_data = g_malloc0(mask->stride * height);
    mask->dirty = TRUE; /* Initially dirty */
    mask->surface = NULL;

    /* Feathering support */
    mask->preview_feather_mask = NULL;
    mask->feather_radius = 0.0f;
    mask->feather_dirty = FALSE;

    return mask;
}

/**
 * Free selection mask
 */
void selection_mask_free(SelectionMask* mask) {
    if (!mask)
        return;

    if (mask->base_mask) {
        g_free(mask->base_mask);
        mask->base_mask = NULL;
    }
    if (mask->temp_data) {
        g_free(mask->temp_data);
        mask->temp_data = NULL;
    }
    if (mask->preview_feather_mask) {
        g_free(mask->preview_feather_mask);
        mask->preview_feather_mask = NULL;
    }
    /* Note: data always points to either base_mask or preview_feather_mask,
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
 * Fill rectangular region - creates hard 0/255 mask in base_mask
 * For combine modes other than NEW, feathering is applied to new rectangle only before combining
 * For NEW mode, feathering is stored for preview rendering during editing
 */
void selection_mask_fill_rect(
    SelectionMask* mask,
    int x, int y, int width, int height,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius) {
    if (!mask || !mask->base_mask)
        return;
    if (width <= 0 || height <= 0)
        return;

    /* Create temporary mask for this rectangle - hard 0/255 only */
    SelectionMask* temp = selection_mask_new(mask->width, mask->height);

    /* Clamp rectangle to bounds */
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + width > mask->width) ? mask->width : (x + width);
    int y2 = (y + height > mask->height) ? mask->height : (y + height);

    /* Always create hard-edge mask in base_mask (ignore smoothing for base) */
    for (int row = y1; row < y2; row++) {
        for (int col = x1; col < x2; col++) {
            temp->base_mask[row * temp->stride + col] = 255;
        }
    }

    /* For combine modes other than NEW, apply feathering to the temporary rectangle
       BEFORE combining it. This ensures feathering only applies to the new geometry. */
    if (combine != SELECTION_COMBINE_NEW && feather_radius > 0.0f) {
        temp->feather_radius = feather_radius;
        temp->feather_dirty = TRUE;
        /* Compute feathering for temp mask */
        compute_preview_feather_mask(temp);
        /* Copy feathered result into temp's base_mask */
        selection_mask_commit_feathering(temp);
    }

    /* Apply temp mask to main mask using combine mode */
    selection_mask_apply(mask, temp, combine);
    selection_mask_free(temp);

    /* For NEW combine mode, store feathering parameters for preview rendering
       This allows the user to see feathering during editing before finalizing */
    if (combine == SELECTION_COMBINE_NEW) {
        mask->feather_radius = feather_radius;
        mask->feather_dirty = TRUE; /* Preview needs recompute */
    } else {
        /* For other combine modes, don't store feathering on the combined result */
        mask->feather_radius = 0.0f;
        mask->feather_dirty = FALSE;
    }

    /* Mark affected region as dirty */
    selection_mask_mark_dirty(mask, x1, y1, x2 - x1, y2 - y1);
}

/**
 * Apply one mask to another (operates on base_mask)
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
    dest->feather_dirty = TRUE; /* Preview needs recompute */
}

/**
 * Compute feathering effect for preview rendering using fast separable distance transform
 * O(n*r) performance, accurate Euclidean distances
 */
static void compute_preview_feather_mask(SelectionMask* mask) {
    if (!mask || !mask->base_mask || mask->feather_radius <= 0.0f) {
        /* No feathering needed, use base_mask directly */
        if (mask->preview_feather_mask) {
            g_free(mask->preview_feather_mask);
            mask->preview_feather_mask = NULL;
        }
        mask->data = mask->base_mask;
        mask->feather_dirty = FALSE;
        return;
    }

    /* Allocate preview feather mask if needed */
    if (!mask->preview_feather_mask) {
        mask->preview_feather_mask = g_malloc0(mask->stride * mask->height);
    }

    /* Copy base mask to preview (keep hard interior) */
    memcpy(mask->preview_feather_mask, mask->base_mask, mask->stride * mask->height);

    int radius = (int)mask->feather_radius;
    if (radius <= 0) {
        mask->data = mask->preview_feather_mask;
        mask->feather_dirty = FALSE;
        return;
    }

    /* Two-pass separable distance transform
       Pass 1: Horizontal distance per row
       Pass 2: Vertical distance per column, combining with horizontal */
    float* dist = g_malloc(mask->stride * mask->height * sizeof(float));

    /* Initialize: 0 for selected, large value for unselected */
    float large_val = (float)(radius + 1);
    for (int i = 0; i < mask->height * mask->stride; i++) {
        dist[i] = (mask->base_mask[i] > 0) ? 0.0f : large_val;
    }

    /* Pass 1: Horizontal - find nearest selected pixel in each row */
    for (int y = 0; y < mask->height; y++) {
        /* Forward pass: propagate distance from left */
        for (int x = 1; x < mask->width; x++) {
            if (dist[y * mask->stride + x - 1] < large_val) {
                float candidate = dist[y * mask->stride + x - 1] + 1.0f;
                dist[y * mask->stride + x] = fminf(dist[y * mask->stride + x], candidate);
            }
        }

        /* Backward pass: propagate distance from right */
        for (int x = mask->width - 2; x >= 0; x--) {
            if (dist[y * mask->stride + x + 1] < large_val) {
                float candidate = dist[y * mask->stride + x + 1] + 1.0f;
                dist[y * mask->stride + x] = fminf(dist[y * mask->stride + x], candidate);
            }
        }
    }

    /* Pass 2: Vertical - combine with vertical distance
       For each column, check pixels within radius vertically
       Combine horizontal distance from those pixels with vertical offset */
    for (int x = 0; x < mask->width; x++) {
        float* temp = g_malloc(mask->height * sizeof(float));
        memset(temp, 0, mask->height * sizeof(float));

        for (int y = 0; y < mask->height; y++) {
            float min_dist = large_val;

            /* Check all pixels within vertical radius */
            for (int dy = -radius; dy <= radius; dy++) {
                int ny = y + dy;
                if (ny >= 0 && ny < mask->height) {
                    float h_dist = dist[ny * mask->stride + x];
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
        for (int y = 0; y < mask->height; y++) {
            dist[y * mask->stride + x] = temp[y];
        }

        g_free(temp);
    }

    /* Apply feathering gradient based on distance field */
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            uint8_t center = mask->base_mask[y * mask->stride + x];

            /* Already selected - keep as is */
            if (center > 0)
                continue;

            float d = dist[y * mask->stride + x];

            /* Apply feathering gradient if within feather radius */
            if (d > 0.0f && d <= (float)radius) {
                /* Create smooth falloff: fully transparent at radius, fully opaque at 0 */
                float t = d / (float)radius;
                uint8_t alpha = (uint8_t)(255.0f * (1.0f - t * t)); /* Quadratic falloff */
                mask->preview_feather_mask[y * mask->stride + x] = alpha;
            }
        }
    }

    g_free(dist);

    /* Use preview feather mask for rendering */
    mask->data = mask->preview_feather_mask;
    mask->feather_dirty = FALSE;
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

    /* Compute feathering if needed */
    if (mask->feather_dirty) {
        compute_preview_feather_mask(mask);
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
 * Apply feathering permanently to base_mask
 * Converts preview feathering to the actual base_mask
 * Call this when finalizing a selection
 */
void selection_mask_commit_feathering(SelectionMask* mask) {
    if (!mask)
        return;

    /* If we have feathering, ensure preview is up to date then bake it */
    if (mask->feather_radius > 0.0f) {
        /* Recompute preview if marked dirty (e.g., after combine operations) */
        if (mask->feather_dirty) {
            compute_preview_feather_mask(mask);
        }

        /* Bake the feathered result into base_mask */
        if (mask->preview_feather_mask) {
            memcpy(mask->base_mask, mask->preview_feather_mask, mask->stride * mask->height);
        }

        /* Reset feathering state - selection is now permanent */
        mask->feather_radius = 0.0f;
        mask->feather_dirty = FALSE;
        /* Use base_mask directly from now on */
        mask->data = mask->base_mask;
        mask->dirty = TRUE;
    }
}
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
