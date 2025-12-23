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

    return cairo_region_num_rectangles(sel->region) == 0;
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
