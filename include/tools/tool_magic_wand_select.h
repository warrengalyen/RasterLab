#ifndef TOOL_MAGIC_WAND_SELECT_H
#define TOOL_MAGIC_WAND_SELECT_H

#include "selection.h"
#include "tool_options.h"
#include "tools.h"

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct SelectionMask SelectionMask;

/**
 * Magic Wand Selection Tool state.
 *
 * After the user clicks, the tool computes a flood-fill-based selection
 * preview immediately.  The start node is draggable: releasing and
 * re-clicking the node moves the seed position and recomputes the fill.
 * ESC cancels the preview without applying it.
 */
typedef struct {
    gboolean has_start_point; /* TRUE once the user has clicked */
    gint start_x;             /* Seed point in document (image) space */
    gint start_y;

    gboolean is_dragging_node; /* TRUE while user drags the start node */
    gint drag_anchor_x;
    gint drag_anchor_y;

    gboolean has_been_finalized; /* Prevents double-apply on tool switch */

    /* Flood-fill preview mask (document-space, bounded) */
    SelectionMask* preview_mask;

    /* Marching ants animation */
    guint animation_timer_id;
    gint animation_phase;

    /* Cached options (read on every action) */
    SelectionCombineMode combine_mode;
    SelectionSmoothingMode smooth_mode;
    gfloat feather_radius;
    gboolean animate;
    gfloat tolerance;
    FillCompareMode compare_mode;
    gboolean contiguous;
} MagicWandSelectToolState;

/** Create the Magic Wand Selection Tool */
Tool* tool_magic_wand_select_create(void);

/** Draw the preview overlay (node + marching-ants outline) */
void tool_magic_wand_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

/** Apply the current preview to the document selection mask */
void tool_magic_wand_select_finalize(Tool* tool, ImageDocument* doc);

/** Re-read options and recompute the preview (called when options change) */
void tool_magic_wand_select_update_preview(Tool* tool, ImageDocument* doc);

/** Reset all transient state (called when switching away from the tool) */
void tool_magic_wand_select_reset(Tool* tool);

#endif /* TOOL_MAGIC_WAND_SELECT_H */
