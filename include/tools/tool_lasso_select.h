#ifndef TOOL_LASSO_SELECT_H
#define TOOL_LASSO_SELECT_H

#include "selection.h"
#include "tools.h"

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct SelectionMask SelectionMask;

/**
 * Lasso Selection Tool state
 * Freehand path in image space. Single node shown while drawing; path completes on mouse up.
 */
typedef struct {
    GArray* points;              /* GdkPoint (x,y) in image space */
    gboolean completed;          /* TRUE when mouse released, path closed */
    gboolean has_been_finalized; /* Has this selection been finalized to undo stack? */
    gboolean is_dragging;        /* TRUE when dragging completed preview */
    gint drag_anchor_x;          /* Mouse X at drag start */
    gint drag_anchor_y;          /* Mouse Y at drag start */
    guint animation_timer_id;    /* Timer ID for marching ants (0 = no timer) */
    gint cursor_x;               /* Current mouse X (for marching ants line to cursor) */
    gint cursor_y;               /* Current mouse Y */
    gint animation_phase;        /* Marching ants phase (0-3) */
    SelectionCombineMode combine_mode;
    SelectionSmoothingMode smooth_mode;
    gint feather_radius;
    gint area_mode;
    gint border_width;

    /* Feathered-preview cache. Invalidation: points or any option. */
    SelectionMask* preview_cache;
    gint cache_n_points;
    gint* cache_points_x;
    gint* cache_points_y;
    gint cache_feather_radius;
    SelectionSmoothingMode cache_smooth_mode;
    SelectionCombineMode cache_combine_mode;
    gint cache_area_mode;
    gint cache_border_width;
} LassoSelectToolState;

/**
 * Create the Lasso Selection Tool
 */
Tool* tool_lasso_select_create(void);

/**
 * Draw lasso selection preview (single node while drawing, marching ants when completed)
 */
void tool_lasso_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

/**
 * Finalize current path to document selection mask
 */
void tool_lasso_select_finalize(Tool* tool, ImageDocument* doc);

/**
 * Reset tool state when switching away
 */
void tool_lasso_select_reset(Tool* tool);

#endif /* TOOL_LASSO_SELECT_H */
