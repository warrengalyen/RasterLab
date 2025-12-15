#include "render/layer.h"
#include "render/compositor.h"
#include "render/mipmap.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create a new image layer
 */
ImageLayer* layer_new(const gchar* name, guint width, guint height, gboolean has_alpha) {
    ImageLayer* layer = (ImageLayer*)g_malloc(sizeof(ImageLayer));

    layer->name = g_strdup(name);

    /* Create layer surface */
    cairo_format_t format = has_alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    layer->surface = cairo_image_surface_create(format, width, height);

    /* Clear with transparent black if has alpha, white otherwise */
    cairo_t* cr = cairo_create(layer->surface);
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
    layer->offset_x = 0; /* Initialize layer offset */
    layer->offset_y = 0;
    layer->cache_surface = NULL;  /* Cache created on demand */
    layer->cache_dirty = TRUE;    /* Cache needs initial generation */
    layer->mipmap_pyramid = NULL; /* Mipmap pyramid created lazily */

    return layer;
}

/**
 * Free an image layer
 */
void layer_free(ImageLayer* layer) {
    if (!layer) {
        return;
    }

    if (layer->name) {
        g_free(layer->name);
        layer->name = NULL;
    }

    if (layer->surface) {
        /* Save surface pointer and clear layer pointer first to prevent double-free */
        cairo_surface_t* surface = layer->surface;
        layer->surface = NULL;

        /* Destroy surface - cairo_surface_destroy() is safe to call on NULL
         * or already-destroyed surfaces. We skip flush/finish to avoid crashes
         * if the surface pointer is invalid (pointing to freed memory). */
        cairo_surface_destroy(surface);
    }

    if (layer->cache_surface) {
        /* Save surface pointer and clear layer pointer first to prevent double-free */
        cairo_surface_t* cache_surface = layer->cache_surface;
        layer->cache_surface = NULL;

        /* Destroy surface - cairo_surface_destroy() is safe to call on NULL
         * or already-destroyed surfaces. We skip flush/finish to avoid crashes
         * if the surface pointer is invalid (pointing to freed memory). */
        cairo_surface_destroy(cache_surface);
    }

    if (layer->mipmap_pyramid) {
        /* Save pyramid pointer and clear layer pointer first to prevent double-free */
        MipmapPyramid* pyramid = layer->mipmap_pyramid;
        layer->mipmap_pyramid = NULL; /* Clear pointer first to prevent double-free */

        /* Free pyramid - if pyramid structure is corrupted, this will segfault.
         * We can't check if a pointer is valid in C, so we just have to try.
         * If it crashes, at least we've cleared the layer pointer so it won't be double-freed. */
        mipmap_pyramid_free(pyramid);
    }

    /* Free layer structure - heap might be corrupted at this point if
     * mipmap_pyramid_free() corrupted it, but we need to try anyway */
    g_free(layer);
}

/**
 * Add a new empty layer to the document
 */
ImageLayer* document_add_layer(ImageDocument* doc, const gchar* name) {
    ImageLayer* layer;

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
gboolean document_delete_layer(ImageDocument* doc, ImageLayer* layer) {
    GList* iter;

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
        ImageLayer* new_selected = document_get_layer(doc, 0);
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
ImageLayer* document_duplicate_layer(ImageDocument* doc, ImageLayer* layer, const gchar* name) {
    ImageLayer* new_layer;
    cairo_t* cr;

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
    GList* iter = g_list_find(doc->layers, layer);
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
gboolean document_layer_move_up(ImageDocument* doc, ImageLayer* layer) {
    GList* iter;
    gint pos;

    if (!doc || !layer) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->next) {
        return FALSE; /* Already at top or not found */
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
gboolean document_layer_move_down(ImageDocument* doc, ImageLayer* layer) {
    GList* iter;
    gint pos;

    if (!doc || !layer) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->prev) {
        return FALSE; /* Already at bottom or not found */
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
gboolean document_layer_can_move_up(ImageDocument* doc, ImageLayer* layer) {
    GList* iter;

    if (!doc || !layer || !doc->layers) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->next) {
        return FALSE; /* Already at top or not found */
    }

    return TRUE;
}

/**
 * Check if a layer can be moved down in the stack
 */
gboolean document_layer_can_move_down(ImageDocument* doc, ImageLayer* layer) {
    GList* iter;

    if (!doc || !layer || !doc->layers) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->prev) {
        return FALSE; /* Already at bottom or not found */
    }

    return TRUE;
}

/**
 * Get the layer at a specific index
 */
ImageLayer* document_get_layer(ImageDocument* doc, guint index) {
    if (!doc) {
        return NULL;
    }

    return (ImageLayer*)g_list_nth_data(doc->layers, index);
}

/**
 * Get the number of layers in the document
 */
guint document_get_layer_count(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return g_list_length(doc->layers);
}

/**
 * Get the top (active) layer
 */
ImageLayer* document_get_active_layer(ImageDocument* doc) {
    if (!doc || !doc->layers) {
        return NULL;
    }

    /* Return the top layer (last in list) */
    return (ImageLayer*)g_list_nth_data(doc->layers, g_list_length(doc->layers) - 1);
}

/**
 * Set the selected layer (for tool operations)
 */
void document_set_selected_layer(ImageDocument* doc, ImageLayer* layer) {
    if (!doc) {
        return;
    }

    doc->selected_layer = layer;
}

/**
 * Get the selected layer (for tool operations)
 */
ImageLayer* document_get_selected_layer(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    /* Return selected layer if set, otherwise return active (top) layer */
    if (doc->selected_layer) {
        return doc->selected_layer;
    }

    return document_get_active_layer(doc);
}

/**
 * Invalidate layer cache (mark as needing regeneration)
 */
void layer_invalidate_cache(ImageLayer* layer) {
    if (!layer) {
        return;
    }

    layer->cache_dirty = TRUE;

    /* Destroy old cache */
    if (layer->cache_surface) {
        cairo_surface_destroy(layer->cache_surface);
        layer->cache_surface = NULL;
    }

    /* Mark mipmap pyramid dirty
     * Safety check: ensure pyramid is still valid (not being freed) */
    if (layer->mipmap_pyramid) {
        /* Additional check: ensure levels array is valid */
        if (layer->mipmap_pyramid->levels) {
            mipmap_mark_dirty(layer->mipmap_pyramid, 0); /* Mark level 0 and all lower levels dirty */
        }
    }
}

/**
 * Ensure mipmap pyramid exists and is initialized
 */
static gboolean layer_ensure_mipmap_pyramid(ImageLayer* layer) {
    if (!layer || !layer->surface) {
        return FALSE;
    }

    /* Create mipmap pyramid if it doesn't exist */
    if (!layer->mipmap_pyramid) {
        const gint tile_size = 128; /* Use same tile size as document */
        layer->mipmap_pyramid = mipmap_pyramid_create(layer->width, layer->height, tile_size);

        if (!layer->mipmap_pyramid) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * Ensure layer cache is up to date (regenerate if dirty)
 * Creates a cached surface with opacity and blend mode pre-applied
 *
 * OPTIMIZATION: For large layers, we can use the source surface directly
 * with opacity applied on-the-fly instead of caching, if the cache
 * would be too expensive to regenerate frequently.
 */
gboolean layer_ensure_cache(ImageLayer* layer) {
    cairo_t* cr;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    /* Return if cache is valid */
    if (!layer->cache_dirty && layer->cache_surface) {
        return TRUE;
    }

    /* For very large layers (2000x2000+), consider skipping cache
       and using source directly with opacity applied on-the-fly.
       Cache is most beneficial for opacity/blend mode changes,
       not for pixel content changes during drawing. */
    guint layer_area = layer->width * layer->height;
    const guint LARGE_LAYER_THRESHOLD = 2000 * 2000; /* 4 million pixels */

    /* If layer is very large and cache is dirty, we might skip caching
       during active drawing operations. But for now, we'll still cache
       since it helps with opacity/blend mode performance. */

    /* Destroy old cache if exists */
    if (layer->cache_surface) {
        cairo_surface_destroy(layer->cache_surface);
        layer->cache_surface = NULL;
    }

    /* Create cache surface with same dimensions as layer */
    layer->cache_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, layer->width, layer->height);

    if (cairo_surface_status(layer->cache_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create layer cache surface");
        layer->cache_surface = NULL;
        return FALSE;
    }

    /* Render layer surface to cache with opacity applied */
    cr = cairo_create(layer->cache_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, layer->surface, 0, 0);

    if (layer->opacity < 1.0) {
        cairo_paint_with_alpha(cr, layer->opacity);
    } else {
        cairo_paint(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(layer->cache_surface);
    layer->cache_dirty = FALSE;

    return TRUE;
}
