#ifndef TILE_WORKER_H
#define TILE_WORKER_H

#include "document.h"
#include "render/tile.h"
#include <glib.h>

G_BEGIN_DECLS

/**
 * Cairo-safe tile worker thread pool
 *
 * ARCHITECTURE:
 * - Worker threads ONLY operate on raw pixel buffers (uint8_t arrays)
 * - Worker threads NEVER create or use Cairo surfaces
 * - Main thread ONLY creates and uses Cairo surfaces
 * - Pixel buffers are uploaded to Cairo surfaces on main thread
 *
 * This design ensures Cairo thread-safety while allowing parallel tile compositing.
 */

/**
 * Opaque worker pool handle
 */
typedef struct TileWorkerPool TileWorkerPool;

/**
 * Worker job for compositing a single tile
 */
typedef struct {
    ImageDocument* doc;
    Tile* tile;
    gint tile_x;
    gint tile_y;
} TileWorkerJob;

/**
 * Create a worker thread pool
 * Workers will only composite into pixel buffers, never touch Cairo
 * @param num_workers Number of worker threads (0 = auto-detect, capped at 4)
 * @return Worker pool handle or NULL on error
 */
TileWorkerPool* tile_worker_pool_create(guint num_workers);

/**
 * Destroy worker thread pool and wait for all pending jobs
 * @param pool Worker pool handle
 */
void tile_worker_pool_destroy(TileWorkerPool* pool);

/**
 * Set the viewport center for priority calculation
 * Tiles closer to this point will be composited first (priority queue)
 * Call this before enqueueing tiles for best results
 * @param pool Worker pool
 * @param center_x Viewport center X in document pixel coordinates
 * @param center_y Viewport center Y in document pixel coordinates
 */
void tile_worker_pool_set_viewport_center(TileWorkerPool* pool,
                                          gint center_x,
                                          gint center_y);

/**
 * Enqueue a tile for background compositing with priority
 * Tiles closer to viewport center (set via tile_worker_pool_set_viewport_center)
 * are processed first. Thread-safe - can be called from any thread.
 * @param pool Worker pool
 * @param doc Document
 * @param tile Tile with allocated pixel_buffer
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @return TRUE if enqueued, FALSE if pool shutdown or tile already queued
 */
gboolean tile_worker_pool_enqueue(TileWorkerPool* pool,
                                  ImageDocument* doc,
                                  Tile* tile,
                                  gint tile_x,
                                  gint tile_y);

/**
 * Get number of pending jobs
 * @param pool Worker pool
 * @return Number of jobs waiting for workers
 */
guint tile_worker_pool_get_pending(TileWorkerPool* pool);

/**
 * Process completed tiles and upload to Cairo surfaces
 * MAIN THREAD ONLY - updates Cairo surfaces for tiles with pending_upload=TRUE
 * Should be called from GTK main loop
 * @param pool Worker pool
 * @param display_transform Optional display color transform (ColorTransform*), or NULL.
 *        When non-NULL, applied to each tile's pixel buffer before creating the surface.
 * @return Number of tiles uploaded to Cairo
 */
guint tile_worker_pool_process_uploads(TileWorkerPool* pool, void* display_transform);

/**
 * Composite a tile's pixel buffer (worker function)
 * This function runs in worker threads and MUST NOT use Cairo
 * @param doc Document
 * @param tile Tile with pixel_buffer allocated
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @return TRUE if successful
 */
gboolean tile_worker_composite_pixels(ImageDocument* doc,
                                      Tile* tile,
                                      gint tile_x,
                                      gint tile_y);

/**
 * Composite a tile's pixel buffer using GPU acceleration
 * MAIN THREAD ONLY - OpenGL context is thread-bound
 * Falls back to CPU compositing if GPU is not available
 * @param doc Document with GPU compositor
 * @param tile Tile with pixel_buffer allocated
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @return TRUE if GPU was used, FALSE if fell back to CPU
 */
gboolean tile_worker_composite_pixels_gpu(ImageDocument* doc,
                                          Tile* tile,
                                          gint tile_x,
                                          gint tile_y);

/**
 * Check if GPU compositing is available for a document
 * @param doc Document to check
 * @return TRUE if GPU compositing can be used
 */
gboolean tile_worker_has_gpu_compositor(ImageDocument* doc);

G_END_DECLS

#endif /* TILE_WORKER_H */
