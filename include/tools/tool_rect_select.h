#ifndef TOOL_RECT_SELECT_H
#define TOOL_RECT_SELECT_H

#include "selection.h"
#include "tools.h"

/**
 * Rectangular Selection Tool state and options
 */
typedef struct {
    gboolean is_dragging;               /* Currently dragging a selection rectangle? */
    gboolean is_editing;                /* Currently editing an existing selection? */
    guint animation_timer_id;           /* Timer ID for marching ants animation (0 = no timer) */
    gint anchor_x;                      /* Mouse down position X in image space */
    gint anchor_y;                      /* Mouse down position Y in image space */
    gint current_x;                     /* Current mouse position X in image space */
    gint current_y;                     /* Current mouse position Y in image space */
    gint selection_x;                   /* Selection rectangle position X */
    gint selection_y;                   /* Selection rectangle position Y */
    gint selection_w;                   /* Selection rectangle width */
    gint selection_h;                   /* Selection rectangle height */
    gint dragging_handle;               /* Which handle is being dragged (-1 = none or move) */
    gint animation_phase;               /* Animation phase for marching ants (0-3) */
    SelectionCombineMode combine_mode;  /* How to combine with existing selection */
    SelectionSmoothingMode smooth_mode; /* Edge smoothing mode */
    gint feather_radius;                /* Feather radius in pixels */
    gint hovered_handle;                /* Which handle is under mouse (-1 = none, 0-3 = corners) */
} RectSelectToolState;

/**
 * Create the Rectangular Selection Tool
 * @return Newly created Tool, or NULL on failure
 */
Tool* tool_rect_select_create(void);

#endif /* TOOL_RECT_SELECT_H */
