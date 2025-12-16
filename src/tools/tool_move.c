#include "tools/tool_move.h"
#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx);

/**
 * Move Tool state
 */
typedef struct {
    gboolean is_dragging;            /* Currently dragging? */
    gdouble start_widget_x;          /* Mouse down position X in widget coordinates */
    gdouble start_widget_y;          /* Mouse down position Y in widget coordinates */
    gint initial_offset_x;           /* Layer offset at drag start */
    gint initial_offset_y;           /* Layer offset at drag start */
    gint last_offset_x;              /* Last known offset (for dirty rect tracking) */
    gint last_offset_y;              /* Last known offset (for dirty rect tracking) */
    struct ImageDocument* doc;       /* Document reference for coordinate conversion */
    struct ImageLayer* active_layer; /* Layer being moved */
} MoveToolState;

/**
 * Move tool: mouse down - start dragging
 */
static void move_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(MoveToolState));
    }
    state = (MoveToolState*)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer) {
        // printf("Move tool: no selected layer\n");
        return;
    }

    /* Start dragging - convert document coordinates back to widget coordinates for accurate delta calculation */
    state->is_dragging = TRUE;
    state->start_widget_x = (gdouble)event->x * doc->zoom_factor;
    state->start_widget_y = (gdouble)event->y * doc->zoom_factor;
    state->doc = doc;
    state->initial_offset_x = active_layer->offset_x;
    state->initial_offset_y = active_layer->offset_y;
    state->last_offset_x = active_layer->offset_x;
    state->last_offset_y = active_layer->offset_y;
    state->active_layer = active_layer;

    // printf("Move tool: started dragging layer at (%d, %d)\n", event->x, event->y);
}

/**
 * Move tool: mouse move - update layer offset
 * Optimized to only invalidate old and new regions
 */
static void move_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    gint dx, dy;
    gint old_x, old_y, new_x, new_y;
    DirtyRect old_rect, new_rect, union_rect;
    gint brush_margin = 2; /* Small margin for anti-aliasing */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState*)tool->user_data;

    if (!state->is_dragging || !state->active_layer) {
        return;
    }

    /* Convert current position to widget coordinates and calculate delta in widget space
     * This avoids accumulating rounding errors from coordinate conversion */
    gdouble current_widget_x = (gdouble)event->x * doc->zoom_factor;
    gdouble current_widget_y = (gdouble)event->y * doc->zoom_factor;
    gdouble delta_widget_x = current_widget_x - state->start_widget_x;
    gdouble delta_widget_y = current_widget_y - state->start_widget_y;

    /* Convert delta to document coordinates with proper rounding */
    gdouble delta_doc_x = delta_widget_x / doc->zoom_factor;
    gdouble delta_doc_y = delta_widget_y / doc->zoom_factor;

    /* Calculate new position with proper rounding */
    new_x = state->initial_offset_x + (gint)(delta_doc_x + 0.5);
    new_y = state->initial_offset_y + (gint)(delta_doc_y + 0.5);

    /* Get old position (from last update) */
    old_x = state->last_offset_x;
    old_y = state->last_offset_y;

    /* If position hasn't changed, do nothing */
    if (old_x == new_x && old_y == new_y) {
        return;
    }

    /* Calculate old region (where layer was last frame) */
    dirty_rect_set(&old_rect, old_x, old_y,
                   state->active_layer->width,
                   state->active_layer->height);
    dirty_rect_clamp(&old_rect, doc->width, doc->height);

    /* Calculate new region (where layer is now) */
    dirty_rect_set(&new_rect, new_x, new_y,
                   state->active_layer->width,
                   state->active_layer->height);
    dirty_rect_clamp(&new_rect, doc->width, doc->height);

    /* Union both regions */
    dirty_rect_union(&old_rect, &new_rect, &union_rect);

    /* Update layer offset */
    state->active_layer->offset_x = new_x;
    state->active_layer->offset_y = new_y;

    /* Update last known position */
    state->last_offset_x = new_x;
    state->last_offset_y = new_y;

    /* Only invalidate the union of old and new regions */
    if (!dirty_rect_is_empty(&union_rect)) {
        document_invalidate_region(doc, &union_rect);
    }

    // printf("Move tool: moved to offset (%d, %d)\n",
    //        state->active_layer->offset_x, state->active_layer->offset_y);
}

/**
 * Move tool: mouse up - end dragging and create undo command
 */
static void move_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    Command* cmd;
    AppContext* ctx;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState*)tool->user_data;

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
            // printf("Move tool: move command pushed to undo stack\n");

            /* Clear redo stack since new action performed */
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }

            /* Update UI */
            ctx = (AppContext*)tool->app_context;
            if (ctx) {
                ui_update_menu_and_button_states(ctx);
                ui_update_window_title(ctx);
            }
        }

        /* Layer was moved - mark document as modified */
        doc->modified = TRUE;
        // printf("Move tool: layer moved - document marked as modified\n");
    }

    state->is_dragging = FALSE;
    state->active_layer = NULL;

    // printf("Move tool: finished dragging\n");
}

/**
 * Create the Move Tool
 */
Tool* tool_move_create(void) {
    Tool* tool;

    /* Move tool doesn't have size/opacity/hardness options */
    tool = tool_new("Move", TOOL_MOVE, GDK_FLEUR, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = move_tool_mouse_down;
    tool->mouse_move = move_tool_mouse_move;
    tool->mouse_up = move_tool_mouse_up;

    // printf("Move tool created\n");

    return tool;
}
