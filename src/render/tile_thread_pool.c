#include "render/tile_thread_pool.h"
#include "debug_logger.h"
#include "render/compositor.h"
#include "render/layer.h"
#include <glib.h>
#include <pthread.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Internal thread pool structure
 */
struct TileThreadPool {
    pthread_t* workers; /* Worker thread handles */
    guint num_workers;  /* Number of worker threads */

    /* Job queue */
    GQueue* job_queue;
    pthread_mutex_t job_mutex;
    pthread_cond_t job_cond;

    /* Completed results queue */
    GQueue* completed_queue;
    pthread_mutex_t completed_mutex;

    /* State management */
    TilePoolState state;
    pthread_mutex_t state_mutex;
};

/**
 * Worker thread function
 */
static gpointer tile_worker_thread(gpointer data);

/**
 * Composite a tile into a new temporary surface (thread-safe)
 * This is the core work done by each worker thread.
 */
static cairo_surface_t* composite_tile_cpu(ImageDocument* doc, Tile* tile);

/**
 * Get CPU count for default worker pool size
 * Limits to reasonable maximum to avoid resource exhaustion
 */
static guint get_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    guint num_cpus = sysinfo.dwNumberOfProcessors;
#else
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    if (num_cpus <= 0) {
        num_cpus = 4; /* Fallback to 4 if query fails */
    }

    /* Cap workers at 8 to prevent resource exhaustion
       (Each worker creates Cairo surfaces which consume memory) */
    if (num_cpus > 8) {
        num_cpus = 8;
    }

    return (guint)num_cpus;
}

/**
 * Create and start a new tile thread pool
 */
TileThreadPool* tile_thread_pool_create(guint num_workers) {
    TileThreadPool* pool;
    guint i;

    pool = (TileThreadPool*)g_malloc0(sizeof(TileThreadPool));
    if (!pool) {
        return NULL;
    }

    /* Auto-detect CPU count if not specified */
    if (num_workers == 0) {
        num_workers = get_cpu_count();
        if (num_workers > 1) {
            num_workers--; /* Reserve one core for UI thread */
        }
    }

    pool->num_workers = num_workers;
    pool->state = TILE_POOL_RUNNING;

    /* Initialize queues */
    pool->job_queue = g_queue_new();
    pool->completed_queue = g_queue_new();

    if (!pool->job_queue || !pool->completed_queue) {
        g_warning("Failed to allocate queues for tile thread pool");
        g_free(pool);
        return NULL;
    }

    /* Initialize synchronization primitives */
    pthread_mutex_init(&pool->job_mutex, NULL);
    pthread_cond_init(&pool->job_cond, NULL);
    pthread_mutex_init(&pool->completed_mutex, NULL);
    pthread_mutex_init(&pool->state_mutex, NULL);

    /* Create worker threads */
    pool->workers = (pthread_t*)g_malloc(sizeof(pthread_t) * num_workers);
    if (!pool->workers) {
        g_warning("Failed to allocate worker thread handles");
        g_queue_free(pool->job_queue);
        g_queue_free(pool->completed_queue);
        g_free(pool);
        return NULL;
    }

    for (i = 0; i < num_workers; i++) {
        if (pthread_create(&pool->workers[i], NULL, tile_worker_thread, pool) != 0) {
            g_warning("Failed to create worker thread %u", i);
            /* Clean up already-created threads */
            pool->num_workers = i;
            tile_thread_pool_destroy(pool);
            return NULL;
        }
    }

    debug_log("DBG", "Created tile thread pool with %u workers", num_workers);

    return pool;
}

/**
 * Destroy thread pool and stop all workers
 */
void tile_thread_pool_destroy(TileThreadPool* pool) {
    guint i;

    if (!pool) {
        return;
    }

    /* Signal shutdown */
    pthread_mutex_lock(&pool->state_mutex);
    pool->state = TILE_POOL_SHUTDOWN;
    pthread_mutex_unlock(&pool->state_mutex);

    /* Wake all workers so they see shutdown signal */
    pthread_mutex_lock(&pool->job_mutex);
    pthread_cond_broadcast(&pool->job_cond);
    pthread_mutex_unlock(&pool->job_mutex);

    /* Wait for all workers to finish */
    for (i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->workers[i], NULL);
    }

    /* Clean up queues and jobs */
    if (pool->job_queue) {
        while (!g_queue_is_empty(pool->job_queue)) {
            TileJob* job = (TileJob*)g_queue_pop_head(pool->job_queue);
            g_free(job);
        }
        g_queue_free(pool->job_queue);
    }

    /* Clean up completed results */
    if (pool->completed_queue) {
        while (!g_queue_is_empty(pool->completed_queue)) {
            CompletedTile* result = (CompletedTile*)g_queue_pop_head(pool->completed_queue);
            if (result->surface) {
                cairo_surface_destroy(result->surface);
            }
            g_free(result);
        }
        g_queue_free(pool->completed_queue);
    }

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&pool->job_mutex);
    pthread_cond_destroy(&pool->job_cond);
    pthread_mutex_destroy(&pool->completed_mutex);
    pthread_mutex_destroy(&pool->state_mutex);

    /* Free worker thread array */
    g_free(pool->workers);

    g_free(pool);
}

/**
 * Enqueue a tile for recomposition
 */
gboolean tile_thread_pool_enqueue_job(TileThreadPool* pool,
                                      ImageDocument* doc,
                                      Tile* tile,
                                      gint tile_x,
                                      gint tile_y,
                                      guint generation_id) {
    TileJob* job;
    gboolean result = FALSE;

    if (!pool || !doc || !tile) {
        return FALSE;
    }

    /* Check if pool is shutdown */
    pthread_mutex_lock(&pool->state_mutex);
    if (pool->state != TILE_POOL_RUNNING) {
        pthread_mutex_unlock(&pool->state_mutex);
        return FALSE;
    }
    pthread_mutex_unlock(&pool->state_mutex);

    /* Allocate job */
    job = (TileJob*)g_malloc(sizeof(TileJob));
    if (!job) {
        return FALSE;
    }

    job->doc = doc;
    job->tile = tile;
    job->tile_x = tile_x;
    job->tile_y = tile_y;
    job->generation_id = generation_id;

    /* Enqueue and signal worker */
    pthread_mutex_lock(&pool->job_mutex);
    g_queue_push_tail(pool->job_queue, job);
    result = TRUE;
    pthread_cond_signal(&pool->job_cond);
    pthread_mutex_unlock(&pool->job_mutex);

    return result;
}

/**
 * Poll completed tiles
 */
gboolean tile_thread_pool_pop_completed(TileThreadPool* pool, CompletedTile* out_tile) {
    CompletedTile* result;

    if (!pool || !out_tile) {
        return FALSE;
    }

    pthread_mutex_lock(&pool->completed_mutex);
    result = (CompletedTile*)g_queue_pop_head(pool->completed_queue);
    pthread_mutex_unlock(&pool->completed_mutex);

    if (result) {
        *out_tile = *result;
        g_free(result);
        return TRUE;
    }

    return FALSE;
}

/**
 * Get pending job count
 */
guint tile_thread_pool_get_pending_jobs(TileThreadPool* pool) {
    guint count;

    if (!pool) {
        return 0;
    }

    pthread_mutex_lock(&pool->job_mutex);
    count = g_queue_get_length(pool->job_queue);
    pthread_mutex_unlock(&pool->job_mutex);

    return count;
}

/**
 * Get completed tile count
 */
guint tile_thread_pool_get_completed_count(TileThreadPool* pool) {
    guint count;

    if (!pool) {
        return 0;
    }

    pthread_mutex_lock(&pool->completed_mutex);
    count = g_queue_get_length(pool->completed_queue);
    pthread_mutex_unlock(&pool->completed_mutex);

    return count;
}

/**
 * Request shutdown
 */
void tile_thread_pool_request_shutdown(TileThreadPool* pool) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->state_mutex);
    pool->state = TILE_POOL_SHUTDOWN;
    pthread_mutex_unlock(&pool->state_mutex);

    pthread_mutex_lock(&pool->job_mutex);
    pthread_cond_broadcast(&pool->job_cond);
    pthread_mutex_unlock(&pool->job_mutex);
}

/**
 * Composite a tile into a new surface (thread-safe, no GTK calls)
 */
static cairo_surface_t* composite_tile_cpu(ImageDocument* doc, Tile* tile) {
    cairo_surface_t* tile_surface;
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;
    gint layer_x, layer_y, layer_right, layer_bottom;
    gint tile_right, tile_bottom;
    gint intersect_left, intersect_top, intersect_right, intersect_bottom;
    gboolean is_first_visible_layer = TRUE;

    if (!doc || !tile) {
        return NULL;
    }

    /* Create temporary tile surface */
    tile_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tile->w, tile->h);
    if (!tile_surface || cairo_surface_status(tile_surface) != CAIRO_STATUS_SUCCESS) {
        if (tile_surface) {
            cairo_surface_destroy(tile_surface);
        }
        g_warning("Worker: Failed to create tile surface (%d x %d)", tile->w, tile->h);
        return NULL;
    }

    /* Create context and clear to transparent */
    cr = cairo_create(tile_surface);
    if (!cr) {
        cairo_surface_destroy(tile_surface);
        g_warning("Worker: Failed to create Cairo context");
        return NULL;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    tile_right = tile->px + tile->w;
    tile_bottom = tile->py + tile->h;

    /* Safety check: if document is being freed, abort */
    if (!doc->layers) {
        cairo_destroy(cr);
        cairo_surface_flush(tile_surface);
        return tile_surface;
    }

    /* Composite all layers that intersect this tile */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Calculate layer bounds */
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
        gint src_x = intersect_left - layer_x;
        gint src_y = intersect_top - layer_y;
        gint src_width = intersect_right - intersect_left;
        gint src_height = intersect_bottom - intersect_top;

        /* Ensure layer cache is valid */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw only the intersecting region */
        cairo_save(cr);

        /* Translate to tile origin */
        cairo_translate(cr, -tile->px, -tile->py);

        /* Clip to intersection region in document coordinates */
        cairo_rectangle(cr, intersect_left, intersect_top, src_width, src_height);
        cairo_clip(cr);

        /* Set blend mode */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);

        /* Draw layer cache (opacity already applied) */
        cairo_set_source_surface(cr, layer->cache_surface, layer_x, layer_y);
        cairo_paint(cr);

        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(tile_surface);

    /* Final safety check before returning */
    if (cairo_surface_status(tile_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Worker: Tile surface status error after compositing: %s",
                  cairo_status_to_string(cairo_surface_status(tile_surface)));
        cairo_surface_destroy(tile_surface);
        return NULL;
    }

    return tile_surface;
}

/**
 * Worker thread main function
 */
static gpointer tile_worker_thread(gpointer data) {
    TileThreadPool* pool = (TileThreadPool*)data;
    TileJob* job;
    CompletedTile* result;
    cairo_surface_t* new_surface;

    if (!pool) {
        return NULL;
    }

    while (TRUE) {
        /* Wait for job or shutdown signal */
        pthread_mutex_lock(&pool->job_mutex);

        while (g_queue_is_empty(pool->job_queue)) {
            /* Check shutdown state while holding job mutex */
            pthread_mutex_lock(&pool->state_mutex);
            if (pool->state == TILE_POOL_SHUTDOWN) {
                pthread_mutex_unlock(&pool->state_mutex);
                pthread_mutex_unlock(&pool->job_mutex);
                return NULL; /* Worker exits on shutdown */
            }
            pthread_mutex_unlock(&pool->state_mutex);

            /* Wait for job signal */
            pthread_cond_wait(&pool->job_cond, &pool->job_mutex);
        }

        /* Pop job from queue */
        job = (TileJob*)g_queue_pop_head(pool->job_queue);
        pthread_mutex_unlock(&pool->job_mutex);

        if (!job) {
            continue;
        }

        /* CRITICAL: Do all CPU work here (NOT inside mutex) */
        new_surface = composite_tile_cpu(job->doc, job->tile);

        /* Enqueue result for main thread */
        if (new_surface && cairo_surface_status(new_surface) == CAIRO_STATUS_SUCCESS) {
            result = (CompletedTile*)g_malloc(sizeof(CompletedTile));
            if (result) {
                result->surface = new_surface;
                result->tile = job->tile;
                result->tile_x = job->tile_x;
                result->tile_y = job->tile_y;
                result->generation_id = job->generation_id;

                pthread_mutex_lock(&pool->completed_mutex);
                g_queue_push_tail(pool->completed_queue, result);
                pthread_mutex_unlock(&pool->completed_mutex);
            } else if (new_surface) {
                cairo_surface_destroy(new_surface);
            }
        } else if (new_surface) {
            g_warning("Worker thread: Invalid Cairo surface status, discarding result");
            cairo_surface_destroy(new_surface);
        }

        g_free(job);
    }

    return NULL;
}
