#include "render/tile_worker.h"
#include "render/compositor.h"
#include "render/layer.h"
#include <glib.h>
#include <string.h>

/**
 * Internal worker pool structure
 */
struct TileWorkerPool {
    GThreadPool* thread_pool;
    GQueue* pending_uploads; /* Tiles ready for Cairo surface upload */
    GMutex upload_mutex;
    guint num_workers;
};

/**
 * Job data structure for thread pool
 */
typedef struct {
    ImageDocument* doc;
    Tile* tile;
    gint tile_x;
    gint tile_y;
} WorkerJob;

/**
 * Composite tile pixels without Cairo
 * Worker threads call this to fill tile->pixel_buffer
 * NO CAIRO CALLS - only raw pixel operations
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

    /* Composite each visible layer that intersects tile */
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

        /* Get layer pixel data */
        cairo_surface_t* layer_surface = layer->surface;
        if (!layer_surface) {
            continue;
        }

        guint8* layer_data = cairo_image_surface_get_data(layer_surface);
        gint layer_stride = cairo_image_surface_get_stride(layer_surface);

        if (!layer_data) {
            continue;
        }

        /* Composite layer pixels into tile pixels */
        for (gint y = 0; y < src_height; y++) {
            guint32* layer_row = (guint32*)(layer_data + (src_y + y) * layer_stride) + src_x;
            guint32* tile_row = (guint32*)(tile->pixel_buffer + (intersect_top - tile->py + y) * tile->stride) + (intersect_left - tile->px);

            for (gint x = 0; x < src_width; x++) {
                guint32 src_pixel = layer_row[x];
                guint32 dst_pixel = tile_row[x];

                /* Simple OVER blend for ARGB32 */
                guint8 src_a = (src_pixel >> 24) & 0xFF;
                guint8 src_r = (src_pixel >> 16) & 0xFF;
                guint8 src_g = (src_pixel >> 8) & 0xFF;
                guint8 src_b = src_pixel & 0xFF;

                guint8 dst_a = (dst_pixel >> 24) & 0xFF;
                guint8 dst_r = (dst_pixel >> 16) & 0xFF;
                guint8 dst_g = (dst_pixel >> 8) & 0xFF;
                guint8 dst_b = dst_pixel & 0xFF;

                /* Alpha blend */
                guint8 out_a = src_a + (guint8)(((guint32)dst_a * (255 - src_a)) / 255);
                guint8 out_r, out_g, out_b;

                if (out_a > 0) {
                    out_r = (guint8)(((guint32)src_r * src_a + (guint32)dst_r * dst_a * (255 - src_a) / 255) / out_a);
                    out_g = (guint8)(((guint32)src_g * src_a + (guint32)dst_g * dst_a * (255 - src_a) / 255) / out_a);
                    out_b = (guint8)(((guint32)src_b * src_a + (guint32)dst_b * dst_a * (255 - src_a) / 255) / out_a);
                } else {
                    out_r = out_g = out_b = 0;
                }

                tile_row[x] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }

    return TRUE;
}

/**
 * Worker thread main function
 */
static void tile_worker_thread_func(gpointer data, gpointer user_data) {
    WorkerJob* job = (WorkerJob*)data;

    if (!job) {
        return;
    }

    /* Composite pixels WITHOUT touching Cairo */
    if (job->tile && job->tile->pixel_buffer) {
        tile_worker_composite_pixels(job->doc, job->tile, job->tile_x, job->tile_y);

        /* Mark tile as ready for upload */
        job->tile->pending_upload = TRUE;
    }

    g_free(job);
}

/**
 * Create worker pool
 */
TileWorkerPool* tile_worker_pool_create(guint num_workers) {
    TileWorkerPool* pool;

    pool = (TileWorkerPool*)g_malloc0(sizeof(TileWorkerPool));
    if (!pool) {
        return NULL;
    }

    /* Default to 4 workers max to keep resource usage reasonable */
    if (num_workers == 0 || num_workers > 4) {
        num_workers = 4;
    }

    pool->num_workers = num_workers;
    pool->pending_uploads = g_queue_new();

    g_mutex_init(&pool->upload_mutex);

    /* Create thread pool */
    pool->thread_pool = g_thread_pool_new(tile_worker_thread_func,
                                          NULL, /* user_data */
                                          num_workers,
                                          FALSE, /* exclusive (don't wait for all) */
                                          NULL); /* error */

    if (!pool->thread_pool) {
        g_queue_free(pool->pending_uploads);
        g_mutex_clear(&pool->upload_mutex);
        g_free(pool);
        return NULL;
    }

    g_message("Created tile worker pool with %u threads", num_workers);

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
    g_free(pool);
}

/**
 * Enqueue a tile for compositing
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

    /* Allocate pixel buffer if needed */
    if (!tile->pixel_buffer) {
        tile->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, tile->w);
        tile->pixel_buffer = (uint8_t*)g_malloc0(tile->stride * tile->h);

        if (!tile->pixel_buffer) {
            g_warning("Failed to allocate pixel buffer for tile");
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
 */
guint tile_worker_pool_process_uploads(TileWorkerPool* pool) {
    Tile* tile;
    guint count = 0;

    if (!pool) {
        return 0;
    }

    g_mutex_lock(&pool->upload_mutex);

    while (!g_queue_is_empty(pool->pending_uploads)) {
        tile = (Tile*)g_queue_pop_head(pool->pending_uploads);

        if (!tile || !tile->pixel_buffer) {
            continue;
        }

        /* Destroy old Cairo surface */
        if (tile->surface) {
            cairo_surface_destroy(tile->surface);
            tile->surface = NULL;
        }

        /* Create new Cairo surface from pixel buffer (main thread safe) */
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
            g_warning("Failed to create Cairo surface for tile");
            cairo_surface_destroy(tile->surface);
            tile->surface = NULL;
        }
    }

    g_mutex_unlock(&pool->upload_mutex);

    return count;
}
