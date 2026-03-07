#ifndef TOOL_POLYGON_SELECT_H
#define TOOL_POLYGON_SELECT_H

#include "selection.h"
#include "tools.h"

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct SelectionMask SelectionMask;

/**
 * Polygon Selection Tool state
 * Points are stored in image space. First point stays highlighted until polygon is closed.
 */
typedef struct {
    GArray* points;                    /* GdkPoint (x,y) in image space */
    gboolean closed;                   /* TRUE when user connected last to first */
    gboolean is_editing;                /* Editing closed polygon (move nodes or move whole) */
    gboolean has_been_finalized;        /* Has this selection been finalized to undo stack? */
    guint animation_timer_id;           /* Timer ID for marching ants (0 = no timer) */
    gint dragging_node;                 /* Node index being dragged, or -1 = move whole, -2 = none */
    gint anchor_x;                      /* Mouse anchor X for drag */
    gint anchor_y;                      /* Mouse anchor Y for drag */
    gint hovered_node;                  /* Node under cursor (-1 = none, 0..n = node index) */
    gboolean hovered_interior;          /* Cursor over polygon interior (for move cursor) */
    gint cursor_x;                      /* Current mouse X (for preview line to cursor) */
    gint cursor_y;                      /* Current mouse Y (for preview line to cursor) */
    gint animation_phase;               /* Marching ants phase (0-3) */
    SelectionCombineMode combine_mode;
    SelectionSmoothingMode smooth_mode;
    gint feather_radius;
    gfloat curvature;
    gint area_mode;
    gint border_width;

    /* Feathered-preview cache — see RectSelectToolState for full description.
     * For the polygon tool the geometry is an arbitrary point list, so we copy
     * the vertex arrays rather than just four integers. */
    SelectionMask*         preview_cache;
    gint                   cache_n_points;
    gint*                  cache_points_x;       /* Heap-allocated copies, NULL = no cache */
    gint*                  cache_points_y;
    gint                   cache_feather_radius;
    SelectionSmoothingMode cache_smooth_mode;
    SelectionCombineMode   cache_combine_mode;
    gfloat                 cache_curvature;
    gint                   cache_area_mode;
    gint                   cache_border_width;
} PolygonSelectToolState;

/**
 * Create the Polygon Selection Tool
 */
Tool* tool_polygon_select_create(void);

/**
 * Draw polygon selection preview (outline, circular nodes, first node highlighted until closed)
 */
void tool_polygon_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

/**
 * Finalize current preview to document selection mask
 */
void tool_polygon_select_finalize(Tool* tool, ImageDocument* doc);

/**
 * Reset tool state when switching away
 */
void tool_polygon_select_reset(Tool* tool);

#endif /* TOOL_POLYGON_SELECT_H */
