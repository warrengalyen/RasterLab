/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "render/tile_worker.h"
#include "render/blend.h"
#include "render/compositor.h"
#include "render/gpu_compositor.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include <glib.h>
#include <string.h>
#if HAVE_LCMS2
#include "color_manager.h"
#include "debug_logger.h"
#endif

/**
 * Internal worker pool structure
 */
struct TileWorkerPool {
    GThreadPool* thread_pool;
    GQueue* pending_uploads; /* Tiles ready for Cairo surface upload */
    GMutex upload_mutex;
    guint num_workers;

    /* Viewport center for priority calculation (in document pixel coordinates) */
    gint viewport_center_x;
    gint viewport_center_y;
    GMutex viewport_mutex;
};

/**
 * Job data structure for thread pool with priority
 */
typedef struct {
    ImageDocument* doc;
    Tile* tile;
    gint tile_x;
    gint tile_y;
    gint priority;  /* Lower = higher priority (distance squared from viewport center) */
} WorkerJob;

/**
 * Composite tile pixels without Cairo
 * Worker threads call this to fill tile->pixel_buffer
 * NO CAIRO CALLS - only raw pixel operations
 * 
 * Supports all blend modes via SIMD-optimized implementations:
 * - Normal (OVER), Multiply, Screen, Overlay
 * - Darken, Lighten, Color Burn, Color Dodge
 * - Soft Light, Hard Light, Difference
 */
gboolean tile_worker_composite_pixels(ImageDocument* doc,
                                      Tile* tile,
                                      gint tile_x,
                                      gint tile_y) {
    GList* iter;
    ImageLayer* layer;
    guint32* tile_pixels;
    gint layer_x, layer_y, layer_right, layer_bottom;
    gint tile_right, tile_bottom;
    gint intersect_left, intersect_top, intersect_right, intersect_bottom;
    gint src_x, src_y, src_width, src_height;
    gboolean is_first_visible_layer = TRUE;

    if (!doc || !tile || !tile->pixel_buffer) {
        return FALSE;
    }

    tile_pixels = (guint32*)tile->pixel_buffer;
    tile_right = tile->px + tile->w;
    tile_bottom = tile->py + tile->h;

    /* Clear tile to transparent */
    memset(tile->pixel_buffer, 0, tile->stride * tile->h);

    /* Safety check */
    if (!doc->layers) {
        return TRUE; /* Empty tile is valid */
    }

    /* Occlusion culling: scan top-to-bottom (last → first) to find the lowest
     * fully-opaque NORMAL-blend layer that completely covers this tile.  All
     * layers below it are invisible in the final composite so we can skip them. */
    GList* start_layer = doc->layers;
    for (iter = g_list_last(doc->layers); iter; iter = iter->prev) {
        layer = (ImageLayer*)iter->data;
        if (!layer || !layer->visible || !layer->surface ||
            layer->layer_type == LAYER_TYPE_TEXT)
            continue;
        if (layer->opacity < 1.0 || layer->blend_mode != BLEND_MODE_NORMAL)
            continue;
        layer_x = layer->offset_x;
        layer_y = layer->offset_y;
        layer_right = layer_x + (gint)layer->width;
        layer_bottom = layer_y + (gint)layer->height;
        if (layer_x <= tile->px && layer_y <= tile->py &&
            layer_right >= tile_right && layer_bottom >= tile_bottom) {
            start_layer = iter;
            break;
        }
    }

    /* Composite each visible layer that intersects tile */
    for (iter = start_layer; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Text (vector) layers are rendered directly via Pango by the display
         * pipeline and must NOT be baked into tiles.  Baking them would lock in
         * a stale position — text would appear at the old location during moves
         * and would be invisible on initial creation (surface is transparent).
         * The on-screen drawing pass handles text layers separately after tiles. */
        if (layer->layer_type == LAYER_TYPE_TEXT) {
            continue;
        }

        /* Calculate layer bounds in document coordinates */
        layer_x = layer->offset_x;
        layer_y = layer->offset_y;
        layer_right = layer_x + layer->width;
        layer_bottom = layer_y + layer->height;

        /* Check if layer intersects tile */
        intersect_left = (layer_x > tile->px) ? layer_x : tile->px;
        intersect_top = (layer_y > tile->py) ? layer_y : tile->py;
        intersect_right = (layer_right < tile_right) ? layer_right : tile_right;
        intersect_bottom = (layer_bottom < tile_bottom) ? layer_bottom : tile_bottom;

        if (intersect_left >= intersect_right || intersect_top >= intersect_bottom) {
            continue; /* No intersection */
        }

        /* Calculate source region in layer coordinates */
        src_x = intersect_left - layer_x;
        src_y = intersect_top - layer_y;
        src_width = intersect_right - intersect_left;
        src_height = intersect_bottom - intersect_top;

        /* Get layer surface and its actual dimensions before clamping */
        cairo_surface_t* layer_surface = layer->surface;
        if (!layer_surface) {
            continue;
        }
        cairo_surface_flush(layer_surface);
        gint surface_w = cairo_image_surface_get_width(layer_surface);
        gint surface_h = cairo_image_surface_get_height(layer_surface);
        if (surface_w <= 0 || surface_h <= 0) {
            continue;
        }

        /* Clamp to surface bounds - prevents reading past valid pixels or into
         * stride padding. Required for Screen, Exclusion, Difference, Subtract,
         * Darker/Lighter Color which show vertical artifacts when reading OOB. */
        if (src_x < 0) { src_width += src_x; src_x = 0; }
        if (src_y < 0) { src_height += src_y; src_y = 0; }
        if (src_x + src_width > surface_w)
            src_width = surface_w - src_x;
        if (src_y + src_height > surface_h)
            src_height = surface_h - src_y;
        if (src_width <= 0 || src_height <= 0)
            continue;

        guint8* layer_data = cairo_image_surface_get_data(layer_surface);
        gint layer_stride = cairo_image_surface_get_stride(layer_surface);

        if (!layer_data) {
            continue;
        }

        /* Clamp to stride: never read past the row (stride may have padding) */
        gint max_col = layer_stride / 4;
        if (src_x + src_width > max_col)
            src_width = max_col - src_x;
        if (src_width <= 0)
            continue;

        /* Get layer opacity (0.0 - 1.0), convert to 0-255 */
        guint8 layer_opacity = (guint8)(layer->opacity * 255.0);
        if (layer_opacity == 0) {
            continue;
        }

        /* Determine blend mode to use
         * First visible layer always uses OVER to establish the base
         * (same behavior as Cairo compositor) */
        BlendMode effective_blend_mode;
        if (is_first_visible_layer) {
            effective_blend_mode = BLEND_MODE_NORMAL;
            is_first_visible_layer = FALSE;
        } else {
            effective_blend_mode = layer->blend_mode;
        }

        /* Composite layer pixels into tile pixels using SIMD
         * This processes 4 pixels at a time with SSE2 intrinsics
         * All blend modes are SIMD-optimized for maximum performance */
        for (gint y = 0; y < src_height; y++) {
            guint32* layer_row = (guint32*)(layer_data + (src_y + y) * layer_stride) + src_x;
            guint32* tile_row = (guint32*)(tile->pixel_buffer + (intersect_top - tile->py + y) * tile->stride) + (intersect_left - tile->px);

            /* Use blend-mode-aware compositing function
             * Pass document coordinates (intersect_left, intersect_top + y) for Dissolve dithering */
            blend_composite_row(layer_row, tile_row, src_width, intersect_left, intersect_top + y, layer_opacity, effective_blend_mode);
        }
    }

    return TRUE;
}

/**
 * Composite tile pixels using GPU acceleration
 * This must be called from the main thread (OpenGL context is thread-bound)
 * Falls back to CPU compositing if GPU is not available.
 * 
 * GPU now supports all blend modes via ping-pong rendering.
 * 
 * @param doc Document containing layers and GPU compositor
 * @param tile Tile with allocated pixel_buffer
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @return TRUE if GPU compositing was used, FALSE if fell back to CPU
 */
gboolean tile_worker_composite_pixels_gpu(ImageDocument* doc,
                                          Tile* tile,
                                          gint tile_x,
                                          gint tile_y) {
    if (!doc || !tile || !tile->pixel_buffer) {
        return FALSE;
    }

    /* Check if GPU compositor is available */
    if (doc->gpu_compositor) {
        if (gpu_compositor_is_ready(doc->gpu_compositor)) {
            /* Try GPU compositing */
            if (gpu_compositor_composite_tile(doc->gpu_compositor, doc, tile, tile_x, tile_y)) {
                return TRUE; /* GPU compositing successful */
            }
            /* GPU compositing failed, fall through to CPU */
            debug_log("WRN", "GPU compositing failed for tile (%d, %d), falling back to CPU", tile_x, tile_y);
        } else {
            g_debug("GPU compositor exists but is not ready");
        }
    }

    /* Fall back to CPU compositing */
    tile_worker_composite_pixels(doc, tile, tile_x, tile_y);
    return FALSE;
}

/**
 * Check if GPU compositing is available for a document
 * @param doc Document to check
 * @return TRUE if GPU compositing can be used
 */
gboolean tile_worker_has_gpu_compositor(ImageDocument* doc) {
    return doc && doc->gpu_compositor && gpu_compositor_is_ready(doc->gpu_compositor);
}

/**
 * Priority comparison function for thread pool
 * Jobs with lower priority value (closer to viewport center) are processed first
 * Returns: negative if a < b (a should come first), positive if a > b, 0 if equal
 */
static gint tile_worker_priority_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
    const WorkerJob* job_a = (const WorkerJob*)a;
    const WorkerJob* job_b = (const WorkerJob*)b;

    /* Lower priority value = higher priority (process first) */
    if (job_a->priority < job_b->priority) {
        return -1;
    } else if (job_a->priority > job_b->priority) {
        return 1;
    }
    return 0;
}

/**
 * Worker thread main function
 */
static void tile_worker_thread_func(gpointer data, gpointer user_data) {
    WorkerJob* job = (WorkerJob*)data;
    TileWorkerPool* pool = (TileWorkerPool*)user_data;

    if (!job || !pool) {
        g_free(job);
        return;
    }

    /* Check generation_id BEFORE compositing to avoid wasted work */
    guint expected_gen = job->tile->generation_id;

    /* Composite pixels WITHOUT touching Cairo */
    if (job->tile && job->tile->pixel_buffer) {
        tile_worker_composite_pixels(job->doc, job->tile, job->tile_x, job->tile_y);

        /* Lock and add to pending uploads queue (only if not stale) */
        g_mutex_lock(&pool->upload_mutex);

        /* Check generation again - may have changed during compositing */
        if (job->tile->generation_id == expected_gen) {
            job->tile->pending_upload = TRUE;
            job->tile->state = TILE_CLEAN; /* Mark as ready */
            g_queue_push_tail(pool->pending_uploads, job->tile);
        } else {
            /* Tile was invalidated during compositing, discard result */
            job->tile->state = TILE_CLEAN; /* Reset state for re-queue */
        }

        g_mutex_unlock(&pool->upload_mutex);
    }

    g_free(job);
}

/**
 * Create worker pool
 */
TileWorkerPool* tile_worker_pool_create(guint num_workers) {
    TileWorkerPool* pool;
    gint cpu_count;

    pool = (TileWorkerPool*)g_malloc0(sizeof(TileWorkerPool));
    if (!pool) {
        return NULL;
    }

    /* Get CPU count and clamp to valid range */
    cpu_count = (gint)g_get_num_processors();
    if (num_workers == 0) {
        num_workers = 4; /* Default to 4 workers */
    }
    if (num_workers < 1) {
        num_workers = 1;
    }
    if (num_workers > (guint)cpu_count) {
        num_workers = (guint)cpu_count;
    }

    pool->num_workers = num_workers;
    pool->pending_uploads = g_queue_new();

    /* Initialize viewport center to 0,0 (will be updated when drawing) */
    pool->viewport_center_x = 0;
    pool->viewport_center_y = 0;

    g_mutex_init(&pool->upload_mutex);
    g_mutex_init(&pool->viewport_mutex);

    /* Create thread pool with pool as user_data so workers can access queue */
    pool->thread_pool = g_thread_pool_new(tile_worker_thread_func,
                                          pool, /* user_data - pass pool for queue access */
                                          num_workers,
                                          FALSE, /* exclusive (don't wait for all) */
                                          NULL); /* error */

    if (!pool->thread_pool) {
        g_queue_free(pool->pending_uploads);
        g_mutex_clear(&pool->upload_mutex);
        g_mutex_clear(&pool->viewport_mutex);
        g_free(pool);
        return NULL;
    }

    /* Set priority sort function - tiles closer to viewport center are processed first */
    g_thread_pool_set_sort_function(pool->thread_pool,
                                    tile_worker_priority_compare,
                                    NULL);

    debug_log("DBG", "Created tile worker pool with %u threads (priority queue enabled)", num_workers);

    return pool;
}

/**
 * Destroy worker pool
 */
void tile_worker_pool_destroy(TileWorkerPool* pool) {
    if (!pool) {
        return;
    }

    /* Wait for all pending jobs */
    if (pool->thread_pool) {
        g_thread_pool_free(pool->thread_pool, TRUE, TRUE);
    }

    /* Free pending uploads queue */
    if (pool->pending_uploads) {
        g_queue_free(pool->pending_uploads);
    }

    g_mutex_clear(&pool->upload_mutex);
    g_mutex_clear(&pool->viewport_mutex);
    g_free(pool);
}

/**
 * Set the viewport center for priority calculation
 * Tiles closer to this point will be composited first
 * @param pool Worker pool
 * @param center_x Viewport center X in document pixel coordinates
 * @param center_y Viewport center Y in document pixel coordinates
 */
void tile_worker_pool_set_viewport_center(TileWorkerPool* pool,
                                          gint center_x,
                                          gint center_y) {
    if (!pool) {
        return;
    }

    g_mutex_lock(&pool->viewport_mutex);
    pool->viewport_center_x = center_x;
    pool->viewport_center_y = center_y;
    g_mutex_unlock(&pool->viewport_mutex);
}

/**
 * Calculate priority (distance squared from viewport center)
 * Lower value = higher priority
 */
static gint calculate_tile_priority(TileWorkerPool* pool, Tile* tile) {
    gint tile_center_x, tile_center_y;
    gint dx, dy;
    gint viewport_cx, viewport_cy;

    /* Get tile center in document coordinates */
    tile_center_x = tile->px + tile->w / 2;
    tile_center_y = tile->py + tile->h / 2;

    /* Get viewport center (lock for thread safety) */
    g_mutex_lock(&pool->viewport_mutex);
    viewport_cx = pool->viewport_center_x;
    viewport_cy = pool->viewport_center_y;
    g_mutex_unlock(&pool->viewport_mutex);

    /* Calculate distance squared (avoid sqrt for performance) */
    dx = tile_center_x - viewport_cx;
    dy = tile_center_y - viewport_cy;

    /* Use distance squared as priority - closer tiles have lower values */
    /* Clamp to avoid overflow for very distant tiles */
    gint64 dist_sq = (gint64)dx * dx + (gint64)dy * dy;
    if (dist_sq > G_MAXINT) {
        return G_MAXINT;
    }
    return (gint)dist_sq;
}

/**
 * Enqueue a tile for compositing with priority
 * Tiles closer to viewport center are processed first
 */
gboolean tile_worker_pool_enqueue(TileWorkerPool* pool,
                                  ImageDocument* doc,
                                  Tile* tile,
                                  gint tile_x,
                                  gint tile_y) {
    WorkerJob* job;

    if (!pool || !pool->thread_pool || !tile) {
        return FALSE;
    }

    /* Skip if tile is already queued or being rendered */
    if (tile->state == TILE_QUEUED || tile->state == TILE_RENDERING) {
        return FALSE; /* Already in pipeline */
    }

    /* Skip if tile is not dirty */
    if (!tile->dirty) {
        return FALSE;
    }

    /* Allocate pixel buffer if needed */
    if (!tile->pixel_buffer) {
        tile->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, tile->w);
        tile->pixel_buffer = (uint8_t*)g_malloc0(tile->stride * tile->h);

        if (!tile->pixel_buffer) {
            debug_log("WRN", "Failed to allocate pixel buffer for tile");
            return FALSE;
        }
    }

    job = (WorkerJob*)g_malloc(sizeof(WorkerJob));
    if (!job) {
        return FALSE;
    }

    job->doc = doc;
    job->tile = tile;
    job->tile_x = tile_x;
    job->tile_y = tile_y;

    /* Calculate priority based on distance from viewport center */
    job->priority = calculate_tile_priority(pool, tile);

    /* Mark tile as queued and increment generation */
    tile->state = TILE_QUEUED;
    tile->generation_id++;

    g_thread_pool_push(pool->thread_pool, job, NULL);

    return TRUE;
}

/**
 * Get pending job count
 */
guint tile_worker_pool_get_pending(TileWorkerPool* pool) {
    if (!pool || !pool->thread_pool) {
        return 0;
    }

    return g_thread_pool_get_num_threads(pool->thread_pool);
}

/**
 * Process completed tiles and upload to Cairo surfaces
 * MAIN THREAD ONLY - this updates Cairo surfaces
 * @param pool Worker pool
 * @return Number of tiles uploaded to Cairo
 */
guint tile_worker_pool_process_uploads(TileWorkerPool* pool, void* display_transform) {
    Tile* tile;
    guint count = 0;
    GQueue* to_process;

    if (!pool) {
        return 0;
    }

    /* Quick check without lock - if empty, nothing to do */
    g_mutex_lock(&pool->upload_mutex);
    if (g_queue_is_empty(pool->pending_uploads)) {
        g_mutex_unlock(&pool->upload_mutex);
        return 0;
    }

    /* Swap queues to minimize lock time - this is the key optimization */
    to_process = pool->pending_uploads;
    pool->pending_uploads = g_queue_new();
    g_mutex_unlock(&pool->upload_mutex);

    /* Process all tiles outside the lock */
    while (!g_queue_is_empty(to_process)) {
        tile = (Tile*)g_queue_pop_head(to_process);

        if (!tile || !tile->pixel_buffer || !tile->pending_upload) {
            continue;
        }

        /* Destroy old Cairo surface if it exists */
        if (tile->surface) {
            cairo_surface_destroy(tile->surface);
            tile->surface = NULL;
        }

#if HAVE_LCMS2
        if (display_transform) {
            cm_apply_transform_argb32((ColorTransform*)display_transform,
                (uint8_t*)tile->pixel_buffer,
                (size_t)(tile->w * tile->h));
        }
#endif

        /* Create new Cairo surface from pixel buffer (main thread safe)
         * Note: cairo_image_surface_create_for_data does NOT copy the data,
         * it uses the provided buffer directly, so we must keep pixel_buffer alive */
        tile->surface = cairo_image_surface_create_for_data(
            tile->pixel_buffer,
            CAIRO_FORMAT_ARGB32,
            tile->w,
            tile->h,
            tile->stride);

        if (cairo_surface_status(tile->surface) == CAIRO_STATUS_SUCCESS) {
            tile->dirty = FALSE;
            tile->pending_upload = FALSE;
            count++;
        } else {
            debug_log("WRN", "Failed to create Cairo surface for tile (%d, %d)", tile->x, tile->y);
            cairo_surface_destroy(tile->surface);
            tile->surface = NULL;
        }
    }

    g_queue_free(to_process);

    return count;
}
