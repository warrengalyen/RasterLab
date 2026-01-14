#include "selection.h"
#include <gdk/gdk.h>
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

/**
 * Detect which handle (if any) is at the given point
 * Used by rectangular and elliptical selection tools for handle detection
 * @param x Point X coordinate in image space
 * @param y Point Y coordinate in image space
 * @param sel_x Selection bounding rectangle X
 * @param sel_y Selection bounding rectangle Y
 * @param sel_w Selection bounding rectangle width
 * @param sel_h Selection bounding rectangle height
 * @param zoom_factor Document zoom factor
 * @return Handle index (0-3 for corners, -1 if no handle detected)
 */
gint selection_detect_handle_at_point(gdouble x, gdouble y,
                                      gdouble sel_x, gdouble sel_y,
                                      gdouble sel_w, gdouble sel_h,
                                      gdouble zoom_factor) {
    /* Handle is 12 screen pixels, so half_handle = 6 screen pixels = 6/zoom image pixels */
    gdouble half_handle = 6.0 / zoom_factor;
    gdouble corners[4][2] = {
        {sel_x, sel_y},                /* top-left */
        {sel_x + sel_w, sel_y},        /* top-right */
        {sel_x, sel_y + sel_h},        /* bottom-left */
        {sel_x + sel_w, sel_y + sel_h} /* bottom-right */
    };

    /* Check if clicking on a handle - use rectangular hit test since handles are square */
    for (gint i = 0; i < 4; i++) {
        gdouble dx = x - corners[i][0];
        gdouble dy = y - corners[i][1];
        /* Rectangular hit test: check if point is within handle square bounds */
        if (fabs(dx) <= half_handle && fabs(dy) <= half_handle) {
            return i;
        }
    }

    return -1; /* No handle detected */
}

/**
 * Set cursor based on selection handle type
 * Used by rectangular and elliptical selection tools
 * @param window GdkWindow to set cursor on
 * @param handle Handle index (-1 = move, 0-3 = corner handles)
 * @param default_cursor Default cursor to use if handle is invalid
 */
void selection_set_cursor_for_handle(GdkWindow* window, gint handle, GdkCursor* default_cursor) {
    if (!window)
        return;

    GdkDisplay* display = gdk_window_get_display(window);
    GdkCursor* cursor = NULL;

    if (handle == -1) {
        /* Move cursor */
        cursor = gdk_cursor_new_from_name(display, "move");
        if (!cursor) {
            cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
        }
    } else if (handle == 0) {
        /* Top-left: NW-SE diagonal */
        cursor = gdk_cursor_new_from_name(display, "nwse-resize");
    } else if (handle == 1) {
        /* Top-right: NE-SW diagonal */
        cursor = gdk_cursor_new_from_name(display, "nesw-resize");
    } else if (handle == 2) {
        /* Bottom-left: NE-SW diagonal */
        cursor = gdk_cursor_new_from_name(display, "nesw-resize");
    } else if (handle == 3) {
        /* Bottom-right: NW-SE diagonal */
        cursor = gdk_cursor_new_from_name(display, "nwse-resize");
    } else {
        /* Default cursor */
        gdk_window_set_cursor(window, default_cursor);
        return;
    }

    if (cursor) {
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }
}
