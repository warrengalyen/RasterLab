/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "commands/command_text_layer.h"
#include "command.h"
#include "document.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include "ui.h"
#include <string.h>

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static gboolean text_cmd_layer_valid(struct ImageDocument *doc,
                                     struct ImageLayer    *layer) {
    if (!doc || !layer)
        return FALSE;
    return g_list_find(doc->layers, layer) != NULL;
}

static void text_cmd_invalidate(struct ImageDocument *doc,
                                struct ImageLayer    *layer) {
    if (layer)
        layer_invalidate_cache(layer);
    document_invalidate_composite(doc);
    if (doc->drawing_area)
        gtk_widget_queue_draw(doc->drawing_area);
    if (doc->viewport)
        gtk_widget_queue_draw(doc->viewport);
}

/* Update undo/redo menu items and window title after a stack change. */
static void text_cmd_refresh_ui(struct ImageDocument *doc) {
    if (!doc || !doc->drawing_area)
        return;
    GtkWidget *win = gtk_widget_get_toplevel(doc->drawing_area);
    if (!win)
        return;
    AppContext *ctx =
        (AppContext *)g_object_get_data(G_OBJECT(win), "app_context");
    if (ctx) {
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
    }
}

/* =========================================================================
 * TextLayerState helpers  (full-snapshot, used by the rasterize command)
 * ========================================================================= */

TextLayerState *text_layer_state_create(const void *tl_ptr) {
    if (!tl_ptr)
        return NULL;

    const TextLayer *tl    = (const TextLayer *)tl_ptr;
    TextLayerState  *state = (TextLayerState *)g_malloc0(sizeof(TextLayerState));

    state->text              = g_strdup(tl->text        ? tl->text        : "");
    state->font_family       = g_strdup(tl->font_family  ? tl->font_family : "");
    state->font_size         = tl->font_size;
    state->font_weight       = tl->font_weight;
    state->font_style        = tl->font_style;
    state->color_r           = tl->color_r;
    state->color_g           = tl->color_g;
    state->color_b           = tl->color_b;
    state->color_a           = tl->color_a;
    state->line_spacing      = tl->line_spacing;
    state->letter_spacing    = tl->letter_spacing;
    state->alignment         = tl->alignment;
    state->rotation          = tl->rotation;
    state->box_x             = tl->box_x;
    state->box_y             = tl->box_y;
    state->box_width         = tl->box_width;
    state->box_height        = tl->box_height;
    state->antialias         = tl->antialias;
    state->kerning           = tl->kerning;
    state->opentype_features = g_strdup(tl->opentype_features ? tl->opentype_features : "");

    return state;
}

void text_layer_state_restore(void *tl_ptr, const TextLayerState *state) {
    if (!tl_ptr || !state)
        return;

    TextLayer *tl = (TextLayer *)tl_ptr;

    g_free(tl->text);
    tl->text = g_strdup(state->text ? state->text : "");

    g_free(tl->font_family);
    tl->font_family = g_strdup(state->font_family ? state->font_family : "");

    tl->font_size         = state->font_size;
    tl->font_weight       = state->font_weight;
    tl->font_style        = state->font_style;
    tl->color_r           = state->color_r;
    tl->color_g           = state->color_g;
    tl->color_b           = state->color_b;
    tl->color_a           = state->color_a;
    tl->line_spacing      = state->line_spacing;
    tl->letter_spacing    = state->letter_spacing;
    tl->alignment         = state->alignment;
    tl->rotation          = state->rotation;
    tl->box_x             = state->box_x;
    tl->box_y             = state->box_y;
    tl->box_width         = state->box_width;
    tl->box_height        = state->box_height;
    tl->antialias         = state->antialias;
    tl->kerning           = state->kerning;
    g_free(tl->opentype_features);
    tl->opentype_features = g_strdup(state->opentype_features ? state->opentype_features : "");
}

void text_layer_state_free(TextLayerState *state) {
    if (!state)
        return;
    g_free(state->text);
    g_free(state->font_family);
    g_free(state->opentype_features);
    g_free(state);
}

/* =========================================================================
 * TextLayerUndoOp — records one property change (before + after values)
 * ========================================================================= */

typedef struct {
    struct ImageLayer  *layer;
    TextLayerProperty   property;
    TextLayerPropValue  before;
    TextLayerPropValue  after;
} TextLayerUndoOp;

static gboolean prop_is_string(TextLayerProperty prop) {
    return prop == TEXT_PROP_TEXT || prop == TEXT_PROP_FONT ||
           prop == TEXT_PROP_OT_FEATURES;
}

static TextLayerPropValue prop_value_dup(TextLayerProperty       prop,
                                         const TextLayerPropValue *src) {
    TextLayerPropValue dst = *src;
    if (prop_is_string(prop))
        dst.string_val = g_strdup(src->string_val ? src->string_val : "");
    return dst;
}

static void prop_value_free(TextLayerProperty prop, TextLayerPropValue *v) {
    if (prop_is_string(prop))
        g_free(v->string_val);
}

/* Allocate a new op (deep-copies string values). */
static TextLayerUndoOp *text_undo_op_new(struct ImageLayer       *layer,
                                          TextLayerProperty        prop,
                                          const TextLayerPropValue *before,
                                          const TextLayerPropValue *after) {
    TextLayerUndoOp *op = (TextLayerUndoOp *)g_malloc0(sizeof(TextLayerUndoOp));
    op->layer    = layer;
    op->property = prop;
    op->before   = prop_value_dup(prop, before);
    op->after    = prop_value_dup(prop, after);
    return op;
}

static void text_undo_op_free(gpointer data) {
    TextLayerUndoOp *op = (TextLayerUndoOp *)data;
    if (!op)
        return;
    prop_value_free(op->property, &op->before);
    prop_value_free(op->property, &op->after);
    g_free(op);
}

/* Apply one value to the matching property of a TextLayer. */
static void text_undo_op_apply_value(TextLayer              *tl,
                                     TextLayerProperty       prop,
                                     const TextLayerPropValue *v) {
    switch (prop) {
    case TEXT_PROP_TEXT:
        g_free(tl->text);
        tl->text = g_strdup(v->string_val ? v->string_val : "");
        break;
    case TEXT_PROP_FONT:
        g_free(tl->font_family);
        tl->font_family = g_strdup(v->string_val ? v->string_val : "");
        break;
    case TEXT_PROP_SIZE:
        tl->font_size = v->int_val;
        break;
    case TEXT_PROP_WEIGHT:
        tl->font_weight = (PangoWeight)v->int_val;
        break;
    case TEXT_PROP_STYLE:
        tl->font_style = (PangoStyle)v->int_val;
        break;
    case TEXT_PROP_COLOR:
        tl->color_r = v->color.r;
        tl->color_g = v->color.g;
        tl->color_b = v->color.b;
        tl->color_a = v->color.a;
        break;
    case TEXT_PROP_ALIGNMENT:
        tl->alignment = v->int_val;
        break;
    case TEXT_PROP_LINE_SPACING:
        tl->line_spacing = v->double_val;
        break;
    case TEXT_PROP_LETTER_SPACING:
        tl->letter_spacing = v->double_val;
        break;
    case TEXT_PROP_ROTATION:
        tl->rotation = v->double_val;
        break;
    case TEXT_PROP_BOX_GEOMETRY:
        tl->box_x      = v->box.x;
        tl->box_y      = v->box.y;
        tl->box_width  = v->box.w;
        tl->box_height = v->box.h;
        break;
    case TEXT_PROP_ANTIALIAS:
        tl->antialias = v->bool_val;
        break;
    case TEXT_PROP_KERNING:
        tl->kerning = v->bool_val;
        break;
    case TEXT_PROP_OT_FEATURES:
        g_free(tl->opentype_features);
        tl->opentype_features = g_strdup(v->string_val ? v->string_val : "");
        break;
    }
}

/* =========================================================================
 * TextLayerOpsCommand — a Command that holds an array of TextLayerUndoOp.
 * apply() replays forward; revert() replays in reverse.
 * ========================================================================= */

typedef struct {
    struct ImageDocument *doc;
    GPtrArray            *ops;  /* TextLayerUndoOp* (owned) */
} TextLayerOpsCmd;

static void text_ops_apply(Command *cmd, struct ImageDocument *doc) {
    if (!cmd || !cmd->user_data || !doc)
        return;
    TextLayerOpsCmd *data = (TextLayerOpsCmd *)cmd->user_data;
    if (!data->ops)
        return;

    for (guint i = 0; i < data->ops->len; i++) {
        TextLayerUndoOp *op = (TextLayerUndoOp *)g_ptr_array_index(data->ops, i);
        if (!text_cmd_layer_valid(doc, op->layer))
            continue;
        if (op->layer->layer_type != LAYER_TYPE_TEXT || !op->layer->text_data)
            continue;
        text_undo_op_apply_value((TextLayer *)op->layer->text_data,
                                 op->property, &op->after);
        layer_invalidate_cache(op->layer);
    }
    document_invalidate_composite(doc);
}

static void text_ops_revert(Command *cmd, struct ImageDocument *doc) {
    if (!cmd || !cmd->user_data || !doc)
        return;
    TextLayerOpsCmd *data = (TextLayerOpsCmd *)cmd->user_data;
    if (!data->ops)
        return;

    for (gint i = (gint)data->ops->len - 1; i >= 0; i--) {
        TextLayerUndoOp *op =
            (TextLayerUndoOp *)g_ptr_array_index(data->ops, (guint)i);
        if (!text_cmd_layer_valid(doc, op->layer))
            continue;
        if (op->layer->layer_type != LAYER_TYPE_TEXT || !op->layer->text_data)
            continue;
        text_undo_op_apply_value((TextLayer *)op->layer->text_data,
                                 op->property, &op->before);
        layer_invalidate_cache(op->layer);
    }
    document_invalidate_composite(doc);
}

static void text_ops_destroy(Command *cmd) {
    if (!cmd || !cmd->user_data)
        return;
    TextLayerOpsCmd *data = (TextLayerOpsCmd *)cmd->user_data;
    if (data->ops)
        g_ptr_array_free(data->ops, TRUE);
    g_free(data);
}

/* Build a Command from a GPtrArray of TextLayerUndoOp (takes ownership). */
static Command *text_ops_command_new(const char *name, GPtrArray *ops) {
    TextLayerOpsCmd *data =
        (TextLayerOpsCmd *)g_malloc0(sizeof(TextLayerOpsCmd));
    data->ops = ops;

    Command *cmd = command_new(name ? name : "Edit Text Layer",
                               COMMAND_LAYER_EDIT,
                               text_ops_apply,
                               text_ops_revert,
                               text_ops_destroy);
    if (!cmd) {
        g_ptr_array_free(ops, TRUE);
        g_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

/* Push a Command onto the undo stack and update UI. */
static void text_push_to_stack(struct ImageDocument *doc, Command *cmd) {
    if (!cmd)
        return;
    if (!doc || !doc->undo_stack) {
        command_free(cmd);
        return;
    }
    command_stack_push(doc->undo_stack, cmd);
    if (doc->redo_stack)
        command_stack_clear(doc->redo_stack);
    doc->modified = TRUE;
    text_cmd_refresh_ui(doc);
}

/* =========================================================================
 * Per-property descriptive name (for the undo history label)
 * ========================================================================= */

static const char *prop_name(TextLayerProperty prop) {
    switch (prop) {
    case TEXT_PROP_TEXT:           return "Edit Text";
    case TEXT_PROP_FONT:           return "Change Font";
    case TEXT_PROP_SIZE:           return "Change Font Size";
    case TEXT_PROP_WEIGHT:         return "Toggle Bold";
    case TEXT_PROP_STYLE:          return "Change Font Style";
    case TEXT_PROP_COLOR:          return "Change Text Color";
    case TEXT_PROP_ALIGNMENT:      return "Change Alignment";
    case TEXT_PROP_LINE_SPACING:   return "Change Line Spacing";
    case TEXT_PROP_LETTER_SPACING: return "Change Letter Spacing";
    case TEXT_PROP_ROTATION:       return "Rotate Text";
    case TEXT_PROP_BOX_GEOMETRY:   return "Transform Text Box";
    case TEXT_PROP_ANTIALIAS:      return "Toggle Antialias";
    case TEXT_PROP_KERNING:        return "Toggle Kerning";
    case TEXT_PROP_OT_FEATURES:    return "Change OpenType Features";
    default:                       return "Edit Text Layer";
    }
}

/* =========================================================================
 * Active transaction state (module-level)
 *
 * Only one transaction may be active at a time.  This is sufficient for a
 * single-document editor where interactive operations are strictly sequential.
 * ========================================================================= */

typedef struct {
    gboolean              active;
    char                 *name;
    GPtrArray            *ops;      /* TextLayerUndoOp* (owned) */
    struct ImageDocument *doc;
} TextUndoTransaction;

static TextUndoTransaction g_text_tx = { FALSE, NULL, NULL, NULL };

/* =========================================================================
 * Transaction API
 * ========================================================================= */

void undo_begin_transaction(struct ImageDocument *doc, const char *name) {
    /* Auto-commit any dangling transaction before starting a new one. */
    if (g_text_tx.active)
        undo_commit_transaction(g_text_tx.doc);

    g_text_tx.active = TRUE;
    g_text_tx.name   = g_strdup(name ? name : "Edit Text Layer");
    g_text_tx.ops    = g_ptr_array_new_with_free_func(text_undo_op_free);
    g_text_tx.doc    = doc;
}

void undo_commit_transaction(struct ImageDocument *doc) {
    if (!g_text_tx.active)
        return;

    gboolean has_ops = (g_text_tx.ops && g_text_tx.ops->len > 0);

    if (has_ops) {
        GPtrArray *ops   = g_text_tx.ops;
        g_text_tx.ops    = NULL;   /* transfer ownership */
        Command   *cmd   = text_ops_command_new(g_text_tx.name, ops);
        text_push_to_stack(doc, cmd);
        if (cmd)
            text_cmd_invalidate(doc, NULL);
    } else if (g_text_tx.ops) {
        g_ptr_array_free(g_text_tx.ops, TRUE);
    }

    g_free(g_text_tx.name);
    g_text_tx.name   = NULL;
    g_text_tx.ops    = NULL;
    g_text_tx.doc    = NULL;
    g_text_tx.active = FALSE;
}

void undo_cancel_transaction(void) {
    if (!g_text_tx.active)
        return;
    if (g_text_tx.ops)
        g_ptr_array_free(g_text_tx.ops, TRUE);
    g_free(g_text_tx.name);
    g_text_tx.name   = NULL;
    g_text_tx.ops    = NULL;
    g_text_tx.doc    = NULL;
    g_text_tx.active = FALSE;
}

/* =========================================================================
 * text_layer_push_property_change
 * ========================================================================= */

void text_layer_push_property_change(struct ImageDocument    *doc,
                                     struct ImageLayer       *layer,
                                     TextLayerProperty        property,
                                     const TextLayerPropValue *before,
                                     const TextLayerPropValue *after) {
    if (!doc || !layer || !before || !after)
        return;

    if (g_text_tx.active && g_text_tx.doc == doc) {
        /* Inside a transaction: try to coalesce with an existing op. */
        if (g_text_tx.ops) {
            for (guint i = 0; i < g_text_tx.ops->len; i++) {
                TextLayerUndoOp *existing =
                    (TextLayerUndoOp *)g_ptr_array_index(g_text_tx.ops, i);
                if (existing->layer == layer && existing->property == property) {
                    /* Coalesce: update only the "after" value. */
                    prop_value_free(property, &existing->after);
                    existing->after = prop_value_dup(property, after);
                    return;
                }
            }
            /* No existing op for this (layer, property) — append a new one. */
            TextLayerUndoOp *op = text_undo_op_new(layer, property, before, after);
            g_ptr_array_add(g_text_tx.ops, op);
        }
    } else {
        /* No active transaction: create and push a Command immediately. */
        GPtrArray *ops = g_ptr_array_new_with_free_func(text_undo_op_free);
        g_ptr_array_add(ops, text_undo_op_new(layer, property, before, after));

        Command *cmd = text_ops_command_new(prop_name(property), ops);
        text_push_to_stack(doc, cmd);
        if (cmd)
            text_cmd_invalidate(doc, layer);
    }
}

/* =========================================================================
 * Rasterize command
 * ========================================================================= */

typedef struct {
    struct ImageDocument *doc;
    struct ImageLayer    *layer;
    TextLayerState       *text_state;       /* before — for undo */
    cairo_surface_t      *raster_snapshot;  /* after  — for redo */
} TextRasterizeCommandData;

static void text_rasterize_apply(Command *cmd, struct ImageDocument *doc) {
    if (!cmd || !cmd->user_data || !doc)
        return;

    TextRasterizeCommandData *data = (TextRasterizeCommandData *)cmd->user_data;
    if (!data->layer || !data->raster_snapshot)
        return;
    if (!text_cmd_layer_valid(doc, data->layer))
        return;
    if (!data->layer->surface)
        return;

    cairo_t *cr = cairo_create(data->layer->surface);
    cairo_set_source_surface(cr, data->raster_snapshot, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    if (data->layer->text_data) {
        text_layer_free((TextLayer *)data->layer->text_data);
        data->layer->text_data = NULL;
    }
    data->layer->layer_type = LAYER_TYPE_RASTER;

    text_cmd_invalidate(doc, data->layer);
}

static void text_rasterize_revert(Command *cmd, struct ImageDocument *doc) {
    if (!cmd || !cmd->user_data || !doc)
        return;

    TextRasterizeCommandData *data = (TextRasterizeCommandData *)cmd->user_data;
    if (!data->layer || !data->text_state)
        return;
    if (!text_cmd_layer_valid(doc, data->layer))
        return;

    if (data->layer->text_data) {
        text_layer_free((TextLayer *)data->layer->text_data);
        data->layer->text_data = NULL;
    }

    TextLayer *tl = (TextLayer *)g_malloc0(sizeof(TextLayer));
    text_layer_state_restore(tl, data->text_state);
    data->layer->text_data  = tl;
    data->layer->layer_type = LAYER_TYPE_TEXT;

    text_cmd_invalidate(doc, data->layer);
}

static void text_rasterize_destroy(Command *cmd) {
    if (!cmd || !cmd->user_data)
        return;
    TextRasterizeCommandData *data = (TextRasterizeCommandData *)cmd->user_data;
    text_layer_state_free(data->text_state);
    if (data->raster_snapshot)
        cairo_surface_destroy(data->raster_snapshot);
    g_free(data);
}

Command *command_create_text_layer_rasterize(struct ImageDocument *doc,
                                              struct ImageLayer    *layer) {
    if (!doc || !layer)
        return NULL;
    if (layer->layer_type != LAYER_TYPE_TEXT || !layer->text_data)
        return NULL;
    if (!layer->surface)
        return NULL;

    /* Capture all text properties before mutation. */
    TextLayerState *text_state = text_layer_state_create(layer->text_data);
    if (!text_state)
        return NULL;

    TextRasterizeCommandData *data =
        (TextRasterizeCommandData *)g_malloc0(sizeof(TextRasterizeCommandData));
    data->doc        = doc;
    data->layer      = layer;
    data->text_state = text_state;

    Command *cmd = command_new("Rasterize Text Layer",
                               COMMAND_LAYER_EDIT,
                               text_rasterize_apply,
                               text_rasterize_revert,
                               text_rasterize_destroy);
    if (!cmd) {
        text_layer_state_free(text_state);
        g_free(data);
        return NULL;
    }
    cmd->user_data = data;

    /* Render text to the layer surface. */
    text_layer_render_to_surface(layer);

    /* Snapshot the rasterized result (the "after" state for redo). */
    data->raster_snapshot = cairo_surface_snapshot(layer->surface);

    /* Convert the layer in-place. */
    text_layer_free((TextLayer *)layer->text_data);
    layer->text_data  = NULL;
    layer->layer_type = LAYER_TYPE_RASTER;

    text_cmd_invalidate(doc, layer);

    return cmd;
}
