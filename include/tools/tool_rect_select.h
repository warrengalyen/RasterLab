/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_RECT_SELECT_H
#define TOOL_RECT_SELECT_H

#include "selection.h"
#include "tools.h"

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct SelectionMask SelectionMask;

/**
 * Rectangular Selection Tool state and options
 */
typedef struct {
    gboolean is_dragging;               /* Currently dragging a selection rectangle? */
    gboolean is_editing;                /* Currently editing an existing selection? */
    gboolean has_been_finalized;        /* Has this selection been finalized to undo stack? */
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

    /* Feathered-preview cache.
     * The cache holds a fully computed SelectionMask (including EDT-based feathered
     * preview) for the current selection geometry and feather parameters.  It is
     * reused on every animation-timer redraw so the expensive EDT is only run when
     * the selection bounds, feather radius, or mode actually change. */
    SelectionMask*         preview_cache;        /* Cached mask, NULL = needs rebuild */
    gint                   cache_rect_x;
    gint                   cache_rect_y;
    gint                   cache_rect_w;
    gint                   cache_rect_h;
    gint                   cache_feather_radius;
    SelectionSmoothingMode cache_smooth_mode;
    SelectionCombineMode   cache_combine_mode;
} RectSelectToolState;

/**
 * Create the Rectangular Selection Tool
 * @return Newly created Tool, or NULL on failure
 */
Tool* tool_rect_select_create(void);

/**
 * Draw rectangular selection preview during editing/dragging
 * @param doc The active image document
 * @param cr Cairo context to draw on
 * @param zoom Current zoom level
 */
void tool_rect_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

/**
 * Animation timer callback for rectangular selection tool preview
 * Updates the marching ants animation phase
 * @param user_data Pointer to ImageDocument
 * @return TRUE to keep timer running, FALSE to stop
 */
gboolean tool_rect_select_animation_timer(gpointer user_data);

/**
 * Finalize the current preview selection to the document selection mask
 * Called when switching tools or clicking outside an editable selection
 * @param tool The rectangular selection tool
 * @param doc The document to finalize to
 */
void tool_rect_select_finalize(Tool* tool, ImageDocument* doc);

/**
 * Reset rectangular selection tool state when switching away
 * Clears the edit preview but preserves the finalized selection in doc->selection_mask
 * @param tool The rectangular selection tool
 */
void tool_rect_select_reset(Tool* tool);

#endif /* TOOL_RECT_SELECT_H */
