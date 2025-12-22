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
 * Selection structure - represents a selection region
 * Manages both pixel-accurate region and optional alpha mask for smooth modes
 */
typedef struct Selection {
    cairo_region_t* region;             /* Base selection mask (pixel-accurate) */
    cairo_surface_t* mask;              /* Optional alpha mask for AA/feathered modes */
    gboolean animated;                  /* Marching ants enabled */
    gint animation_phase;               /* Dash offset for animated outline */
    SelectionSmoothingMode smooth_mode; /* Current smoothing mode */
    gint feather_radius;                /* Feather radius in pixels (for SMOOTH_FEATHERED) */
} Selection;

/**
 * Create a new empty selection
 * @return Newly allocated Selection with empty region
 */
Selection* selection_new(void);

/**
 * Free a selection and its resources
 * @param sel The selection to free
 */
void selection_free(Selection* sel);

/**
 * Check if a selection is empty
 * @param sel The selection to check
 * @return TRUE if selection is empty or NULL, FALSE otherwise
 */
gboolean selection_is_empty(Selection* sel);

/**
 * Clear a selection (make it empty)
 * @param sel The selection to clear
 */
void selection_clear(Selection* sel);

/**
 * Create a rectangular selection
 * @param x X coordinate in image space
 * @param y Y coordinate in image space
 * @param width Width of rectangle
 * @param height Height of rectangle
 * @param smooth_mode Smoothing mode to apply
 * @param feather_radius Feather radius (only used for SMOOTH_FEATHERED)
 * @return Newly allocated Selection with rectangular region
 */
Selection* selection_create_rectangle(gint x, gint y, gint width, gint height,
                                      SelectionSmoothingMode smooth_mode,
                                      gint feather_radius);

/**
 * Combine two selections
 * @param dest The destination selection (modified in place)
 * @param src The source selection to combine
 * @param mode How to combine the selections
 * @return TRUE on success, FALSE on failure
 */
gboolean selection_combine(Selection* dest, Selection* src, SelectionCombineMode mode);

/**
 * Get the alpha mask for rendering (creates if needed)
 * @param sel The selection
 * @return Cairo surface with alpha mask, or NULL if none
 */
cairo_surface_t* selection_get_mask(Selection* sel);

/**
 * Get the region (pixel-accurate representation)
 * @param sel The selection
 * @return Cairo region, or NULL if empty
 */
cairo_region_t* selection_get_region(Selection* sel);

/**
 * Update animation phase for marching ants
 * Should be called periodically (e.g., every 100ms)
 * @param sel The selection
 */
void selection_update_animation(Selection* sel);

/**
 * Enable or disable animated marching ants
 * @param sel The selection
 * @param enabled TRUE to enable, FALSE to disable
 */
void selection_set_animated(Selection* sel, gboolean enabled);

/**
 * Render selection overlay to Cairo context
 * Draws marching ants outline and/or mask visualization
 * @param sel The selection
 * @param cr Cairo context to draw on
 * @param zoom_factor Current zoom level (for line width adjustment)
 */
void selection_render_overlay(Selection* sel, cairo_t* cr, gdouble zoom_factor);

/**
 * Draw marching ants outline for a rectangle
 * Used by both selection preview and final selection rendering
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

#endif /* SELECTION_H */
