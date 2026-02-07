#include "render/compositor.h"
#include "render/layer.h"
#include "render/mipmap.h"
#include "render/tile.h"
#include "render/tile_thread_pool.h"
#include "render/tile_worker.h"
#include "ui/layers_panel.h"
#include "ui/workspace.h"
#include <stdio.h>

/**
 * Map BlendMode enum to Cairo operator
 * Exported for use by tile rendering
 */
cairo_operator_t blend_mode_to_cairo_operator(BlendMode blend_mode) {
    switch (blend_mode) {
        case BLEND_MODE_NORMAL:
            return CAIRO_OPERATOR_OVER;
        case BLEND_MODE_DARKEN:
            return CAIRO_OPERATOR_DARKEN;
        case BLEND_MODE_MULTIPLY:
            return CAIRO_OPERATOR_MULTIPLY;
        case BLEND_MODE_COLOR_BURN:
            return CAIRO_OPERATOR_COLOR_BURN;
        case BLEND_MODE_LIGHTEN:
            return CAIRO_OPERATOR_LIGHTEN;
        case BLEND_MODE_SCREEN:
            return CAIRO_OPERATOR_SCREEN;
        case BLEND_MODE_COLOR_DODGE:
            return CAIRO_OPERATOR_COLOR_DODGE;
        case BLEND_MODE_OVERLAY:
            return CAIRO_OPERATOR_OVERLAY;
        case BLEND_MODE_SOFT_LIGHT:
            return CAIRO_OPERATOR_SOFT_LIGHT;
        case BLEND_MODE_HARD_LIGHT:
            return CAIRO_OPERATOR_HARD_LIGHT;
        case BLEND_MODE_DIFFERENCE:
            return CAIRO_OPERATOR_DIFFERENCE;
        default:
            return CAIRO_OPERATOR_OVER;
    }
}

/**
 * Render all layers to composite surface (full render)
 */
gboolean document_render_composite(ImageDocument* doc) {
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    /* Create composite surface if needed */
    if (!doc->composite_surface) {
        doc->composite_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    }

    if (cairo_surface_status(doc->composite_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create composite surface");
        return FALSE;
    }

    /* Clear composite surface to transparent */
    cr = cairo_create(doc->composite_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);

    /* Set operator for proper alpha blending */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Safety check: if document is being freed (layers is NULL), don't render */
    if (!doc->layers) {
        cairo_destroy(cr);
        cairo_surface_flush(doc->composite_surface);
        return FALSE;
    }

    /* Track if this is the first visible layer */
    gboolean is_first_visible_layer = TRUE;

    /* Composite each visible layer using cached surfaces */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        /* Skip if layer is NULL (shouldn't happen, but be safe) */
        if (!layer) {
            continue;
        }

        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Skip if layer surface is NULL or invalid */
        if (!layer->surface) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw layer with offset using cached surface */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

        /* Set operator based on layer's blend mode
           First visible layer always uses OVER to establish the base */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    /* Finish all Cairo operations and flush the surface */
    cairo_surface_flush(doc->composite_surface);
    cairo_destroy(cr);
    doc->composite_dirty = FALSE;

    /* Clear dirty region after full render */
    dirty_rect_init(&doc->dirty_region);

    return TRUE;
}

/**
 * Render only dirty regions to composite surface (optimized)
 */
gboolean document_render_composite_dirty(ImageDocument* doc, const DirtyRect* dirty_rect) {
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;
    DirtyRect clipped_rect;
    gint layer_x, layer_y, layer_right, layer_bottom;
    gint dirty_left, dirty_top, dirty_right, dirty_bottom;
    gint intersect_left, intersect_top, intersect_right, intersect_bottom;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    if (!dirty_rect || dirty_rect_is_empty(dirty_rect)) {
        return TRUE; /* Nothing to render */
    }

    /* Create composite surface if needed */
    if (!doc->composite_surface) {
        doc->composite_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    }

    if (cairo_surface_status(doc->composite_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create composite surface");
        return FALSE;
    }

    /* Clip dirty rect to document bounds */
    clipped_rect = *dirty_rect;
    dirty_rect_clamp(&clipped_rect, doc->width, doc->height);

    if (dirty_rect_is_empty(&clipped_rect)) {
        return TRUE; /* Clipped to nothing */
    }

    dirty_left = clipped_rect.x;
    dirty_top = clipped_rect.y;
    dirty_right = dirty_left + clipped_rect.width;
    dirty_bottom = dirty_top + clipped_rect.height;

    cr = cairo_create(doc->composite_surface);

    /* First, clear the entire dirty region to transparent */
    cairo_save(cr);
    cairo_rectangle(cr, dirty_left, dirty_top, clipped_rect.width, clipped_rect.height);
    cairo_clip(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    /* Safety check: if document is being freed (layers is NULL), don't render */
    if (!doc->layers) {
        cairo_destroy(cr);
        return TRUE; /* Nothing to render */
    }

    /* Track if this is the first visible layer */
    gboolean is_first_visible_layer = TRUE;

    /* Composite each visible layer that intersects the dirty region */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Calculate layer bounds in document coordinates */
        layer_x = layer->offset_x;
        layer_y = layer->offset_y;
        layer_right = layer_x + layer->width;
        layer_bottom = layer_y + layer->height;

        /* Check if layer intersects dirty region */
        intersect_left = (layer_x > dirty_left) ? layer_x : dirty_left;
        intersect_top = (layer_y > dirty_top) ? layer_y : dirty_top;
        intersect_right = (layer_right < dirty_right) ? layer_right : dirty_right;
        intersect_bottom = (layer_bottom < dirty_bottom) ? layer_bottom : dirty_bottom;

        if (intersect_left >= intersect_right || intersect_top >= intersect_bottom) {
            continue; /* No intersection */
        }

        /* Calculate source region in layer coordinates */
        gint src_x = intersect_left - layer_x;
        gint src_y = intersect_top - layer_y;
        gint src_width = intersect_right - intersect_left;
        gint src_height = intersect_bottom - intersect_top;

        /* Draw only the intersecting region */
        cairo_save(cr);

        /* Clip to intersection region in document coordinates */
        cairo_rectangle(cr, intersect_left, intersect_top, src_width, src_height);
        cairo_clip(cr);

        /* Set operator based on layer's blend mode
           First visible layer always uses OVER to establish the base */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);

        /* OPTIMIZATION: For large layers with dirty cache, use source directly
           with opacity applied on-the-fly instead of regenerating entire cache.
           This is much faster for frequent updates during drawing. */
        guint layer_area = layer->width * layer->height;
        const guint LARGE_LAYER_THRESHOLD = 1500 * 1500; /* ~2.25 million pixels */

        if (layer->cache_dirty && layer_area > LARGE_LAYER_THRESHOLD) {
            /* Use source surface directly with opacity - no cache needed for this render */
            cairo_set_source_surface(cr, layer->surface, layer_x, layer_y);
            if (layer->opacity < 1.0) {
                cairo_paint_with_alpha(cr, layer->opacity);
            } else {
                cairo_paint(cr);
            }
        } else {
            /* Use cached surface (either valid or small enough to regenerate quickly) */
            if (!layer_ensure_cache(layer)) {
                cairo_restore(cr);
                continue;
            }
            cairo_set_source_surface(cr, layer->cache_surface, layer_x, layer_y);
            cairo_paint(cr);
        }

        cairo_restore(cr);
    }

    /* Finish all Cairo operations and flush the surface */
    cairo_surface_flush(doc->composite_surface);
    cairo_destroy(cr);

    return TRUE;
}

/**
 * Get the composite rendered surface
 */
cairo_surface_t* document_get_composite_surface(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    /* TILE-BASED: For tile-based rendering, composite tiles if needed */
    if (doc->tile_grid && doc->composite_dirty) {
        tile_grid_composite(doc, doc->tile_grid);
        doc->composite_dirty = FALSE;
        dirty_rect_init(&doc->dirty_region);
    }

    /* Only create composite surface when actually needed (e.g., for saving) */
    /* For overview thumbnails, use document_generate_thumbnail_from_tiles() instead */
    if (!doc->composite_surface && doc->width > 0 && doc->height > 0) {
        doc->composite_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, doc->width, doc->height);

        if (cairo_surface_status(doc->composite_surface) == CAIRO_STATUS_SUCCESS) {
            /* Copy from tiles if available, otherwise render */
            if (doc->tile_grid) {
                /* Ensure all tiles are composited first */
                tile_grid_composite(doc, doc->tile_grid);

                /* Copy tiles to composite surface */
                cairo_t* cr = cairo_create(doc->composite_surface);
                gint tx, ty;

                for (ty = 0; ty < doc->tile_grid->tiles_y; ty++) {
                    for (tx = 0; tx < doc->tile_grid->tiles_x; tx++) {
                        Tile* tile = &doc->tile_grid->tiles[ty][tx];
                        if (tile && tile->surface) {
                            cairo_set_source_surface(cr, tile->surface, tile->px, tile->py);
                            cairo_paint(cr);
                        }
                    }
                }

                cairo_destroy(cr);
                cairo_surface_flush(doc->composite_surface);
            } else {
                /* Fallback to full render */
                document_render_composite(doc);
            }
        }
    }

    return doc->composite_surface;
}

/**
 * Generate a fresh composite surface with all layers for export/saving
 * This always generates a new surface (doesn't use cache) to ensure
 * all layers are included and the image is up to date
 * @param doc The document
 * @return New composite surface, or NULL on error. Caller must call cairo_surface_destroy().
 */
cairo_surface_t* document_export_composite_surface(ImageDocument* doc) {
    cairo_surface_t* export_surface;
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;
    gboolean is_first_visible_layer = TRUE;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Create new surface for export (always fresh) */
    export_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, doc->width, doc->height);

    if (cairo_surface_status(export_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create export composite surface");
        return NULL;
    }

    cr = cairo_create(export_surface);

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* For tile-based rendering, ensure all tiles are composited first */
    if (doc->tile_grid) {
        /* Mark all tiles as dirty to ensure they're all composited */
        gint tx, ty;
        for (ty = 0; ty < doc->tile_grid->tiles_y; ty++) {
            for (tx = 0; tx < doc->tile_grid->tiles_x; tx++) {
                Tile* tile = &doc->tile_grid->tiles[ty][tx];
                if (tile) {
                    tile->dirty = TRUE;
                }
            }
        }
        /* Composite all tiles */
        tile_grid_composite(doc, doc->tile_grid);

        /* Copy tiles to export surface */
        for (ty = 0; ty < doc->tile_grid->tiles_y; ty++) {
            for (tx = 0; tx < doc->tile_grid->tiles_x; tx++) {
                Tile* tile = &doc->tile_grid->tiles[ty][tx];
                if (tile && tile->surface) {
                    cairo_set_source_surface(cr, tile->surface, tile->px, tile->py);
                    cairo_paint(cr);
                }
            }
        }
    } else {
        /* Safety check: if document is being freed (layers is NULL), don't render */
        if (!doc->layers) {
            cairo_destroy(cr);
            cairo_surface_flush(export_surface);
            return export_surface;
        }

        /* Non-tile-based: composite all visible layers directly */
        for (iter = doc->layers; iter; iter = iter->next) {
            layer = (ImageLayer*)iter->data;

            if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
                continue;
            }

            /* Ensure layer cache is up to date */
            if (!layer_ensure_cache(layer)) {
                continue;
            }

            /* Draw layer with offset using cached surface */
            cairo_save(cr);
            cairo_translate(cr, layer->offset_x, layer->offset_y);
            cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

            /* Set operator based on layer's blend mode */
            cairo_operator_t op;
            if (is_first_visible_layer) {
                op = CAIRO_OPERATOR_OVER;
                is_first_visible_layer = FALSE;
            } else {
                op = blend_mode_to_cairo_operator(layer->blend_mode);
            }
            cairo_set_operator(cr, op);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(export_surface);

    return export_surface;
}

/**
 * Generate a thumbnail surface by directly compositing layers at thumbnail size
 * This avoids tile boundary artifacts by rendering layers directly at scale
 */
cairo_surface_t* document_generate_thumbnail_from_tiles(ImageDocument* doc, gint thumb_width, gint thumb_height) {
    cairo_surface_t* thumb_surface;
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;
    gdouble scale_x, scale_y, scale;
    gboolean is_first_visible_layer = TRUE;

    if (!doc || doc->width <= 0 || doc->height <= 0) {
        return NULL;
    }

    if (thumb_width <= 0 || thumb_height <= 0) {
        return NULL;
    }

    /* Calculate scale to fit thumbnail */
    scale_x = (gdouble)thumb_width / doc->width;
    scale_y = (gdouble)thumb_height / doc->height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    /* Create thumbnail surface */
    thumb_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, thumb_width, thumb_height);
    if (cairo_surface_status(thumb_surface) != CAIRO_STATUS_SUCCESS) {
        return NULL;
    }

    cr = cairo_create(thumb_surface);

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Scale context to thumbnail size */
    cairo_scale(cr, scale, scale);

    /* Set bilinear filtering for better quality when scaling down layers */
    cairo_pattern_t* dummy = cairo_pattern_create_rgba(0, 0, 0, 0);
    cairo_set_source(cr, dummy);
    cairo_pattern_destroy(dummy);

    /* Safety check: if document is being freed (layers is NULL), don't render */
    if (!doc->layers) {
        cairo_destroy(cr);
        cairo_surface_flush(thumb_surface);
        return thumb_surface;
    }

    /* Composite each visible layer directly at thumbnail scale */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        cairo_save(cr);

        /* Translate to layer position */
        cairo_translate(cr, layer->offset_x, layer->offset_y);

        /* Set source with bilinear filtering for smooth scaling */
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);
        cairo_pattern_t* pattern = cairo_get_source(cr);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_BILINEAR);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);

        /* Paint layer (opacity is already applied in cache) */
        cairo_paint(cr);

        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(thumb_surface);

    return thumb_surface;
}

/**
 * Render layers directly to a Cairo context at zoom scale
 * Uses mipmaps when zoomed out to avoid expensive scaling operations
 */
void document_render_layers_at_zoom(ImageDocument* doc, cairo_t* cr,
                                    gint viewport_x, gint viewport_y,
                                    gint viewport_w, gint viewport_h) {
    GList* iter;
    ImageLayer* layer;
    gboolean is_first_visible_layer = TRUE;
    gdouble zoom_factor = doc ? doc->zoom_factor : 1.0;

    if (!doc || !cr) {
        return;
    }

    /* Safety check: if document is being freed (layers is NULL), don't render */
    if (!doc->layers) {
        return;
    }

    /* Clip to viewport for efficiency */
    cairo_save(cr);
    cairo_rectangle(cr, viewport_x, viewport_y, viewport_w, viewport_h);
    cairo_clip(cr);

    /* Composite each visible layer directly at zoom scale */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Check if layer intersects viewport */
        gint layer_x = layer->offset_x;
        gint layer_y = layer->offset_y;
        gint layer_right = layer_x + layer->width;
        gint layer_bottom = layer_y + layer->height;

        if (layer_right <= viewport_x || layer_x >= viewport_x + viewport_w ||
            layer_bottom <= viewport_y || layer_y >= viewport_y + viewport_h) {
            continue; /* Layer doesn't intersect viewport */
        }

        cairo_save(cr);

        /* Translate to layer position */
        cairo_translate(cr, layer->offset_x, layer->offset_y);

        /* Use mipmap if zoomed out */
        if (zoom_factor < 1.0) {
            /* Ensure mipmap pyramid exists and is valid */
            if (!layer->mipmap_pyramid || !layer->mipmap_pyramid->levels) {
                const gint tile_size = 128;
                layer->mipmap_pyramid = mipmap_pyramid_create(layer->width, layer->height, tile_size);
            }

            /* Safety check: ensure pyramid is still valid before using it */
            if (layer->mipmap_pyramid && layer->mipmap_pyramid->levels) {
                /* Select appropriate mipmap level */
                guint mip_level = mipmap_select_level(layer->mipmap_pyramid, zoom_factor);
                MipmapLevel* level = mipmap_get_level(layer->mipmap_pyramid, mip_level);

                /* Use cache surface (with opacity) for mipmap generation */
                if (!layer_ensure_cache(layer)) {
                    cairo_restore(cr);
                    continue;
                }

                /* Safety check: ensure level is still valid and pyramid hasn't been freed */
                if (level && layer->mipmap_pyramid && layer->mipmap_pyramid->levels &&
                    mipmap_ensure_level(layer->mipmap_pyramid, mip_level, layer->cache_surface)) {
                    /* Re-get level pointer after ensure_level (it might have changed) */
                    level = mipmap_get_level(layer->mipmap_pyramid, mip_level);

                    /* Use mipmap surface if available, otherwise use tile grid */
                    if (level && level->surface) {
                        /* Calculate scale to match zoom */
                        gdouble mip_scale = zoom_factor / level->scale_factor;

                        cairo_save(cr);
                        cairo_scale(cr, mip_scale, mip_scale);
                        cairo_set_source_surface(cr, level->surface, 0, 0);
                        
                        /* CRITICAL: Use NEAREST filter to prevent interpolation artifacts
                         * that cause visible seams during scrolling */
                        cairo_pattern_t* mip_pattern = cairo_get_source(cr);
                        cairo_pattern_set_filter(mip_pattern, CAIRO_FILTER_NEAREST);
                        /* Use PAD extend mode to prevent edge sampling artifacts */
                        cairo_pattern_set_extend(mip_pattern, CAIRO_EXTEND_PAD);

                        /* Set operator based on layer's blend mode */
                        cairo_operator_t op;
                        if (is_first_visible_layer) {
                            op = CAIRO_OPERATOR_OVER;
                            is_first_visible_layer = FALSE;
                        } else {
                            op = blend_mode_to_cairo_operator(layer->blend_mode);
                        }
                        cairo_set_operator(cr, op);

                        /* Use explicit rectangle + fill instead of paint() */
                        cairo_rectangle(cr, 0, 0, level->width, level->height);
                        cairo_fill(cr);

                        cairo_restore(cr);
                        continue;
                    }
                }
            }
        }

        /* Fallback to normal rendering (full resolution or mipmap unavailable) */
        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            cairo_restore(cr);
            continue;
        }

        /* Set source with NEAREST filtering to prevent interpolation artifacts
         * that cause visible seams during scrolling. This applies at all zoom levels. */
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);
        cairo_pattern_t* pattern = cairo_get_source(cr);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
        /* Use PAD extend mode to prevent edge sampling artifacts */
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);

        /* Use explicit rectangle + fill instead of paint() to avoid clip edge artifacts */
        cairo_rectangle(cr, 0, 0, layer->width, layer->height);
        cairo_fill(cr);

        cairo_restore(cr);
    }

    cairo_restore(cr);
}

/**
 * Mark composite surface as needing re-render
 *
 * TILE-BASED: Now marks all tiles as dirty instead of full surface
 */
void document_invalidate_composite(ImageDocument* doc) {
    LayersPanel* layers_panel;

    if (!doc) {
        return;
    }

    doc->composite_dirty = TRUE;

    /* TILE-BASED: Mark all tiles as dirty - composite surface will be regenerated on-demand when needed */
    /* Don't invalidate composite surface here - it's only created when actually needed (e.g., saving) */

    /* Mark entire document as dirty */
    dirty_rect_set(&doc->dirty_region, 0, 0, doc->width, doc->height);

    /* Mark all tiles as dirty for tile-based rendering */
    if (doc->tile_grid) {
        tile_grid_mark_rect_dirty(doc->tile_grid, 0, 0, doc->width, doc->height);
    }

    /* Trigger redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);

        /* Update selected layer thumbnail if layers panel is available */
        layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(doc->drawing_area), "layers_panel");
        if (layers_panel) {
            /* Only update if this document is the current one in the layers panel */
            if (layers_panel->current_doc == doc) {
                layers_panel_update_selected_thumbnail(layers_panel);

                /* Update overview widget */
                GtkWidget* window = gtk_widget_get_toplevel(doc->drawing_area);
                if (GTK_IS_WINDOW(window)) {
                    Workspace* workspace = (Workspace*)g_object_get_data(G_OBJECT(window), "workspace");
                    if (workspace) {
                        GtkWidget* overview_widget = workspace_get_overview_widget(workspace);
                        if (overview_widget) {
                            gtk_widget_queue_draw(overview_widget);
                        }
                    }
                }
            }
        }
    }
}

/**
 * Mark a specific region as dirty
 *
 * TILE-BASED: Now marks intersecting tiles as dirty instead of just tracking rectangle
 */
void document_invalidate_region(ImageDocument* doc, const DirtyRect* dirty_rect) {
    LayersPanel* layers_panel;
    DirtyRect clamped_rect;

    if (!doc || !dirty_rect) {
        return;
    }

    if (dirty_rect_is_empty(dirty_rect)) {
        return;
    }

    /* Clamp to document bounds */
    clamped_rect = *dirty_rect;
    dirty_rect_clamp(&clamped_rect, doc->width, doc->height);

    if (dirty_rect_is_empty(&clamped_rect)) {
        return;
    }

    /* Union with existing dirty region */
    if (dirty_rect_is_empty(&doc->dirty_region)) {
        doc->dirty_region = clamped_rect;
    } else {
        dirty_rect_union(&doc->dirty_region, &clamped_rect, &doc->dirty_region);
    }

    doc->composite_dirty = TRUE;

    /* TILE-BASED: Mark intersecting tiles as dirty - composite surface will be regenerated on-demand when needed */
    /* Don't invalidate composite surface here - it's only created when actually needed (e.g., saving) */

    /* Mark intersecting tiles as dirty for tile-based rendering */
    if (doc->tile_grid) {
        tile_grid_mark_rect_dirty(doc->tile_grid,
                                  clamped_rect.x, clamped_rect.y,
                                  clamped_rect.width, clamped_rect.height);

        /* Enqueue dirty tiles to Cairo-safe worker pool for pixel compositing */
        if (doc->tile_worker_pool) {
            gint tx, ty, start_tx, start_ty, end_tx, end_ty;
            Tile* tile;

            /* Calculate which tiles need recomposition */
            start_tx = clamped_rect.x / doc->tile_grid->tile_size;
            start_ty = clamped_rect.y / doc->tile_grid->tile_size;
            end_tx = (clamped_rect.x + clamped_rect.width - 1) / doc->tile_grid->tile_size;
            end_ty = (clamped_rect.y + clamped_rect.height - 1) / doc->tile_grid->tile_size;

            /* Clamp to grid bounds */
            if (start_tx < 0)
                start_tx = 0;
            if (start_ty < 0)
                start_ty = 0;
            if (end_tx >= doc->tile_grid->tiles_x)
                end_tx = doc->tile_grid->tiles_x - 1;
            if (end_ty >= doc->tile_grid->tiles_y)
                end_ty = doc->tile_grid->tiles_y - 1;

            /* Enqueue dirty tiles for worker pool */
            for (ty = start_ty; ty <= end_ty; ty++) {
                for (tx = start_tx; tx <= end_tx; tx++) {
                    tile = &doc->tile_grid->tiles[ty][tx];
                    if (tile && tile->dirty) {
                        /* Worker threads will composite into pixel_buffer */
                        if (!tile_worker_pool_enqueue(doc->tile_worker_pool,
                                                      doc, tile, tx, ty)) {
                            g_debug("Worker pool rejected tile (%d, %d), will fallback to main thread", tx, ty);
                            /* Fallback: composite on main thread */
                            if (tile_worker_composite_pixels(doc, tile, tx, ty)) {
                                tile->pending_upload = TRUE;
                            }
                        }
                    }
                }
            }
        } else {
            /* Worker pool not available - composite on main thread (fallback) */
            gint tx, ty, start_tx, start_ty, end_tx, end_ty;
            Tile* tile;

            start_tx = clamped_rect.x / doc->tile_grid->tile_size;
            start_ty = clamped_rect.y / doc->tile_grid->tile_size;
            end_tx = (clamped_rect.x + clamped_rect.width - 1) / doc->tile_grid->tile_size;
            end_ty = (clamped_rect.y + clamped_rect.height - 1) / doc->tile_grid->tile_size;

            if (start_tx < 0)
                start_tx = 0;
            if (start_ty < 0)
                start_ty = 0;
            if (end_tx >= doc->tile_grid->tiles_x)
                end_tx = doc->tile_grid->tiles_x - 1;
            if (end_ty >= doc->tile_grid->tiles_y)
                end_ty = doc->tile_grid->tiles_y - 1;

            for (ty = start_ty; ty <= end_ty; ty++) {
                for (tx = start_tx; tx <= end_tx; tx++) {
                    tile = &doc->tile_grid->tiles[ty][tx];
                    if (tile && tile->dirty) {
                        if (tile_worker_composite_pixels(doc, tile, tx, ty)) {
                            tile->pending_upload = TRUE;
                        }
                    }
                }
            }
        }

        /* Legacy thread pool support (disabled for Cairo safety) */
        if (doc->tile_thread_pool) {
            g_debug("Legacy tile_thread_pool is set but not used (Cairo-safe worker pool active)");
        }
    }

    /* Trigger redraw - use queue_draw instead of queue_draw_area for now
       queue_draw_area can cause artifacts and doesn't work well with zoom/transform
       GTK will optimize the redraw based on the clip region in the draw callback */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);

        /* Update selected layer thumbnail if layers panel is available */
        layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(doc->drawing_area), "layers_panel");
        if (layers_panel) {
            /* Only update if this document is the current one in the layers panel */
            if (layers_panel->current_doc == doc) {
                layers_panel_update_selected_thumbnail(layers_panel);

                /* Update overview widget */
                GtkWidget* window = gtk_widget_get_toplevel(doc->drawing_area);
                if (GTK_IS_WINDOW(window)) {
                    Workspace* workspace = (Workspace*)g_object_get_data(G_OBJECT(window), "workspace");
                    if (workspace) {
                        GtkWidget* overview_widget = workspace_get_overview_widget(workspace);
                        if (overview_widget) {
                            gtk_widget_queue_draw(overview_widget);
                        }
                    }
                }
            }
        }
    }
}

/**
 * Flatten image to white background (for JPEG)
 */
cairo_surface_t* compositor_flatten_to_white_background(cairo_surface_t* composite, guint width, guint height) {
    cairo_surface_t* flattened;
    cairo_t* cr;

    /* Create RGB surface (no alpha) */
    flattened = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (!flattened) {
        return NULL;
    }

    cr = cairo_create(flattened);

    /* Paint white background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Composite the image on top */
    cairo_set_source_surface(cr, composite, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);

    cairo_destroy(cr);

    return flattened;
}
