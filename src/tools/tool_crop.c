#include "tools/tool_crop.h"
#include "document.h"
#include "tool_manager.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <stdlib.h>

/**
 * Crop Tool: mouse down - placeholder for Stage 2
 */
static void crop_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    (void)tool;
    (void)doc;
    (void)event;
    /* Stage 2: start new crop, detect move, detect resize handles */
}

/**
 * Crop Tool: mouse move - placeholder for Stage 2
 */
static void crop_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    (void)tool;
    (void)doc;
    (void)event;
    /* Stage 2: update rectangle based on drag mode */
}

/**
 * Crop Tool: mouse up - placeholder for Stage 2
 */
static void crop_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    (void)tool;
    (void)doc;
    (void)event;
    /* Stage 2: finalize rect state, do not apply crop yet */
}

/**
 * Reset crop tool state (called when tool is deactivated)
 */
void tool_crop_reset(Tool* tool) {
    if (!tool || !tool->user_data) {
        return;
    }

    CropToolState* state = (CropToolState*)tool->user_data;

    state->is_active = FALSE;
    state->is_dragging = FALSE;
    state->drag_mode = -2;
    state->hovered_handle = -1;
}

/**
 * Create the Crop Tool
 */
Tool* tool_crop_create(void) {
    Tool* tool = tool_new("Crop", TOOL_CROP, GDK_CROSSHAIR, TOOL_OPT_NONE);

    if (!tool) {
        return NULL;
    }

    tool->mouse_down = crop_tool_mouse_down;
    tool->mouse_move = crop_tool_mouse_move;
    tool->mouse_up = crop_tool_mouse_up;

    /* Allocate and initialize tool state */
    tool->user_data = g_malloc0(sizeof(CropToolState));
    if (!tool->user_data) {
        tool_free(tool);
        return NULL;
    }

    return tool;
}

/**
 * Draw crop overlay during drag/edit - placeholder for Stage 3
 */
void tool_crop_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    (void)doc;
    (void)cr;
    (void)zoom;
    /* Stage 3: draw solid border, handles, darken outside, overlay guides */
}
