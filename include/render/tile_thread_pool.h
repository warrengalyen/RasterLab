#ifndef TILE_THREAD_POOL_H
#define TILE_THREAD_POOL_H

#include "document.h"
#include "render/tile.h"
#include <cairo/cairo.h>
#include <glib.h>

G_BEGIN_DECLS

/**
 * Thread pool for asynchronous tile recomposition
 *
 * Manages a fixed number of worker threads that recomposite dirty tiles
 * independently of the GTK UI thread. Completed tiles are queued for
 * swapping on the main thread.
 */

/**
 * Tile job for worker threads to process
 */
typedef struct {
    ImageDocument* doc;
    Tile* tile;
    gint tile_x;         /* Tile grid X coordinate */
    gint tile_y;         /* Tile grid Y coordinate */
    guint generation_id; /* Prevents stale work from overwriting newer tiles */
} TileJob;

/**
 * Completed tile result ready to be swapped on main thread
 */
typedef struct {
    cairo_surface_t* surface; /* New composited surface */
    Tile* tile;
    gint tile_x;
    gint tile_y;
    guint generation_id; /* Matches tile generation at time of completion */
} CompletedTile;

/**
 * Thread pool state machine
 */
typedef enum {
    TILE_POOL_RUNNING,
    TILE_POOL_SHUTDOWN
} TilePoolState;

/**
 * Opaque thread pool handle
 */
typedef struct TileThreadPool TileThreadPool;

/**
 * Create and start a new tile thread pool
 * @param num_workers Number of worker threads (0 = auto-detect CPU count - 1)
 * @return New thread pool, or NULL on error. Caller must call tile_thread_pool_destroy().
 */
TileThreadPool* tile_thread_pool_create(guint num_workers);

/**
 * Destroy thread pool and stop all workers
 * Blocks until all workers complete current jobs and shutdown.
 * @param pool Thread pool to destroy
 */
void tile_thread_pool_destroy(TileThreadPool* pool);

/**
 * Enqueue a tile for recomposition
 * Thread-safe. Can be called from any thread.
 * @param pool Thread pool
 * @param doc Document containing tile
 * @param tile Tile to recomposite
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @param generation_id Current generation ID of tile (prevents stale results)
 * @return TRUE if queued successfully, FALSE on error or pool shutdown
 */
gboolean tile_thread_pool_enqueue_job(TileThreadPool* pool,
                                      ImageDocument* doc,
                                      Tile* tile,
                                      gint tile_x,
                                      gint tile_y,
                                      guint generation_id);

/**
 * Poll completed tiles from the result queue
 * Thread-safe. Should be called periodically from GTK main thread.
 * @param pool Thread pool
 * @param out_tile Output: completed tile result (caller must free the surface)
 * @return TRUE if a completed tile was returned, FALSE if queue empty
 */
gboolean tile_thread_pool_pop_completed(TileThreadPool* pool, CompletedTile* out_tile);

/**
 * Get number of pending jobs in queue
 * @param pool Thread pool
 * @return Number of jobs awaiting worker threads
 */
guint tile_thread_pool_get_pending_jobs(TileThreadPool* pool);

/**
 * Get number of completed tiles ready for main thread
 * @param pool Thread pool
 * @return Number of completed tiles in result queue
 */
guint tile_thread_pool_get_completed_count(TileThreadPool* pool);

/**
 * Request graceful shutdown of all workers
 * Does not block. Use tile_thread_pool_destroy() to wait for completion.
 * @param pool Thread pool
 */
void tile_thread_pool_request_shutdown(TileThreadPool* pool);

G_END_DECLS

#endif /* TILE_THREAD_POOL_H */
