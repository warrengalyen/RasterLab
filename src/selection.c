/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "selection.h"
#include <gdk/gdk.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>

/**
 * Draw marching ants outline for a rectangle using alternating pixels
 * Dash size = ANT_DASH_SIZE/zoom in image space so it appears constant on screen (like finalized outline).
 */
void selection_draw_marching_ants(cairo_t* cr, gdouble x, gdouble y,
                                  gdouble width, gdouble height,
                                  gdouble line_width, gdouble animation_phase, gdouble zoom) {
    if (!cr || width <= 0 || height <= 0) {
        return;
    }

    gint x_start = (gint)x;
    gint y_start = (gint)y;
    gint x_end = (gint)(x + width);
    gint y_end = (gint)(y + height);
    gint dash_phase = (gint)animation_phase;
    gint dash_size = (gint)(ANT_DASH_SIZE / zoom);
    if (dash_size < 1) {
        dash_size = 1;
    }

    (void)line_width; /* Unused parameter - kept for API compatibility */

    /* Draw top and bottom edges */
    for (gint px = x_start; px <= x_end; px++) {
        /* Top edge - shift pattern by dash_phase for animation */
        int pattern = ((px + y_start) / dash_size + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, px, y_start, 1.0, 1.0);
        cairo_fill(cr);

        /* Bottom edge */
        pattern = ((px + y_end) / dash_size + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, px, y_end, 1.0, 1.0);
        cairo_fill(cr);
    }

    /* Draw left and right edges */
    for (gint py = y_start; py <= y_end; py++) {
        /* Left edge */
        int pattern = ((x_start + py) / dash_size + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, x_start, py, 1.0, 1.0);
        cairo_fill(cr);

        /* Right edge */
        pattern = ((x_end + py) / dash_size + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, x_end, py, 1.0, 1.0);
        cairo_fill(cr);
    }
}

/**
 * Draw marching ants outline for an ellipse using alternating pixels
 * Dash size = ANT_DASH_SIZE/zoom in image space so it appears constant on screen (like finalized outline).
 */
void selection_draw_marching_ants_ellipse(cairo_t* cr, gdouble x, gdouble y,
                                          gdouble width, gdouble height,
                                          gdouble line_width, gdouble animation_phase, gdouble zoom) {
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
    gdouble dash_size = ANT_DASH_SIZE / zoom;
    if (dash_size < 1.0) {
        dash_size = 1.0;
    }

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
    gdouble prev_ex = 0.0, prev_ey = 0.0;
    gboolean first_sample = TRUE;

    for (gint i = 0; i <= num_samples; i++) {
        gdouble theta = (2.0 * M_PI * i) / num_samples;

        /* Parametric ellipse equations */
        gdouble ex = cx + rx * cos(theta);
        gdouble ey = cy + ry * sin(theta);

        /* Accumulate arc length using float-to-float distance at every sample so
         * the measurement is a smooth numerical integral of the ellipse arc length,
         * independent of which samples happen to land on duplicate pixels. */
        if (!first_sample) {
            gdouble ddx = ex - prev_ex;
            gdouble ddy = ey - prev_ey;
            arc_length += sqrt(ddx * ddx + ddy * ddy);
        }
        prev_ex = ex;
        prev_ey = ey;
        first_sample = FALSE;

        gint px = (gint)ex;
        gint py = (gint)ey;

        /* Skip duplicate pixels - only render each unique pixel once */
        if (px == prev_px && py == prev_py) {
            continue;
        }

        /* Calculate pattern based on arc length for consistent marching effect */
        gint pattern = ((gint)(arc_length / dash_size) + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, px, py, 1.0, 1.0);
        cairo_fill(cr);

        prev_px = px;
        prev_py = py;
    }
}

/**
 * Rasterize line (x0,y0)->(x1,y1) and draw marching ants at each pixel.
 * Uses path-length-based pattern for consistent dash lengths at all angles.
 * @param path_pos_start Accumulated path length at start of this segment
 * @return Accumulated path length at end of this segment
 */
static gdouble draw_marching_ants_line(cairo_t* cr, int x0, int y0, int x1, int y1,
                                       gdouble dash_size, gint dash_phase,
                                       gdouble path_pos_start) {
    double dx = x1 - x0;
    double dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9) {
        return path_pos_start;
    }
    int steps = (int)ceil(len);
    if (steps < 1) {
        steps = 1;
    }
    for (int i = 0; i <= steps; i++) {
        double t = (steps > 0) ? (double)i / steps : 0;
        double px = x0 + t * dx;
        double py = y0 + t * dy;
        int x = (int)round(px);
        int y = (int)round(py);
        double path_pos = path_pos_start + t * len;
        int pattern = ((int)(path_pos / dash_size) + dash_phase) % 2;
        cairo_set_source_rgb(cr, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0, pattern ? 0.0 : 1.0);
        cairo_rectangle(cr, (gdouble)x, (gdouble)y, 1.0, 1.0);
        cairo_fill(cr);
    }
    return path_pos_start + len;
}

/**
 * Draw marching ants outline along a polygonal path - same style as closed selection.
 * Uses path-length-based pattern for consistent dash lengths at all angles.
 */
void selection_draw_marching_ants_path(cairo_t* cr, GArray* points, gboolean closed,
                                       gint cursor_x, gint cursor_y,
                                       gdouble animation_phase, gdouble zoom) {
    if (!cr || !points || points->len < 1) {
        return;
    }

    gint dash_phase = (gint)animation_phase;
    gdouble dash_size = ANT_DASH_SIZE / zoom;
    if (dash_size < 1.0) {
        dash_size = 1.0;
    }

    guint n = points->len;
    gdouble path_pos = 0.0;

    /* Draw each edge - path-length-based pattern for consistent dash lengths */
    for (guint i = 0; i < n; i++) {
        GdkPoint* a = &g_array_index(points, GdkPoint, i);
        GdkPoint* b = &g_array_index(points, GdkPoint, (i + 1) % n);
        if (!closed && (i + 1) == n) {
            break;
        }
        path_pos = draw_marching_ants_line(cr, a->x, a->y, b->x, b->y,
                                          dash_size, dash_phase, path_pos);
    }

    /* When not closed, draw from last point to cursor */
    if (!closed && n >= 1) {
        GdkPoint* a = &g_array_index(points, GdkPoint, n - 1);
        draw_marching_ants_line(cr, a->x, a->y, cursor_x, cursor_y,
                               dash_size, dash_phase, path_pos);
    }
}

#define LASSO_DOUBLE_STROKE_WIDTH 0.5

/**
 * Draw closed path: normal marching ants dashed line with black 0.5px stroke on each side.
 * Used for lasso completed preview.
 */
void selection_draw_marching_ants_path_double_stroke(cairo_t* cr, GArray* points,
                                                     gdouble animation_phase, gdouble zoom) {
    if (!cr || !points || points->len < 2) {
        return;
    }

    guint n = points->len;
    gdouble stroke_width = LASSO_DOUBLE_STROKE_WIDTH;

    /* 1. Black 0.5px stroke on each side: line_width 1.0 = 0.5px inside + 0.5px outside path */
    cairo_new_path(cr);
    GdkPoint* p0 = &g_array_index(points, GdkPoint, 0);
    cairo_move_to(cr, p0->x + 0.5, p0->y + 0.5);
    for (guint i = 1; i < n; i++) {
        GdkPoint* p = &g_array_index(points, GdkPoint, i);
        cairo_line_to(cr, p->x + 0.5, p->y + 0.5);
    }
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, stroke_width * 2.0);
    cairo_stroke(cr);

    /* 2. Normal marching ants dashed line along the path */
    gint dash_phase = (gint)animation_phase;
    gdouble dash_size = ANT_DASH_SIZE / zoom;
    if (dash_size < 1.0) {
        dash_size = 1.0;
    }
    gdouble path_pos = 0.0;
    for (guint i = 0; i < n; i++) {
        GdkPoint* a = &g_array_index(points, GdkPoint, i);
        GdkPoint* b = &g_array_index(points, GdkPoint, (i + 1) % n);
        path_pos = draw_marching_ants_line(cr, a->x, a->y, b->x, b->y,
                                          dash_size, dash_phase, path_pos);
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
