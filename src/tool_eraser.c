#include "tool_eraser.h"
#include "command.h"
#include "document.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext *ctx);
extern void ui_update_window_title(AppContext *ctx);

/**
 * Eraser Tool state
 */
typedef struct {
    gboolean is_erasing;          /* Currently erasing? */
    gint last_x;                  /* Last mouse X position */
    gint last_y;                  /* Last mouse Y position */
    struct ImageLayer *active_layer; /* Layer being erased from */
    Command *current_command;     /* Current erase command for undo */
} EraserToolState;

/**
 * Erase pixels from the layer surface (make transparent)
 */
static void eraser_erase_line(cairo_surface_t *surface,
                              gint x1, gint y1, gint x2, gint y2)
{
    cairo_t *cr;

    if (!surface) {
        return;
    }

    cr = cairo_create(surface);

    /* Set eraser properties */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);  /* Transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);   /* Clear operator for transparency */
    cairo_set_line_width(cr, 5.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    /* Draw eraser stroke */
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);

    cairo_destroy(cr);

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Eraser tool: mouse down - start erasing
 */
static void eraser_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    EraserToolState *state;
    struct ImageLayer *active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(EraserToolState));
    }
    state = (EraserToolState *)tool->user_data;

    /* Get the active layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer || !active_layer->surface) {
        printf("Eraser tool: no active layer with surface\n");
        return;
    }

    /* Start erasing */
    state->is_erasing = TRUE;
    state->last_x = event->x;
    state->last_y = event->y;
    state->active_layer = active_layer;

    /* Create a draw command for undo/redo */
    state->current_command = command_create_draw(active_layer);
    if (state->current_command && doc->undo_stack) {
        printf("Eraser tool: erase command created\n");
    }

    printf("Eraser tool: started erasing at (%d, %d)\n", event->x, event->y);
}

/**
 * Eraser tool: mouse move - erase strokes
 */
static void eraser_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    EraserToolState *state;
    struct ImageLayer *active_layer;
    gint layer_x1, layer_y1, layer_x2, layer_y2;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (EraserToolState *)tool->user_data;

    if (!state->is_erasing || !state->active_layer) {
        return;
    }

    /* Get the active layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer || !active_layer->surface) {
        printf("Eraser tool: active layer deleted during erasing\n");
        state->is_erasing = FALSE;
        return;
    }

    /* Erase line from last position to current,
       adjusted for layer offset */
    layer_x1 = state->last_x - active_layer->offset_x;
    layer_y1 = state->last_y - active_layer->offset_y;
    layer_x2 = event->x - active_layer->offset_x;
    layer_y2 = event->y - active_layer->offset_y;

    eraser_erase_line(active_layer->surface, layer_x1, layer_y1, layer_x2, layer_y2);

    /* Store the image-space coordinates for next iteration */
    state->last_x = event->x;
    state->last_y = event->y;

    /* Mark composite for redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    printf("Eraser tool: erasing line from (%d, %d) to (%d, %d)\n",
           layer_x1, layer_y1, layer_x2, layer_y2);
}

/**
 * Eraser tool: mouse up - end erasing
 */
static void eraser_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    EraserToolState *state;
    AppContext *ctx;

    (void)event;  /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (EraserToolState *)tool->user_data;

    if (!state->is_erasing) {
        return;
    }

    /* Push erase command to undo stack */
    if (state->current_command && doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);
        printf("Eraser tool: erase command pushed to undo stack\n");

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }

        /* Update UI */
        ctx = (AppContext *)tool->app_context;
        if (ctx) {
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx);
        }
    }

    /* Mark document as modified */
    doc->modified = TRUE;

    state->is_erasing = FALSE;
    state->current_command = NULL;
    state->active_layer = NULL;

    printf("Eraser tool: finished erasing\n");
}

/**
 * Create the Eraser Tool
 */
Tool* tool_eraser_create(void)
{
    Tool *tool;

    tool = tool_new("Eraser", TOOL_ERASER, GDK_CROSSHAIR);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = eraser_tool_mouse_down;
    tool->mouse_move = eraser_tool_mouse_move;
    tool->mouse_up = eraser_tool_mouse_up;

    printf("Eraser tool created\n");

    return tool;
}

