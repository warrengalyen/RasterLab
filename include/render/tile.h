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
 * Tile state machine for thread pool coordination
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

#endif /* TILE_H */
