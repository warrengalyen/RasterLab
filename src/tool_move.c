#include "tool_move.h"
#include "command.h"
#include "document.h"
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext *ctx);
extern void ui_update_window_title(AppContext *ctx);

/**
 * Move Tool state
 */
typedef struct {
    gboolean is_dragging;         /* Currently dragging? */
    gint start_x;                 /* Mouse down position X */
    gint start_y;                 /* Mouse down position Y */
    gint initial_offset_x;        /* Layer offset at drag start */
    gint initial_offset_y;        /* Layer offset at drag start */
    struct ImageLayer *active_layer; /* Layer being moved */
} MoveToolState;

/**
 * Move tool: mouse down - start dragging
 */
static void move_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    MoveToolState *state;
    struct ImageLayer *active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(MoveToolState));
    }
    state = (MoveToolState *)tool->user_data;

    /* Get the top (active) layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer) {
        printf("Move tool: no active layer\n");
        return;
    }

    /* Start dragging */
    state->is_dragging = TRUE;
    state->start_x = event->x;
    state->start_y = event->y;
    state->initial_offset_x = active_layer->offset_x;
    state->initial_offset_y = active_layer->offset_y;
    state->active_layer = active_layer;

    printf("Move tool: started dragging layer at (%d, %d)\n", event->x, event->y);
}

/**
 * Move tool: mouse move - update layer offset
 */
static void move_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    MoveToolState *state;
    gint dx, dy;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState *)tool->user_data;

    if (!state->is_dragging || !state->active_layer) {
        return;
    }

    /* Calculate delta from start position */
    dx = event->x - state->start_x;
    dy = event->y - state->start_y;

    /* Update layer offset */
    state->active_layer->offset_x = state->initial_offset_x + dx;
    state->active_layer->offset_y = state->initial_offset_y + dy;

    /* Mark composite for redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    printf("Move tool: moved to offset (%d, %d)\n", 
           state->active_layer->offset_x, state->active_layer->offset_y);
}

/**
 * Move tool: mouse up - end dragging and create undo command
 */
static void move_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    MoveToolState *state;
    Command *cmd;
    AppContext *ctx;

    (void)event;  /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState *)tool->user_data;

    if (!state->is_dragging) {
        return;
    }

    /* Check if we actually moved */
    if (state->active_layer && 
        (state->active_layer->offset_x != state->initial_offset_x ||
         state->active_layer->offset_y != state->initial_offset_y)) {
        
        /* Create undo command for the move */
        cmd = command_create_move(
            state->active_layer,
            state->initial_offset_x,
            state->initial_offset_y,
            state->active_layer->offset_x,
            state->active_layer->offset_y);

        if (cmd && doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);
            printf("Move tool: move command pushed to undo stack\n");

            /* Clear redo stack since new action performed */
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

        /* Layer was moved - mark document as modified */
        doc->modified = TRUE;
        printf("Move tool: layer moved - document marked as modified\n");
    }

    state->is_dragging = FALSE;
    state->active_layer = NULL;

    printf("Move tool: finished dragging\n");
}

/**
 * Create the Move Tool
 */
Tool* tool_move_create(void)
{
    Tool *tool;

    tool = tool_new("Move", TOOL_MOVE, GDK_FLEUR);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = move_tool_mouse_down;
    tool->mouse_move = move_tool_mouse_move;
    tool->mouse_up = move_tool_mouse_up;

    printf("Move tool created\n");

    return tool;
}

