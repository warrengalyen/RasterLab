#include "selection.h"
#include <math.h>
#include <string.h>

/**
 * Draw marching ants outline for a rectangle using alternating pixels
 * Used by both selection preview and final selection rendering
 * @param cr Cairo context to draw on
 * @param x Rectangle x position
 * @param y Rectangle y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param dash_phase Animation phase (0-3) for marching effect
 */
void selection_draw_marching_ants(cairo_t* cr, gdouble x, gdouble y,
                                  gdouble width, gdouble height,
                                  gdouble line_width, gdouble animation_phase) {
    if (!cr || width <= 0 || height <= 0) {
        return;
    }

    gint x_start = (gint)x;
    gint y_start = (gint)y;
    gint x_end = (gint)(x + width);
    gint y_end = (gint)(y + height);
    gint dash_phase = (gint)animation_phase;

    (void)line_width; /* Unused parameter - kept for API compatibility */

    /* Draw top and bottom edges */
    for (gint px = x_start; px <= x_end; px++) {
        /* Top edge - shift pattern by dash_phase for animation */
        int pattern = ((px + y_start) / (int)ANT_DASH_SIZE + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, px, y_start, 1.0, 1.0);
        cairo_fill(cr);

        /* Bottom edge */
        pattern = ((px + y_end) / (int)ANT_DASH_SIZE + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, px, y_end, 1.0, 1.0);
        cairo_fill(cr);
    }

    /* Draw left and right edges */
    for (gint py = y_start; py <= y_end; py++) {
        /* Left edge */
        int pattern = ((x_start + py) / (int)ANT_DASH_SIZE + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, x_start, py, 1.0, 1.0);
        cairo_fill(cr);

        /* Right edge */
        pattern = ((x_end + py) / (int)ANT_DASH_SIZE + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, x_end, py, 1.0, 1.0);
        cairo_fill(cr);
    }
}

/**
 * Create a new empty selection
 */
Selection* selection_new(void) {
    Selection* sel = (Selection*)g_malloc0(sizeof(Selection));
    sel->region = cairo_region_create();
    sel->mask = NULL;
    sel->animated = FALSE;
    sel->animation_phase = 0;
    sel->smooth_mode = SELECTION_SMOOTH_NONE;
    sel->feather_radius = 0;
    return sel;
}

/**
 * Free a selection and its resources
 */
void selection_free(Selection* sel) {
    if (!sel) {
        return;
    }

    if (sel->region) {
        cairo_region_destroy(sel->region);
        sel->region = NULL;
    }

    if (sel->mask) {
        cairo_surface_destroy(sel->mask);
        sel->mask = NULL;
    }

    g_free(sel);
}

/**
 * Check if a selection is empty
 */
gboolean selection_is_empty(Selection* sel) {
    if (!sel || !sel->region) {
        return TRUE;
    }

    cairo_region_num_rectangles(sel->region);
    return cairo_region_num_rectangles(sel->region) == 0;
}

/**
 * Clear a selection (make it empty)
 */
void selection_clear(Selection* sel) {
    if (!sel) {
        return;
    }

    if (sel->region) {
        cairo_region_destroy(sel->region);
        sel->region = cairo_region_create();
    }

    if (sel->mask) {
        cairo_surface_destroy(sel->mask);
        sel->mask = NULL;
    }

    sel->animation_phase = 0;
}

/**
 * Apply Gaussian blur to a surface (simple box blur for feathering)
 * Operates on alpha channel only
 */
static void blur_surface_alpha(cairo_surface_t* surface, gint radius) {
    if (!surface || radius <= 0) {
        return;
    }

    /* Get surface info */
    gint width = cairo_image_surface_get_width(surface);
    gint height = cairo_image_surface_get_height(surface);

    if (width <= 0 || height <= 0) {
        return;
    }

    /* Get pixel data */
    guint8* data = cairo_image_surface_get_data(surface);
    gint stride = cairo_image_surface_get_stride(surface);

    if (!data) {
        return;
    }

    /* Simple box blur: pass horizontal then vertical
     * This gives approximation of Gaussian blur */
    gint kernel_size = 2 * radius + 1;
    guint32 kernel_sum = kernel_size * kernel_size;

    /* Temporary buffer for blur pass */
    guint8* temp = (guint8*)g_malloc(stride * height);

    /* Horizontal pass */
    for (gint y = 0; y < height; y++) {
        for (gint x = 0; x < width; x++) {
            guint32 sum = 0;
            gint count = 0;

            /* Sample neighborhood */
            for (gint dx = -radius; dx <= radius; dx++) {
                gint nx = x + dx;
                if (nx >= 0 && nx < width) {
                    guint8* pixel = data + y * stride + nx * 4 + 3; /* alpha channel */
                    sum += *pixel;
                    count++;
                }
            }

            /* Store blurred alpha */
            guint8* out_pixel = temp + y * stride + x * 4 + 3;
            *out_pixel = (guint8)(sum / count);
        }
    }

    /* Copy temp back to data for vertical pass */
    memcpy(data, temp, stride * height);

    /* Vertical pass */
    for (gint x = 0; x < width; x++) {
        for (gint y = 0; y < height; y++) {
            guint32 sum = 0;
            gint count = 0;

            /* Sample neighborhood */
            for (gint dy = -radius; dy <= radius; dy++) {
                gint ny = y + dy;
                if (ny >= 0 && ny < height) {
                    guint8* pixel = data + ny * stride + x * 4 + 3; /* alpha channel */
                    sum += *pixel;
                    count++;
                }
            }

            /* Store blurred alpha */
            guint8* out_pixel = temp + y * stride + x * 4 + 3;
            *out_pixel = (guint8)(sum / count);
        }
    }

    /* Copy result back */
    memcpy(data, temp, stride * height);
    g_free(temp);

    /* Mark surface as dirty */
    cairo_surface_mark_dirty(surface);
}

/**
 * Create antialiased or feathered alpha mask for a rectangle
 */
static cairo_surface_t* create_smooth_mask(gint width, gint height,
                                           gint rect_x, gint rect_y,
                                           gint rect_w, gint rect_h,
                                           SelectionSmoothingMode mode,
                                           gint feather_radius) {
    /* Create ARGB32 surface (alpha channel will store selection mask) */
    cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    if (!mask || cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
        if (mask)
            cairo_surface_destroy(mask);
        return NULL;
    }

    /* Clear surface to fully transparent */
    guint8* data = cairo_image_surface_get_data(mask);
    gint stride = cairo_image_surface_get_stride(mask);
    memset(data, 0, stride * height);

    /* Draw rectangle with full opacity (alpha = 255) */
    for (gint y = rect_y; y < rect_y + rect_h && y < height; y++) {
        if (y < 0)
            continue;
        for (gint x = rect_x; x < rect_x + rect_w && x < width; x++) {
            if (x < 0)
                continue;

            guint8* pixel = data + y * stride + x * 4;
            /* Set ARGB: A=255, RGB=ignored (will be blended) */
            pixel[3] = 255; /* Alpha */
        }
    }

    cairo_surface_mark_dirty(mask);

    /* Apply feathering if requested */
    if (mode == SELECTION_SMOOTH_FEATHERED && feather_radius > 0) {
        blur_surface_alpha(mask, feather_radius);
    }

    return mask;
}

/**
 * Create a rectangular selection
 */
Selection* selection_create_rectangle(gint x, gint y, gint width, gint height,
                                      SelectionSmoothingMode smooth_mode,
                                      gint feather_radius) {
    Selection* sel = selection_new();

    /* Normalize rectangle (handle negative width/height) */
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }

    if (width <= 0 || height <= 0) {
        return sel; /* Empty selection */
    }

    /* Create region with rectangle */
    cairo_rectangle_int_t rect = {x, y, width, height};
    cairo_region_destroy(sel->region);
    sel->region = cairo_region_create_rectangle(&rect);

    sel->smooth_mode = smooth_mode;
    sel->feather_radius = feather_radius;

    /* For antialiased mode, create mask (feathered mask created above)
     * For NONE mode, region is sufficient */
    if (smooth_mode == SELECTION_SMOOTH_ANTIALIASED ||
        smooth_mode == SELECTION_SMOOTH_FEATHERED) {
        /* We'll create the mask on demand (lazy initialization) */
        sel->mask = NULL; /* Will be created when needed */
    }

    return sel;
}

/**
 * Combine two selections using cairo_region operations
 */
gboolean selection_combine(Selection* dest, Selection* src, SelectionCombineMode mode) {
    if (!dest || !src) {
        return FALSE;
    }

    cairo_region_t* result = NULL;
    cairo_status_t status;

    switch (mode) {
        case SELECTION_COMBINE_NEW:
            /* Replace with source */
            if (dest->region) {
                cairo_region_destroy(dest->region);
            }
            dest->region = cairo_region_copy(src->region);
            break;

        case SELECTION_COMBINE_ADD:
            /* Union */
            result = cairo_region_copy(dest->region);
            status = cairo_region_union(result, src->region);
            if (status != CAIRO_STATUS_SUCCESS) {
                cairo_region_destroy(result);
                return FALSE;
            }
            cairo_region_destroy(dest->region);
            dest->region = result;
            break;

        case SELECTION_COMBINE_SUBTRACT:
            /* Subtract - subtract src from dest */
            result = cairo_region_copy(dest->region);
            status = cairo_region_subtract(result, src->region);
            if (status != CAIRO_STATUS_SUCCESS) {
                cairo_region_destroy(result);
                return FALSE;
            }
            cairo_region_destroy(dest->region);
            dest->region = result;
            break;

        case SELECTION_COMBINE_INTERSECT:
            /* Intersect - keep only overlapping parts */
            result = cairo_region_copy(dest->region);
            status = cairo_region_intersect(result, src->region);
            if (status != CAIRO_STATUS_SUCCESS) {
                cairo_region_destroy(result);
                return FALSE;
            }
            cairo_region_destroy(dest->region);
            dest->region = result;
            break;

        default:
            return FALSE;
    }

    /* Clear mask when combining (it's now invalid) */
    if (dest->mask) {
        cairo_surface_destroy(dest->mask);
        dest->mask = NULL;
    }

    return TRUE;
}

/**
 * Get the alpha mask (creates if needed)
 */
cairo_surface_t* selection_get_mask(Selection* sel) {
    if (!sel) {
        return NULL;
    }

    /* If mask already exists, return it */
    if (sel->mask) {
        return sel->mask;
    }

    /* For ANTIALIASED or FEATHERED modes, create mask from region
     * This is lazy initialization - mask created on first access */
    if (sel->smooth_mode == SELECTION_SMOOTH_ANTIALIASED ||
        sel->smooth_mode == SELECTION_SMOOTH_FEATHERED) {

        /* Get region bounds to size the mask */
        cairo_rectangle_int_t extents;
        if (cairo_region_num_rectangles(sel->region) > 0) {
            cairo_region_get_extents(sel->region, &extents);

            /* Create mask surface covering the entire region bounds */
            sel->mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                   extents.width + extents.x,
                                                   extents.height + extents.y);
        }
    }

    return sel->mask;
}

/**
 * Get the region
 */
cairo_region_t* selection_get_region(Selection* sel) {
    if (!sel) {
        return NULL;
    }
    return sel->region;
}

/**
 * Update animation phase for marching ants
 */
void selection_update_animation(Selection* sel) {
    if (!sel) {
        return;
    }

    if (sel->animated) {
        /* Increment phase for dash animation (2-pixel dash = 4 pixel period) */
        sel->animation_phase = (sel->animation_phase + 1) % 4;
    }
}

/**
 * Enable or disable animated marching ants
 */
void selection_set_animated(Selection* sel, gboolean enabled) {
    if (!sel) {
        return;
    }
    sel->animated = enabled;
}

/**
 * Render selection overlay to Cairo context
 */
void selection_render_overlay(Selection* sel, cairo_t* cr, gdouble zoom_factor) {
    if (!sel || !cr || selection_is_empty(sel)) {
        return;
    }

    gint num_rects = cairo_region_num_rectangles(sel->region);
    if (num_rects == 0) {
        return;
    }

    /* Draw marching ants outline for each rectangle in the region using shared helper */
    gdouble line_width = 1.0 / zoom_factor;
    gdouble animation_offset = (gdouble)sel->animation_phase;

    for (gint i = 0; i < num_rects; i++) {
        cairo_rectangle_int_t rect;
        cairo_region_get_rectangle(sel->region, i, &rect);

        selection_draw_marching_ants(cr, (double)rect.x, (double)rect.y,
                                     (double)rect.width, (double)rect.height,
                                     line_width, animation_offset);
    }
}
