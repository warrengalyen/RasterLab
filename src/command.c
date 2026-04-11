/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "command.h"
#include "document.h"
#include "filters.h"
#include "ocular.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/thumbnail_worker.h"
#include "render/tile.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "selection/selection_undo.h"
#include "undo/undo_disk.h"
#include <math.h>
#include <stdbool.h>

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
    cmd->subtitle = NULL;
    cmd->type = type;
    cmd->apply = apply;
    cmd->revert = revert;
    cmd->destroy = destroy;
    cmd->user_data = NULL;
    cmd->document = NULL;
    cmd->thumbnail = NULL;
    cmd->thumbnail_task = NULL;

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

    /* Cancel any in-flight thumbnail generation.
     * The worker checks task->cmd under the mutex; NULLing it here prevents
     * the worker from writing to this (soon-to-be-freed) command. */
    if (cmd->thumbnail_task) {
        g_mutex_lock(&cmd->thumbnail_task->mutex);
        cmd->thumbnail_task->cmd = NULL;
        g_mutex_unlock(&cmd->thumbnail_task->mutex);
        cmd->thumbnail_task = NULL;
    }

    if (cmd->thumbnail) {
        cairo_surface_destroy(cmd->thumbnail);
        cmd->thumbnail = NULL;
    }

    g_free(cmd->name);
    g_free(cmd->subtitle);
    g_free(cmd);
}

/**
 * Set (or replace) the thumbnail on a command, taking ownership of the surface.
 */
void command_set_thumbnail(Command* cmd, cairo_surface_t* surf) {
    if (!cmd) {
        if (surf) {
            cairo_surface_destroy(surf);
        }
        return;
    }
    if (cmd->thumbnail) {
        cairo_surface_destroy(cmd->thumbnail);
    }
    cmd->thumbnail = surf; /* takes ownership */
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
        return;
    }

    /* Verify layer surface still exists */
    if (!data->layer->surface) {
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

cairo_surface_t* cairo_surface_snapshot(cairo_surface_t* source) {
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
        "Pencil",
        "Eraser",
        "Paintbucket",
        "Move Layer",
        "Add Layer",
        "Delete Layer",
        "Duplicate Layer",
        "Move Layer Up",
        "Move Layer Down",
        "Move Layer to Top",
        "Move Layer to Bottom",
        "Merge Layer Up",
        "Merge Layer Down",
        "Canvas size",
        "Resize image",
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
        "Move Selected Pixels",
        "Crop to Selection",
        "Crop tool",
        "Trim Borders"};

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
    GList* deltas_to_remove = NULL; /* Track deltas to remove from hash table */
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

            if (delta->after && delta->before) {
                /* Check if the tile actually changed by comparing before and after */
                gint before_w = cairo_image_surface_get_width(delta->before);
                gint before_h = cairo_image_surface_get_height(delta->before);
                gint after_w = cairo_image_surface_get_width(delta->after);
                gint after_h = cairo_image_surface_get_height(delta->after);

                gboolean tiles_identical = FALSE;
                if (before_w == after_w && before_h == after_h) {
                    /* Compare pixel data */
                    cairo_surface_flush(delta->before);
                    cairo_surface_flush(delta->after);
                    uint8_t* before_data = cairo_image_surface_get_data(delta->before);
                    uint8_t* after_data = cairo_image_surface_get_data(delta->after);
                    gint stride = cairo_image_surface_get_stride(delta->before);
                    size_t data_size = stride * before_h;

                    tiles_identical = (memcmp(before_data, after_data, data_size) == 0);
                }

                if (!tiles_identical) {
                    /* Tile actually changed, add to deltas array */
                    g_ptr_array_add(data->tile_deltas, delta);
                } else {
                    /* Tile didn't change (e.g., drawing outside selection), discard */
                    cairo_surface_destroy(delta->before);
                    cairo_surface_destroy(delta->after);
                    /* Create key for removal from hash table */
                    guint tile_key = (guint)((delta->tile_x << 16) | (delta->tile_y & 0xFFFF));
                    gpointer key = GUINT_TO_POINTER(tile_key);
                    deltas_to_remove = g_list_prepend(deltas_to_remove, key);
                    g_free(delta);
                }
            } else if (delta->after) {
                /* No before snapshot (shouldn't happen), but keep the delta anyway */
                g_ptr_array_add(data->tile_deltas, delta);
            } else {
                /* Failed to capture after snapshot, free this delta */
                if (delta->before) {
                    cairo_surface_destroy(delta->before);
                }
                /* Create key for removal from hash table */
                guint tile_key = (guint)((delta->tile_x << 16) | (delta->tile_y & 0xFFFF));
                gpointer key = GUINT_TO_POINTER(tile_key);
                deltas_to_remove = g_list_prepend(deltas_to_remove, key);
                g_free(delta);
            }
        }
    }
    g_list_free(all_deltas);

    /* Remove freed deltas from hash table to prevent double-free
     * Use g_hash_table_steal() instead of g_hash_table_remove() because we've
     * already manually freed the delta - we don't want the hash table's destroy
     * function to try to free it again */
    for (GList* l = deltas_to_remove; l; l = l->next) {
        gpointer key = l->data;
        g_hash_table_steal(transaction->modified_tiles, key);
    }
    g_list_free(deltas_to_remove);

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

gint get_layer_position(struct ImageDocument* doc, struct ImageLayer* layer) {
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

void composite_layers_to_surface(cairo_surface_t* target, struct ImageDocument* doc, gboolean only_visible) {
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

void composite_layers_onto_surface(cairo_surface_t* target, struct ImageDocument* doc, struct ImageLayer* skip_layer) {
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
 * Composite a single source layer onto a target layer's surface.
 * Used for merge down/up operations.
 */
void composite_layer_onto_layer(struct ImageLayer* target, struct ImageLayer* source) {
    cairo_t* cr;

    if (!target || !target->surface || !source) {
        return;
    }

    if (!layer_ensure_cache(source)) {
        return;
    }

    cr = cairo_create(target->surface);
    if (!cr) {
        return;
    }

    /* Don't clear - composite on top of existing content */
    cairo_save(cr);
    cairo_translate(cr, source->offset_x, source->offset_y);
    cairo_set_source_surface(cr, source->cache_surface, 0, 0);
    cairo_set_operator(cr, blend_mode_to_cairo_operator(source->blend_mode));
    cairo_paint_with_alpha(cr, source->opacity);
    cairo_restore(cr);

    cairo_destroy(cr);
    cairo_surface_flush(target->surface);
}
