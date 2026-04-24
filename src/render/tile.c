/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "render/tile.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

/**
 * Create a new tile grid for a document
 */
TileGrid* tile_grid_create(gint width, gint height, gint tile_size) {
    TileGrid* grid;
    gint x, y;

    if (width <= 0 || height <= 0 || tile_size <= 0) {
        return NULL;
    }

    grid = (TileGrid*)g_malloc(sizeof(TileGrid));
    grid->tile_size = tile_size;

    /* Calculate number of tiles needed */
    grid->tiles_x = (width + tile_size - 1) / tile_size;  /* Round up */
    grid->tiles_y = (height + tile_size - 1) / tile_size; /* Round up */

    /* Allocate 2D array: tiles[y][x] */
    grid->tiles = (Tile**)g_malloc(sizeof(Tile*) * grid->tiles_y);

    for (y = 0; y < grid->tiles_y; y++) {
        grid->tiles[y] = (Tile*)g_malloc(sizeof(Tile) * grid->tiles_x);

        for (x = 0; x < grid->tiles_x; x++) {
            Tile* tile = &grid->tiles[y][x];

            /* Initialize tile */
            tile->x = x;
            tile->y = y;
            tile->px = x * tile_size;
            tile->py = y * tile_size;

            /* Calculate actual tile size (edge tiles may be smaller) */
            tile->w = (tile->px + tile_size > width) ? (width - tile->px) : tile_size;
            tile->h = (tile->py + tile_size > height) ? (height - tile->py) : tile_size;

            /* Allocate pixel buffer for worker threads */
            tile->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, tile->w);
            tile->pixel_buffer = (uint8_t*)g_malloc0(tile->stride * tile->h);

            if (!tile->pixel_buffer) {
                debug_log("WRN", "Failed to allocate pixel buffer for tile at (%d, %d)", x, y);
            }

            /* Create tile surface with Cairo's recommended stride */
            tile->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tile->w, tile->h);

            if (cairo_surface_status(tile->surface) != CAIRO_STATUS_SUCCESS) {
                debug_log("WRN", "Failed to create tile surface at (%d, %d)", x, y);
                tile->surface = NULL;
            }

            /* Initialize to transparent */
            if (tile->surface) {
                cairo_t* cr = cairo_create(tile->surface);
                cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                cairo_paint(cr);
                cairo_destroy(cr);
            }

            tile->dirty = TRUE; /* All tiles start dirty and need initial compositing */
            tile->state = TILE_CLEAN;
            tile->generation_id = 0;
            tile->pending_upload = FALSE;

            /* Initialize mipmaps to NULL */
            for (int m = 0; m < TILE_MIPMAP_LEVELS; m++) {
                tile->mipmaps[m] = NULL;
            }
            tile->mipmaps_dirty = TRUE;
            
            /* Initialize layer intersection cache */
            tile->layer_intersection_cache = g_hash_table_new_full(
                g_direct_hash, g_direct_equal, NULL, g_free);
        }
    }

    return grid;
}

/**
 * Free a tile grid and all its tiles
 */
void tile_grid_free(TileGrid* grid) {
    gint x, y;

    if (!grid) {
        return;
    }

    /* Free all tile resources */
    if (grid->tiles) {
        for (y = 0; y < grid->tiles_y; y++) {
            if (grid->tiles[y]) {
                for (x = 0; x < grid->tiles_x; x++) {
                    Tile* tile = &grid->tiles[y][x];

                    /* Free Cairo surface */
                    if (tile->surface) {
                        cairo_surface_t* surface = tile->surface;
                        tile->surface = NULL;
                        cairo_surface_destroy(surface);
                    }

                    /* Free pixel buffer (allocated by worker pool or init) */
                    if (tile->pixel_buffer) {
                        g_free(tile->pixel_buffer);
                        tile->pixel_buffer = NULL;
                    }

                    /* Free mipmaps */
                    tile_free_mipmaps(tile);
                    
                    /* Free layer intersection cache */
                    tile_free_intersection_cache(tile);
                }
                g_free(grid->tiles[y]);
                grid->tiles[y] = NULL; /* Set to NULL after freeing */
            }
        }
        g_free(grid->tiles);
        grid->tiles = NULL; /* Set to NULL after freeing */
    }

    g_free(grid);
}

/**
 * Mark a specific tile as dirty
 */
void tile_mark_dirty(Tile* tile) {
    if (tile) {
        tile->dirty = TRUE;
        tile->mipmaps_dirty = TRUE; /* Mipmaps also need regeneration */
    }
}

/**
 * Mark all tiles intersecting a rectangle as dirty
 */
void tile_grid_mark_rect_dirty(TileGrid* grid, gint x, gint y, gint w, gint h) {
    gint start_tile_x, start_tile_y;
    gint end_tile_x, end_tile_y;
    gint tx, ty;

    if (!grid || w <= 0 || h <= 0) {
        return;
    }

    /* Calculate which tiles intersect the rectangle */
    start_tile_x = x / grid->tile_size;
    start_tile_y = y / grid->tile_size;
    end_tile_x = (x + w - 1) / grid->tile_size;
    end_tile_y = (y + h - 1) / grid->tile_size;

    /* Clamp to grid bounds */
    if (start_tile_x < 0)
        start_tile_x = 0;
    if (start_tile_y < 0)
        start_tile_y = 0;
    if (end_tile_x >= grid->tiles_x)
        end_tile_x = grid->tiles_x - 1;
    if (end_tile_y >= grid->tiles_y)
        end_tile_y = grid->tiles_y - 1;

    /* Mark all intersecting tiles as dirty */
    for (ty = start_tile_y; ty <= end_tile_y; ty++) {
        for (tx = start_tile_x; tx <= end_tile_x; tx++) {
            tile_mark_dirty(&grid->tiles[ty][tx]);
        }
    }
}

/**
 * Get a tile at specific grid coordinates
 */
Tile* tile_grid_get_tile(TileGrid* grid, gint tile_x, gint tile_y) {
    if (!grid || !grid->tiles) {
        return NULL;
    }

    if (tile_x < 0 || tile_x >= grid->tiles_x ||
        tile_y < 0 || tile_y >= grid->tiles_y) {
        return NULL;
    }

    return &grid->tiles[tile_y][tile_x];
}

/**
 * Get tile grid coordinates from document pixel coordinates
 */
void tile_grid_pixel_to_tile(TileGrid* grid, gint px, gint py, gint* tile_x, gint* tile_y) {
    if (!grid || !tile_x || !tile_y) {
        return;
    }

    *tile_x = px / grid->tile_size;
    *tile_y = py / grid->tile_size;
}

/**
 * Composite a single tile by rendering all layers that intersect it
 * This replaces the old full-surface compositing for this tile region.
 * Uses cached layer-tile intersection geometry to avoid recomputing bounds.
 */
static gboolean tile_composite(ImageDocument* doc, Tile* tile) {
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;
    const LayerTileIntersection* intersection;
    gboolean is_first_visible_layer = TRUE;

    if (!doc || !tile || !tile->surface) {
        return FALSE;
    }

    /* Clear tile to transparent */
    cr = cairo_create(tile->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Safety check: if document is being freed (layers is NULL), don't composite */
    if (!doc->layers) {
        cairo_destroy(cr);
        return FALSE;
    }

    /* Occlusion culling: find the lowest fully-opaque NORMAL-blend layer that
     * completely covers this tile — all layers below it are invisible. */
    gint tile_right = tile->px + tile->w;
    gint tile_bottom = tile->py + tile->h;
    GList* start_layer = doc->layers;
    for (iter = g_list_last(doc->layers); iter; iter = iter->prev) {
        layer = (ImageLayer*)iter->data;
        if (!layer || !layer->visible || !layer->surface ||
            layer->layer_type == LAYER_TYPE_TEXT)
            continue;
        if (layer->opacity < 1.0 || layer->blend_mode != BLEND_MODE_NORMAL)
            continue;
        gint lx = layer->offset_x, ly = layer->offset_y;
        gint lr = lx + (gint)layer->width, lb = ly + (gint)layer->height;
        if (lx <= tile->px && ly <= tile->py && lr >= tile_right && lb >= tile_bottom) {
            start_layer = iter;
            break;
        }
    }

    /* Composite each visible layer that intersects this tile */
    for (iter = start_layer; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Get cached intersection (computes if not cached or invalidated) */
        intersection = tile_get_layer_intersection(tile, layer);
        
        if (!intersection || !intersection->intersects) {
            continue; /* No intersection */
        }

        /* Clip to tile bounds using cached coordinates */
        cairo_save(cr);
        cairo_rectangle(cr, intersection->tile_local_x, intersection->tile_local_y,
                        intersection->tile_local_w, intersection->tile_local_h);
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

        /* For text (vector) layers in the large-layer fast path, ensure the
         * Pango text has been drawn into the surface before we composite it. */
        switch (layer->layer_type) {
            case LAYER_TYPE_TEXT:
                if (layer->text_data && layer->cache_dirty) {
                    text_layer_render_to_surface(layer);
                }
                break;
            default:
                break;
        }

        /* OPTIMIZATION: For large layers with dirty cache, use source directly
           with opacity applied on-the-fly instead of regenerating entire cache.
           This is much faster for frequent updates during drawing. */
        guint layer_area = layer->width * layer->height;
        const guint LARGE_LAYER_THRESHOLD = 1500 * 1500; /* ~2.25 million pixels */

        if (layer->cache_dirty && layer_area > LARGE_LAYER_THRESHOLD) {
            /* Use source surface directly with opacity - no cache needed for this render */
            /* Translate to position layer correctly relative to tile */
            cairo_set_source_surface(cr, layer->surface,
                                     layer->offset_x - tile->px,
                                     layer->offset_y - tile->py);
            /* Set nearest filter to prevent edge artifacts at tile boundaries */
            // cairo_pattern_t* pattern = cairo_get_source(cr);
            // cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
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
            /* Translate to position cached layer correctly relative to tile */
            cairo_set_source_surface(cr, layer->cache_surface,
                                     layer->offset_x - tile->px,
                                     layer->offset_y - tile->py);
            /* Set nearest filter to prevent edge artifacts at tile boundaries */
            // cairo_pattern_t* pattern = cairo_get_source(cr);
            // cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
        }

        cairo_restore(cr);
    }

    /* Finish all Cairo operations and flush the surface */
    cairo_surface_flush(tile->surface);
    cairo_destroy(cr);
    tile->dirty = FALSE;

    /* Generate mipmaps for fast zoom-out rendering */
    tile_generate_mipmaps(tile);

    return TRUE;
}

/**
 * Composite all dirty tiles in the grid
 * This replaces document_render_composite() - instead of rendering the entire
 * document surface, we only render tiles that are marked dirty.
 */
gboolean tile_grid_composite(ImageDocument* doc, TileGrid* grid) {
    gint x, y;
    Tile* tile;
    gint dirty_count = 0;

    if (!doc || !grid) {
        return FALSE;
    }

    /* Composite each dirty tile */
    for (y = 0; y < grid->tiles_y; y++) {
        for (x = 0; x < grid->tiles_x; x++) {
            tile = &grid->tiles[y][x];

            if (tile->dirty && tile->surface) {
                if (tile_composite(doc, tile)) {
                    dirty_count++;
                } else {
                    debug_log("WRN", "Failed to composite tile at (%d, %d)", x, y);
                }
            }
        }
    }

    return TRUE;
}

/**
 * Mark a tile as needing recomposition via thread pool
 * Increments generation_id to invalidate any pending work
 */
void tile_mark_dirty_for_thread_pool(Tile* tile) {
    if (!tile) {
        return;
    }

    tile->dirty = TRUE;
    tile->mipmaps_dirty = TRUE; /* Mipmaps also need regeneration */
    tile->generation_id++;      /* Invalidate any pending work */
    tile->state = TILE_CLEAN;   /* Reset state for new work */
}

/**
 * Apply completed tile result from worker thread to main tile cache
 * Should only be called from GTK main thread
 * Returns TRUE if result was applied, FALSE if stale
 */
gboolean tile_apply_completed_result(Tile* tile, cairo_surface_t* new_surface, guint generation_id) {
    if (!tile || !new_surface) {
        return FALSE;
    }

    /* Check if this result is stale (newer work has been queued) */
    if (generation_id != tile->generation_id) {
        return FALSE; /* Stale result, discard */
    }

    /* Swap surfaces */
    if (tile->surface) {
        cairo_surface_destroy(tile->surface);
    }

    tile->surface = new_surface;
    tile->dirty = FALSE;
    tile->state = TILE_CLEAN;

    return TRUE;
}

/**
 * Free all mipmaps for a tile
 */
void tile_free_mipmaps(Tile* tile) {
    if (!tile) {
        return;
    }

    for (int i = 0; i < TILE_MIPMAP_LEVELS; i++) {
        if (tile->mipmaps[i]) {
            cairo_surface_destroy(tile->mipmaps[i]);
            tile->mipmaps[i] = NULL;
        }
    }
    tile->mipmaps_dirty = TRUE;
}

/**
 * Generate mipmaps for a tile
 * Creates downscaled versions at 50%, 25%, 12.5%, 6.25%
 */
gboolean tile_generate_mipmaps(Tile* tile) {
    cairo_surface_t* source;
    cairo_t* cr;
    int level;
    gdouble scale;
    gint src_w, src_h;
    gint mip_w, mip_h;

    if (!tile || !tile->surface) {
        return FALSE;
    }

    /* Get source dimensions */
    src_w = tile->w;
    src_h = tile->h;

    /* Don't generate mipmaps for very small tiles */
    if (src_w < 4 || src_h < 4) {
        tile->mipmaps_dirty = FALSE;
        return TRUE;
    }

    /* Free existing mipmaps before regenerating */
    tile_free_mipmaps(tile);

    /* Generate each mipmap level */
    source = tile->surface;
    scale = 0.5; /* Start at 50% */

    for (level = 0; level < TILE_MIPMAP_LEVELS; level++) {
        /* Calculate mipmap dimensions */
        mip_w = (gint)(src_w * scale + 0.5);
        mip_h = (gint)(src_h * scale + 0.5);

        /* Stop if mipmap would be too small */
        if (mip_w < 2 || mip_h < 2) {
            break;
        }

        /* Create mipmap surface */
        tile->mipmaps[level] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, mip_w, mip_h);
        if (cairo_surface_status(tile->mipmaps[level]) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(tile->mipmaps[level]);
            tile->mipmaps[level] = NULL;
            break;
        }

        /* Scale from original tile surface (not cascaded) for best quality */
        cr = cairo_create(tile->mipmaps[level]);

        /* Use bilinear filtering for smooth downscaling */
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, tile->surface, 0, 0);

        /* Set filter for quality downscaling */
        cairo_pattern_t* pattern = cairo_get_source(cr);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_BILINEAR);

        cairo_paint(cr);
        cairo_destroy(cr);

        cairo_surface_flush(tile->mipmaps[level]);

        /* Next level is half the previous */
        scale *= 0.5;
    }

    tile->mipmaps_dirty = FALSE;
    return TRUE;
}

/**
 * Get the appropriate mipmap surface for a given zoom factor
 * Returns the best matching mipmap or the full tile surface
 */
cairo_surface_t* tile_get_mipmap_for_zoom(Tile* tile, gdouble zoom_factor, gdouble* out_scale) {
    int level;
    gdouble level_scales[TILE_MIPMAP_LEVELS] = {0.5, 0.25, 0.125, 0.0625};

    if (!tile || !tile->surface) {
        if (out_scale) *out_scale = 1.0;
        return NULL;
    }

    /* For zoom >= 1.0 or no mipmaps, use full resolution */
    if (zoom_factor >= 1.0 || tile->mipmaps_dirty) {
        if (out_scale) *out_scale = 1.0;
        return tile->surface;
    }

    /* Find the best mipmap level for this zoom.
     * We want the SMALLEST mipmap that's >= zoom so we only DOWNSAMPLE, never upscale.
     * Upscaling mipmaps causes visible gaps between tiles due to interpolation artifacts.
     * Iterate from smallest (level 3 = 6.25%) to largest (level 0 = 50%)
     * and return the first one where mipmap_scale >= zoom. */
    for (level = TILE_MIPMAP_LEVELS - 1; level >= 0; level--) {
        if (tile->mipmaps[level] && level_scales[level] >= zoom_factor) {
            if (out_scale) *out_scale = level_scales[level];
            return tile->mipmaps[level];
        }
    }

    /* No suitable mipmap found (zoom > 0.5), use full resolution */
    if (out_scale) *out_scale = 1.0;
    return tile->surface;
}

/* ============================================================================
 * Layer-Tile Intersection Cache Implementation
 * ============================================================================ */

/**
 * Compute the intersection between a layer and a tile.
 * Internal helper function that always computes (doesn't check cache).
 */
static void compute_layer_tile_intersection(Tile* tile, ImageLayer* layer,
                                            LayerTileIntersection* result) {
    gint layer_x, layer_y, layer_right, layer_bottom;
    gint tile_right, tile_bottom;
    
    if (!tile || !layer || !result) {
        if (result) {
            result->valid = FALSE;
            result->intersects = FALSE;
        }
        return;
    }
    
    /* Initialize result */
    result->valid = TRUE;
    result->cached_layer_offset_x = layer->offset_x;
    result->cached_layer_offset_y = layer->offset_y;
    result->cached_layer_width = layer->width;
    result->cached_layer_height = layer->height;
    
    /* Calculate layer bounds in document coordinates */
    layer_x = layer->offset_x;
    layer_y = layer->offset_y;
    layer_right = layer_x + layer->width;
    layer_bottom = layer_y + layer->height;
    
    /* Calculate tile bounds */
    tile_right = tile->px + tile->w;
    tile_bottom = tile->py + tile->h;
    
    /* Calculate intersection */
    result->intersect_left = (layer_x > tile->px) ? layer_x : tile->px;
    result->intersect_top = (layer_y > tile->py) ? layer_y : tile->py;
    result->intersect_right = (layer_right < tile_right) ? layer_right : tile_right;
    result->intersect_bottom = (layer_bottom < tile_bottom) ? layer_bottom : tile_bottom;
    
    /* Check if there's a valid intersection */
    if (result->intersect_left >= result->intersect_right ||
        result->intersect_top >= result->intersect_bottom) {
        result->intersects = FALSE;
        return;
    }
    
    result->intersects = TRUE;
    
    /* Calculate intersection in tile-local coordinates */
    result->tile_local_x = result->intersect_left - tile->px;
    result->tile_local_y = result->intersect_top - tile->py;
    result->tile_local_w = result->intersect_right - result->intersect_left;
    result->tile_local_h = result->intersect_bottom - result->intersect_top;
    
    /* Calculate source region in layer coordinates */
    result->src_x = result->intersect_left - layer_x;
    result->src_y = result->intersect_top - layer_y;
    result->src_width = result->tile_local_w;
    result->src_height = result->tile_local_h;
}

/**
 * Check if a cached intersection is still valid for the given layer.
 */
static gboolean is_intersection_cache_valid(const LayerTileIntersection* cached,
                                            const ImageLayer* layer) {
    if (!cached || !cached->valid || !layer) {
        return FALSE;
    }
    
    /* Check if layer position/size has changed since caching */
    return (cached->cached_layer_offset_x == layer->offset_x &&
            cached->cached_layer_offset_y == layer->offset_y &&
            cached->cached_layer_width == layer->width &&
            cached->cached_layer_height == layer->height);
}

/**
 * Get or compute the intersection between a layer and a tile.
 * Returns cached result if valid, otherwise computes and caches it.
 */
const LayerTileIntersection* tile_get_layer_intersection(Tile* tile, ImageLayer* layer) {
    LayerTileIntersection* cached;
    
    if (!tile || !layer) {
        return NULL;
    }
    
    /* Ensure cache exists */
    if (!tile->layer_intersection_cache) {
        tile->layer_intersection_cache = g_hash_table_new_full(
            g_direct_hash, g_direct_equal, NULL, g_free);
    }
    
    /* Look up in cache */
    cached = (LayerTileIntersection*)g_hash_table_lookup(
        tile->layer_intersection_cache, layer);
    
    /* Check if cached value is still valid */
    if (cached && is_intersection_cache_valid(cached, layer)) {
        return cached;
    }
    
    /* Need to compute (either not cached or invalidated) */
    if (!cached) {
        cached = g_new(LayerTileIntersection, 1);
        g_hash_table_insert(tile->layer_intersection_cache, layer, cached);
    }
    
    compute_layer_tile_intersection(tile, layer, cached);
    return cached;
}

/**
 * Invalidate all layer intersection cache entries for a tile.
 */
void tile_invalidate_intersection_cache(Tile* tile) {
    if (!tile || !tile->layer_intersection_cache) {
        return;
    }
    
    g_hash_table_remove_all(tile->layer_intersection_cache);
}

/**
 * Invalidate intersection cache entries for a specific layer across all tiles.
 */
void tile_grid_invalidate_layer_cache(TileGrid* grid, ImageLayer* layer) {
    gint x, y;
    
    if (!grid || !layer) {
        return;
    }
    
    for (y = 0; y < grid->tiles_y; y++) {
        for (x = 0; x < grid->tiles_x; x++) {
            Tile* tile = &grid->tiles[y][x];
            if (tile->layer_intersection_cache) {
                g_hash_table_remove(tile->layer_intersection_cache, layer);
            }
        }
    }
}

/**
 * Free the layer intersection cache for a tile.
 */
void tile_free_intersection_cache(Tile* tile) {
    if (!tile) {
        return;
    }
    
    if (tile->layer_intersection_cache) {
        g_hash_table_destroy(tile->layer_intersection_cache);
        tile->layer_intersection_cache = NULL;
    }
}

/* ============================================================================
 * Tile Snapshot Helpers
 * ============================================================================ */

/**
 * Tile snapshot helpers for delta-based undo system
 * These functions work with tile-sized regions of layer surfaces
 */

/**
 * Create a snapshot of a tile-sized region from a layer surface
 * Used for delta-based undo to capture "before" or "after" state of a modified region
 */
cairo_surface_t* tile_snapshot_create(cairo_surface_t* layer_surface,
                                      gint tile_x,
                                      gint tile_y,
                                      gint tile_size,
                                      guint layer_width,
                                      guint layer_height) {
    gint src_x, src_y;
    gint region_w, region_h;
    cairo_surface_t* snapshot;
    cairo_t* cr;
    cairo_format_t format;

    if (!layer_surface || tile_size <= 0) {
        return NULL;
    }

    /* Calculate source coordinates in layer space */
    src_x = tile_x * tile_size;
    src_y = tile_y * tile_size;

    /* Calculate actual region size (may be smaller at edges) */
    region_w = (src_x + tile_size > (gint)layer_width) ? ((gint)layer_width - src_x) : tile_size;
    region_h = (src_y + tile_size > (gint)layer_height) ? ((gint)layer_height - src_y) : tile_size;

    /* Bounds check */
    if (src_x < 0 || src_y < 0 || region_w <= 0 || region_h <= 0 ||
        src_x >= (gint)layer_width || src_y >= (gint)layer_height) {
        return NULL;
    }

    /* Get format from source surface */
    format = cairo_image_surface_get_format(layer_surface);
    if (format != CAIRO_FORMAT_ARGB32 && format != CAIRO_FORMAT_RGB24) {
        format = CAIRO_FORMAT_ARGB32; /* Default to ARGB32 */
    }

    /* Create snapshot surface with the same format */
    snapshot = cairo_image_surface_create(format, region_w, region_h);
    if (cairo_surface_status(snapshot) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(snapshot);
        return NULL;
    }

    /* Copy the region from layer surface to snapshot */
    cr = cairo_create(snapshot);
    if (!cr) {
        cairo_surface_destroy(snapshot);
        return NULL;
    }

    /* Set source to the layer surface, offset to the tile region */
    cairo_set_source_surface(cr, layer_surface, -src_x, -src_y);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Flush to ensure all operations are complete */
    cairo_surface_flush(snapshot);

    return snapshot;
}

/**
 * Apply a tile snapshot to a layer surface (restore region from snapshot)
 * Used for undo/redo operations to restore a tile-sized region
 */
gboolean tile_snapshot_apply(cairo_surface_t* layer_surface,
                             cairo_surface_t* snapshot,
                             gint tile_x,
                             gint tile_y,
                             gint tile_size,
                             guint layer_width,
                             guint layer_height) {
    gint dest_x, dest_y;
    gint snapshot_w, snapshot_h;
    cairo_t* cr;

    if (!layer_surface || !snapshot || tile_size <= 0) {
        return FALSE;
    }

    /* Calculate destination coordinates in layer space */
    dest_x = tile_x * tile_size;
    dest_y = tile_y * tile_size;

    /* Get snapshot dimensions */
    snapshot_w = cairo_image_surface_get_width(snapshot);
    snapshot_h = cairo_image_surface_get_height(snapshot);

    /* Bounds check */
    if (dest_x < 0 || dest_y < 0 ||
        dest_x >= (gint)layer_width || dest_y >= (gint)layer_height) {
        return FALSE;
    }

    /* Create Cairo context for layer surface */
    cr = cairo_create(layer_surface);
    if (!cr) {
        return FALSE;
    }

    /* Copy snapshot to the tile region in layer surface */
    cairo_set_source_surface(cr, snapshot, dest_x, dest_y);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    /* Paint only the valid region (may be smaller at edges) */
    cairo_rectangle(cr, dest_x, dest_y, snapshot_w, snapshot_h);
    cairo_fill(cr);

    cairo_destroy(cr);

    /* Flush to ensure all operations are complete */
    cairo_surface_flush(layer_surface);

    return TRUE;
}
