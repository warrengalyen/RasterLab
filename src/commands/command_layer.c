/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "commands/command_layer.h"
#include "command.h"
#include "document.h"
#include "render/layer.h"

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
 * Create a layer move to top command
 */
Command* command_create_layer_move_to_top(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerMoveUpCommandData* data;
    gint old_pos;
    GList* iter;
    guint count;

    if (!doc || !layer) {
        return NULL;
    }

    iter = g_list_find(doc->layers, layer);
    if (!iter || !iter->next) {
        return NULL; /* Already at top */
    }

    count = g_list_length(doc->layers);
    old_pos = g_list_position(doc->layers, iter);

    data = (LayerMoveUpCommandData*)g_malloc(sizeof(LayerMoveUpCommandData));
    data->doc = doc;
    data->layer = layer;
    data->old_position = old_pos;
    data->new_position = (gint)count - 1;

    cmd = command_new(command_get_name_string(CMD_NAME_MOVE_LAYER_TO_TOP),
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
 * Create a layer move to bottom command
 */
Command* command_create_layer_move_to_bottom(struct ImageDocument* doc, struct ImageLayer* layer) {
    Command* cmd;
    LayerMoveDownCommandData* data;
    gint old_pos;
    GList* iter;

    if (!doc || !layer) {
        return NULL;
    }

    iter = g_list_find(doc->layers, layer);
    if (!iter || !iter->prev) {
        return NULL; /* Already at bottom */
    }

    old_pos = g_list_position(doc->layers, iter);

    data = (LayerMoveDownCommandData*)g_malloc(sizeof(LayerMoveDownCommandData));
    data->doc = doc;
    data->layer = layer;
    data->old_position = old_pos;
    data->new_position = 0;

    cmd = command_new(command_get_name_string(CMD_NAME_MOVE_LAYER_TO_BOTTOM),
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
 * Layer merge command apply callback (composite source onto target, remove source)
 */
static void layer_merge_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerMergeCommandData* data;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerMergeCommandData*)cmd->user_data;

    if (!data->target_layer || !data->target_layer->surface || !data->source_layer) {
        return;
    }

    /* Composite source layer onto target */
    composite_layer_onto_layer(data->target_layer, data->source_layer);
    layer_invalidate_cache(data->target_layer);

    /* Remove source layer from document */
    doc->layers = g_list_remove(doc->layers, data->source_layer);
    if (doc->selected_layer == data->source_layer) {
        doc->selected_layer = data->target_layer;
    }
    layer_free(data->source_layer);
    data->source_layer = NULL;

    document_invalidate_composite(doc);
}

/**
 * Layer merge command revert callback (restore target and source layers)
 */
static void layer_merge_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerMergeCommandData* data;
    struct ImageLayer* restored_layer;
    cairo_t* cr;
    GList* insert_point;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (LayerMergeCommandData*)cmd->user_data;

    if (!data->target_snapshot || !data->target_layer || !data->removed_snapshot) {
        return;
    }

    /* Restore target layer from snapshot */
    cr = cairo_create(data->target_layer->surface);
    cairo_set_source_surface(cr, data->target_snapshot, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(data->target_layer->surface);
    layer_invalidate_cache(data->target_layer);

    /* Recreate and reinsert the removed layer */
    restored_layer = layer_new(data->removed_layer_name, data->removed_width, data->removed_height,
                               TRUE, LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT,
                               NULL, doc);
    if (restored_layer) {
        cr = cairo_create(restored_layer->surface);
        cairo_set_source_surface(cr, data->removed_snapshot, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(restored_layer->surface);

        restored_layer->opacity = data->removed_opacity;
        restored_layer->blend_mode = data->removed_blend_mode;
        restored_layer->offset_x = data->removed_offset_x;
        restored_layer->offset_y = data->removed_offset_y;
        restored_layer->visible = data->removed_visible;
        layer_invalidate_cache(restored_layer);

        insert_point = g_list_nth(doc->layers, data->removed_position);
        if (insert_point) {
            doc->layers = g_list_insert_before(doc->layers, insert_point, restored_layer);
        } else {
            doc->layers = g_list_append(doc->layers, restored_layer);
        }
        doc->selected_layer = restored_layer;
        data->source_layer = restored_layer;
    }

    document_invalidate_composite(doc);
}

/**
 * Layer merge command destroy callback
 */
static void layer_merge_command_destroy(Command* cmd) {
    LayerMergeCommandData* data;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (LayerMergeCommandData*)cmd->user_data;

    if (data->target_snapshot) {
        cairo_surface_destroy(data->target_snapshot);
    }
    if (data->removed_snapshot) {
        cairo_surface_destroy(data->removed_snapshot);
    }
    if (data->removed_layer_name) {
        g_free(data->removed_layer_name);
    }
    if (data->source_layer) {
        if (!data->doc || !data->doc->layers || !g_list_find(data->doc->layers, data->source_layer)) {
            layer_free(data->source_layer);
        }
    }
    g_free(data);
}

/**
 * Create a layer merge down command (merge selected into layer below)
 */
Command* command_create_layer_merge_down(struct ImageDocument* doc, struct ImageLayer* selected_layer) {
    Command* cmd;
    LayerMergeCommandData* data;
    GList* iter;

    if (!doc || !selected_layer) {
        return NULL;
    }

    iter = g_list_find(doc->layers, selected_layer);
    if (!iter || !iter->prev) {
        return NULL; /* No layer below to merge into */
    }

    data = (LayerMergeCommandData*)g_malloc(sizeof(LayerMergeCommandData));
    data->doc = doc;
    data->target_layer = (struct ImageLayer*)iter->prev->data;
    data->source_layer = selected_layer;
    data->target_snapshot = cairo_surface_snapshot(data->target_layer->surface);
    data->removed_position = get_layer_position(doc, selected_layer);
    data->removed_layer_name = g_strdup(selected_layer->name);
    data->removed_width = selected_layer->width;
    data->removed_height = selected_layer->height;
    data->removed_snapshot = cairo_surface_snapshot(selected_layer->surface);
    data->removed_opacity = selected_layer->opacity;
    data->removed_blend_mode = selected_layer->blend_mode;
    data->removed_offset_x = selected_layer->offset_x;
    data->removed_offset_y = selected_layer->offset_y;
    data->removed_visible = selected_layer->visible;

    if (!data->target_snapshot || !data->removed_snapshot) {
        if (data->target_snapshot) cairo_surface_destroy(data->target_snapshot);
        if (data->removed_snapshot) cairo_surface_destroy(data->removed_snapshot);
        g_free(data->removed_layer_name);
        g_free(data);
        return NULL;
    }

    cmd = command_new(command_get_name_string(CMD_NAME_MERGE_LAYER_DOWN),
                      COMMAND_LAYER_EDIT,
                      layer_merge_command_apply,
                      layer_merge_command_revert,
                      layer_merge_command_destroy);
    if (!cmd) {
        cairo_surface_destroy(data->target_snapshot);
        cairo_surface_destroy(data->removed_snapshot);
        g_free(data->removed_layer_name);
        g_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

/**
 * Create a layer merge up command (merge selected into layer above)
 */
Command* command_create_layer_merge_up(struct ImageDocument* doc, struct ImageLayer* selected_layer) {
    Command* cmd;
    LayerMergeCommandData* data;
    GList* iter;

    if (!doc || !selected_layer) {
        return NULL;
    }

    iter = g_list_find(doc->layers, selected_layer);
    if (!iter || !iter->next) {
        return NULL; /* No layer above to merge into */
    }

    data = (LayerMergeCommandData*)g_malloc(sizeof(LayerMergeCommandData));
    data->doc = doc;
    data->target_layer = (struct ImageLayer*)iter->next->data;
    data->source_layer = selected_layer;
    data->target_snapshot = cairo_surface_snapshot(data->target_layer->surface);
    data->removed_position = get_layer_position(doc, selected_layer);
    data->removed_layer_name = g_strdup(selected_layer->name);
    data->removed_width = selected_layer->width;
    data->removed_height = selected_layer->height;
    data->removed_snapshot = cairo_surface_snapshot(selected_layer->surface);
    data->removed_opacity = selected_layer->opacity;
    data->removed_blend_mode = selected_layer->blend_mode;
    data->removed_offset_x = selected_layer->offset_x;
    data->removed_offset_y = selected_layer->offset_y;
    data->removed_visible = selected_layer->visible;

    if (!data->target_snapshot || !data->removed_snapshot) {
        if (data->target_snapshot) cairo_surface_destroy(data->target_snapshot);
        if (data->removed_snapshot) cairo_surface_destroy(data->removed_snapshot);
        g_free(data->removed_layer_name);
        g_free(data);
        return NULL;
    }

    cmd = command_new(command_get_name_string(CMD_NAME_MERGE_LAYER_UP),
                      COMMAND_LAYER_EDIT,
                      layer_merge_command_apply,
                      layer_merge_command_revert,
                      layer_merge_command_destroy);
    if (!cmd) {
        cairo_surface_destroy(data->target_snapshot);
        cairo_surface_destroy(data->removed_snapshot);
        g_free(data->removed_layer_name);
        g_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

/**
 * Layer visibility command apply callback
 */
static void layer_visibility_command_apply(Command* cmd, struct ImageDocument* doc) {
    LayerVisibilityCommandData* data;
    GList* iter;
    LayerVisibilityState* state;

    if (!cmd || !cmd->user_data || !doc) return;
    data = (LayerVisibilityCommandData*)cmd->user_data;

    for (iter = data->states; iter; iter = iter->next) {
        state = (LayerVisibilityState*)iter->data;
        if (state && state->layer)
            state->layer->visible = state->visible_after;
    }
    document_invalidate_composite(doc);
}

/**
 * Layer visibility command revert callback
 */
static void layer_visibility_command_revert(Command* cmd, struct ImageDocument* doc) {
    LayerVisibilityCommandData* data;
    GList* iter;
    LayerVisibilityState* state;

    if (!cmd || !cmd->user_data || !doc) return;
    data = (LayerVisibilityCommandData*)cmd->user_data;

    for (iter = data->states; iter; iter = iter->next) {
        state = (LayerVisibilityState*)iter->data;
        if (state && state->layer)
            state->layer->visible = state->visible_before;
    }
    document_invalidate_composite(doc);
}

/**
 * Layer visibility command destroy callback
 */
static void layer_visibility_command_destroy(Command* cmd) {
    LayerVisibilityCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data) return;
    data = (LayerVisibilityCommandData*)cmd->user_data;
    if (data->states) {
        for (iter = data->states; iter; iter = iter->next)
            g_free(iter->data);
        g_list_free(data->states);
    }
    g_free(data);
}

static Command* layer_visibility_command_create(struct ImageDocument* doc,
                                                GList* states,
                                                const gchar* name) {
    Command* cmd;
    LayerVisibilityCommandData* data;

    if (!doc || !states || !name) return NULL;

    data = (LayerVisibilityCommandData*)g_malloc(sizeof(LayerVisibilityCommandData));
    data->doc = doc;
    data->states = states;

    cmd = command_new(name, COMMAND_LAYER_EDIT,
                      layer_visibility_command_apply,
                      layer_visibility_command_revert,
                      layer_visibility_command_destroy);
    if (!cmd) {
        for (GList* i = states; i; i = i->next) g_free(i->data);
        g_list_free(states);
        g_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

Command* command_create_layer_visibility_toggle(struct ImageDocument* doc, struct ImageLayer* layer) {
    LayerVisibilityState* state;
    GList* states;

    if (!doc || !layer) return NULL;

    state = (LayerVisibilityState*)g_malloc(sizeof(LayerVisibilityState));
    state->layer = layer;
    state->visible_before = layer->visible;
    state->visible_after = !layer->visible;
    states = g_list_append(NULL, state);
    return layer_visibility_command_create(doc, states, "Toggle layer visibility");
}

Command* command_create_layer_visibility_show_only(struct ImageDocument* doc, struct ImageLayer* layer) {
    GList* states = NULL;
    guint count = document_get_layer_count(doc);

    if (!doc || !layer) return NULL;

    for (guint i = 0; i < count; i++) {
        ImageLayer* l = document_get_layer(doc, i);
        if (!l) continue;
        LayerVisibilityState* state = (LayerVisibilityState*)g_malloc(sizeof(LayerVisibilityState));
        state->layer = l;
        state->visible_before = l->visible;
        state->visible_after = (l == layer);
        states = g_list_append(states, state);
    }
    return layer_visibility_command_create(doc, states, "Show only this layer");
}

Command* command_create_layer_visibility_hide_only(struct ImageDocument* doc, struct ImageLayer* layer) {
    GList* states = NULL;
    guint count = document_get_layer_count(doc);

    if (!doc || !layer) return NULL;

    for (guint i = 0; i < count; i++) {
        ImageLayer* l = document_get_layer(doc, i);
        if (!l) continue;
        LayerVisibilityState* state = (LayerVisibilityState*)g_malloc(sizeof(LayerVisibilityState));
        state->layer = l;
        state->visible_before = l->visible;
        state->visible_after = (l != layer);  /* Hide selected, show all others */
        states = g_list_append(states, state);
    }
    return layer_visibility_command_create(doc, states, "Hide only this layer");
}

Command* command_create_layer_visibility_show_all(struct ImageDocument* doc) {
    GList* states = NULL;
    guint count = document_get_layer_count(doc);

    if (!doc || count == 0) return NULL;

    for (guint i = 0; i < count; i++) {
        ImageLayer* l = document_get_layer(doc, i);
        if (!l) continue;
        LayerVisibilityState* state = (LayerVisibilityState*)g_malloc(sizeof(LayerVisibilityState));
        state->layer = l;
        state->visible_before = l->visible;
        state->visible_after = TRUE;
        states = g_list_append(states, state);
    }
    return layer_visibility_command_create(doc, states, "Show all layers");
}

Command* command_create_layer_visibility_hide_all(struct ImageDocument* doc) {
    GList* states = NULL;
    guint count = document_get_layer_count(doc);

    if (!doc || count == 0) return NULL;

    for (guint i = 0; i < count; i++) {
        ImageLayer* l = document_get_layer(doc, i);
        if (!l) continue;
        LayerVisibilityState* state = (LayerVisibilityState*)g_malloc(sizeof(LayerVisibilityState));
        state->layer = l;
        state->visible_before = l->visible;
        state->visible_after = FALSE;
        states = g_list_append(states, state);
    }
    return layer_visibility_command_create(doc, states, "Hide all layers");
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