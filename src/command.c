#include "command.h"
#include "document.h"
#include "render/layer.h"
#include "render/tile.h"
#include "render/dirty.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * Create a new command
 */
Command* command_new(const gchar *name,
                     CommandType type,
                     CommandApplyFunc apply,
                     CommandRevertFunc revert,
                     CommandDestroyFunc destroy)
{
    Command *cmd;

    if (!name || !apply || !revert) {
        return NULL;
    }

    cmd = (Command *)g_malloc(sizeof(Command));
    cmd->name = g_strdup(name);
    cmd->type = type;
    cmd->apply = apply;
    cmd->revert = revert;
    cmd->destroy = destroy;
    cmd->user_data = NULL;
    cmd->document = NULL;

    return cmd;
}

/**
 * Free a command and its resources
 */
void command_free(Command *cmd)
{
    if (!cmd) {
        return;
    }

    /* Call custom destroy callback if provided */
    if (cmd->destroy) {
        cmd->destroy(cmd);
    }

    g_free(cmd->name);
    g_free(cmd);
}

/**
 * Execute a command (apply it)
 */
void command_execute(Command *cmd, struct ImageDocument *doc)
{
    if (!cmd || !doc) {
        return;
    }

    cmd->document = doc;

    if (cmd->apply) {
        cmd->apply(cmd, doc);
    }

    //printf("Command executed: %s\n", cmd->name);
}

/**
 * Undo a command (revert it)
 */
void command_undo(Command *cmd, struct ImageDocument *doc)
{
    if (!cmd || !doc) {
        return;
    }

    if (cmd->revert) {
        cmd->revert(cmd, doc);
    }

    //printf("Command undone: %s\n", cmd->name);
}

/**
 * Create a new command stack
 */
CommandStack* command_stack_new(guint max_depth)
{
    CommandStack *stack = (CommandStack *)g_malloc(sizeof(CommandStack));
    stack->commands = NULL;
    stack->max_depth = max_depth;
    return stack;
}

/**
 * Push a command onto the stack
 */
gboolean command_stack_push(CommandStack *stack, Command *cmd)
{
    if (!stack || !cmd) {
        return FALSE;
    }

    /* Check depth limit */
    if (stack->max_depth > 0 && g_list_length(stack->commands) >= (guint)stack->max_depth) {
        /* Remove oldest command from bottom */
        GList *last = g_list_last(stack->commands);
        if (last) {
            command_free((Command *)last->data);
            stack->commands = g_list_delete_link(stack->commands, last);
        }
    }

    /* Prepend to top of stack */
    stack->commands = g_list_prepend(stack->commands, cmd);
    return TRUE;
}

/**
 * Pop a command from the stack
 */
Command* command_stack_pop(CommandStack *stack)
{
    Command *cmd;

    if (!stack || !stack->commands) {
        return NULL;
    }

    cmd = (Command *)stack->commands->data;
    stack->commands = g_list_remove_link(stack->commands, stack->commands);

    return cmd;
}

/**
 * Peek at the top command
 */
Command* command_stack_peek(CommandStack *stack)
{
    if (!stack || !stack->commands) {
        return NULL;
    }

    return (Command *)stack->commands->data;
}

/**
 * Check if stack is empty
 */
gboolean command_stack_is_empty(CommandStack *stack)
{
    if (!stack) {
        return TRUE;
    }

    return stack->commands == NULL;
}

/**
 * Get the size of the stack
 */
guint command_stack_size(CommandStack *stack)
{
    if (!stack) {
        return 0;
    }

    return g_list_length(stack->commands);
}

/**
 * Clear all commands from the stack
 */
void command_stack_clear(CommandStack *stack)
{
    if (!stack) {
        return;
    }

    for (GList *iter = stack->commands; iter; iter = iter->next) {
        command_free((Command *)iter->data);
    }

    g_list_free(stack->commands);
    stack->commands = NULL;
}

/**
 * Free the command stack and all commands
 */
void command_stack_free(CommandStack *stack)
{
    if (!stack) {
        return;
    }

    command_stack_clear(stack);
    g_free(stack);
}

/**
 * Draw command apply callback
 * Restore layer from after_snapshot (for redo)
 */
static void draw_command_apply(Command *cmd, struct ImageDocument *doc)
{
    DrawCommandData *data;
    cairo_t *cr;
    gboolean layer_found = FALSE;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (DrawCommandData *)cmd->user_data;

    if (!data->layer || !data->after_snapshot) {
        //printf("Draw command apply: missing data\n");
        return;
    }

    /* Verify that the layer still exists in the document */
    if (doc->layers) {
        for (GList *iter = doc->layers; iter; iter = iter->next) {
            if (iter->data == data->layer) {
                layer_found = TRUE;
                break;
            }
        }
    }

    if (!layer_found) {
        //printf("Draw command apply: layer has been deleted, cannot redo\n");
        return;
    }

    /* Verify layer surface still exists */
    if (!data->layer->surface) {
        //printf("Draw command apply: layer surface is NULL, cannot redo\n");
        return;
    }

    /* Restore after_snapshot to layer (state after drawing) */
    cr = cairo_create(data->layer->surface);
    cairo_set_source_surface(cr, data->after_snapshot, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Mark layer cache as dirty since pixels changed */
    layer_invalidate_cache(data->layer);

    /* Mark composite as dirty and invalidate tiles */
    if (doc) {
        doc->composite_dirty = TRUE;
        
        /* Mark tiles as dirty for tile-based rendering */
        if (doc->tile_grid && data->layer) {
            /* Mark the entire layer region as dirty */
            DirtyRect dirty_rect;
            dirty_rect_set(&dirty_rect, 
                          data->layer->offset_x, 
                          data->layer->offset_y,
                          data->layer->width, 
                          data->layer->height);
            dirty_rect_clamp(&dirty_rect, doc->width, doc->height);
            if (!dirty_rect_is_empty(&dirty_rect)) {
                tile_grid_mark_rect_dirty(doc->tile_grid,
                                          dirty_rect.x, dirty_rect.y,
                                          dirty_rect.width, dirty_rect.height);
            }
        }
    }

}

/**
 * Draw command revert callback
 * Restore layer from snapshot
 */
static void draw_command_revert(Command *cmd, struct ImageDocument *doc)
{
    DrawCommandData *data;
    cairo_t *cr;
    gboolean layer_found = FALSE;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (DrawCommandData *)cmd->user_data;

    if (!data->layer || !data->before_snapshot) {
        //printf("Draw command revert: missing data\n");
        return;
    }

    /* Verify that the layer still exists in the document */
    if (doc->layers) {
        for (GList *iter = doc->layers; iter; iter = iter->next) {
            if (iter->data == data->layer) {
                layer_found = TRUE;
                break;
            }
        }
    }

    if (!layer_found) {
        //printf("Draw command revert: layer has been deleted, cannot undo\n");
        return;
    }

    /* Verify layer surface still exists */
    if (!data->layer->surface) {
        //printf("Draw command revert: layer surface is NULL, cannot undo\n");
        return;
    }

    /* Restore before_snapshot to layer (state before drawing) */
    cr = cairo_create(data->layer->surface);
    cairo_set_source_surface(cr, data->before_snapshot, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Mark layer cache as dirty since pixels changed */
    layer_invalidate_cache(data->layer);

    /* Mark composite as dirty and invalidate tiles */
    if (doc) {
        doc->composite_dirty = TRUE;
        
        /* Mark tiles as dirty for tile-based rendering */
        if (doc->tile_grid && data->layer) {
            /* Mark the entire layer region as dirty */
            DirtyRect dirty_rect;
            dirty_rect_set(&dirty_rect, 
                          data->layer->offset_x, 
                          data->layer->offset_y,
                          data->layer->width, 
                          data->layer->height);
            dirty_rect_clamp(&dirty_rect, doc->width, doc->height);
            if (!dirty_rect_is_empty(&dirty_rect)) {
                tile_grid_mark_rect_dirty(doc->tile_grid,
                                          dirty_rect.x, dirty_rect.y,
                                          dirty_rect.width, dirty_rect.height);
            }
        }
    }

    //printf("Draw command reverted: restored layer from snapshot\n");
}

/**
 * Draw command destroy callback
 * Free the snapshot surface
 */
static void draw_command_destroy(Command *cmd)
{
    DrawCommandData *data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (DrawCommandData *)cmd->user_data;

    if (data->before_snapshot) {
        cairo_surface_destroy(data->before_snapshot);
    }
    if (data->after_snapshot) {
        cairo_surface_destroy(data->after_snapshot);
    }

    g_free(data);
}

/**
 * Create a snapshot of a Cairo surface
 */
static cairo_surface_t* cairo_surface_snapshot(cairo_surface_t *source)
{
    cairo_surface_t *snapshot;
    cairo_t *cr;
    int width, height;

    if (!source) {
        return NULL;
    }

    width = cairo_image_surface_get_width(source);
    height = cairo_image_surface_get_height(source);

    /* Create new surface with same format */
    snapshot = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    if (!snapshot) {
        return NULL;
    }

    /* Copy source to snapshot */
    cr = cairo_create(snapshot);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    return snapshot;
}

/**
 * Create a draw command
 */
Command* command_create_draw(struct ImageLayer *layer)
{
    Command *cmd;
    DrawCommandData *data;
    cairo_surface_t *snapshot;

    if (!layer || !layer->surface) {
        return NULL;
    }

    /* Create snapshot of current state (before drawing) */
    snapshot = cairo_surface_snapshot(layer->surface);
    if (!snapshot) {
        return NULL;
    }

    /* Create command data */
    data = (DrawCommandData *)g_malloc(sizeof(DrawCommandData));
    data->layer = layer;
    data->before_snapshot = snapshot;
    data->after_snapshot = NULL;  /* Will be set when finalized */

    /* Create command */
    cmd = command_new("Draw Brush Stroke",
                      COMMAND_DRAW,
                      draw_command_apply,
                      draw_command_revert,
                      draw_command_destroy);

    if (!cmd) {
        cairo_surface_destroy(snapshot);
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Finalize a draw command by taking snapshot of state after drawing
 */
gboolean command_finalize_draw(Command *cmd)
{
    DrawCommandData *data;
    cairo_surface_t *after_snapshot;

    if (!cmd || !cmd->user_data) {
        return FALSE;
    }

    data = (DrawCommandData *)cmd->user_data;

    if (!data->layer || !data->layer->surface) {
        return FALSE;
    }

    /* If after_snapshot already exists, destroy it first */
    if (data->after_snapshot) {
        cairo_surface_destroy(data->after_snapshot);
    }

    /* Create snapshot of current state (after drawing) */
    after_snapshot = cairo_surface_snapshot(data->layer->surface);
    if (!after_snapshot) {
        return FALSE;
    }

    data->after_snapshot = after_snapshot;

    return TRUE;
}

/**
 * Move command apply callback (restore to new position)
 */
static void move_command_apply(Command *cmd, struct ImageDocument *doc)
{
    MoveCommandData *data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveCommandData *)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Apply new position */
    data->layer->offset_x = data->new_offset_x;
    data->layer->offset_y = data->new_offset_y;

    /* Mark composite as dirty and queue redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    // printf("Move command applied: layer moved to (%d, %d)\n", 
    //        data->new_offset_x, data->new_offset_y);
}

/**
 * Move command revert callback (restore to old position)
 */
static void move_command_revert(Command *cmd, struct ImageDocument *doc)
{
    MoveCommandData *data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveCommandData *)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Restore old position */
    data->layer->offset_x = data->old_offset_x;
    data->layer->offset_y = data->old_offset_y;

    /* Mark composite as dirty and queue redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    // printf("Move command reverted: layer restored to (%d, %d)\n",
    //        data->old_offset_x, data->old_offset_y);
}

/**
 * Move command destroy callback
 */
static void move_command_destroy(Command *cmd)
{
    MoveCommandData *data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MoveCommandData *)cmd->user_data;
    g_free(data);
}

/**
 * Create a move command
 */
Command* command_create_move(struct ImageLayer *layer,
                             gint old_x, gint old_y,
                             gint new_x, gint new_y)
{
    Command *cmd;
    MoveCommandData *data;

    if (!layer) {
        return NULL;
    }

    /* Create command data */
    data = (MoveCommandData *)g_malloc(sizeof(MoveCommandData));
    data->layer = layer;
    data->old_offset_x = old_x;
    data->old_offset_y = old_y;
    data->new_offset_x = new_x;
    data->new_offset_y = new_y;

    /* Create command */
    cmd = command_new("Move Layer",
                      COMMAND_MOVE,
                      move_command_apply,
                      move_command_revert,
                      move_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

