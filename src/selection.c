#include "selection.h"
#include <math.h>

/**
 * Draw marching ants outline for a rectangle using alternating pixels
 * @param cr Cairo context to draw on
 * @param x Rectangle x position
 * @param y Rectangle y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param line_width Line width (unused, kept for API compatibility)
 * @param animation_phase Animation phase (0-3) for marching effect
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
