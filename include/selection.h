/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef SELECTION_H
#define SELECTION_H

#include <cairo.h>
#include <glib.h>

/* Forward declarations */
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkCursor GdkCursor;

/* Selection animation constants */
#define ANT_DASH_SIZE 4.0f /* Marching ants dash length in pixels */
#define ANT_DASH_SPEED_SLOW 200 /* Frame time in ms (5 fps) */
#define ANT_DASH_SPEED_NORMAL 67 /* Frame time in ms (15 fps) */
#define ANT_DASH_SPEED_FAST 33 /* Frame time in ms (30 fps) */
#define ANT_DASH_SPEED_MIN 16 /* Frame time in ms (60 fps) */

/**
 * Selection combination modes - how to combine a new selection with existing one
 */
typedef enum {
    SELECTION_COMBINE_NEW,      /* Replace existing selection entirely */
    SELECTION_COMBINE_ADD,      /* Union rectangle with existing selection */
    SELECTION_COMBINE_SUBTRACT, /* Subtract rectangle from existing selection */
    SELECTION_COMBINE_INTERSECT /* Intersect rectangle with existing selection */
} SelectionCombineMode;

/**
 * Selection smoothing modes - how to handle selection edges
 */
typedef enum {
    SELECTION_SMOOTH_NONE,        /* Hard pixel edges, pixel-accurate */
    SELECTION_SMOOTH_ANTIALIASED, /* Antialiased edges via alpha mask */
    SELECTION_SMOOTH_FEATHERED    /* Feathered (blurred) edges */
} SelectionSmoothingMode;

/**
 * Draw marching ants outline for a rectangle
 * Used by selection preview rendering. Pass zoom so dash size is constant on screen (like finalized outline).
 * @param cr Cairo context (expected in image space with scale(zoom) already applied by caller)
 * @param zoom Zoom factor; dash size = ANT_DASH_SIZE/zoom in image space for constant screen size
 */
void selection_draw_marching_ants(cairo_t* cr, gdouble x, gdouble y,
                                  gdouble width, gdouble height,
                                  gdouble line_width, gdouble animation_phase, gdouble zoom);

/**
 * Draw marching ants outline for an ellipse
 * Used by elliptical selection preview rendering. Pass zoom so dash size is constant on screen.
 * @param cr Cairo context (expected in image space with scale(zoom) already applied by caller)
 * @param zoom Zoom factor; dash size = ANT_DASH_SIZE/zoom in image space for constant screen size
 */
void selection_draw_marching_ants_ellipse(cairo_t* cr, gdouble x, gdouble y,
                                          gdouble width, gdouble height,
                                          gdouble line_width, gdouble animation_phase, gdouble zoom);

/**
 * Draw marching ants outline along a polygonal path (same style as rect/ellipse).
 * points: GArray of GdkPoint. closed: if FALSE, also draw from last point to (cursor_x, cursor_y).
 * @param cr Cairo context (expected in image space with scale(zoom) already applied)
 * @param zoom Zoom factor; dash size = ANT_DASH_SIZE/zoom
 */
void selection_draw_marching_ants_path(cairo_t* cr, GArray* points, gboolean closed,
                                       gint cursor_x, gint cursor_y,
                                       gdouble animation_phase, gdouble zoom);

/**
 * Draw marching ants outline along a closed path with 1px stroke on each side.
 * Same style as selection_draw_marching_ants_path but with perpendicular offset strokes.
 * Used for lasso selection completed preview.
 */
void selection_draw_marching_ants_path_double_stroke(cairo_t* cr, GArray* points,
                                                     gdouble animation_phase, gdouble zoom);

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
                                      gdouble zoom_factor);

/**
 * Set cursor based on selection handle type
 * Used by rectangular and elliptical selection tools
 * @param window GdkWindow to set cursor on
 * @param handle Handle index (-1 = move, 0-3 = corner handles)
 * @param default_cursor Default cursor to use if handle is invalid
 */
void selection_set_cursor_for_handle(GdkWindow* window, gint handle, GdkCursor* default_cursor);

#endif /* SELECTION_H */
