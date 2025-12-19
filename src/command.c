#include "command.h"
#include "document.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/tile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Create a new command
 */
Command* command_new(const gchar* name,
                     CommandType type,
                     CommandApplyFunc apply,
                     CommandRevertFunc revert,
                     CommandDestroyFunc destroy) {
    Command* cmd;

    if (!name || !apply || !revert) {
        return NULL;
    }

    cmd = (Command*)g_malloc(sizeof(Command));
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
void command_free(Command* cmd) {
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
void command_execute(Command* cmd, struct ImageDocument* doc) {
    if (!cmd || !doc) {
        return;
    }

    cmd->document = doc;

    if (cmd->apply) {
        cmd->apply(cmd, doc);
    }

    // printf("Command executed: %s\n", cmd->name);
}

/**
 * Undo a command (revert it)
 */
void command_undo(Command* cmd, struct ImageDocument* doc) {
    if (!cmd || !doc) {
        return;
    }

    if (cmd->revert) {
        cmd->revert(cmd, doc);
    }

    // printf("Command undone: %s\n", cmd->name);
}

/**
 * Create a new command stack
 */
CommandStack* command_stack_new(guint max_depth) {
    CommandStack* stack = (CommandStack*)g_malloc(sizeof(CommandStack));
    stack->commands = NULL;
    stack->max_depth = max_depth;
    return stack;
}

/**
 * Push a command onto the stack
 */
gboolean command_stack_push(CommandStack* stack, Command* cmd) {
    if (!stack || !cmd) {
        return FALSE;
    }

    /* Add to front of list (most recent first) */
    stack->commands = g_list_prepend(stack->commands, cmd);

    /* Enforce max depth if set */
    if (stack->max_depth > 0) {
        while (g_list_length(stack->commands) > stack->max_depth) {
            GList* last = g_list_last(stack->commands);
            if (last) {
                Command* old_cmd = (Command*)last->data;
                command_free(old_cmd);
                stack->commands = g_list_delete_link(stack->commands, last);
            }
        }
    }

    return TRUE;
}

/**
 * Pop a command from the stack
 */
Command* command_stack_pop(CommandStack* stack) {
    Command* cmd;
    GList* first;

    if (!stack || !stack->commands) {
        return NULL;
    }

    first = stack->commands;
    cmd = (Command*)first->data;
    stack->commands = g_list_delete_link(stack->commands, first);

    return cmd;
}

/**
 * Peek at the top command
 */
Command* command_stack_peek(CommandStack* stack) {
    if (!stack || !stack->commands) {
        return NULL;
    }

    return (Command*)stack->commands->data;
}

/**
 * Check if stack is empty
 */
gboolean command_stack_is_empty(CommandStack* stack) {
    if (!stack) {
        return TRUE;
    }

    return (stack->commands == NULL);
}

/**
 * Get the size of the stack
 */
guint command_stack_size(CommandStack* stack) {
    if (!stack) {
        return 0;
    }

    return g_list_length(stack->commands);
}

/**
 * Clear all commands from the stack
 */
void command_stack_clear(CommandStack* stack) {
    if (!stack) {
        return;
    }

    if (stack->commands) {
        GList* iter;
        for (iter = stack->commands; iter; iter = iter->next) {
            Command* cmd = (Command*)iter->data;
            if (cmd) {
                command_free(cmd);
            }
        }
        g_list_free(stack->commands);
        stack->commands = NULL;
    }
}

/**
 * Free the command stack and all commands
 */
void command_stack_free(CommandStack* stack) {
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
static void draw_command_apply(Command* cmd, struct ImageDocument* doc) {
    DrawCommandData* data;
    cairo_t* cr;
    gboolean layer_found = FALSE;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (DrawCommandData*)cmd->user_data;

    if (!data->layer || !data->after_snapshot) {
        // printf("Draw command apply: missing data\n");
        return;
    }

    /* Verify that the layer still exists in the document */
    if (doc->layers) {
        for (GList* iter = doc->layers; iter; iter = iter->next) {
            if (iter->data == data->layer) {
                layer_found = TRUE;
                break;
            }
        }
    }

    if (!layer_found) {
        // printf("Draw command apply: layer has been deleted, cannot redo\n");
        return;
    }

    /* Verify layer surface still exists */
    if (!data->layer->surface) {
        // printf("Draw command apply: layer surface is NULL, cannot redo\n");
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
static void draw_command_revert(Command* cmd, struct ImageDocument* doc) {
    DrawCommandData* data;
    cairo_t* cr;
    gboolean layer_found = FALSE;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (DrawCommandData*)cmd->user_data;

    if (!data->layer || !data->before_snapshot) {
        // printf("Draw command revert: missing data\n");
        return;
    }

    /* Verify that the layer still exists in the document */
    if (doc->layers) {
        for (GList* iter = doc->layers; iter; iter = iter->next) {
            if (iter->data == data->layer) {
                layer_found = TRUE;
                break;
            }
        }
    }

    if (!layer_found) {
        // printf("Draw command revert: layer has been deleted, cannot undo\n");
        return;
    }

    /* Verify layer surface still exists */
    if (!data->layer->surface) {
        // printf("Draw command revert: layer surface is NULL, cannot undo\n");
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

    // printf("Draw command reverted: restored layer from snapshot\n");
}

/**
 * Draw command destroy callback
 * Free the snapshot surface
 */
static void draw_command_destroy(Command* cmd) {
    DrawCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (DrawCommandData*)cmd->user_data;

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
static cairo_surface_t* cairo_surface_snapshot(cairo_surface_t* source) {
    cairo_surface_t* snapshot;
    cairo_t* cr;
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
 * Get command name string from enum
 */
const gchar* command_get_name_string(CommandName name) {
    static const gchar* names[] = {
        "Paintbrush",
        "Eraser",
        "Paintbucket",
        "Move Layer",
        "Add Layer",
        "Delete Layer",
        "Duplicate Layer",
        "Move Layer Up",
        "Move Layer Down"};

    if (name < 0 || name >= CMD_NAME_COUNT) {
        return NULL;
    }

    return names[name];
}

/**
 * Create a draw command
 */
Command* command_create_draw(struct ImageLayer* layer, const gchar* name) {
    Command* cmd;
    DrawCommandData* data;
    cairo_surface_t* snapshot;
    const gchar* cmd_name;

    if (!layer || !layer->surface) {
        return NULL;
    }

    cmd_name = name ? name : command_get_name_string(CMD_NAME_DRAW_BRUSH_STROKE);

    /* Create snapshot of current state (before drawing) */
    snapshot = cairo_surface_snapshot(layer->surface);
    if (!snapshot) {
        return NULL;
    }

    /* Create command data */
    data = (DrawCommandData*)g_malloc(sizeof(DrawCommandData));
    data->layer = layer;
    data->before_snapshot = snapshot;
    data->after_snapshot = NULL; /* Will be set when finalized */

    /* Create command */
    cmd = command_new(cmd_name,
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
gboolean command_finalize_draw(Command* cmd) {
    DrawCommandData* data;
    cairo_surface_t* after_snapshot;

    if (!cmd || !cmd->user_data) {
        return FALSE;
    }

    data = (DrawCommandData*)cmd->user_data;

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
static void move_command_apply(Command* cmd, struct ImageDocument* doc) {
    MoveCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveCommandData*)cmd->user_data;

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
}

/**
 * Move command revert callback (restore to old position)
 */
static void move_command_revert(Command* cmd, struct ImageDocument* doc) {
    MoveCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveCommandData*)cmd->user_data;

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
}

/**
 * Move command destroy callback
 */
static void move_command_destroy(Command* cmd) {
    MoveCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MoveCommandData*)cmd->user_data;
    g_free(data);
}

/**
 * Create a move command
 */
Command* command_create_move(struct ImageLayer* layer,
                             gint old_x, gint old_y,
                             gint new_x, gint new_y) {
    Command* cmd;
    MoveCommandData* data;

    if (!layer) {
        return NULL;
    }

    /* Create command data */
    data = (MoveCommandData*)g_malloc(sizeof(MoveCommandData));
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

/**
 * Helper function to get layer position in document
 */
static gint get_layer_position(struct ImageDocument* doc, struct ImageLayer* layer) {
    GList* iter;
    gint pos = 0;

    if (!doc || !layer || !doc->layers) {
        return -1;
    }

    for (iter = doc->layers; iter; iter = iter->next, pos++) {
        if (iter->data == layer) {
            return pos;
        }
    }

    return -1;
}

/**
 * Layer add command apply callback (add layer)
 */
static void layer_add_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerAddCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerAddCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Add layer to document */
    if (!g_list_find(doc->layers, data->layer)) {
        doc->layers = g_list_append(doc->layers, data->layer);
        doc->selected_layer = data->layer;
        document_invalidate_composite(doc);
    }
}

/**
 * Layer add command revert callback (remove layer)
 */
static void layer_add_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerAddCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerAddCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Remove layer from document */
    if (g_list_find(doc->layers, data->layer)) {
        doc->layers = g_list_remove(doc->layers, data->layer);

        /* Update selected layer if needed */
        if (doc->selected_layer == data->layer) {
            if (doc->layers) {
                doc->selected_layer = (struct ImageLayer*)doc->layers->data;
            } else {
                doc->selected_layer = NULL;
            }
        }

        document_invalidate_composite(doc);
    }
}

/**
 * Layer add command destroy callback
 */
static void layer_add_command_destroy(Command* cmd) {
    LayerAddCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (LayerAddCommandData*)cmd->user_data;

    /* Only free layer if it's not in the document (undo case)
     * IMPORTANT: If doc->layers is NULL, the document is being freed and
     * document_free() will handle freeing all layers. Don't free here to avoid double-free. */
    if (data->layer) {
        if (!data->doc) {
            /* Document pointer is NULL - document was already freed, free the layer */
            layer_free(data->layer);
            data->layer = NULL; /* Set to NULL after freeing to prevent double-free */
        } else if (!data->doc->layers) {
            /* Document is being freed (layers list is NULL) - DON'T free the layer here.
             * document_free() will free all layers. Freeing here would cause double-free. */
            data->layer = NULL; /* Just clear the pointer */
        } else {
            /* Document still exists - only free if layer is not in the list */
            GList* found = g_list_find(data->doc->layers, data->layer);
            if (!found) {
                layer_free(data->layer);
                data->layer = NULL; /* Set to NULL after freeing to prevent double-free */
            }
        }
    }

    g_free(data);
}

/**
 * Layer delete command apply callback (delete layer)
 */
static void layer_delete_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerDeleteCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerDeleteCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Delete layer from document */
    if (g_list_find(doc->layers, data->layer)) {
        /* Flush layer surface to ensure all operations are complete */
        if (data->layer && data->layer->surface) {
            cairo_surface_flush(data->layer->surface);
        }

        doc->layers = g_list_remove(doc->layers, data->layer);

        if (doc->selected_layer == data->layer) {
            if (doc->layers) {
                doc->selected_layer = (struct ImageLayer*)doc->layers->data;
            } else {
                doc->selected_layer = NULL;
            }
        }

        if (doc->composite_surface) {
            cairo_surface_flush(doc->composite_surface);
            cairo_surface_destroy(doc->composite_surface);
            doc->composite_surface = NULL;
        }
        doc->composite_dirty = TRUE;

        layer_free(data->layer);
        data->layer = NULL; /* Set to NULL after freeing to prevent double-free */

        document_invalidate_composite(doc);
    }
}

/**
 * Layer delete command revert callback (restore layer)
 */
static void layer_delete_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerDeleteCommandData* data;
    struct ImageLayer* restored_layer;
    cairo_t* cr;
    GList* iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerDeleteCommandData*)cmd->user_data;

    if (!data->snapshot || data->position < 0) {
        return;
    }

    /* Recreate layer */
    restored_layer = layer_new(data->layer_name, data->width, data->height, TRUE,
                               LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
    if (!restored_layer) {
        return;
    }

    /* Restore content from snapshot */
    cr = cairo_create(restored_layer->surface);
    cairo_set_source_surface(cr, data->snapshot, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Flush the restored layer surface to ensure all operations are complete */
    if (restored_layer->surface) {
        cairo_surface_flush(restored_layer->surface);
    }

    /* Restore properties */
    restored_layer->opacity = data->opacity;
    restored_layer->blend_mode = data->blend_mode;

    /* Insert at original position */
    iter = g_list_nth(doc->layers, data->position);
    if (iter) {
        doc->layers = g_list_insert_before(doc->layers, iter, restored_layer);
    } else {
        doc->layers = g_list_append(doc->layers, restored_layer);
    }

    data->layer = restored_layer;
    doc->selected_layer = restored_layer;

    document_invalidate_composite(doc);
}

/**
 * Layer delete command destroy callback
 */
static void layer_delete_command_destroy(Command* cmd) {
    LayerDeleteCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (LayerDeleteCommandData*)cmd->user_data;

    if (data->snapshot) {
        /* Flush snapshot before destroying to ensure all operations are complete */
        cairo_surface_flush(data->snapshot);
        cairo_surface_destroy(data->snapshot);
    }

    if (data->layer_name) {
        g_free(data->layer_name);
    }

    /* Free layer if it still exists (redo case where layer was deleted)
     * IMPORTANT: If doc->layers is NULL, the document is being freed and
     * document_free() will handle freeing all layers. Don't free here to avoid double-free.
     * Note: data->layer may be NULL if it was already freed in layer_delete_command_apply */
    if (data->layer) {
        if (!data->doc) {
            /* Document pointer is NULL - document was already freed, free the layer */
            layer_free(data->layer);
            data->layer = NULL; /* Set to NULL after freeing to prevent double-free */
        } else if (!data->doc->layers) {
            /* Document is being freed (layers list is NULL) - DON'T free the layer here.
             * document_free() will free all layers. Freeing here would cause double-free. */
            data->layer = NULL; /* Just clear the pointer */
        } else {
            /* Document still exists - only free if layer is not in the list
             * Note: We can safely use g_list_find here because:
             * 1. data->doc->layers is valid (not NULL)
             * 2. g_list_find only compares pointers, it doesn't dereference data->layer
             * 3. If data->layer was freed in layer_delete_command_apply, it should be NULL already */
            GList* found = g_list_find(data->doc->layers, data->layer);
            if (!found) {
                layer_free(data->layer);
                data->layer = NULL; /* Set to NULL after freeing to prevent double-free */
            }
        }
    }

    g_free(data);
}

/**
 * Layer duplicate command apply callback (add duplicated layer)
 */
static void layer_duplicate_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerDuplicateCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerDuplicateCommandData*)cmd->user_data;

    if (!data->new_layer || !data->source_layer) {
        return;
    }

    /* Add duplicated layer to document (after source layer) */
    if (!g_list_find(doc->layers, data->new_layer)) {
        GList* iter = g_list_find(doc->layers, data->source_layer);
        if (iter && iter->next) {
            doc->layers = g_list_insert_before(doc->layers, iter->next, data->new_layer);
        } else {
            doc->layers = g_list_append(doc->layers, data->new_layer);
        }
        doc->selected_layer = data->new_layer;
        document_invalidate_composite(doc);
    }
}

/**
 * Layer duplicate command revert callback (remove duplicated layer)
 */
static void layer_duplicate_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerDuplicateCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerDuplicateCommandData*)cmd->user_data;

    if (!data->new_layer) {
        return;
    }

    /* Remove duplicated layer from document */
    if (g_list_find(doc->layers, data->new_layer)) {
        doc->layers = g_list_remove(doc->layers, data->new_layer);

        if (doc->selected_layer == data->new_layer) {
            doc->selected_layer = data->source_layer;
        }

        document_invalidate_composite(doc);
    }
}

/**
 * Layer duplicate command destroy callback
 */
static void layer_duplicate_command_destroy(Command* cmd) {
    LayerDuplicateCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (LayerDuplicateCommandData*)cmd->user_data;

    /* Only free duplicated layer if it's not in the document (undo case)
     * IMPORTANT: If doc->layers is NULL, the document is being freed and
     * document_free() will handle freeing all layers. Don't free here to avoid double-free. */
    if (data->new_layer) {
        if (!data->doc) {
            /* Document pointer is NULL - document was already freed, free the layer */
            layer_free(data->new_layer);
            data->new_layer = NULL; /* Set to NULL after freeing to prevent double-free */
        } else if (!data->doc->layers) {
            /* Document is being freed (layers list is NULL) - DON'T free the layer here.
             * document_free() will free all layers. Freeing here would cause double-free. */
            data->new_layer = NULL; /* Just clear the pointer */
        } else {
            /* Document still exists - only free if layer is not in the list */
            GList* found = g_list_find(data->doc->layers, data->new_layer);
            if (!found) {
                layer_free(data->new_layer);
                data->new_layer = NULL; /* Set to NULL after freeing to prevent double-free */
            }
        }
    }

    g_free(data);
}

/**
 * Layer move up command apply callback (move layer up)
 */
static void layer_move_up_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerMoveUpCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerMoveUpCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Move layer to new position */
    iter = g_list_find(doc->layers, data->layer);
    if (iter && iter->next) {
        doc->layers = g_list_remove(doc->layers, data->layer);
        doc->layers = g_list_insert(doc->layers, data->layer, data->new_position);
        document_invalidate_composite(doc);
    }
}

/**
 * Layer move up command revert callback (move layer back down)
 */
static void layer_move_up_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerMoveUpCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerMoveUpCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Move layer back to old position */
    iter = g_list_find(doc->layers, data->layer);
    if (iter) {
        doc->layers = g_list_remove(doc->layers, data->layer);
        doc->layers = g_list_insert(doc->layers, data->layer, data->old_position);
        document_invalidate_composite(doc);
    }
}

/**
 * Layer move up command destroy callback
 */
static void layer_move_up_command_destroy(Command* cmd) {
    LayerMoveUpCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (LayerMoveUpCommandData*)cmd->user_data;
    g_free(data);
}

/**
 * Layer move down command apply callback (move layer down)
 */
static void layer_move_down_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerMoveDownCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerMoveDownCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Move layer to new position */
    iter = g_list_find(doc->layers, data->layer);
    if (iter && iter->prev) {
        doc->layers = g_list_remove(doc->layers, data->layer);
        doc->layers = g_list_insert(doc->layers, data->layer, data->new_position);
        document_invalidate_composite(doc);
    }
}

/**
 * Layer move down command revert callback (move layer back up)
 */
static void layer_move_down_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerMoveDownCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerMoveDownCommandData*)cmd->user_data;

    if (!data->layer) {
        return;
    }

    /* Move layer back to old position */
    iter = g_list_find(doc->layers, data->layer);
    if (iter) {
        doc->layers = g_list_remove(doc->layers, data->layer);
        doc->layers = g_list_insert(doc->layers, data->layer, data->old_position);
        document_invalidate_composite(doc);
    }
}

/**
 * Layer move down command destroy callback
 */
static void layer_move_down_command_destroy(Command* cmd) {
    LayerMoveDownCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (LayerMoveDownCommandData*)cmd->user_data;
    g_free(data);
}

/**
 * Create a layer add command
 */
Command* command_create_layer_add(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerAddCommandData* data;

    if (!doc || !layer) {
        return NULL;
    }

    data = (LayerAddCommandData*)g_malloc(sizeof(LayerAddCommandData));
    data->doc = doc;
    data->layer = layer;

    cmd = command_new(command_get_name_string(CMD_NAME_ADD_LAYER),
                      COMMAND_LAYER_EDIT,
                      layer_add_command_apply,
                      layer_add_command_revert,
                      layer_add_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    return cmd;
}

/**
 * Create a layer delete command
 */
Command* command_create_layer_delete(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerDeleteCommandData* data;
    gint position;

    if (!doc || !layer) {
        return NULL;
    }

    position = get_layer_position(doc, layer);
    if (position < 0) {
        return NULL;
    }

    data = (LayerDeleteCommandData*)g_malloc(sizeof(LayerDeleteCommandData));
    data->doc = doc;
    data->layer = layer;
    data->position = position;
    data->layer_name = g_strdup(layer->name);
    data->width = layer->width;
    data->height = layer->height;
    data->snapshot = cairo_surface_snapshot(layer->surface);
    data->opacity = layer->opacity;
    data->blend_mode = layer->blend_mode;

    if (!data->snapshot) {
        g_free(data->layer_name);
        g_free(data);
        return NULL;
    }

    cmd = command_new(command_get_name_string(CMD_NAME_DELETE_LAYER),
                      COMMAND_LAYER_EDIT,
                      layer_delete_command_apply,
                      layer_delete_command_revert,
                      layer_delete_command_destroy);

    if (!cmd) {
        cairo_surface_destroy(data->snapshot);
        g_free(data->layer_name);
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    return cmd;
}

/**
 * Create a layer duplicate command
 */
Command* command_create_layer_duplicate(struct ImageDocument* doc,
                                        struct ImageLayer* source_layer,
                                        struct ImageLayer* new_layer) {
    Command* cmd;
    LayerDuplicateCommandData* data;

    if (!doc || !source_layer || !new_layer) {
        return NULL;
    }

    data = (LayerDuplicateCommandData*)g_malloc(sizeof(LayerDuplicateCommandData));
    data->doc = doc;
    data->source_layer = source_layer;
    data->new_layer = new_layer;

    cmd = command_new(command_get_name_string(CMD_NAME_DUPLICATE_LAYER),
                      COMMAND_LAYER_EDIT,
                      layer_duplicate_command_apply,
                      layer_duplicate_command_revert,
                      layer_duplicate_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    return cmd;
}

/**
 * Create a layer move up command
 */
Command* command_create_layer_move_up(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerMoveUpCommandData* data;
    gint old_pos, new_pos;
    GList* iter;

    if (!doc || !layer) {
        return NULL;
    }

    iter = g_list_find(doc->layers, layer);
    if (!iter || !iter->next) {
        return NULL; /* Can't move up */
    }

    old_pos = g_list_position(doc->layers, iter);
    new_pos = old_pos + 1;

    data = (LayerMoveUpCommandData*)g_malloc(sizeof(LayerMoveUpCommandData));
    data->doc = doc;
    data->layer = layer;
    data->old_position = old_pos;
    data->new_position = new_pos;

    cmd = command_new(command_get_name_string(CMD_NAME_MOVE_LAYER_UP),
                      COMMAND_LAYER_EDIT,
                      layer_move_up_command_apply,
                      layer_move_up_command_revert,
                      layer_move_up_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    return cmd;
}

/**
 * Create a layer move down command
 */
Command* command_create_layer_move_down(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerMoveDownCommandData* data;
    gint old_pos, new_pos;
    GList* iter;

    if (!doc || !layer) {
        return NULL;
    }

    iter = g_list_find(doc->layers, layer);
    if (!iter || !iter->prev) {
        return NULL; /* Can't move down */
    }

    old_pos = g_list_position(doc->layers, iter);
    new_pos = old_pos - 1;

    data = (LayerMoveDownCommandData*)g_malloc(sizeof(LayerMoveDownCommandData));
    data->doc = doc;
    data->layer = layer;
    data->old_position = old_pos;
    data->new_position = new_pos;

    cmd = command_new(command_get_name_string(CMD_NAME_MOVE_LAYER_DOWN),
                      COMMAND_LAYER_EDIT,
                      layer_move_down_command_apply,
                      layer_move_down_command_revert,
                      layer_move_down_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    return cmd;
}