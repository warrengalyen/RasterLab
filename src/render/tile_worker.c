#include "render/tile_worker.h"
#include "render/compositor.h"
#include "render/layer.h"
#include <glib.h>
#include <string.h>

/* SIMD support via SIMDe (SIMD Everywhere) for cross-platform SSE2
 * This provides portable SIMD that works on x86, ARM, etc. */
#define SIMDE_ENABLE_NATIVE_ALIASES
#include "simde/simde/x86/sse2.h"

/* ============================================================================
 * SIMD Alpha Blending Implementation
 * ============================================================================
 * Performs OVER blend for premultiplied alpha:
 *   out = src + dst * (1 - src_a)
 * 
 * Processes 4 ARGB32 pixels at once using 128-bit SSE2 registers.
 * Pixel format: 0xAARRGGBB (premultiplied alpha)
 * ============================================================================ */

/**
 * Apply layer opacity to 4 pixels using SIMD
 * Multiplies all components (ARGB) by opacity and divides by 255
 * @param pixels 4 packed ARGB32 pixels
 * @param opacity Layer opacity (0-255)
 * @return 4 pixels with opacity applied
 */
static inline __m128i simd_apply_opacity(__m128i pixels, __m128i opacity_vec) {
    /* Zero vector for unpacking */
    __m128i zero = _mm_setzero_si128();
    
    /* Unpack lower 2 pixels (bytes 0-7) to 16-bit */
    __m128i pixels_lo = _mm_unpacklo_epi8(pixels, zero);
    /* Unpack upper 2 pixels (bytes 8-15) to 16-bit */
    __m128i pixels_hi = _mm_unpackhi_epi8(pixels, zero);
    
    /* Multiply by opacity (16-bit multiplication) */
    pixels_lo = _mm_mullo_epi16(pixels_lo, opacity_vec);
    pixels_hi = _mm_mullo_epi16(pixels_hi, opacity_vec);
    
    /* Divide by 255 using the approximation: (x + 128) >> 8
     * This is faster than actual division and accurate for blending */
    __m128i round = _mm_set1_epi16(128);
    pixels_lo = _mm_add_epi16(pixels_lo, round);
    pixels_hi = _mm_add_epi16(pixels_hi, round);
    pixels_lo = _mm_srli_epi16(pixels_lo, 8);
    pixels_hi = _mm_srli_epi16(pixels_hi, 8);
    
    /* Pack back to 8-bit with unsigned saturation */
    return _mm_packus_epi16(pixels_lo, pixels_hi);
}

/**
 * Extract alpha channel from 4 ARGB pixels and broadcast to all components
 * Memory layout (little-endian): [B0 G0 R0 A0 | B1 G1 R1 A1 | B2 G2 R2 A2 | B3 G3 R3 A3]
 * Output: [A0 A0 A0 A0 | A1 A1 A1 A1 | A2 A2 A2 A2 | A3 A3 A3 A3]
 */
static inline __m128i simd_extract_alpha(__m128i pixels) {
    /* Shift right by 24 bits to get alpha in lowest byte of each 32-bit element */
    __m128i a = _mm_srli_epi32(pixels, 24);  /* [0 0 0 A0 | 0 0 0 A1 | ...] */
    
    /* Broadcast alpha to all bytes within each 32-bit element using shift+OR
     * This is SSE2 compatible (no _mm_mullo_epi32 or SSSE3 shuffle needed) */
    __m128i a8  = _mm_slli_epi32(a, 8);      /* [0 0 A0 0 | ...] */
    __m128i a16 = _mm_slli_epi32(a, 16);     /* [0 A0 0 0 | ...] */
    __m128i a24 = _mm_slli_epi32(a, 24);     /* [A0 0 0 0 | ...] */
    
    /* Combine: [A0 A0 A0 A0 | A1 A1 A1 A1 | ...] */
    return _mm_or_si128(_mm_or_si128(a, a8), _mm_or_si128(a16, a24));
}

/**
 * SIMD OVER blend for 4 premultiplied ARGB32 pixels
 * Formula: out = src + dst * (255 - src_alpha) / 255
 * 
 * @param src 4 source pixels (with layer opacity already applied)
 * @param dst 4 destination pixels
 * @return 4 blended output pixels
 */
static inline __m128i simd_blend_over(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    
    /* Extract source alpha and calculate inverse (255 - alpha) */
    __m128i src_alpha = simd_extract_alpha(src);
    __m128i inv_alpha = _mm_sub_epi8(_mm_set1_epi8((char)255), src_alpha);
    
    /* Unpack destination pixels to 16-bit for multiplication */
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Unpack inverse alpha to 16-bit */
    __m128i inv_alpha_lo = _mm_unpacklo_epi8(inv_alpha, zero);
    __m128i inv_alpha_hi = _mm_unpackhi_epi8(inv_alpha, zero);
    
    /* Multiply dst by inverse alpha */
    dst_lo = _mm_mullo_epi16(dst_lo, inv_alpha_lo);
    dst_hi = _mm_mullo_epi16(dst_hi, inv_alpha_hi);
    
    /* Divide by 255 using (x + 128) >> 8 approximation */
    __m128i round = _mm_set1_epi16(128);
    dst_lo = _mm_add_epi16(dst_lo, round);
    dst_hi = _mm_add_epi16(dst_hi, round);
    dst_lo = _mm_srli_epi16(dst_lo, 8);
    dst_hi = _mm_srli_epi16(dst_hi, 8);
    
    /* Pack back to 8-bit */
    __m128i dst_scaled = _mm_packus_epi16(dst_lo, dst_hi);
    
    /* Add source (out = src + dst * inv_alpha) */
    return _mm_adds_epu8(src, dst_scaled);
}

/**
 * Composite a row of pixels using SIMD
 * Processes 4 pixels at a time, with scalar fallback for remaining pixels
 * 
 * @param src_row Source pixel row (layer pixels)
 * @param dst_row Destination pixel row (tile pixels)
 * @param width Number of pixels to composite
 * @param layer_opacity Layer opacity (0-255)
 */
static void simd_composite_row(const guint32* src_row, guint32* dst_row,
                               gint width, guint8 layer_opacity) {
    gint x = 0;
    
    /* Process 4 pixels at a time with SIMD */
    if (width >= 4) {
        /* Create opacity vector (broadcast to all 16-bit lanes) */
        __m128i opacity_vec = _mm_set1_epi16(layer_opacity);
        
        gint simd_width = width & ~3; /* Round down to multiple of 4 */
        
        for (; x < simd_width; x += 4) {
            /* Load 4 source and destination pixels */
            __m128i src = _mm_loadu_si128((const __m128i*)&src_row[x]);
            __m128i dst = _mm_loadu_si128((const __m128i*)&dst_row[x]);
            
            /* Apply layer opacity to source pixels */
            if (layer_opacity < 255) {
                src = simd_apply_opacity(src, opacity_vec);
            }
            
            /* Perform OVER blend */
            __m128i result = simd_blend_over(src, dst);
            
            /* Store result */
            _mm_storeu_si128((__m128i*)&dst_row[x], result);
        }
    }
    
    /* Scalar fallback for remaining pixels (0-3) */
    for (; x < width; x++) {
        guint32 src_pixel = src_row[x];
        guint32 dst_pixel = dst_row[x];
        
        /* Extract source components */
        guint8 src_a = (src_pixel >> 24) & 0xFF;
        guint8 src_r = (src_pixel >> 16) & 0xFF;
        guint8 src_g = (src_pixel >> 8) & 0xFF;
        guint8 src_b = src_pixel & 0xFF;
        
        /* Apply layer opacity */
        src_a = (guint8)(((guint32)src_a * layer_opacity + 128) >> 8);
        if (src_a == 0) continue;
        
        if (layer_opacity < 255) {
            src_r = (guint8)(((guint32)src_r * layer_opacity + 128) >> 8);
            src_g = (guint8)(((guint32)src_g * layer_opacity + 128) >> 8);
            src_b = (guint8)(((guint32)src_b * layer_opacity + 128) >> 8);
        }
        
        /* Extract destination components */
        guint8 dst_a = (dst_pixel >> 24) & 0xFF;
        guint8 dst_r = (dst_pixel >> 16) & 0xFF;
        guint8 dst_g = (dst_pixel >> 8) & 0xFF;
        guint8 dst_b = dst_pixel & 0xFF;
        
        /* OVER blend */
        guint8 inv_src_a = 255 - src_a;
        guint8 out_a = src_a + (guint8)(((guint32)dst_a * inv_src_a + 128) >> 8);
        guint8 out_r = src_r + (guint8)(((guint32)dst_r * inv_src_a + 128) >> 8);
        guint8 out_g = src_g + (guint8)(((guint32)dst_g * inv_src_a + 128) >> 8);
        guint8 out_b = src_b + (guint8)(((guint32)dst_b * inv_src_a + 128) >> 8);
        
        dst_row[x] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
    }
}

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

        /* Get layer opacity (0.0 - 1.0), convert to 0-255 */
        guint8 layer_opacity = (guint8)(layer->opacity * 255.0);
        if (layer_opacity == 0) {
            continue;
        }

        /* Composite layer pixels into tile pixels using SIMD
         * This processes 4 pixels at a time with SSE2 intrinsics */
        for (gint y = 0; y < src_height; y++) {
            guint32* layer_row = (guint32*)(layer_data + (src_y + y) * layer_stride) + src_x;
            guint32* tile_row = (guint32*)(tile->pixel_buffer + (intersect_top - tile->py + y) * tile->stride) + (intersect_left - tile->px);

            simd_composite_row(layer_row, tile_row, src_width, layer_opacity);
        }
    }

    return TRUE;
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

    g_message("Created tile worker pool with %u threads (priority queue enabled)", num_workers);

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
guint tile_worker_pool_process_uploads(TileWorkerPool* pool) {
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
            g_warning("Failed to create Cairo surface for tile (%d, %d)", tile->x, tile->y);
            cairo_surface_destroy(tile->surface);
            tile->surface = NULL;
        }
    }

    g_queue_free(to_process);

    return count;
}
