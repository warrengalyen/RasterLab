#ifndef TOOL_TEXT_H
#define TOOL_TEXT_H

#include "tools.h"
#include <cairo/cairo.h>

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct ImageLayer    ImageLayer;

/**
 * Text Tool drag-mode values:
 *  -2  creating a new box (initial drag from empty canvas)
 *  -1  moving the whole box
 *   0  resize handle — top-left corner
 *   1  resize handle — top-right corner
 *   2  resize handle — bottom-left corner
 *   3  resize handle — bottom-right corner
 *   4  rotation handle — top-edge midpoint   (circular)
 *   5  rotation handle — right-edge midpoint (circular)
 *   6  rotation handle — bottom-edge midpoint(circular)
 *   7  rotation handle — left-edge midpoint  (circular)
 */

/**
 * Per-instance state for the Text Tool.
 *
 * Coordinates are always in document (image) space.
 * The tool keeps a weak pointer to the ImageLayer it last created / is
 * editing.  If the layer is removed by the user, that pointer becomes
 * stale – we guard every dereference with a list-search check.
 */
typedef struct {
    /* Box / layer */
    gboolean  has_box;          /* A box / text layer exists */
    gboolean  is_dragging;      /* Mouse button is held */
    gint      drag_mode;        /* See drag-mode values above */
    gint      start_x;          /* Mouse-down position (image space) */
    gint      start_y;
    gint      current_x;        /* Current mouse position (image space) */
    gint      current_y;
    gint      box_x;            /* Finalized box origin */
    gint      box_y;
    gint      box_w;            /* Finalized box dimensions */
    gint      box_h;
    gint      hovered_handle;   /* -2=outside, -1=inside(move), 0-7=handle */
    ImageLayer* layer;          /* Weak ref to the text layer being edited */

    /* Text editing */
    gboolean  is_editing;       /* TRUE while keyboard input is active */
    gint      cursor_pos;       /* Cursor byte-offset in TextLayer->text */
    gboolean  cursor_visible;   /* Current blink state (TRUE = draw caret) */
    guint     cursor_blink_tag; /* GLib timeout source id; 0 when stopped */
    ImageDocument* blink_doc;   /* Weak ref for blink-timer callback */
} TextToolState;

/**
 * Create the Text Tool.
 * @return Newly allocated Tool, or NULL on failure.
 */
Tool* tool_text_create(void);

/**
 * Draw the bounding-box overlay and resize handles.
 * Called from the viewport draw callback so handles are not clipped.
 * @param doc  Active image document
 * @param cr   Cairo context (viewport coordinates, no zoom applied yet)
 * @param zoom Current zoom factor
 */
void tool_text_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

/**
 * Reset the tool state (called when switching away from the tool).
 * Exits edit mode and stops the blink timer. Does NOT destroy the text layer.
 * @param tool The text tool
 */
void tool_text_reset(Tool* tool);

/**
 * Returns TRUE if the text tool is currently in text-editing mode.
 * Safe to call with any tool pointer; returns FALSE if @tool is not TOOL_TEXT.
 */
gboolean tool_text_is_editing(Tool* tool);

#endif /* TOOL_TEXT_H */
