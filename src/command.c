#include "command.h"
#include "document.h"
#include "filters.h"
#include "ocular.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/tile.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "selection/selection_undo.h"
#include "undo/undo_disk.h"
#include <math.h>
#include <stdbool.h>
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
        "Move Layer Down",
        "Canvas size",
        "Flip Horizontal",
        "Flip Vertical",
        "Transpose",
        "Fit Canvas to Active Layer",
        "Fit Canvas to All Layers",
        "Merge Visible Layers",
        "Flatten Image",
        "Select All",
        "Deselect All",
        "Invert Selection",
        "Feather Selection",
        "Move Selected Pixels"};

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
 * Tile undo transaction structure (internal)
 * Tracks which tile regions are modified during a drawing operation
 */
struct _TileUndoTransaction {
    struct ImageLayer* layer;   /* Layer being modified */
    struct ImageDocument* doc;  /* Document (for tile_size) */
    gint tile_size;             /* Tile size for region division */
    gchar* name;                /* Command name */
    GHashTable* modified_tiles; /* Hash table: (tile_x << 16 | tile_y) -> TileUndoDelta* */
                                /* Tracks which tiles have been touched (first time only) */
};

/**
 * Hash function for tile coordinates
 */
static guint tile_coord_hash(gconstpointer key) {
    return GPOINTER_TO_UINT(key);
}

/**
 * Equality function for tile coordinates
 */
static gboolean tile_coord_equal(gconstpointer a, gconstpointer b) {
    return a == b;
}

/**
 * Tile undo command apply callback (restore to "after" state for redo)
 */
static void tile_undo_command_apply(Command* cmd, struct ImageDocument* doc) {
    TileUndoCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (TileUndoCommandData*)cmd->user_data;

    if (!data->layer || !data->layer->surface) {
        return;
    }

    /* If entry is on disk, read from disk journal */
    if (data->entry_index && doc->undo_journal) {
        if (undo_journal_read_redo(doc->undo_journal, data->entry_index, doc)) {
            /* Successfully read and applied from disk */
            return;
        }
        /* Fall through to in-memory path if disk read failed */
    }

    /* In-memory path: apply from tile_deltas */
    if (!data->tile_deltas) {
        return;
    }

    gboolean layer_found = FALSE;
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
        return;
    }

    /* Apply "after" snapshot to each modified tile region */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta && delta->after) {
            tile_snapshot_apply(data->layer->surface,
                                delta->after,
                                delta->tile_x,
                                delta->tile_y,
                                data->tile_size,
                                data->layer->width,
                                data->layer->height);
        }
    }

    /* Mark layer cache as dirty since pixels changed */
    layer_invalidate_cache(data->layer);

    /* Mark composite as dirty and invalidate affected tiles */
    if (doc && doc->tile_grid && data->layer) {
        for (guint i = 0; i < data->tile_deltas->len; i++) {
            TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
            if (delta) {
                /* Convert layer tile coordinates to document coordinates */
                gint doc_x = data->layer->offset_x + (delta->tile_x * data->tile_size);
                gint doc_y = data->layer->offset_y + (delta->tile_y * data->tile_size);
                tile_grid_mark_rect_dirty(doc->tile_grid, doc_x, doc_y,
                                          data->tile_size, data->tile_size);
            }
        }
        doc->composite_dirty = TRUE;
    }
}

/**
 * Tile undo command revert callback (restore to "before" state for undo)
 */
static void tile_undo_command_revert(Command* cmd, struct ImageDocument* doc) {
    TileUndoCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (TileUndoCommandData*)cmd->user_data;

    if (!data->layer || !data->layer->surface) {
        return;
    }

    /* If entry is on disk, read from disk journal */
    if (data->entry_index && doc->undo_journal) {
        if (undo_journal_read_undo(doc->undo_journal, data->entry_index, doc)) {
            /* Successfully read and applied from disk */
            return;
        }
        /* Fall through to in-memory path if disk read failed */
    }

    /* In-memory path: apply from tile_deltas */
    if (!data->tile_deltas) {
        return;
    }

    gboolean layer_found = FALSE;
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
        return;
    }

    /* Apply "before" snapshot to each modified tile region */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta && delta->before) {
            tile_snapshot_apply(data->layer->surface,
                                delta->before,
                                delta->tile_x,
                                delta->tile_y,
                                data->tile_size,
                                data->layer->width,
                                data->layer->height);
        }
    }

    /* Mark layer cache as dirty since pixels changed */
    layer_invalidate_cache(data->layer);

    /* Mark composite as dirty and invalidate affected tiles */
    if (doc && doc->tile_grid && data->layer) {
        for (guint i = 0; i < data->tile_deltas->len; i++) {
            TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
            if (delta) {
                /* Convert layer tile coordinates to document coordinates */
                gint doc_x = data->layer->offset_x + (delta->tile_x * data->tile_size);
                gint doc_y = data->layer->offset_y + (delta->tile_y * data->tile_size);
                tile_grid_mark_rect_dirty(doc->tile_grid, doc_x, doc_y,
                                          data->tile_size, data->tile_size);
            }
        }
        doc->composite_dirty = TRUE;
    }
}

/**
 * Tile undo command destroy callback
 */
static void tile_undo_command_destroy(Command* cmd) {
    TileUndoCommandData* data;
    guint i;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (TileUndoCommandData*)cmd->user_data;

    /* Free all tile deltas (may be empty if on disk) */
    if (data->tile_deltas) {
        for (i = 0; i < data->tile_deltas->len; i++) {
            TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
            if (delta) {
                if (delta->before) {
                    cairo_surface_destroy(delta->before);
                }
                if (delta->after) {
                    cairo_surface_destroy(delta->after);
                }
                g_free(delta);
            }
        }
        g_ptr_array_free(data->tile_deltas, TRUE);
    }

    /* Note: entry_index is owned by the journal, not freed here */

    g_free(data);
}

/**
 * Begin a tile-based undo transaction
 */
TileUndoTransaction* tile_undo_transaction_begin(struct ImageLayer* layer,
                                                 struct ImageDocument* doc,
                                                 const gchar* name) {
    TileUndoTransaction* transaction;

    if (!layer || !layer->surface || !doc) {
        return NULL;
    }

    /* Get tile size from document (use default if no tile grid exists yet) */
    gint tile_size = doc->tile_grid ? doc->tile_grid->tile_size : 128;

    transaction = (TileUndoTransaction*)g_malloc0(sizeof(TileUndoTransaction));
    transaction->layer = layer;
    transaction->doc = doc;
    transaction->tile_size = tile_size;
    transaction->name = g_strdup(name ? name : command_get_name_string(CMD_NAME_DRAW_BRUSH_STROKE));

    /* Create hash table to track modified tiles */
    transaction->modified_tiles = g_hash_table_new_full(
        tile_coord_hash,
        tile_coord_equal,
        NULL,                    /* key destroy func (not needed, keys are integers) */
        (GDestroyNotify)g_free); /* value destroy func (TileUndoDelta*) */

    return transaction;
}

/**
 * Register a tile region as modified (captures "before" state on first call)
 */
gboolean tile_undo_transaction_register_tile(TileUndoTransaction* transaction,
                                             struct ImageDocument* doc,
                                             gint layer_x,
                                             gint layer_y) {
    gint tile_x, tile_y;
    guint tile_key;
    TileUndoDelta* delta;

    if (!transaction || !transaction->layer || !transaction->layer->surface) {
        return FALSE;
    }

    /* Convert layer pixel coordinates to tile coordinates */
    tile_x = layer_x / transaction->tile_size;
    tile_y = layer_y / transaction->tile_size;

    /* Create hash key from tile coordinates */
    tile_key = (guint)((tile_x << 16) | (tile_y & 0xFFFF));
    gpointer key = GUINT_TO_POINTER(tile_key);

    /* Check if this tile has already been registered */
    if (g_hash_table_contains(transaction->modified_tiles, key)) {
        return TRUE; /* Already registered, no need to capture again */
    }

    /* Create new delta and capture "before" snapshot */
    delta = (TileUndoDelta*)g_malloc0(sizeof(TileUndoDelta));
    delta->tile_x = tile_x;
    delta->tile_y = tile_y;
    delta->before = tile_snapshot_create(transaction->layer->surface,
                                         tile_x,
                                         tile_y,
                                         transaction->tile_size,
                                         transaction->layer->width,
                                         transaction->layer->height);
    delta->after = NULL; /* Will be set on commit */

    if (!delta->before) {
        g_free(delta);
        return FALSE;
    }

    /* Store in hash table */
    g_hash_table_insert(transaction->modified_tiles, key, delta);

    return TRUE;
}

/**
 * Commit a tile-based undo transaction (captures "after" state and creates command)
 */
Command* tile_undo_transaction_commit(TileUndoTransaction* transaction) {
    TileUndoCommandData* data;
    Command* cmd;
    GHashTableIter iter;
    gpointer key, value;
    TileUndoDelta* delta;

    if (!transaction || !transaction->layer || !transaction->layer->surface) {
        if (transaction) {
            tile_undo_transaction_cancel(transaction);
        }
        return NULL;
    }

    /* Create command data */
    data = (TileUndoCommandData*)g_malloc0(sizeof(TileUndoCommandData));
    data->layer = transaction->layer;
    data->tile_size = transaction->tile_size;
    data->tile_deltas = g_ptr_array_new();
    data->entry_index = NULL; /* Will be set if written to disk */

    /* Collect all deltas first, then process them */
    GList* all_deltas = NULL;
    g_hash_table_iter_init(&iter, transaction->modified_tiles);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        delta = (TileUndoDelta*)value;
        if (delta) {
            all_deltas = g_list_prepend(all_deltas, delta);
        }
    }

    /* Process each delta: capture "after" snapshot and add to command */
    for (GList* l = all_deltas; l; l = l->next) {
        delta = (TileUndoDelta*)l->data;
        if (delta) {
            /* Capture "after" snapshot */
            delta->after = tile_snapshot_create(transaction->layer->surface,
                                                delta->tile_x,
                                                delta->tile_y,
                                                transaction->tile_size,
                                                transaction->layer->width,
                                                transaction->layer->height);

            if (delta->after) {
                /* Add to deltas array */
                g_ptr_array_add(data->tile_deltas, delta);
            } else {
                /* Failed to capture after snapshot, free this delta */
                if (delta->before) {
                    cairo_surface_destroy(delta->before);
                }
                g_free(delta);
            }
        }
    }
    g_list_free(all_deltas);

    /* If no valid deltas were captured, free and return NULL */
    if (data->tile_deltas->len == 0) {
        g_ptr_array_free(data->tile_deltas, TRUE);
        g_free(data);
        tile_undo_transaction_cancel(transaction);
        return NULL;
    }

    /* Create command */
    cmd = command_new(transaction->name,
                      COMMAND_DRAW,
                      tile_undo_command_apply,
                      tile_undo_command_revert,
                      tile_undo_command_destroy);
    if (!cmd) {
        /* Free all deltas */
        guint i;
        for (i = 0; i < data->tile_deltas->len; i++) {
            delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
            if (delta) {
                if (delta->before)
                    cairo_surface_destroy(delta->before);
                if (delta->after)
                    cairo_surface_destroy(delta->after);
                g_free(delta);
            }
        }
        g_ptr_array_free(data->tile_deltas, TRUE);
        g_free(data);
        tile_undo_transaction_cancel(transaction);
        return NULL;
    }

    cmd->user_data = data;

    /* Save doc pointer before freeing transaction */
    struct ImageDocument* doc = transaction->doc;

    /* Free transaction structure (deltas have been transferred to command) */
    /* Steal all items from hash table without calling destroy function (ownership transferred) */
    if (transaction->modified_tiles) {
        g_hash_table_steal_all(transaction->modified_tiles);
        g_hash_table_destroy(transaction->modified_tiles);
    }
    if (transaction->name) {
        g_free(transaction->name);
    }
    g_free(transaction);

    /* If document has a disk journal, write command to disk and free in-memory deltas */
    if (doc && doc->undo_journal) {
        /* Write to disk - this will create an entry_index and store it in cmd->user_data */
        if (undo_journal_write_tile_command(doc->undo_journal, cmd)) {
            /* Free in-memory pixel data since it's now on disk */
            for (guint i = 0; i < data->tile_deltas->len; i++) {
                TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
                if (delta) {
                    if (delta->before) {
                        cairo_surface_destroy(delta->before);
                        delta->before = NULL;
                    }
                    if (delta->after) {
                        cairo_surface_destroy(delta->after);
                        delta->after = NULL;
                    }
                }
            }
        }
    }

    return cmd;
}

/**
 * Cancel a tile-based undo transaction (frees resources without creating command)
 */
void tile_undo_transaction_cancel(TileUndoTransaction* transaction) {
    if (!transaction) {
        return;
    }

    /* Free all deltas in hash table */
    if (transaction->modified_tiles) {
        GHashTableIter iter;
        gpointer key, value;
        TileUndoDelta* delta;

        g_hash_table_iter_init(&iter, transaction->modified_tiles);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            delta = (TileUndoDelta*)value;
            if (delta) {
                if (delta->before) {
                    cairo_surface_destroy(delta->before);
                }
                if (delta->after) {
                    cairo_surface_destroy(delta->after);
                }
                g_free(delta);
            }
        }
        g_hash_table_destroy(transaction->modified_tiles);
    }

    if (transaction->name) {
        g_free(transaction->name);
    }

    g_free(transaction);
}

/**
 * Move selected pixels command data structure
 */
typedef struct {
    struct ImageLayer* new_layer;       /* The extracted layer with selected pixels */
    struct ImageLayer* original_layer;  /* The original layer pixels were extracted from */
    struct ImageDocument* doc;          /* Document reference */
    cairo_surface_t* original_snapshot; /* Snapshot of original layer before extraction (to restore on undo) */
    gint initial_offset_x;              /* Initial position of extracted layer */
    gint initial_offset_y;
    gint final_offset_x; /* Final position after moving */
    gint final_offset_y;
    gboolean had_selection;    /* TRUE if selection existed and was cleared */
    uint8_t* selection_backup; /* Backup of base_mask before clearing (NULL if no selection) */
    GList* selections_backup;  /* Backup of selections list before clearing (NULL if no selection) */
} MoveSelectedPixelsCommandData;

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
 * Move selected pixels command apply callback
 * Adds the extracted layer and moves it to final position, and clears selection
 */
static void move_selected_pixels_command_apply(Command* cmd, struct ImageDocument* doc) {
    MoveSelectedPixelsCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveSelectedPixelsCommandData*)cmd->user_data;

    if (!data->new_layer) {
        return;
    }

    /* Check if this is a redo (new_layer not in document) or initial execution (new_layer already in document) */
    gboolean is_redo = !g_list_find(doc->layers, data->new_layer);

    /* Add layer to document if not already there */
    if (is_redo) {
        doc->layers = g_list_append(doc->layers, data->new_layer);
        doc->selected_layer = data->new_layer;
    }

    /* Set final position */
    data->new_layer->offset_x = data->final_offset_x;
    data->new_layer->offset_y = data->final_offset_y;

    /* Restore original layer pixels if needed (for redo after undo) */
    /* Only restore snapshot on redo - on initial execution, pixels are already cleared by extraction */
    if (is_redo && data->original_layer && data->original_snapshot) {
        cairo_t* cr = cairo_create(data->original_layer->surface);
        cairo_set_source_surface(cr, data->original_snapshot, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_mark_dirty(data->original_layer->surface);
        layer_invalidate_cache(data->original_layer);

        /* After restoring snapshot, clear pixels from original layer where selection was active */
        /* This is needed for redo - the snapshot has original pixels, but we need to clear selected area again */
        /* Use selection_build_combined_mask to get feathered mask, just like extract_selection_to_layer does */
        if (data->had_selection && data->selection_backup && doc->selection_mask) {
            int stride = doc->selection_mask->stride;
            int width = doc->selection_mask->width;
            int height = doc->selection_mask->height;

            /* Restore selection backup to selection mask temporarily */
            /* Restore base_mask */
            for (int y = 0; y < height; y++) {
                uint8_t* row = doc->selection_mask->base_mask + y * stride;
                uint8_t* backup_row = data->selection_backup + y * stride;
                for (int x = 0; x < width; x++) {
                    row[x] = backup_row[x];
                }
            }

            /* Restore selections list */
            if (doc->selection_mask->selections) {
                GList* iter;
                for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                    Selection* sel = (Selection*)iter->data;
                    if (sel) {
                        selection_unref(sel);
                    }
                }
                g_list_free(doc->selection_mask->selections);
            }
            doc->selection_mask->selections = NULL;

            if (data->selections_backup) {
                GList* iter;
                for (iter = data->selections_backup; iter != NULL; iter = iter->next) {
                    Selection* sel = (Selection*)iter->data;
                    if (sel) {
                        selection_mask_add_selection(doc->selection_mask, selection_ref(sel));
                    }
                }
            }

            /* Regenerate feathered preview if needed */
            doc->selection_mask->feather_dirty = TRUE;
            selection_mask_regenerate_combined_feather_preview(doc->selection_mask);

            /* Calculate layer region in document coordinates */
            gint layer_x_min = data->original_layer->offset_x;
            gint layer_y_min = data->original_layer->offset_y;
            gint layer_x_max = data->original_layer->offset_x + data->original_layer->width;
            gint layer_y_max = data->original_layer->offset_y + data->original_layer->height;

            /* Clamp to document bounds */
            layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
            layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
            layer_x_max = (layer_x_max > doc->width) ? doc->width : layer_x_max;
            layer_y_max = (layer_y_max > doc->height) ? doc->height : layer_y_max;

            if (layer_x_max > layer_x_min && layer_y_max > layer_y_min) {
                /* Create dirty rect for the layer region */
                DirtyRect layer_rect;
                dirty_rect_set(&layer_rect, layer_x_min, layer_y_min,
                               layer_x_max - layer_x_min, layer_y_max - layer_y_min);

                /* Get feathered selection mask for this region */
                DirtyRect actual_region;
                SelectionMask* region_mask = selection_build_combined_mask(
                    doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

                if (region_mask && region_mask->data) {
                    /* Clear pixels from original layer where selection is active (using feathered mask) */
                    cairo_surface_flush(data->original_layer->surface);
                    guchar* src_data = cairo_image_surface_get_data(data->original_layer->surface);
                    gint src_stride = cairo_image_surface_get_stride(data->original_layer->surface);
                    gint layer_width = data->original_layer->width;
                    gint layer_height = data->original_layer->height;

                    for (gint y = 0; y < layer_height; y++) {
                        gint doc_y = data->original_layer->offset_y + y;
                        gint mask_y = doc_y - actual_region.y;

                        if (mask_y < 0 || mask_y >= region_mask->height) {
                            continue;
                        }

                        for (gint x = 0; x < layer_width; x++) {
                            gint doc_x = data->original_layer->offset_x + x;
                            gint mask_x = doc_x - actual_region.x;

                            if (mask_x < 0 || mask_x >= region_mask->width) {
                                continue;
                            }

                            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
                            if (mask_alpha > 0) {
                                /* Clear pixel in source layer (set to transparent) */
                                /* This matches extract_selection_to_layer behavior */
                                guchar* src_pixel = src_data + y * src_stride + x * 4;
                                src_pixel[0] = 0;
                                src_pixel[1] = 0;
                                src_pixel[2] = 0;
                                src_pixel[3] = 0;
                            }
                        }
                    }

                    cairo_surface_mark_dirty(data->original_layer->surface);
                    selection_mask_free(region_mask);
                }
            }

            /* Clear selection mask that we temporarily restored (selection will be cleared at bottom) */
            for (int y = 0; y < height; y++) {
                uint8_t* row = doc->selection_mask->base_mask + y * stride;
                for (int x = 0; x < width; x++) {
                    row[x] = 0;
                }
            }

            /* Clear selections list */
            if (doc->selection_mask->selections) {
                GList* iter;
                for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                    Selection* sel = (Selection*)iter->data;
                    if (sel) {
                        selection_unref(sel);
                    }
                }
                g_list_free(doc->selection_mask->selections);
                doc->selection_mask->selections = NULL;
            }
        }
    }

    /* Clear selection if it was backed up (meaning it existed) */
    if (data->had_selection && doc->selection_mask && data->selection_backup) {
        int stride = doc->selection_mask->stride;
        int width = doc->selection_mask->width;
        int height = doc->selection_mask->height;
        uint8_t* base_mask = doc->selection_mask->base_mask;

        /* Clear entire base_mask */
        for (int y = 0; y < height; y++) {
            uint8_t* row = base_mask + y * stride;
            for (int x = 0; x < width; x++) {
                row[x] = 0;
            }
        }

        /* Clear selections list */
        if (doc->selection_mask->selections) {
            GList* iter;
            for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    selection_unref(sel);
                }
            }
            g_list_free(doc->selection_mask->selections);
            doc->selection_mask->selections = NULL;
        }

        /* Set data pointer to base_mask (no feathering) */
        doc->selection_mask->data = doc->selection_mask->base_mask;

        /* Mark mask as dirty */
        selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
        doc->selection_mask->feather_dirty = TRUE;
    }

    document_invalidate_composite(doc);
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Move selected pixels command revert callback
 * Removes the extracted layer and restores original layer pixels and selection
 */
static void move_selected_pixels_command_revert(Command* cmd, struct ImageDocument* doc) {
    MoveSelectedPixelsCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MoveSelectedPixelsCommandData*)cmd->user_data;

    if (!data->new_layer || !data->original_layer) {
        return;
    }

    /* Remove extracted layer from document */
    if (g_list_find(doc->layers, data->new_layer)) {
        doc->layers = g_list_remove(doc->layers, data->new_layer);

        /* Update selected layer if needed */
        if (doc->selected_layer == data->new_layer) {
            doc->selected_layer = data->original_layer;
        }
    }

    /* Restore original layer pixels from snapshot */
    if (data->original_snapshot) {
        cairo_t* cr = cairo_create(data->original_layer->surface);
        cairo_set_source_surface(cr, data->original_snapshot, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_mark_dirty(data->original_layer->surface);
        layer_invalidate_cache(data->original_layer);
    }

    /* Restore selection if it was backed up */
    if (data->had_selection && doc->selection_mask && data->selection_backup) {
        int stride = doc->selection_mask->stride;
        int width = doc->selection_mask->width;
        int height = doc->selection_mask->height;
        uint8_t* base_mask = doc->selection_mask->base_mask;

        /* Restore base_mask from backup */
        for (int y = 0; y < height; y++) {
            uint8_t* row = base_mask + y * stride;
            uint8_t* backup_row = data->selection_backup + y * stride;
            for (int x = 0; x < width; x++) {
                row[x] = backup_row[x];
            }
        }

        /* Restore selections list */
        if (doc->selection_mask->selections) {
            GList* iter;
            for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    selection_unref(sel);
                }
            }
            g_list_free(doc->selection_mask->selections);
        }
        doc->selection_mask->selections = NULL;

        /* Restore selections from backup */
        if (data->selections_backup) {
            GList* iter;
            for (iter = data->selections_backup; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    selection_mask_add_selection(doc->selection_mask, selection_ref(sel));
                }
            }
        }

        /* Set data pointer - will be set by selection_mask_add_selection based on feathering */
        doc->selection_mask->data = doc->selection_mask->base_mask;

        /* Mark mask as dirty */
        selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
        doc->selection_mask->feather_dirty = TRUE;
    }

    document_invalidate_composite(doc);
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Move selected pixels command destroy callback
 */
static void move_selected_pixels_command_destroy(Command* cmd) {
    MoveSelectedPixelsCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MoveSelectedPixelsCommandData*)cmd->user_data;

    /* Free snapshot if exists */
    if (data->original_snapshot) {
        cairo_surface_destroy(data->original_snapshot);
    }

    /* Free selection backup if exists */
    if (data->selection_backup) {
        g_free(data->selection_backup);
    }

    /* Free selections backup if exists */
    if (data->selections_backup) {
        GList* iter;
        for (iter = data->selections_backup; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(data->selections_backup);
    }

    /* Free extracted layer if not in document (undo case) */
    if (data->new_layer) {
        if (!data->doc || !data->doc->layers) {
            /* Document is being freed - don't free layer */
        } else if (!g_list_find(data->doc->layers, data->new_layer)) {
            /* Layer not in document - free it */
            layer_free(data->new_layer);
        }
    }

    g_free(data);
}

/**
 * Create a move selected pixels command
 * This command handles extracting selected pixels to a new layer and moving that layer
 * @param snapshot Optional snapshot of original layer (taken BEFORE extraction). If NULL, creates one.
 */
Command* command_create_move_selected_pixels_with_snapshot(struct ImageDocument* doc,
                                                           struct ImageLayer* new_layer,
                                                           struct ImageLayer* original_layer,
                                                           gint initial_x, gint initial_y,
                                                           gint final_x, gint final_y,
                                                           cairo_surface_t* snapshot) {
    Command* cmd;
    MoveSelectedPixelsCommandData* data;

    if (!doc || !new_layer || !original_layer || !original_layer->surface) {
        if (snapshot) {
            cairo_surface_destroy(snapshot);
        }
        return NULL;
    }

    /* Create snapshot if not provided */
    if (!snapshot) {
        snapshot = cairo_surface_snapshot(original_layer->surface);
        if (!snapshot) {
            return NULL;
        }
    }

    /* Create command data */
    data = (MoveSelectedPixelsCommandData*)g_malloc(sizeof(MoveSelectedPixelsCommandData));
    data->new_layer = new_layer;
    data->original_layer = original_layer;
    data->doc = doc;
    data->original_snapshot = snapshot;
    data->initial_offset_x = initial_x;
    data->initial_offset_y = initial_y;
    data->final_offset_x = final_x;
    data->final_offset_y = final_y;
    data->had_selection = FALSE;
    data->selection_backup = NULL;
    data->selections_backup = NULL;

    /* Backup selection state if selection exists */
    if (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        int stride = doc->selection_mask->stride;
        int width = doc->selection_mask->width;
        int height = doc->selection_mask->height;

        /* Backup base_mask */
        data->selection_backup = g_malloc(stride * height);
        for (int y = 0; y < height; y++) {
            uint8_t* src_row = doc->selection_mask->base_mask + y * stride;
            uint8_t* dst_row = data->selection_backup + y * stride;
            for (int x = 0; x < width; x++) {
                dst_row[x] = src_row[x];
            }
        }

        /* Backup selections list (create references) */
        if (doc->selection_mask->selections) {
            GList* iter;
            for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
                Selection* sel = (Selection*)iter->data;
                if (sel) {
                    data->selections_backup = g_list_append(data->selections_backup, selection_ref(sel));
                }
            }
        }

        data->had_selection = TRUE;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_MOVE_SELECTED_PIXELS),
                      COMMAND_LAYER_EDIT,
                      move_selected_pixels_command_apply,
                      move_selected_pixels_command_revert,
                      move_selected_pixels_command_destroy);

    if (!cmd) {
        cairo_surface_destroy(snapshot);
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a move selected pixels command (creates snapshot automatically)
 * This command handles extracting selected pixels to a new layer and moving that layer
 * NOTE: This creates snapshot AFTER extraction, so pixels are already cleared. Use with_snapshot version instead.
 */
Command* command_create_move_selected_pixels(struct ImageDocument* doc,
                                             struct ImageLayer* new_layer,
                                             struct ImageLayer* original_layer,
                                             gint initial_x, gint initial_y,
                                             gint final_x, gint final_y) {
    /* Create snapshot automatically (but this will be AFTER extraction, so pixels are already cleared) */
    /* This function should only be used when snapshot is taken before extraction */
    return command_create_move_selected_pixels_with_snapshot(
        doc, new_layer, original_layer, initial_x, initial_y, final_x, final_y, NULL);
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
                               LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
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
 * Create a paste command (similar to layer add but with "Paste" name)
 */
Command* command_create_paste(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerAddCommandData* data;

    if (!doc || !layer) {
        return NULL;
    }

    data = (LayerAddCommandData*)g_malloc(sizeof(LayerAddCommandData));
    data->doc = doc;
    data->layer = layer;

    cmd = command_new("Paste",
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

/**
 * Structure to store old layer offsets for canvas resize undo
 */
typedef struct {
    struct ImageLayer* layer;
    gint old_offset_x;
    gint old_offset_y;
} LayerOffsetPair;

/**
 * Canvas resize command apply callback (restore to new size)
 */
static void canvas_resize_command_apply(Command* cmd, struct ImageDocument* doc) {
    CanvasResizeCommandData* data;
    GList* iter;
    LayerOffsetPair* pair;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (CanvasResizeCommandData*)cmd->user_data;

    /* Restore new canvas dimensions */
    doc->width = data->new_width;
    doc->height = data->new_height;

    /* Restore layer offsets to new positions */
    for (iter = data->layer_offsets; iter; iter = iter->next) {
        pair = (LayerOffsetPair*)iter->data;
        if (pair && pair->layer) {
            /* Apply offset adjustment */
            pair->layer->offset_x = pair->old_offset_x + data->offset_x;
            pair->layer->offset_y = pair->old_offset_y + data->offset_y;
            layer_invalidate_cache(pair->layer);
        }
    }

    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after canvas resize redo");
    }

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);
}

/**
 * Canvas resize command revert callback (restore to old size)
 */
static void canvas_resize_command_revert(Command* cmd, struct ImageDocument* doc) {
    CanvasResizeCommandData* data;
    GList* iter;
    LayerOffsetPair* pair;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (CanvasResizeCommandData*)cmd->user_data;

    /* Restore old canvas dimensions */
    doc->width = data->old_width;
    doc->height = data->old_height;

    /* Restore layer offsets to old positions */
    for (iter = data->layer_offsets; iter; iter = iter->next) {
        pair = (LayerOffsetPair*)iter->data;
        if (pair && pair->layer) {
            /* Restore old offset */
            pair->layer->offset_x = pair->old_offset_x;
            pair->layer->offset_y = pair->old_offset_y;
            layer_invalidate_cache(pair->layer);
        }
    }

    /* Recreate tile grid with old dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after canvas size undo");
    }

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);
}

/**
 * Canvas resize command destroy callback
 */
static void canvas_resize_command_destroy(Command* cmd) {
    CanvasResizeCommandData* data;
    GList* iter;
    LayerOffsetPair* pair;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (CanvasResizeCommandData*)cmd->user_data;

    /* Free layer offset pairs */
    if (data->layer_offsets) {
        for (iter = data->layer_offsets; iter; iter = iter->next) {
            pair = (LayerOffsetPair*)iter->data;
            if (pair) {
                g_free(pair);
            }
        }
        g_list_free(data->layer_offsets);
    }

    g_free(data);
}

/**
 * Create a canvas resize command
 */
Command* command_create_canvas_resize(guint old_width, guint old_height,
                                      guint new_width, guint new_height,
                                      gdouble old_resolution, gdouble new_resolution,
                                      gint offset_x, gint offset_y,
                                      struct ImageDocument* doc) {
    Command* cmd;
    CanvasResizeCommandData* data;
    GList* iter;
    ImageLayer* layer;
    LayerOffsetPair* pair;

    if (!doc) {
        return NULL;
    }

    /* Create command data */
    data = (CanvasResizeCommandData*)g_malloc(sizeof(CanvasResizeCommandData));
    data->old_width = old_width;
    data->old_height = old_height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->old_resolution = old_resolution;
    data->new_resolution = new_resolution;
    data->offset_x = offset_x;
    data->offset_y = offset_y;
    data->layer_offsets = NULL;

    /* Store old offsets for all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (layer) {
            pair = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
            pair->layer = layer;
            pair->old_offset_x = layer->offset_x;
            pair->old_offset_y = layer->offset_y;
            data->layer_offsets = g_list_append(data->layer_offsets, pair);
        }
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_CANVAS_SIZE),
                      COMMAND_CANVAS_RESIZE,
                      canvas_resize_command_apply,
                      canvas_resize_command_revert,
                      canvas_resize_command_destroy);

    if (!cmd) {
        /* Free layer offset pairs */
        if (data->layer_offsets) {
            for (iter = data->layer_offsets; iter; iter = iter->next) {
                pair = (LayerOffsetPair*)iter->data;
                if (pair) {
                    g_free(pair);
                }
            }
            g_list_free(data->layer_offsets);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Helper function to flip a layer using Ocular library
 */
static gboolean flip_layer_impl(struct ImageLayer* layer, OcDirection direction) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    if (width == 0 || height == 0) {
        return FALSE;
    }

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Flip layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        g_warning("Flip layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply flip using Ocular library (4 channels for RGBA) */
    status = ocularFlipImage(rgba_input, rgba_output, width, height, width * 4, direction);

    if (status != OC_STATUS_OK) {
        g_warning("Flip layer: Ocular flip returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        g_warning("Flip layer: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper function to transpose a layer using Ocular library
 */
static gboolean transpose_layer_impl(struct ImageLayer* layer) {
    cairo_surface_t* old_surface;
    cairo_surface_t* new_surface;
    gint old_width, old_height;
    gint new_width, new_height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    old_surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(old_surface, &old_width, &old_height)) {
        return FALSE;
    }

    if (old_width == 0 || old_height == 0) {
        return FALSE;
    }

    /* Transpose swaps width and height */
    new_width = old_height;
    new_height = old_width;

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(old_width * old_height * 4);
    rgba_output = (guchar*)g_malloc(new_width * new_height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Transpose layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(old_surface, rgba_input)) {
        g_warning("Transpose layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply transpose using Ocular library
       Stride is width * 4 for RGBA format */
    status = ocularTransposeImage(rgba_input, rgba_output, old_width, old_height, old_width * 4);

    if (status != OC_STATUS_OK) {
        g_warning("Transpose layer: Ocular transpose returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Create new surface with swapped dimensions */
    new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!new_surface) {
        g_warning("Transpose layer: Failed to create new surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert RGBA output to new Cairo surface */
    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        g_warning("Transpose layer: Failed to convert RGBA to new surface");
        cairo_surface_destroy(new_surface);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Replace old surface with new one */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;

    /* Update layer dimensions */
    layer->width = new_width;
    layer->height = new_height;

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper: Rotate a layer using Ocular library
 */
static gboolean rotate_layer_impl(struct ImageLayer* layer,
                                  gfloat angle_degrees,
                                  gboolean preserve_size,
                                  gboolean use_transparency,
                                  OcInterpolationMode interpolation_mode,
                                  guchar fill_r,
                                  guchar fill_g,
                                  guchar fill_b,
                                  gint target_width,
                                  gint target_height,
                                  gint doc_width,
                                  gint doc_height) {
    if (!layer || !layer->surface) {
        return FALSE;
    }

    cairo_surface_t* old_surface = layer->surface;
    gint layer_w, layer_h;
    if (!adjustments_validate_surface(old_surface, &layer_w, &layer_h)) {
        return FALSE;
    }

    if (layer_w <= 0 || layer_h <= 0 || doc_width <= 0 || doc_height <= 0) {
        return FALSE;
    }

    /* Build a full-canvas surface with the layer painted at its document offset.
     * This makes Image-menu rotation apply to ALL layers consistently in document space. */
    cairo_surface_t* doc_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, doc_width, doc_height);
    if (!doc_surface || cairo_surface_status(doc_surface) != CAIRO_STATUS_SUCCESS) {
        if (doc_surface) {
            cairo_surface_destroy(doc_surface);
        }
        return FALSE;
    }

    cairo_t* cr = cairo_create(doc_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, old_surface, layer->offset_x, layer->offset_y);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Convert to RGBA for Ocular */
    guchar* rgba_input = (guchar*)g_malloc((gsize)doc_width * (gsize)doc_height * 4);
    if (!rgba_input) {
        cairo_surface_destroy(doc_surface);
        return FALSE;
    }
    if (!adjustments_cairo_to_rgba(doc_surface, rgba_input)) {
        cairo_surface_destroy(doc_surface);
        g_free(rgba_input);
        return FALSE;
    }
    cairo_surface_destroy(doc_surface);

    gint new_w = target_width;
    gint new_h = target_height;
    if (preserve_size) {
        new_w = doc_width;
        new_h = doc_height;
    }
    if (new_w <= 0 || new_h <= 0) {
        g_free(rgba_input);
        return FALSE;
    }

    guchar* rgba_output = (guchar*)g_malloc((gsize)new_w * (gsize)new_h * 4);
    if (!rgba_output) {
        g_free(rgba_input);
        return FALSE;
    }

    OC_STATUS status = ocularRotateImage(rgba_input, doc_width, doc_height, doc_width * 4, rgba_output,
                                         &new_w, &new_h,
                                         angle_degrees,
                                         preserve_size ? true : false,
                                         use_transparency ? true : false,
                                         interpolation_mode,
                                         fill_r, fill_g, fill_b);
    g_free(rgba_input);

    if (status != OC_STATUS_OK) {
        g_warning("Rotate layer: Ocular rotate returned error %d", status);
        g_free(rgba_output);
        return FALSE;
    }

    if (new_w <= 0 || new_h <= 0) {
        g_free(rgba_output);
        return FALSE;
    }

    cairo_surface_t* new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
    if (!new_surface || cairo_surface_status(new_surface) != CAIRO_STATUS_SUCCESS) {
        if (new_surface) {
            cairo_surface_destroy(new_surface);
        }
        g_free(rgba_output);
        return FALSE;
    }

    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        cairo_surface_destroy(new_surface);
        g_free(rgba_output);
        return FALSE;
    }

    g_free(rgba_output);

    /* Replace surface (layer becomes full-canvas after document-space rotation) */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;
    layer->width = new_w;
    layer->height = new_h;
    layer->offset_x = 0;
    layer->offset_y = 0;
    layer_invalidate_cache(layer);

    return TRUE;
}

typedef struct {
    struct ImageDocument* doc;
    GList* layer_snapshots;
    GList* layers;
    GList* layer_offsets; /* List of LayerOffset* */
    guint old_width;
    guint old_height;
    guint new_width;
    guint new_height;
    gfloat angle_degrees;
    gboolean preserve_size;
    gboolean use_transparency;
    gint interpolation_mode;
    guchar fill_r, fill_g, fill_b;
} RotateCommandData;

typedef struct {
    gint x;
    gint y;
} LayerOffset;

static void rotate_command_data_free(RotateCommandData* data) {
    if (!data) {
        return;
    }

    if (data->layer_snapshots) {
        for (GList* iter = data->layer_snapshots; iter; iter = iter->next) {
            cairo_surface_t* snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    if (data->layers) {
        g_list_free(data->layers);
    }

    if (data->layer_offsets) {
        for (GList* iter = data->layer_offsets; iter; iter = iter->next) {
            LayerOffset* off = (LayerOffset*)iter->data;
            g_free(off);
        }
        g_list_free(data->layer_offsets);
    }

    g_free(data);
}

static void rotate_command_apply(Command* cmd, struct ImageDocument* doc) {
    RotateCommandData* data;
    if (!cmd || !cmd->user_data || !doc) {
        return;
    }
    data = (RotateCommandData*)cmd->user_data;

    OcInterpolationMode interp = (OcInterpolationMode)data->interpolation_mode;

    for (GList* iter = data->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface) {
            continue;
        }
        if (!rotate_layer_impl(layer,
                               data->angle_degrees,
                               data->preserve_size,
                               data->use_transparency,
                               interp,
                               data->fill_r, data->fill_g, data->fill_b,
                               (gint)data->new_width, (gint)data->new_height,
                               (gint)data->old_width, (gint)data->old_height)) {
            g_warning("Failed to rotate layer: %s", layer->name);
        }
    }

    doc->width = data->new_width;
    doc->height = data->new_height;

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after rotate");
    }

    document_invalidate_composite(doc);
}

static void rotate_command_revert(Command* cmd, struct ImageDocument* doc) {
    RotateCommandData* data;
    if (!cmd || !cmd->user_data || !doc) {
        return;
    }
    data = (RotateCommandData*)cmd->user_data;

    GList* layer_iter = data->layers;
    GList* snapshot_iter = data->layer_snapshots;
    GList* offset_iter = data->layer_offsets;
    while (layer_iter && snapshot_iter && offset_iter) {
        struct ImageLayer* layer = (struct ImageLayer*)layer_iter->data;
        cairo_surface_t* snapshot = (cairo_surface_t*)snapshot_iter->data;
        LayerOffset* off = (LayerOffset*)offset_iter->data;
        if (layer && snapshot) {
            gint w = cairo_image_surface_get_width(snapshot);
            gint h = cairo_image_surface_get_height(snapshot);

            if (layer->surface) {
                cairo_surface_destroy(layer->surface);
            }
            layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            if (layer->surface) {
                cairo_t* cr = cairo_create(layer->surface);
                cairo_set_source_surface(cr, snapshot, 0, 0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_destroy(cr);
                layer->width = w;
                layer->height = h;
                if (off) {
                    layer->offset_x = off->x;
                    layer->offset_y = off->y;
                }
                layer_invalidate_cache(layer);
            }
        }
        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
        offset_iter = offset_iter->next;
    }

    doc->width = data->old_width;
    doc->height = data->old_height;

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after rotate revert");
    }

    document_invalidate_composite(doc);
}

static void rotate_command_destroy(Command* cmd) {
    RotateCommandData* data;
    if (!cmd || !cmd->user_data) {
        return;
    }
    data = (RotateCommandData*)cmd->user_data;
    rotate_command_data_free(data);
}

Command* command_create_rotate_arbitrary_named(const gchar* name,
                                               struct ImageDocument* doc,
                                               gfloat angle_degrees,
                                               gboolean preserve_size,
                                               gboolean use_transparency,
                                               gint interpolation_mode,
                                               guchar fill_r,
                                               guchar fill_g,
                                               guchar fill_b) {
    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    RotateCommandData* data = (RotateCommandData*)g_malloc0(sizeof(RotateCommandData));
    if (!data) {
        return NULL;
    }

    data->doc = doc;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->angle_degrees = angle_degrees;
    data->preserve_size = preserve_size;
    data->use_transparency = use_transparency;
    data->interpolation_mode = interpolation_mode;
    data->fill_r = fill_r;
    data->fill_g = fill_g;
    data->fill_b = fill_b;

    /* Compute new doc dimensions */
    if (preserve_size) {
        data->new_width = doc->width;
        data->new_height = doc->height;
    } else {
        /* Avoid +1px from floating point error at exact right angles */
        gdouble a = fmod(fabs((gdouble)angle_degrees), 360.0);
        if (fabs(a - 90.0) < 1e-6 || fabs(a - 270.0) < 1e-6) {
            data->new_width = doc->height;
            data->new_height = doc->width;
        } else if (fabs(a - 180.0) < 1e-6 || fabs(a) < 1e-6) {
            data->new_width = doc->width;
            data->new_height = doc->height;
        } else {
            gdouble rad = (gdouble)angle_degrees * (G_PI / 180.0);
            gdouble c = fabs(cos(rad));
            gdouble s = fabs(sin(rad));
            if (c < 1e-12)
                c = 0.0;
            if (s < 1e-12)
                s = 0.0;
            if (fabs(1.0 - c) < 1e-12)
                c = 1.0;
            if (fabs(1.0 - s) < 1e-12)
                s = 1.0;
            data->new_width = (guint)ceil((gdouble)doc->width * c + (gdouble)doc->height * s);
            data->new_height = (guint)ceil((gdouble)doc->width * s + (gdouble)doc->height * c);
        }
        if (data->new_width == 0)
            data->new_width = doc->width;
        if (data->new_height == 0)
            data->new_height = doc->height;
    }

    /* Snapshot all layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface) {
            continue;
        }
        cairo_surface_t* snapshot = cairo_surface_snapshot(layer->surface);
        if (snapshot) {
            data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
            data->layers = g_list_append(data->layers, layer);
            LayerOffset* off = (LayerOffset*)g_malloc(sizeof(LayerOffset));
            if (off) {
                off->x = layer->offset_x;
                off->y = layer->offset_y;
                data->layer_offsets = g_list_append(data->layer_offsets, off);
            } else {
                data->layer_offsets = g_list_append(data->layer_offsets, NULL);
            }
        }
    }

    if (!data->layers) {
        rotate_command_data_free(data);
        return NULL;
    }

    const gchar* cmd_name = (name && name[0]) ? name : "Arbitrary image rotation";
    Command* cmd = command_new(cmd_name,
                               COMMAND_LAYER_EDIT,
                               rotate_command_apply,
                               rotate_command_revert,
                               rotate_command_destroy);
    if (!cmd) {
        rotate_command_data_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

Command* command_create_rotate_arbitrary(struct ImageDocument* doc,
                                         gfloat angle_degrees,
                                         gboolean preserve_size,
                                         gboolean use_transparency,
                                         gint interpolation_mode,
                                         guchar fill_r,
                                         guchar fill_g,
                                         guchar fill_b) {
    return command_create_rotate_arbitrary_named("Arbitrary image rotation",
                                                 doc,
                                                 angle_degrees,
                                                 preserve_size,
                                                 use_transparency,
                                                 interpolation_mode,
                                                 fill_r, fill_g, fill_b);
}

/**
 * Flip command apply callback (apply flip)
 */
static void flip_command_apply(Command* cmd, struct ImageDocument* doc) {
    FlipCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    OcDirection direction;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (FlipCommandData*)cmd->user_data;

    /* Determine direction from command name */
    if (g_strcmp0(cmd->name, command_get_name_string(CMD_NAME_FLIP_HORIZONTAL)) == 0) {
        direction = OC_DIRECTION_HORIZONTAL;
    } else {
        direction = OC_DIRECTION_VERTICAL;
    }

    /* Apply flip to all layers */
    for (iter = data->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            if (!flip_layer_impl(layer, direction)) {
                g_warning("Failed to flip layer: %s", layer->name);
            }
        }
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flip command revert callback (restore from snapshots)
 */
static void flip_command_revert(Command* cmd, struct ImageDocument* doc) {
    FlipCommandData* data;
    GList *layer_iter, *snapshot_iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;
    cairo_t* cr;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (FlipCommandData*)cmd->user_data;

    /* Restore all layers from snapshots */
    layer_iter = data->layers;
    snapshot_iter = data->layer_snapshots;
    while (layer_iter && snapshot_iter) {
        layer = (struct ImageLayer*)layer_iter->data;
        snapshot = (cairo_surface_t*)snapshot_iter->data;

        if (layer && layer->surface && snapshot) {
            /* Restore layer from snapshot */
            cr = cairo_create(layer->surface);
            cairo_set_source_surface(cr, snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Invalidate layer cache */
            layer_invalidate_cache(layer);
        }

        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flip command destroy callback
 */
static void flip_command_destroy(Command* cmd) {
    FlipCommandData* data;
    GList* iter;
    cairo_surface_t* snapshot;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (FlipCommandData*)cmd->user_data;

    /* Free all snapshots */
    if (data->layer_snapshots) {
        for (iter = data->layer_snapshots; iter; iter = iter->next) {
            snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    /* Free layers list (but don't free the layers themselves) */
    if (data->layers) {
        g_list_free(data->layers);
    }

    g_free(data);
}

/**
 * Transpose command apply callback (apply transpose)
 */
static void transpose_command_apply(Command* cmd, struct ImageDocument* doc) {
    TransposeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (TransposeCommandData*)cmd->user_data;

    /* Apply transpose to all layers */
    for (iter = data->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            if (!transpose_layer_impl(layer)) {
                g_warning("Failed to transpose layer: %s", layer->name);
            }
        }
    }

    /* Update document dimensions */
    doc->width = data->new_width;
    doc->height = data->new_height;

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after transpose");
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Transpose command revert callback (restore from snapshots)
 */
static void transpose_command_revert(Command* cmd, struct ImageDocument* doc) {
    TransposeCommandData* data;
    GList *layer_iter, *snapshot_iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;
    cairo_t* cr;
    gint old_width, old_height;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (TransposeCommandData*)cmd->user_data;

    /* Restore all layers from snapshots */
    layer_iter = data->layers;
    snapshot_iter = data->layer_snapshots;
    while (layer_iter && snapshot_iter) {
        layer = (struct ImageLayer*)layer_iter->data;
        snapshot = (cairo_surface_t*)snapshot_iter->data;

        if (layer && snapshot) {
            /* Get original dimensions from snapshot */
            old_width = cairo_image_surface_get_width(snapshot);
            old_height = cairo_image_surface_get_height(snapshot);

            /* Destroy current surface and create new one with original dimensions */
            if (layer->surface) {
                cairo_surface_destroy(layer->surface);
            }
            layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, old_width, old_height);
            if (layer->surface) {
                /* Restore layer from snapshot */
                cr = cairo_create(layer->surface);
                cairo_set_source_surface(cr, snapshot, 0, 0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_destroy(cr);

                /* Update layer dimensions */
                layer->width = old_width;
                layer->height = old_height;

                /* Invalidate layer cache */
                layer_invalidate_cache(layer);
            }
        }

        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
    }

    /* Restore document dimensions */
    doc->width = data->old_width;
    doc->height = data->old_height;

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate tile grid with original dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after transpose revert");
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Transpose command destroy callback
 */
static void transpose_command_destroy(Command* cmd) {
    TransposeCommandData* data;
    GList* iter;
    cairo_surface_t* snapshot;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (TransposeCommandData*)cmd->user_data;

    /* Free all snapshots */
    if (data->layer_snapshots) {
        for (iter = data->layer_snapshots; iter; iter = iter->next) {
            snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    /* Free layers list (but don't free the layers themselves) */
    if (data->layers) {
        g_list_free(data->layers);
    }

    g_free(data);
}

/**
 * Create a flip horizontal command
 */
Command* command_create_flip_horizontal(struct ImageDocument* doc) {
    Command* cmd;
    FlipCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Create command data */
    data = (FlipCommandData*)g_malloc(sizeof(FlipCommandData));
    data->doc = doc;
    data->layer_snapshots = NULL;
    data->layers = NULL;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);
            }
        }
    }

    if (!data->layers || g_list_length(data->layers) == 0) {
        /* No valid layers found */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_FLIP_HORIZONTAL),
                      COMMAND_LAYER_EDIT,
                      flip_command_apply,
                      flip_command_revert,
                      flip_command_destroy);

    if (!cmd) {
        /* Free snapshots */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a flip vertical command
 */
Command* command_create_flip_vertical(struct ImageDocument* doc) {
    Command* cmd;
    FlipCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Create command data */
    data = (FlipCommandData*)g_malloc(sizeof(FlipCommandData));
    data->doc = doc;
    data->layer_snapshots = NULL;
    data->layers = NULL;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);
            }
        }
    }

    if (!data->layers || g_list_length(data->layers) == 0) {
        /* No valid layers found */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_FLIP_VERTICAL),
                      COMMAND_LAYER_EDIT,
                      flip_command_apply,
                      flip_command_revert,
                      flip_command_destroy);

    if (!cmd) {
        /* Free snapshots */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a transpose command
 */
Command* command_create_transpose(struct ImageDocument* doc) {
    Command* cmd;
    TransposeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Create command data */
    data = (TransposeCommandData*)g_malloc(sizeof(TransposeCommandData));
    data->doc = doc;
    data->layer_snapshots = NULL;
    data->layers = NULL;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->new_width = doc->height; /* Transpose swaps width/height */
    data->new_height = doc->width;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);
            }
        }
    }

    if (!data->layers || g_list_length(data->layers) == 0) {
        /* No valid layers found */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_TRANSPOSE),
                      COMMAND_LAYER_EDIT,
                      transpose_command_apply,
                      transpose_command_revert,
                      transpose_command_destroy);

    if (!cmd) {
        /* Free snapshots */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a fit canvas to active layer command
 */
Command* command_create_fit_active_layer(guint old_width, guint old_height,
                                         guint new_width, guint new_height,
                                         gdouble old_resolution, gdouble new_resolution,
                                         gint offset_x, gint offset_y,
                                         struct ImageDocument* doc) {
    Command* cmd;
    CanvasResizeCommandData* data;
    GList* iter;
    ImageLayer* layer;
    LayerOffsetPair* pair;

    if (!doc) {
        return NULL;
    }

    /* Create command data (same structure as canvas resize) */
    data = (CanvasResizeCommandData*)g_malloc(sizeof(CanvasResizeCommandData));
    data->old_width = old_width;
    data->old_height = old_height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->old_resolution = old_resolution;
    data->new_resolution = new_resolution;
    data->offset_x = offset_x;
    data->offset_y = offset_y;
    data->layer_offsets = NULL;

    /* Store old offsets for all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (layer) {
            pair = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
            pair->layer = layer;
            pair->old_offset_x = layer->offset_x;
            pair->old_offset_y = layer->offset_y;
            data->layer_offsets = g_list_append(data->layer_offsets, pair);
        }
    }

    /* Create command with specific name but reuse canvas resize callbacks */
    cmd = command_new(command_get_name_string(CMD_NAME_FIT_ACTIVE_LAYER),
                      COMMAND_CANVAS_RESIZE,
                      canvas_resize_command_apply,
                      canvas_resize_command_revert,
                      canvas_resize_command_destroy);

    if (!cmd) {
        /* Free layer offset pairs */
        if (data->layer_offsets) {
            for (iter = data->layer_offsets; iter; iter = iter->next) {
                pair = (LayerOffsetPair*)iter->data;
                if (pair) {
                    g_free(pair);
                }
            }
            g_list_free(data->layer_offsets);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a fit canvas to all layers command
 */
Command* command_create_fit_all_layers(guint old_width, guint old_height,
                                       guint new_width, guint new_height,
                                       gdouble old_resolution, gdouble new_resolution,
                                       gint offset_x, gint offset_y,
                                       struct ImageDocument* doc) {
    Command* cmd;
    CanvasResizeCommandData* data;
    GList* iter;
    ImageLayer* layer;
    LayerOffsetPair* pair;

    if (!doc) {
        return NULL;
    }

    /* Create command data (same structure as canvas resize) */
    data = (CanvasResizeCommandData*)g_malloc(sizeof(CanvasResizeCommandData));
    data->old_width = old_width;
    data->old_height = old_height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->old_resolution = old_resolution;
    data->new_resolution = new_resolution;
    data->offset_x = offset_x;
    data->offset_y = offset_y;
    data->layer_offsets = NULL;

    /* Store old offsets for all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (layer) {
            pair = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
            pair->layer = layer;
            pair->old_offset_x = layer->offset_x;
            pair->old_offset_y = layer->offset_y;
            data->layer_offsets = g_list_append(data->layer_offsets, pair);
        }
    }

    /* Create command with specific name but reuse canvas resize callbacks */
    cmd = command_new(command_get_name_string(CMD_NAME_FIT_ALL_LAYERS),
                      COMMAND_CANVAS_RESIZE,
                      canvas_resize_command_apply,
                      canvas_resize_command_revert,
                      canvas_resize_command_destroy);

    if (!cmd) {
        /* Free layer offset pairs */
        if (data->layer_offsets) {
            for (iter = data->layer_offsets; iter; iter = iter->next) {
                pair = (LayerOffsetPair*)iter->data;
                if (pair) {
                    g_free(pair);
                }
            }
            g_list_free(data->layer_offsets);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Helper function to composite layers into a target surface
 */
static void composite_layers_to_surface(cairo_surface_t* target, struct ImageDocument* doc, gboolean only_visible) {
    cairo_t* cr;
    GList* iter;
    struct ImageLayer* layer;
    gboolean is_first_layer = TRUE;

    if (!target || !doc) {
        return;
    }

    cr = cairo_create(target);
    if (!cr) {
        return;
    }

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Composite each layer */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;

        if (!layer || !layer->surface) {
            continue;
        }

        /* Skip invisible layers if only merging visible */
        if (only_visible && (!layer->visible || layer->opacity <= 0.0)) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw layer with offset */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op;
        if (is_first_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(target);
}

/**
 * Helper function to composite layers into a target surface without clearing it first
 * Used for flatten operation where we want to composite into an existing layer
 */
static void composite_layers_onto_surface(cairo_surface_t* target, struct ImageDocument* doc, struct ImageLayer* skip_layer) {
    cairo_t* cr;
    GList* iter;
    struct ImageLayer* layer;

    if (!target || !doc) {
        return;
    }

    cr = cairo_create(target);
    if (!cr) {
        return;
    }

    /* Don't clear - composite on top of existing content */

    /* Composite each layer */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;

        if (!layer || !layer->surface || layer == skip_layer) {
            continue;
        }

        /* Skip invisible layers */
        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw layer with offset */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op = blend_mode_to_cairo_operator(layer->blend_mode);
        cairo_set_operator(cr, op);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(target);
}

/**
 * Merge visible command apply callback (merge visible layers)
 */
static void merge_visible_command_apply(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    if (!data->merged_layer || !data->merged_layer->surface) {
        return;
    }

    /* Composite visible layers into merged layer */
    composite_layers_to_surface(data->merged_layer->surface, doc, TRUE);

    /* Delete all visible layers except the merged layer */
    iter = doc->layers;
    while (iter) {
        layer = (struct ImageLayer*)iter->data;
        GList* next = iter->next;

        if (layer && layer != data->merged_layer && layer->visible && layer->opacity > 0.0) {
            /* Remove from document */
            doc->layers = g_list_remove(doc->layers, layer);

            /* Update selected layer if needed */
            if (doc->selected_layer == layer) {
                doc->selected_layer = data->merged_layer;
            }

            /* Free the layer */
            layer_free(layer);
        }

        iter = next;
    }

    /* Add merged layer to document at the position where first visible layer was */
    if (!g_list_find(doc->layers, data->merged_layer)) {
        /* Recalculate position after deletions - count non-visible layers before original position */
        gint insert_pos = 0;
        GList* current = doc->layers;
        gint pos = 0;
        while (current && pos < data->merged_layer_position) {
            layer = (struct ImageLayer*)current->data;
            if (layer && (!layer->visible || layer->opacity <= 0.0)) {
                insert_pos++;
            }
            current = current->next;
            pos++;
        }

        GList* insert_point = g_list_nth(doc->layers, insert_pos);
        if (insert_point) {
            doc->layers = g_list_insert_before(doc->layers, insert_point, data->merged_layer);
        } else {
            doc->layers = g_list_append(doc->layers, data->merged_layer);
        }
        doc->selected_layer = data->merged_layer;
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Merge visible command revert callback (restore deleted layers)
 */
static void merge_visible_command_revert(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* info_iter;
    MergedLayerInfo* info;
    struct ImageLayer* restored_layer;
    cairo_t* cr;
    GList* insert_point;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    /* Remove merged layer from document */
    if (data->merged_layer && g_list_find(doc->layers, data->merged_layer)) {
        doc->layers = g_list_remove(doc->layers, data->merged_layer);

        /* Update selected layer if needed */
        if (doc->selected_layer == data->merged_layer) {
            if (doc->layers && doc->layers->data) {
                doc->selected_layer = (struct ImageLayer*)doc->layers->data;
            } else {
                doc->selected_layer = NULL;
            }
        }
    }

    /* Restore deleted layers from stored metadata and snapshots */
    if (data->layer_infos) {
        /* Sort by position (descending) to insert from back to front */
        GList* sorted_infos = NULL;
        for (info_iter = data->layer_infos; info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (info) {
                /* Insert in position order (highest position first) */
                GList* insert = sorted_infos;
                GList* prev = NULL;
                while (insert) {
                    MergedLayerInfo* existing = (MergedLayerInfo*)insert->data;
                    if (existing && existing->position < info->position) {
                        break;
                    }
                    prev = insert;
                    insert = insert->next;
                }
                if (prev) {
                    sorted_infos = g_list_insert_before(sorted_infos, insert, info);
                } else {
                    sorted_infos = g_list_prepend(sorted_infos, info);
                }
            }
        }

        /* Restore layers in reverse position order (from highest to lowest)
         * This way, each insertion doesn't affect subsequent insertions */
        for (info_iter = sorted_infos; info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (!info || !info->snapshot) {
                continue;
            }

            /* Recreate layer */
            restored_layer = layer_new(info->layer_name, info->width, info->height, TRUE,
                                       LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
            if (!restored_layer) {
                continue;
            }

            /* Restore content from snapshot */
            cr = cairo_create(restored_layer->surface);
            cairo_set_source_surface(cr, info->snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Flush the restored layer surface */
            if (restored_layer->surface) {
                cairo_surface_flush(restored_layer->surface);
            }

            /* Restore properties */
            restored_layer->opacity = info->opacity;
            restored_layer->blend_mode = info->blend_mode;
            restored_layer->offset_x = info->offset_x;
            restored_layer->offset_y = info->offset_y;
            restored_layer->visible = info->visible;

            /* Invalidate layer cache */
            layer_invalidate_cache(restored_layer);

            /* Calculate insertion position.
             * After removing merged layer, layers originally after merged_layer_position
             * have shifted down by 1. Since we're inserting in descending order,
             * we can use the original position directly (adjusted for the removed merged layer). */
            gint insert_pos = info->position;
            if (info->position > data->merged_layer_position) {
                /* This layer was originally after the merged layer position,
                 * so after removing merged layer, it should be at position-1 */
                insert_pos = info->position - 1;
            }
            /* Layers originally at or before merged_layer_position use their original position */

            /* Insert at calculated position */
            insert_point = g_list_nth(doc->layers, insert_pos);
            if (insert_point) {
                doc->layers = g_list_insert_before(doc->layers, insert_point, restored_layer);
            } else {
                doc->layers = g_list_append(doc->layers, restored_layer);
            }

            /* Update info with restored layer pointer */
            info->layer = restored_layer;
        }

        g_list_free(sorted_infos);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flatten command apply callback (merge all layers into bottom)
 */
static void flatten_command_apply(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    struct ImageLayer* bottom_layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    if (!data->merged_layer || !data->merged_layer->surface) {
        return;
    }

    bottom_layer = data->merged_layer;

    /* Composite all layers onto bottom layer (don't clear bottom layer first) */
    composite_layers_onto_surface(bottom_layer->surface, doc, bottom_layer);

    /* Invalidate bottom layer cache so thumbnail updates */
    layer_invalidate_cache(bottom_layer);

    /* Delete all layers except the bottom layer */
    iter = doc->layers;
    while (iter) {
        layer = (struct ImageLayer*)iter->data;
        GList* next = iter->next;

        if (layer && layer != bottom_layer) {
            /* Remove from document */
            doc->layers = g_list_remove(doc->layers, layer);

            /* Update selected layer if needed */
            if (doc->selected_layer == layer) {
                doc->selected_layer = bottom_layer;
            }

            /* Free the layer */
            layer_free(layer);
        }

        iter = next;
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flatten command revert callback (restore deleted layers)
 */
static void flatten_command_revert(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* info_iter;
    MergedLayerInfo* info;
    struct ImageLayer* restored_layer;
    cairo_t* cr;
    GList* insert_point;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    /* Restore bottom layer from first layer info (if it exists) */
    if (data->merged_layer && data->layer_infos) {
        info = (MergedLayerInfo*)g_list_nth_data(data->layer_infos, 0);
        if (info && info->snapshot && data->merged_layer->surface) {
            cr = cairo_create(data->merged_layer->surface);
            cairo_set_source_surface(cr, info->snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Restore bottom layer properties */
            if (info) {
                data->merged_layer->opacity = info->opacity;
                data->merged_layer->blend_mode = info->blend_mode;
                data->merged_layer->offset_x = info->offset_x;
                data->merged_layer->offset_y = info->offset_y;
                data->merged_layer->visible = info->visible;
            }

            layer_invalidate_cache(data->merged_layer);
        }
    }

    /* Restore deleted layers from layer infos (skip first which is bottom layer) */
    if (data->layer_infos) {
        /* Sort by position (descending) to insert from back to front, so positions don't shift */
        GList* sorted_infos = NULL;
        for (info_iter = g_list_next(data->layer_infos); info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (info) {
                /* Insert in position order (highest position first) */
                GList* insert = sorted_infos;
                GList* prev = NULL;
                while (insert) {
                    MergedLayerInfo* existing = (MergedLayerInfo*)insert->data;
                    if (existing && existing->position < info->position) {
                        break;
                    }
                    prev = insert;
                    insert = insert->next;
                }
                if (prev) {
                    sorted_infos = g_list_insert_before(sorted_infos, insert, info);
                } else {
                    sorted_infos = g_list_prepend(sorted_infos, info);
                }
            }
        }

        /* Restore layers in position order */
        for (info_iter = sorted_infos; info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (!info || !info->snapshot) {
                continue;
            }

            /* Recreate layer */
            restored_layer = layer_new(info->layer_name, info->width, info->height, TRUE,
                                       LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
            if (!restored_layer) {
                continue;
            }

            /* Restore content from snapshot */
            cr = cairo_create(restored_layer->surface);
            cairo_set_source_surface(cr, info->snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Flush the restored layer surface */
            if (restored_layer->surface) {
                cairo_surface_flush(restored_layer->surface);
            }

            /* Restore properties */
            restored_layer->opacity = info->opacity;
            restored_layer->blend_mode = info->blend_mode;
            restored_layer->offset_x = info->offset_x;
            restored_layer->offset_y = info->offset_y;
            restored_layer->visible = info->visible;

            /* Invalidate layer cache */
            layer_invalidate_cache(restored_layer);

            /* Insert at original position */
            insert_point = g_list_nth(doc->layers, info->position);
            if (insert_point) {
                doc->layers = g_list_insert_before(doc->layers, insert_point, restored_layer);
            } else {
                doc->layers = g_list_append(doc->layers, restored_layer);
            }

            /* Update info with restored layer pointer */
            info->layer = restored_layer;
        }

        g_list_free(sorted_infos);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Merge command destroy callback
 */
static void merge_command_destroy(Command* cmd) {
    MergeCommandData* data;
    GList* iter;
    MergedLayerInfo* info;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    /* Free all layer info structures */
    if (data->layer_infos) {
        for (iter = data->layer_infos; iter; iter = iter->next) {
            info = (MergedLayerInfo*)iter->data;
            if (info) {
                if (info->snapshot) {
                    cairo_surface_destroy(info->snapshot);
                }
                if (info->layer_name) {
                    g_free(info->layer_name);
                }
                /* Free layer if it's not in the document
                 * IMPORTANT: If doc->layers is NULL, the document is being freed and
                 * document_free() will handle freeing all layers. Don't free here to avoid double-free. */
                if (info->layer) {
                    if (!data->doc) {
                        /* Document pointer is NULL - document was already freed, free the layer */
                        layer_free(info->layer);
                    } else if (!data->doc->layers) {
                        /* Document is being freed (layers list is NULL) - DON'T free the layer here.
                         * document_free() will free all layers. Freeing here would cause double-free. */
                    } else {
                        /* Document still exists - only free if layer is not in the list */
                        GList* found = g_list_find(data->doc->layers, info->layer);
                        if (!found) {
                            layer_free(info->layer);
                        }
                    }
                }
                g_free(info);
            }
        }
        g_list_free(data->layer_infos);
    }

    /* Free merged layer if it's not in the document
     * IMPORTANT: If doc->layers is NULL, the document is being freed and
     * document_free() will handle freeing all layers. Don't free here to avoid double-free. */
    if (data->merged_layer) {
        if (!data->doc) {
            /* Document pointer is NULL - document was already freed, free the layer */
            layer_free(data->merged_layer);
        } else if (!data->doc->layers) {
            /* Document is being freed (layers list is NULL) - DON'T free the layer here.
             * document_free() will free all layers. Freeing here would cause double-free. */
        } else {
            /* Document still exists - only free if layer is not in the list */
            GList* found = g_list_find(data->doc->layers, data->merged_layer);
            if (!found) {
                layer_free(data->merged_layer);
            }
        }
    }

    g_free(data);
}

/**
 * Create a merge visible layers command
 */
Command* command_create_merge_visible(struct ImageDocument* doc) {
    Command* cmd;
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    struct ImageLayer* merged_layer;
    cairo_surface_t* snapshot;
    gint visible_count = 0;
    gint merged_position = 0;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Count visible layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->visible && layer->opacity > 0.0) {
            visible_count++;
        }
    }

    if (visible_count == 0) {
        g_warning("No visible layers to merge");
        return NULL;
    }

    if (visible_count == 1) {
        g_warning("Only one visible layer, nothing to merge");
        return NULL;
    }

    /* Create command data */
    data = (MergeCommandData*)g_malloc(sizeof(MergeCommandData));
    data->doc = doc;
    data->merged_layer = NULL;
    data->layer_infos = NULL;
    data->merged_layer_position = 0;

    /* Create layer info structures for visible layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->visible && layer->opacity > 0.0) {
            gint position = g_list_position(doc->layers, iter);
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                MergedLayerInfo* info = (MergedLayerInfo*)g_malloc(sizeof(MergedLayerInfo));
                info->layer = layer;
                info->position = position;
                info->layer_name = g_strdup(layer->name);
                info->width = layer->width;
                info->height = layer->height;
                info->snapshot = snapshot;
                info->opacity = layer->opacity;
                info->blend_mode = layer->blend_mode;
                info->offset_x = layer->offset_x;
                info->offset_y = layer->offset_y;
                info->visible = layer->visible;

                data->layer_infos = g_list_append(data->layer_infos, info);
            }
            if (merged_position == 0) {
                merged_position = position;
            }
        }
    }

    if (!data->layer_infos || g_list_length(data->layer_infos) == 0) {
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    data->merged_layer_position = merged_position;

    /* Create new merged layer */
    merged_layer = layer_new("Merged layers", doc->width, doc->height, TRUE,
                             LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!merged_layer) {
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    data->merged_layer = merged_layer;

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_MERGE_VISIBLE),
                      COMMAND_LAYER_EDIT,
                      merge_visible_command_apply,
                      merge_visible_command_revert,
                      merge_command_destroy);

    if (!cmd) {
        /* Free merged layer */
        layer_free(merged_layer);
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Selection command apply callback (restore "after" state)
 */
static void selection_command_apply(Command* cmd, struct ImageDocument* doc) {
    SelectionCommandData* data;
    extern gboolean selection_undo_apply_after(SelectionMask*, SelectionUndoDelta*);

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (SelectionCommandData*)cmd->user_data;

    if (!data->mask || !data->delta) {
        return;
    }

    /* Apply "after" state for redo (includes feathering parameter restoration) */
    selection_undo_apply_after(data->mask, data->delta);

    /* Destroy cached surface to force rebuild from fresh mask data
       (feathered_preview was already regenerated by selection_undo_apply_after) */
    if (data->mask->surface) {
        cairo_surface_destroy(data->mask->surface);
        data->mask->surface = NULL;
    }
    data->mask->dirty = TRUE;

    /* Mark document dirty and trigger selection redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Selection command revert callback (restore "before" state)
 */
static void selection_command_revert(Command* cmd, struct ImageDocument* doc) {
    SelectionCommandData* data;
    extern gboolean selection_undo_apply_before(SelectionMask*, SelectionUndoDelta*);

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (SelectionCommandData*)cmd->user_data;

    if (!data->mask || !data->delta) {
        return;
    }

    /* Apply "before" state for undo (includes feathering parameter restoration) */
    selection_undo_apply_before(data->mask, data->delta);

    /* Destroy cached surface to force complete rebuild
       (feathered_preview was already regenerated by selection_undo_apply_before) */
    if (data->mask->surface) {
        cairo_surface_destroy(data->mask->surface);
        data->mask->surface = NULL;
    }
    data->mask->dirty = TRUE;

    /* Mark document dirty and trigger selection redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Selection command destroy callback
 */
static void selection_command_destroy(Command* cmd) {
    SelectionCommandData* data;
    extern void selection_undo_delta_free(SelectionUndoDelta*);

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (SelectionCommandData*)cmd->user_data;

    if (data->delta) {
        selection_undo_delta_free(data->delta);
    }

    g_free(data);
}

/**
 * Create a selection command
 * Wraps a SelectionUndoDelta for undo/redo
 */
Command* command_create_selection(struct SelectionMask* mask,
                                  SelectionUndoDelta* delta,
                                  struct ImageDocument* doc,
                                  const gchar* name) {
    Command* cmd;
    SelectionCommandData* data;
    const gchar* cmd_name;

    if (!mask || !delta || !doc) {
        return NULL;
    }

    cmd_name = name ? name : "Selection Operation";

    /* Create command data */
    data = (SelectionCommandData*)g_malloc(sizeof(SelectionCommandData));
    data->mask = mask;
    data->delta = delta; /* ownership transferred */
    data->doc = doc;

    /* Create command */
    cmd = command_new(cmd_name,
                      COMMAND_SELECTION,
                      selection_command_apply,
                      selection_command_revert,
                      selection_command_destroy);

    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    cmd->document = doc;

    return cmd;
}

/**
 * Create a flatten image command
 */
Command* command_create_flatten(struct ImageDocument* doc) {
    Command* cmd;
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    struct ImageLayer* bottom_layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    if (g_list_length(doc->layers) == 1) {
        g_warning("Only one layer, nothing to flatten");
        return NULL;
    }

    /* Get bottom layer */
    bottom_layer = (struct ImageLayer*)g_list_nth_data(doc->layers, 0);
    if (!bottom_layer) {
        return NULL;
    }

    /* Create command data */
    data = (MergeCommandData*)g_malloc(sizeof(MergeCommandData));
    data->doc = doc;
    data->merged_layer = bottom_layer;
    data->layer_infos = NULL;
    data->merged_layer_position = 0;

    /* Create layer info for bottom layer (first in list) */
    snapshot = cairo_surface_snapshot(bottom_layer->surface);
    if (snapshot) {
        MergedLayerInfo* info = (MergedLayerInfo*)g_malloc(sizeof(MergedLayerInfo));
        info->layer = bottom_layer;
        info->position = 0;
        info->layer_name = g_strdup(bottom_layer->name);
        info->width = bottom_layer->width;
        info->height = bottom_layer->height;
        info->snapshot = snapshot;
        info->opacity = bottom_layer->opacity;
        info->blend_mode = bottom_layer->blend_mode;
        info->offset_x = bottom_layer->offset_x;
        info->offset_y = bottom_layer->offset_y;
        info->visible = bottom_layer->visible;

        data->layer_infos = g_list_append(data->layer_infos, info);
    }

    /* Create layer info structures for all other layers */
    for (iter = g_list_next(doc->layers); iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer) {
            gint position = g_list_position(doc->layers, iter);
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                MergedLayerInfo* info = (MergedLayerInfo*)g_malloc(sizeof(MergedLayerInfo));
                info->layer = layer;
                info->position = position;
                info->layer_name = g_strdup(layer->name);
                info->width = layer->width;
                info->height = layer->height;
                info->snapshot = snapshot;
                info->opacity = layer->opacity;
                info->blend_mode = layer->blend_mode;
                info->offset_x = layer->offset_x;
                info->offset_y = layer->offset_y;
                info->visible = layer->visible;

                data->layer_infos = g_list_append(data->layer_infos, info);
            }
        }
    }

    if (!data->layer_infos || g_list_length(data->layer_infos) < 2) {
        /* Need at least bottom layer + one other layer */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_FLATTEN),
                      COMMAND_LAYER_EDIT,
                      flatten_command_apply,
                      flatten_command_revert,
                      merge_command_destroy);

    if (!cmd) {
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}