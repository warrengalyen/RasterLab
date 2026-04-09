/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TILE_H
#define TILE_H

#include "document.h"
#include <cairo/cairo.h>
#include <glib.h>

/**
 * Tile-based rendering system for large image performance
 *
 * This replaces full-surface rendering with a tile grid where each tile
 * is independently composited and cached. Only dirty tiles are recomposited,
 * dramatically improving performance for large images (2000x2000+).
 */

/**
 * Maximum number of tile mipmap levels
 * Level 0 = full resolution (the main tile surface)
 * Level 1 = 50% (half size)
 * Level 2 = 25% (quarter size)
 * Level 3 = 12.5% (eighth size)
 */
#define TILE_MIPMAP_LEVELS 4

/**
 * Cached layer-tile intersection geometry
 * Avoids recomputing intersection bounds every frame during compositing.
 * Intersection is computed once and cached until layer position/size changes.
 */
typedef struct {
    gboolean valid;           /* Is this cache entry valid? */
    gboolean intersects;      /* Does layer intersect this tile? */
    
    /* Intersection rectangle in document coordinates */
    gint intersect_left;
    gint intersect_top;
    gint intersect_right;
    gint intersect_bottom;
    
    /* Intersection rectangle in tile-local coordinates */
    gint tile_local_x;
    gint tile_local_y;
    gint tile_local_w;
    gint tile_local_h;
    
    /* Source region in layer coordinates */
    gint src_x;
    gint src_y;
    gint src_width;
    gint src_height;
    
    /* Cache validation - stores layer position/size when cached */
    gint cached_layer_offset_x;
    gint cached_layer_offset_y;
    guint cached_layer_width;
    guint cached_layer_height;
} LayerTileIntersection;

/**
 * Tile state for thread pool coordination
 */
typedef enum {
    TILE_CLEAN,    /* Tile surface is valid and up-to-date */
    TILE_QUEUED,   /* Tile has been queued for worker thread */
    TILE_RENDERING /* Worker thread is currently compositing this tile */
} TileState;

/**
 * A single tile in the tile grid
 * Each tile caches a composited region of the document
 */
typedef struct {
    gint x, y;                /* Tile index in grid (tile coordinates) */
    gint px, py;              /* Pixel offset in document (document coordinates) */
    gint w, h;                /* Tile size in pixels (usually constant except edge tiles) */
    cairo_surface_t* surface; /* Cached composited tile (ARGB32) - MAIN THREAD ONLY */
    gboolean dirty;           /* Whether the tile must be recomposited */

    /* Cairo-safe worker thread support */
    uint8_t* pixel_buffer;   /* Raw ARGB32 pixel data - worker threads write to this */
    gint stride;             /* Stride for pixel_buffer */
    gboolean pending_upload; /* If TRUE, main thread should upload pixel_buffer to Cairo surface */

    /* Thread pool coordination (legacy - kept for compatibility) */
    TileState state;     /* Current state in render pipeline */
    guint generation_id; /* Incremented when tile marked dirty; prevents stale results */

    /* Pre-computed mipmaps for fast zooming
     * mipmap[0] = 50% (half size)
     * mipmap[1] = 25% (quarter size)
     * mipmap[2] = 12.5% (eighth size)
     * mipmap[3] = 6.25% (sixteenth size)
     * Generated after tile compositing for fast zoom-out rendering */
    cairo_surface_t* mipmaps[TILE_MIPMAP_LEVELS];
    gboolean mipmaps_dirty; /* Whether mipmaps need regeneration */
    
    /* Layer-tile intersection cache
     * Maps ImageLayer* -> LayerTileIntersection*
     * Caches intersection geometry to avoid recomputing every frame */
    GHashTable* layer_intersection_cache;
} Tile;

/**
 * Tile grid managing all tiles for a document
 */
struct TileGrid {
    gint tile_size;        /* Size of each tile (e.g., 128 pixels) */
    gint tiles_x, tiles_y; /* Number of tiles in X and Y directions */
    Tile** tiles;          /* 2D array of tiles: tiles[y][x] */
};
typedef struct TileGrid TileGrid;

/**
 * Create a new tile grid for a document
 * @param width Document width in pixels
 * @param height Document height in pixels
 * @param tile_size Size of each tile (default 128)
 * @return New tile grid, or NULL on error. Caller must call tile_grid_free().
 */
TileGrid* tile_grid_create(gint width, gint height, gint tile_size);

/**
 * Free a tile grid and all its tiles
 * @param grid Tile grid to free
 */
void tile_grid_free(TileGrid* grid);

/**
 * Mark a specific tile as dirty
 * @param tile Tile to mark dirty
 */
void tile_mark_dirty(Tile* tile);

/**
 * Mark all tiles intersecting a rectangle as dirty
 * @param grid Tile grid
 * @param x Left edge of rectangle in document coordinates
 * @param y Top edge of rectangle in document coordinates
 * @param w Width of rectangle
 * @param h Height of rectangle
 */
void tile_grid_mark_rect_dirty(TileGrid* grid, gint x, gint y, gint w, gint h);

/**
 * Composite all dirty tiles in the grid
 * @param doc Document containing layers to composite
 * @param grid Tile grid to composite into
 * @return TRUE if successful, FALSE otherwise
 */
gboolean tile_grid_composite(ImageDocument* doc, TileGrid* grid);

/**
 * Get a tile at specific grid coordinates
 * @param grid Tile grid
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @return Tile at coordinates, or NULL if out of bounds
 */
Tile* tile_grid_get_tile(TileGrid* grid, gint tile_x, gint tile_y);

/**
 * Get tile grid coordinates from document pixel coordinates
 * @param grid Tile grid
 * @param px Pixel X coordinate
 * @param py Pixel Y coordinate
 * @param tile_x Output: tile X coordinate
 * @param tile_y Output: tile Y coordinate
 */
void tile_grid_pixel_to_tile(TileGrid* grid, gint px, gint py, gint* tile_x, gint* tile_y);

/**
 * Mark a tile as needing recomposition (thread pool version)
 * Increments generation_id to prevent stale results from overwriting newer work
 * @param tile Tile to mark dirty
 */
void tile_mark_dirty_for_thread_pool(Tile* tile);

/**
 * Apply a completed tile result from a worker thread
 * Should only be called from GTK main thread
 * @param tile Target tile to update
 * @param new_surface Composited surface from worker thread (ownership transferred)
 * @param generation_id Generation ID when work was queued
 * @return TRUE if result was applied, FALSE if stale
 */
gboolean tile_apply_completed_result(Tile* tile, cairo_surface_t* new_surface, guint generation_id);

/**
 * Generate mipmaps for a tile
 * Creates downscaled versions at 50%, 25%, 12.5%, 6.25% for fast zoom-out rendering.
 * Should be called after tile compositing completes.
 * @param tile Tile to generate mipmaps for
 * @return TRUE on success, FALSE on error
 */
gboolean tile_generate_mipmaps(Tile* tile);

/**
 * Free all mipmaps for a tile
 * @param tile Tile to free mipmaps for
 */
void tile_free_mipmaps(Tile* tile);

/**
 * Get the appropriate mipmap surface for a given zoom factor
 * @param tile Tile to get mipmap from
 * @param zoom_factor Zoom factor (1.0 = full resolution)
 * @param out_scale Output: scale factor of the returned mipmap (e.g., 0.5 for 50%)
 * @return Mipmap surface, or tile->surface if zoom >= 1.0 or mipmaps unavailable
 */
cairo_surface_t* tile_get_mipmap_for_zoom(Tile* tile, gdouble zoom_factor, gdouble* out_scale);

/* ============================================================================
 * Layer-Tile Intersection Cache
 * ============================================================================
 * Caches the intersection geometry between layers and tiles to avoid
 * recomputing bounds on every frame. Cache entries are automatically
 * invalidated when layer position or size changes.
 * ============================================================================ */

/**
 * Get or compute the intersection between a layer and a tile.
 * Returns cached result if valid, otherwise computes and caches it.
 * @param tile Tile to check intersection with
 * @param layer Layer to check intersection with
 * @return Pointer to cached intersection data (do not free)
 */
const LayerTileIntersection* tile_get_layer_intersection(Tile* tile, ImageLayer* layer);

/**
 * Invalidate all layer intersection cache entries for a tile.
 * Call when tile position/size changes (rare).
 * @param tile Tile to invalidate cache for
 */
void tile_invalidate_intersection_cache(Tile* tile);

/**
 * Invalidate intersection cache entries for a specific layer across all tiles.
 * Call when layer position, size, or visibility changes.
 * @param grid Tile grid containing all tiles
 * @param layer Layer whose cache entries should be invalidated
 */
void tile_grid_invalidate_layer_cache(TileGrid* grid, ImageLayer* layer);

/**
 * Free the layer intersection cache for a tile.
 * Called during tile cleanup.
 * @param tile Tile to free cache for
 */
void tile_free_intersection_cache(Tile* tile);

/**
 * Tile snapshot helpers for delta-based undo system
 * These functions work with tile-sized regions of layer surfaces
 */

/**
 * Create a snapshot of a tile-sized region from a layer surface
 * Used for delta-based undo to capture "before" or "after" state of a modified region
 * @param layer_surface The layer surface to snapshot from
 * @param tile_x Tile X coordinate (grid position)
 * @param tile_y Tile Y coordinate (grid position)
 * @param tile_size Size of each tile
 * @param layer_width Width of the layer (for bounds checking)
 * @param layer_height Height of the layer (for bounds checking)
 * @return New Cairo surface containing the snapshot, or NULL on error
 *         Caller must call cairo_surface_destroy() when done.
 */
cairo_surface_t* tile_snapshot_create(cairo_surface_t* layer_surface,
                                      gint tile_x,
                                      gint tile_y,
                                      gint tile_size,
                                      guint layer_width,
                                      guint layer_height);

/**
 * Apply a tile snapshot to a layer surface (restore region from snapshot)
 * Used for undo/redo operations to restore a tile-sized region
 * @param layer_surface The layer surface to restore to
 * @param snapshot The snapshot surface to restore from
 * @param tile_x Tile X coordinate (grid position)
 * @param tile_y Tile Y coordinate (grid position)
 * @param tile_size Size of each tile
 * @param layer_width Width of the layer (for bounds checking)
 * @param layer_height Height of the layer (for bounds checking)
 * @return TRUE on success, FALSE on error
 */
gboolean tile_snapshot_apply(cairo_surface_t* layer_surface,
                             cairo_surface_t* snapshot,
                             gint tile_x,
                             gint tile_y,
                             gint tile_size,
                             guint layer_width,
                             guint layer_height);

#endif /* TILE_H */
