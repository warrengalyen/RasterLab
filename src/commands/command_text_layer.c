#include "commands/command_text_layer.h"
#include "command.h"
#include "document.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include <string.h>

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * Return TRUE if @layer still exists in @doc->layers.
 */
static gboolean text_cmd_layer_valid(struct ImageDocument *doc,
                                     struct ImageLayer    *layer) {
    if (!doc || !layer)
        return FALSE;
    return g_list_find(doc->layers, layer) != NULL;
}

/**
 * Trigger a full redraw after an undo/redo operation.
 */
static void text_cmd_invalidate(struct ImageDocument *doc,
                                struct ImageLayer    *layer) {
    if (layer)
        layer_invalidate_cache(layer);
    document_invalidate_composite(doc);
}

/* =========================================================================
 * TextLayerState helpers
 * ========================================================================= */

TextLayerState *text_layer_state_create(const void *tl_ptr) {
    if (!tl_ptr)
        return NULL;

    const TextLayer *tl   = (const TextLayer *)tl_ptr;
    TextLayerState  *state = (TextLayerState *)g_malloc0(sizeof(TextLayerState));

    state->text           = g_strdup(tl->text        ? tl->text        : "");
    state->font_family    = g_strdup(tl->font_family  ? tl->font_family : "");
    state->font_size      = tl->font_size;
    state->font_weight    = tl->font_weight;
    state->italic         = tl->italic;
    state->color_r        = tl->color_r;
    state->color_g        = tl->color_g;
    state->color_b        = tl->color_b;
    state->color_a        = tl->color_a;
    state->line_spacing   = tl->line_spacing;
    state->letter_spacing = tl->letter_spacing;
    state->alignment      = tl->alignment;
    state->rotation       = tl->rotation;
    state->box_x          = tl->box_x;
    state->box_y          = tl->box_y;
    state->box_width      = tl->box_width;
    state->box_height     = tl->box_height;
    state->antialias      = tl->antialias;

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

    tl->font_size      = state->font_size;
    tl->font_weight    = state->font_weight;
    tl->italic         = state->italic;
    tl->color_r        = state->color_r;
    tl->color_g        = state->color_g;
    tl->color_b        = state->color_b;
    tl->color_a        = state->color_a;
    tl->line_spacing   = state->line_spacing;
    tl->letter_spacing = state->letter_spacing;
    tl->alignment      = state->alignment;
    tl->rotation       = state->rotation;
    tl->box_x          = state->box_x;
    tl->box_y          = state->box_y;
    tl->box_width      = state->box_width;
    tl->box_height     = state->box_height;
    tl->antialias      = state->antialias;
}

void text_layer_state_free(TextLayerState *state) {
    if (!state)
        return;
    g_free(state->text);
    g_free(state->font_family);
    g_free(state);
}

/* =========================================================================
 * Text layer property-change command
 * ========================================================================= */

typedef struct {
    struct ImageDocument *doc;
    struct ImageLayer    *layer;
    TextLayerState       *before;
    TextLayerState       *after;
} TextLayerPropCommandData;

static void text_prop_apply(Command *cmd, struct ImageDocument *doc) {
    TextLayerPropCommandData *data;

    if (!cmd || !cmd->user_data || !doc)
        return;

    data = (TextLayerPropCommandData *)cmd->user_data;

    if (!data->after || !data->layer)
        return;
    if (!text_cmd_layer_valid(doc, data->layer))
        return;
    if (data->layer->layer_type != LAYER_TYPE_TEXT || !data->layer->text_data)
        return;

    text_layer_state_restore(data->layer->text_data, data->after);
    text_cmd_invalidate(doc, data->layer);
}

static void text_prop_revert(Command *cmd, struct ImageDocument *doc) {
    TextLayerPropCommandData *data;

    if (!cmd || !cmd->user_data || !doc)
        return;

    data = (TextLayerPropCommandData *)cmd->user_data;

    if (!data->before || !data->layer)
        return;
    if (!text_cmd_layer_valid(doc, data->layer))
        return;
    if (data->layer->layer_type != LAYER_TYPE_TEXT || !data->layer->text_data)
        return;

    text_layer_state_restore(data->layer->text_data, data->before);
    text_cmd_invalidate(doc, data->layer);
}

static void text_prop_destroy(Command *cmd) {
    TextLayerPropCommandData *data;

    if (!cmd || !cmd->user_data)
        return;

    data = (TextLayerPropCommandData *)cmd->user_data;
    text_layer_state_free(data->before);
    text_layer_state_free(data->after);
    g_free(data);
}

Command *command_create_text_layer_prop(struct ImageDocument *doc,
                                        struct ImageLayer    *layer,
                                        TextLayerState       *before,
                                        TextLayerState       *after,
                                        const char           *name) {
    TextLayerPropCommandData *data;
    Command                  *cmd;

    if (!doc || !layer || !before || !after) {
        text_layer_state_free(before);
        text_layer_state_free(after);
        return NULL;
    }

    data = (TextLayerPropCommandData *)g_malloc0(sizeof(TextLayerPropCommandData));
    data->doc    = doc;
    data->layer  = layer;
    data->before = before;
    data->after  = after;

    cmd = command_new(name ? name : "Edit Text Layer",
                      COMMAND_LAYER_EDIT,
                      text_prop_apply,
                      text_prop_revert,
                      text_prop_destroy);
    if (!cmd) {
        text_layer_state_free(before);
        text_layer_state_free(after);
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    return cmd;
}

/* =========================================================================
 * Text layer rasterize command
 * ========================================================================= */

typedef struct {
    struct ImageDocument *doc;
    struct ImageLayer    *layer;
    TextLayerState       *text_state;       /* before state, for undo */
    cairo_surface_t      *raster_snapshot;  /* after state, for redo  */
} TextRasterizeCommandData;

/* Redo: re-apply the rasterization (restore raster surface, remove text_data) */
static void text_rasterize_apply(Command *cmd, struct ImageDocument *doc) {
    TextRasterizeCommandData *data;
    cairo_t                  *cr;

    if (!cmd || !cmd->user_data || !doc)
        return;

    data = (TextRasterizeCommandData *)cmd->user_data;

    if (!data->layer || !data->raster_snapshot)
        return;
    if (!text_cmd_layer_valid(doc, data->layer))
        return;
    if (!data->layer->surface)
        return;

    /* Restore the rasterized surface */
    cr = cairo_create(data->layer->surface);
    cairo_set_source_surface(cr, data->raster_snapshot, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Free text_data if it was re-created by a previous undo */
    if (data->layer->text_data) {
        text_layer_free((TextLayer *)data->layer->text_data);
        data->layer->text_data = NULL;
    }
    data->layer->layer_type = LAYER_TYPE_RASTER;

    text_cmd_invalidate(doc, data->layer);
}

/* Undo: restore text_data from captured state, revert to LAYER_TYPE_TEXT */
static void text_rasterize_revert(Command *cmd, struct ImageDocument *doc) {
    TextRasterizeCommandData *data;
    TextLayer                *tl;

    if (!cmd || !cmd->user_data || !doc)
        return;

    data = (TextRasterizeCommandData *)cmd->user_data;

    if (!data->layer || !data->text_state)
        return;
    if (!text_cmd_layer_valid(doc, data->layer))
        return;

    /* Drop any existing text_data */
    if (data->layer->text_data) {
        text_layer_free((TextLayer *)data->layer->text_data);
        data->layer->text_data = NULL;
    }

    /* Recreate text_data from the saved state */
    tl = (TextLayer *)g_malloc0(sizeof(TextLayer));
    text_layer_state_restore(tl, data->text_state);
    data->layer->text_data  = tl;
    data->layer->layer_type = LAYER_TYPE_TEXT;

    text_cmd_invalidate(doc, data->layer);
}

static void text_rasterize_destroy(Command *cmd) {
    TextRasterizeCommandData *data;

    if (!cmd || !cmd->user_data)
        return;

    data = (TextRasterizeCommandData *)cmd->user_data;
    text_layer_state_free(data->text_state);
    if (data->raster_snapshot)
        cairo_surface_destroy(data->raster_snapshot);
    g_free(data);
}

Command *command_create_text_layer_rasterize(struct ImageDocument *doc,
                                              struct ImageLayer    *layer) {
    TextRasterizeCommandData *data;
    Command                  *cmd;
    TextLayerState           *text_state;
    cairo_surface_t          *snapshot;

    if (!doc || !layer)
        return NULL;
    if (layer->layer_type != LAYER_TYPE_TEXT || !layer->text_data)
        return NULL;
    if (!layer->surface)
        return NULL;

    /* Step 1: capture text properties before rasterization */
    text_state = text_layer_state_create(layer->text_data);
    if (!text_state)
        return NULL;

    /* Step 2: allocate data and create command BEFORE mutating the layer */
    data = (TextRasterizeCommandData *)g_malloc0(sizeof(TextRasterizeCommandData));
    data->doc        = doc;
    data->layer      = layer;
    data->text_state = text_state;

    cmd = command_new("Rasterize Text Layer",
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

    /* Step 3: render text to layer surface */
    text_layer_render_to_surface(layer);

    /* Step 4: snapshot the result (the "after" raster state for redo) */
    snapshot = cairo_surface_snapshot(layer->surface);
    data->raster_snapshot = snapshot; /* NULL is tolerated — redo re-renders */

    /* Step 5: convert the layer to raster */
    text_layer_free((TextLayer *)layer->text_data);
    layer->text_data  = NULL;
    layer->layer_type = LAYER_TYPE_RASTER;

    text_cmd_invalidate(doc, layer);

    return cmd;
}
