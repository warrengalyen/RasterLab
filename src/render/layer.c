#include "render/layer.h"
#include "render/compositor.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create a new image layer
 */
ImageLayer* layer_new(const gchar *name, guint width, guint height, gboolean has_alpha)
{
    ImageLayer *layer = (ImageLayer *)g_malloc(sizeof(ImageLayer));

    layer->name = g_strdup(name);
    
    /* Create layer surface */
    cairo_format_t format = has_alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    layer->surface = cairo_image_surface_create(format, width, height);
    
    /* Clear with transparent black if has alpha, white otherwise */
    cairo_t *cr = cairo_create(layer->surface);
    if (has_alpha) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    }
    cairo_paint(cr);
    cairo_destroy(cr);

    layer->opacity = 1.0;
    layer->visible = TRUE;
    layer->width = width;
    layer->height = height;
    layer->blend_mode = BLEND_MODE_NORMAL;
    layer->offset_x = 0;  /* Initialize layer offset */
    layer->offset_y = 0;

    return layer;
}

/**
 * Free an image layer
 */
void layer_free(ImageLayer *layer)
{
    if (!layer) {
        return;
    }

    if (layer->name) {
        g_free(layer->name);
    }

    if (layer->surface) {
        cairo_surface_destroy(layer->surface);
    }

    g_free(layer);
}

/**
 * Add a new empty layer to the document
 */
ImageLayer* document_add_layer(ImageDocument *doc, const gchar *name)
{
    ImageLayer *layer;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Create new layer with document dimensions */
    layer = layer_new(name, doc->width, doc->height, TRUE);

    if (!layer) {
        return NULL;
    }

    /* Add to top of layer stack */
    doc->layers = g_list_append(doc->layers, layer);

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    return layer;
}

/**
 * Delete a layer from the document
 */
gboolean document_delete_layer(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;

    if (!doc || !layer) {
        return FALSE;
    }

    /* Find and remove layer */
    iter = g_list_find(doc->layers, layer);

    if (!iter) {
        return FALSE;
    }

    /* Don't delete the last layer */
    if (g_list_length(doc->layers) == 1) {
        g_warning("Cannot delete the last layer");
        return FALSE;
    }

    /* Clear selected_layer reference if it's the layer being deleted */
    if (doc->selected_layer == layer) {
        doc->selected_layer = NULL;
    }

    /* Remove layer from list FIRST to prevent any code from accessing it */
    doc->layers = g_list_remove(doc->layers, layer);

    /* Destroy composite surface to ensure no references to layer surface remain */
    /* This must happen after removing from list but before freeing the layer */
    if (doc->composite_surface) {
        /* Flush any pending operations on the composite surface */
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }
    doc->composite_dirty = TRUE;
    
    /* Now free the layer (destroys its surface) */
    /* This must happen after removing from list and destroying composite */
    layer_free(layer);

    /* Update selected layer to point to a valid layer (layer at index 0) */
    if (doc->layers) {
        ImageLayer *new_selected = document_get_layer(doc, 0);
        if (new_selected) {
            doc->selected_layer = new_selected;
        }
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    return TRUE;
}

/**
 * Duplicate an existing layer
 */
ImageLayer* document_duplicate_layer(ImageDocument *doc, ImageLayer *layer, const gchar *name)
{
    ImageLayer *new_layer;
    cairo_t *cr;

    if (!doc || !layer) {
        return NULL;
    }

    /* Create new layer */
    new_layer = layer_new(name, layer->width, layer->height, TRUE);

    if (!new_layer) {
        return NULL;
    }

    /* Copy content from source layer */
    cr = cairo_create(new_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Copy properties */
    new_layer->opacity = layer->opacity;
    new_layer->blend_mode = layer->blend_mode;

    /* Add to layer stack (after source layer) */
    GList *iter = g_list_find(doc->layers, layer);
    if (iter && iter->next) {
        doc->layers = g_list_insert_before(doc->layers, iter->next, new_layer);
    } else {
        doc->layers = g_list_append(doc->layers, new_layer);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    return new_layer;
}

/**
 * Move a layer up in the stack
 */
gboolean document_layer_move_up(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;
    gint pos;

    if (!doc || !layer) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->next) {
        return FALSE;  /* Already at top or not found */
    }

    pos = g_list_position(doc->layers, iter);
    doc->layers = g_list_remove(doc->layers, layer);
    doc->layers = g_list_insert(doc->layers, layer, pos + 1);

    document_invalidate_composite(doc);

    return TRUE;
}

/**
 * Move a layer down in the stack
 */
gboolean document_layer_move_down(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;
    gint pos;

    if (!doc || !layer) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->prev) {
        return FALSE;  /* Already at bottom or not found */
    }

    pos = g_list_position(doc->layers, iter);
    doc->layers = g_list_remove(doc->layers, layer);
    doc->layers = g_list_insert(doc->layers, layer, pos - 1);

    document_invalidate_composite(doc);

    return TRUE;
}

/**
 * Check if a layer can be moved up in the stack
 */
gboolean document_layer_can_move_up(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;

    if (!doc || !layer || !doc->layers) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->next) {
        return FALSE;  /* Already at top or not found */
    }

    return TRUE;
}

/**
 * Check if a layer can be moved down in the stack
 */
gboolean document_layer_can_move_down(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;

    if (!doc || !layer || !doc->layers) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->prev) {
        return FALSE;  /* Already at bottom or not found */
    }

    return TRUE;
}

/**
 * Get the layer at a specific index
 */
ImageLayer* document_get_layer(ImageDocument *doc, guint index)
{
    if (!doc) {
        return NULL;
    }

    return (ImageLayer *)g_list_nth_data(doc->layers, index);
}

/**
 * Get the number of layers in the document
 */
guint document_get_layer_count(ImageDocument *doc)
{
    if (!doc) {
        return 0;
    }

    return g_list_length(doc->layers);
}

/**
 * Get the top (active) layer
 */
ImageLayer* document_get_active_layer(ImageDocument *doc)
{
    if (!doc || !doc->layers) {
        return NULL;
    }

    /* Return the top layer (last in list) */
    return (ImageLayer *)g_list_nth_data(doc->layers, g_list_length(doc->layers) - 1);
}

/**
 * Set the selected layer (for tool operations)
 */
void document_set_selected_layer(ImageDocument *doc, ImageLayer *layer)
{
    if (!doc) {
        return;
    }

    doc->selected_layer = layer;
}

/**
 * Get the selected layer (for tool operations)
 */
ImageLayer* document_get_selected_layer(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    /* Return selected layer if set, otherwise return active (top) layer */
    if (doc->selected_layer) {
        return doc->selected_layer;
    }

    return document_get_active_layer(doc);
}

