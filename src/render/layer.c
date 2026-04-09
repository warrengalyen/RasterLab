/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "render/layer.h"
#include "i18n.h"
#include "render/text_layer.h"
#include "document.h"
#include "render/compositor.h"
#include "render/mipmap.h"
#include "render/tile.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

/* Forward declarations */
static void layer_fill_background(ImageLayer* layer, LayerBackgroundType background, const gdouble* custom_color);
static ImageLayer* layer_copy_surface_and_metadata_to_new(const ImageLayer* src, struct ImageDocument* doc,
                                                          const gchar* name_for_new_layer);

/**
 * Create a new image layer
 */
/**
 * Generate a unique layer name by appending a count if the name already exists
 * @param doc Document to check for existing layer names (can be NULL to skip checking)
 * @param base_name Base name to use
 * @return Allocated string with unique name (caller must free with g_free)
 */
static gchar* layer_generate_unique_name(struct ImageDocument* doc, const gchar* base_name) {
    if (!doc || !base_name) {
        return g_strdup(base_name ? base_name : "Layer");
    }

    /* Check if base name is unique */
    gboolean name_exists = FALSE;
    for (GList* iter = doc->layers; iter != NULL; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;
        if (layer && layer->name && g_strcmp0(layer->name, base_name) == 0) {
            name_exists = TRUE;
            break;
        }
    }

    if (!name_exists) {
        return g_strdup(base_name);
    }

    /* Name exists, append count */
    int count = 2;
    gchar* unique_name = NULL;
    do {
        g_free(unique_name);
        unique_name = g_strdup_printf(_("%s (%d)"), base_name, count);
        name_exists = FALSE;

        /* Check if this name exists */
        for (GList* iter = doc->layers; iter != NULL; iter = iter->next) {
            ImageLayer* layer = (ImageLayer*)iter->data;
            if (layer && layer->name && g_strcmp0(layer->name, unique_name) == 0) {
                name_exists = TRUE;
                break;
            }
        }

        count++;
    } while (name_exists);

    return unique_name;
}

ImageLayer* layer_new(const gchar* name, guint width, guint height, gboolean has_alpha,
                      LayerBackgroundType background, LayerPosition position,
                      const gdouble* custom_color, struct ImageDocument* doc) {
    ImageLayer* layer = (ImageLayer*)g_malloc(sizeof(ImageLayer));
    if (!layer) {
        return NULL;
    }

    /* Generate unique name if document is provided */
    if (doc && name) {
        layer->name = layer_generate_unique_name(doc, name);
    } else {
        layer->name = g_strdup(name);
    }

    if (!layer->name) {
        g_free(layer);
        return NULL;
    }

    /* Create layer surface */
    cairo_format_t format = has_alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    layer->surface = cairo_image_surface_create(format, width, height);

    /* Check if surface creation succeeded */
    if (cairo_surface_status(layer->surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(layer->surface);
        g_free(layer->name);
        g_free(layer);
        return NULL;
    }

    /* Use defaults if not specified */
    if (background < LAYER_BACKGROUND_TRANSPARENT || background > LAYER_BACKGROUND_CUSTOM) {
        background = LAYER_BACKGROUND_TRANSPARENT;
    }
    if (position < LAYER_POSITION_ABOVE_CURRENT || position > LAYER_POSITION_BOTTOM) {
        position = LAYER_POSITION_ABOVE_CURRENT;
    }

    /* Fill layer with background color */
    layer_fill_background(layer, background, custom_color);

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
    layer->layer_type = LAYER_TYPE_RASTER;
    layer->text_data  = NULL;

    return layer;
}

/**
 * Copy pixels/text from @a src into a new layer (not attached to any document).
 * Caller must ensure @a src has a surface and @a name_for_new_layer is non-NULL.
 */
static ImageLayer* layer_copy_surface_and_metadata_to_new(const ImageLayer* src, struct ImageDocument* doc,
                                                        const gchar* name_for_new_layer) {
    cairo_format_t fmt;
    gboolean has_alpha;
    cairo_t* cr;
    ImageLayer* dst;

    fmt = cairo_image_surface_get_format(src->surface);
    has_alpha = (fmt == CAIRO_FORMAT_ARGB32);

    dst = layer_new(name_for_new_layer, src->width, src->height, has_alpha,
                    LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!dst) {
        return NULL;
    }

    cr = cairo_create(dst->surface);
    cairo_set_source_surface(cr, src->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(dst->surface);

    dst->opacity = src->opacity;
    dst->visible = src->visible;
    dst->blend_mode = src->blend_mode;
    dst->offset_x = src->offset_x;
    dst->offset_y = src->offset_y;
    dst->content_version = src->content_version;
    dst->cache_dirty = TRUE;
    dst->layer_type = src->layer_type;

    if (src->layer_type == LAYER_TYPE_TEXT && src->text_data) {
        TextLayer* td = text_layer_duplicate((TextLayer*)src->text_data);
        if (td) {
            dst->text_data = td;
            text_layer_render_to_surface(dst);
        } else {
            dst->layer_type = LAYER_TYPE_RASTER;
        }
    }

    return dst;
}

ImageLayer* layer_duplicate_deep(const ImageLayer* src, struct ImageDocument* doc) {
    if (!src || !src->surface) {
        return NULL;
    }
    return layer_copy_surface_and_metadata_to_new(src, doc, src->name);
}

ImageLayer* layer_new_from_layer_content(const ImageLayer* src, struct ImageDocument* doc,
                                         const gchar* layer_name) {
    if (!src || !src->surface || !layer_name) {
        return NULL;
    }
    return layer_copy_surface_and_metadata_to_new(src, doc, layer_name);
}

static gboolean layer_surface_pixels_equal(cairo_surface_t* sa, cairo_surface_t* sb) {
    cairo_format_t fa;
    cairo_format_t fb;
    int wa, ha, wb, hb;
    int stra, strb;
    guchar *pa, *pb;
    int y;

    if (!sa || !sb) {
        return sa == sb;
    }
    fa = cairo_image_surface_get_format(sa);
    fb = cairo_image_surface_get_format(sb);
    if (fa != fb) {
        return FALSE;
    }
    wa = cairo_image_surface_get_width(sa);
    wb = cairo_image_surface_get_width(sb);
    ha = cairo_image_surface_get_height(sa);
    hb = cairo_image_surface_get_height(sb);
    if (wa != wb || ha != hb) {
        return FALSE;
    }
    stra = cairo_image_surface_get_stride(sa);
    strb = cairo_image_surface_get_stride(sb);
    cairo_surface_flush(sa);
    cairo_surface_flush(sb);
    pa = cairo_image_surface_get_data(sa);
    pb = cairo_image_surface_get_data(sb);
    if (!pa || !pb) {
        return FALSE;
    }

    if (fa == CAIRO_FORMAT_ARGB32) {
        for (y = 0; y < ha; y++) {
            if (memcmp(pa + (size_t)y * stra, pb + (size_t)y * strb, (size_t)wa * 4u) != 0) {
                return FALSE;
            }
        }
        return TRUE;
    }
    if (fa == CAIRO_FORMAT_RGB24) {
        for (y = 0; y < ha; y++) {
            if (memcmp(pa + (size_t)y * stra, pb + (size_t)y * strb, (size_t)wa * 3u) != 0) {
                return FALSE;
            }
        }
        return TRUE;
    }

    /* Other formats: compare full stride rows */
    for (y = 0; y < ha; y++) {
        if (memcmp(pa + (size_t)y * stra, pb + (size_t)y * strb, (size_t)stra) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean layer_equal_content(const ImageLayer* a, const ImageLayer* b) {
    if (!a || !b) {
        return a == b;
    }
    if (g_strcmp0(a->name, b->name) != 0) {
        return FALSE;
    }
    if (a->width != b->width || a->height != b->height) {
        return FALSE;
    }
    if (a->opacity != b->opacity) {
        return FALSE;
    }
    if (a->visible != b->visible) {
        return FALSE;
    }
    if (a->blend_mode != b->blend_mode) {
        return FALSE;
    }
    if (a->offset_x != b->offset_x || a->offset_y != b->offset_y) {
        return FALSE;
    }
    if (a->layer_type != b->layer_type) {
        return FALSE;
    }
    if (a->layer_type == LAYER_TYPE_TEXT) {
        if (!text_layer_equal((TextLayer*)a->text_data, (TextLayer*)b->text_data)) {
            return FALSE;
        }
    }
    return layer_surface_pixels_equal(a->surface, b->surface);
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

    /* Free text layer data if present */
    if (layer->layer_type == LAYER_TYPE_TEXT && layer->text_data) {
        text_layer_free((TextLayer*)layer->text_data);
        layer->text_data = NULL;
    }

    /* Free layer structure - heap might be corrupted at this point if
     * mipmap_pyramid_free() corrupted it, but we need to try anyway */
    g_free(layer);
}

/**
 * Fill layer surface with background color
 */
static void layer_fill_background(ImageLayer* layer, LayerBackgroundType background, const gdouble* custom_color) {
    cairo_t* cr;
    gdouble r, g, b, a;

    if (!layer || !layer->surface) {
        return;
    }

    cr = cairo_create(layer->surface);
    if (!cr) {
        return;
    }

    /* Determine color based on background type */
    switch (background) {
        case LAYER_BACKGROUND_TRANSPARENT:
            r = 0.0;
            g = 0.0;
            b = 0.0;
            a = 0.0;
            break;
        case LAYER_BACKGROUND_BLACK:
            r = 0.0;
            g = 0.0;
            b = 0.0;
            a = 1.0;
            break;
        case LAYER_BACKGROUND_WHITE:
            r = 1.0;
            g = 1.0;
            b = 1.0;
            a = 1.0;
            break;
        case LAYER_BACKGROUND_CUSTOM:
            if (custom_color && custom_color[0] >= 0.0 && custom_color[0] <= 1.0 &&
                custom_color[1] >= 0.0 && custom_color[1] <= 1.0 &&
                custom_color[2] >= 0.0 && custom_color[2] <= 1.0 &&
                custom_color[3] >= 0.0 && custom_color[3] <= 1.0) {
                r = custom_color[0];
                g = custom_color[1];
                b = custom_color[2];
                a = custom_color[3];
            } else {
                /* Fallback to transparent if custom color is invalid */
                r = 0.0;
                g = 0.0;
                b = 0.0;
                a = 0.0;
            }
            break;
        default:
            /* Default to transparent */
            r = 0.0;
            g = 0.0;
            b = 0.0;
            a = 0.0;
            break;
    }

    /* Fill layer with background color */
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_paint(cr);
    cairo_destroy(cr);
}

/**
 * Add a new empty layer to the document
 */
ImageLayer* document_add_layer(ImageDocument* doc, const gchar* name,
                               LayerBackgroundType background,
                               LayerPosition position,
                               const gdouble* custom_color) {
    ImageLayer* layer;
    ImageLayer* reference_layer;
    GList* iter;
    gint pos;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Use defaults if not specified */
    if (background < LAYER_BACKGROUND_TRANSPARENT || background > LAYER_BACKGROUND_CUSTOM) {
        background = LAYER_BACKGROUND_TRANSPARENT;
    }
    if (position < LAYER_POSITION_ABOVE_CURRENT || position > LAYER_POSITION_BOTTOM) {
        position = LAYER_POSITION_ABOVE_CURRENT;
    }

    /* Create new layer with document dimensions */
    layer = layer_new(name, doc->width, doc->height, TRUE, background, position, custom_color, doc);

    if (!layer) {
        return NULL;
    }

    /* Fill layer with background color */
    layer_fill_background(layer, background, custom_color);

    /* Determine insertion position */
    switch (position) {
        case LAYER_POSITION_TOP:
            /* Add to top of layer stack (end of list) */
            doc->layers = g_list_append(doc->layers, layer);
            break;

        case LAYER_POSITION_BOTTOM:
            /* Add to bottom of layer stack (beginning of list) */
            doc->layers = g_list_prepend(doc->layers, layer);
            break;

        case LAYER_POSITION_ABOVE_CURRENT:
            /* Add above current/selected layer */
            reference_layer = document_get_selected_layer(doc);
            if (reference_layer) {
                iter = g_list_find(doc->layers, reference_layer);
                if (iter && iter->next) {
                    /* Insert after reference layer */
                    pos = g_list_position(doc->layers, iter);
                    doc->layers = g_list_remove(doc->layers, layer);
                    doc->layers = g_list_insert(doc->layers, layer, pos + 1);
                } else if (iter) {
                    /* Reference layer is at end, append */
                    doc->layers = g_list_append(doc->layers, layer);
                } else {
                    /* Fallback: add to top */
                    doc->layers = g_list_append(doc->layers, layer);
                }
            } else {
                /* No selected layer: add to top */
                doc->layers = g_list_append(doc->layers, layer);
            }
            break;

        case LAYER_POSITION_BELOW_CURRENT:
            /* Add below current/selected layer */
            reference_layer = document_get_selected_layer(doc);
            if (reference_layer) {
                iter = g_list_find(doc->layers, reference_layer);
                if (iter) {
                    /* Insert before reference layer */
                    pos = g_list_position(doc->layers, iter);
                    doc->layers = g_list_remove(doc->layers, layer);
                    doc->layers = g_list_insert(doc->layers, layer, pos);
                } else {
                    /* Fallback: add to bottom */
                    doc->layers = g_list_prepend(doc->layers, layer);
                }
            } else {
                /* No selected layer: add to bottom */
                doc->layers = g_list_prepend(doc->layers, layer);
            }
            break;

        default:
            /* Default: add to top */
            doc->layers = g_list_append(doc->layers, layer);
            break;
    }

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
        debug_log("WRN", "Cannot delete the last layer");
        return FALSE;
    }

    /* Clear selected_layer reference if it's the layer being deleted */
    if (doc->selected_layer == layer) {
        doc->selected_layer = NULL;
    }

    /* Remove layer from list FIRST to prevent any code from accessing it */
    doc->layers = g_list_remove(doc->layers, layer);

    /* Invalidate layer intersection cache BEFORE freeing the layer
     * This removes stale cache entries that would point to freed memory */
    if (doc->tile_grid) {
        tile_grid_invalidate_layer_cache(doc->tile_grid, layer);
    }

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
    new_layer = layer_new(name, layer->width, layer->height, TRUE,
                          LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);

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
    
    /* Increment content version to invalidate GPU texture cache */
    layer->content_version++;

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

    /* For text layers, re-render the Pango+Cairo text into the surface
     * before the cache is built from it. */
    switch (layer->layer_type) {
        case LAYER_TYPE_TEXT:
            if (layer->text_data) {
                text_layer_render_to_surface(layer);
            }
            break;
        default:
            break;
    }

    /* Destroy old cache if exists */
    if (layer->cache_surface) {
        cairo_surface_destroy(layer->cache_surface);
        layer->cache_surface = NULL;
    }

    /* Create cache surface with same dimensions as layer */
    layer->cache_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, layer->width, layer->height);

    if (cairo_surface_status(layer->cache_surface) != CAIRO_STATUS_SUCCESS) {
        debug_log("WRN", "Failed to create layer cache surface");
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
