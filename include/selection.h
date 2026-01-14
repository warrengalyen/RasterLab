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
 * Used by selection preview rendering
 * @param cr Cairo context to draw on
 * @param x X coordinate of rectangle
 * @param y Y coordinate of rectangle
 * @param width Width of rectangle
 * @param height Height of rectangle
 * @param line_width Line width to use
 * @param animation_phase Dash offset for animation
 */
void selection_draw_marching_ants(cairo_t* cr, gdouble x, gdouble y,
                                  gdouble width, gdouble height,
                                  gdouble line_width, gdouble animation_phase);

/**
 * Draw marching ants outline for an ellipse
 * Used by elliptical selection preview rendering
 * @param cr Cairo context to draw on
 * @param x X coordinate of bounding rectangle
 * @param y Y coordinate of bounding rectangle
 * @param width Width of bounding rectangle
 * @param height Height of bounding rectangle
 * @param line_width Line width to use (unused, kept for API compatibility)
 * @param animation_phase Dash offset for animation
 */
void selection_draw_marching_ants_ellipse(cairo_t* cr, gdouble x, gdouble y,
                                          gdouble width, gdouble height,
                                          gdouble line_width, gdouble animation_phase);

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
