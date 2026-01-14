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

/**
 * Draw marching ants outline for an ellipse using alternating pixels
 * @param cr Cairo context to draw on
 * @param x Bounding rectangle x position
 * @param y Bounding rectangle y position
 * @param width Bounding rectangle width
 * @param height Bounding rectangle height
 * @param line_width Line width (unused, kept for API compatibility)
 * @param animation_phase Animation phase (0-3) for marching effect
 */
void selection_draw_marching_ants_ellipse(cairo_t* cr, gdouble x, gdouble y,
                                          gdouble width, gdouble height,
                                          gdouble line_width, gdouble animation_phase) {
    if (!cr || width <= 0 || height <= 0) {
        return;
    }

    (void)line_width; /* Unused parameter - kept for API compatibility */

    /* Calculate ellipse center and radii */
    gdouble cx = x + width / 2.0;
    gdouble cy = y + height / 2.0;
    gdouble rx = width / 2.0;
    gdouble ry = height / 2.0;
    gint dash_phase = (gint)animation_phase;

    /* Draw the ellipse outline using Bresenham-style pixel plotting
     * We'll iterate around the ellipse and draw individual pixels */

    /* Use parametric approach: for each angle, compute the pixel on the ellipse boundary */
    /* Number of samples should be proportional to the ellipse circumference */
    /* Approximation of ellipse circumference: pi * (3*(rx+ry) - sqrt((3*rx+ry)*(rx+3*ry))) */
    gdouble circumference = M_PI * (3.0 * (rx + ry) - sqrt((3.0 * rx + ry) * (rx + 3.0 * ry)));
    gint num_samples = (gint)(circumference * 2.0); /* 2 samples per pixel for smoother outline */
    if (num_samples < 100)
        num_samples = 100;
    if (num_samples > 10000)
        num_samples = 10000;

    gint prev_px = -1000, prev_py = -1000;
    gdouble arc_length = 0.0;

    for (gint i = 0; i <= num_samples; i++) {
        gdouble theta = (2.0 * M_PI * i) / num_samples;

        /* Parametric ellipse equations */
        gdouble ex = cx + rx * cos(theta);
        gdouble ey = cy + ry * sin(theta);

        gint px = (gint)ex;
        gint py = (gint)ey;

        /* Skip duplicate pixels */
        if (px == prev_px && py == prev_py) {
            continue;
        }

        /* Calculate arc length for animation (approximate) */
        if (prev_px != -1000) {
            gdouble dx = ex - (prev_px + 0.5);
            gdouble dy = ey - (prev_py + 0.5);
            arc_length += sqrt(dx * dx + dy * dy);
        }

        /* Calculate pattern based on arc length for consistent marching effect */
        gint pattern = ((gint)(arc_length / ANT_DASH_SIZE) + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, px, py, 1.0, 1.0);
        cairo_fill(cr);

        prev_px = px;
        prev_py = py;
    }
}
