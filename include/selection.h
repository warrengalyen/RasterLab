#ifndef SELECTION_H
#define SELECTION_H

#include <cairo.h>
#include <glib.h>

/* Selection animation constants */
#define ANT_DASH_SIZE 4.0f       /* Marching ants dash length in pixels */
#define ANT_DASH_SPEED_SLOW 200  /* Frame time in ms (5 fps) */
#define ANT_DASH_SPEED_NORMAL 67 /* Frame time in ms (15 fps) */
#define ANT_DASH_SPEED_FAST 33   /* Frame time in ms (30 fps) */
#define ANT_DASH_SPEED_MIN 16    /* Frame time in ms (60 fps) */

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

#endif /* SELECTION_H */
