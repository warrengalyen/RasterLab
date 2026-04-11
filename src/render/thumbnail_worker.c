/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "render/thumbnail_worker.h"
#include "render/blend.h"
#include "document.h"

#include <string.h>

/*
 * Worker thread: composites pre-scaled layer snapshots into a flat ARGB buffer.
 *
 * All pixel data is raw CAIRO_FORMAT_ARGB32 (premultiplied alpha, native-endian).
 * No Cairo API is called here.
 *
 * Blend compositing is delegated to blend_composite_row (blend.h), which is the
 * same SIMD-optimized path used by the tile rendering pipeline — all 27
 * Photoshop-compatible blend modes are supported automatically.
 *
 * Layer opacity is already baked into the premultiplied alpha of each snapshot
 * pixel (captured from layer->cache_surface), so blend_composite_row receives
 * layer_opacity = 255 to avoid double-application.
 *
 * The first visible layer always uses BLEND_MODE_NORMAL regardless of the layer's
 * own blend mode, matching the tile compositor behaviour — blend modes only have
 * meaning when compositing against an existing (non-transparent) backdrop.
 */

/**
 * Worker thread function — called by GThreadPool for each ThumbnailTask.
 * @param data      ThumbnailTask*
 * @param user_data ImageDocument* (for completion queue)
 */
void thumbnail_worker_func(gpointer data, gpointer user_data) {
    ThumbnailTask* task = (ThumbnailTask*)data;
    ImageDocument* doc  = (ImageDocument*)user_data;
    gsize buf_bytes;
    uint32_t* accum;
    guint i;
    gint row;

    if (!task || !doc) {
        return;
    }

    /* Check for cancellation before starting work.
     * is_initial tasks have cmd==NULL by design; they are NOT cancelled. */
    g_mutex_lock(&task->mutex);
    gboolean cancelled = (task->cmd == NULL && !task->is_initial);
    g_mutex_unlock(&task->mutex);

    if (cancelled) {
        g_mutex_lock(&doc->thumbnail_completion_mutex);
        g_queue_push_tail(doc->thumbnail_completion_queue, task);
        g_mutex_unlock(&doc->thumbnail_completion_mutex);
        return;
    }

    buf_bytes = (gsize)(task->thumb_w * task->thumb_h) * 4;
    accum = (uint32_t*)g_malloc0(buf_bytes); /* zeroed = fully transparent */

    for (i = 0; i < task->snapshots->len; i++) {
        ThumbnailLayerSnapshot* snap =
            (ThumbnailLayerSnapshot*)g_ptr_array_index(task->snapshots, i);
        if (!snap || !snap->pixels) {
            continue;
        }

        const guint32* src_px = (const guint32*)snap->pixels;
        BlendMode mode = (BlendMode)snap->blend_mode;

        /* Composite row by row using the existing SIMD blend pipeline.
         * opacity=255: layer opacity is already encoded in the premultiplied
         * alpha of cache_surface pixels, so no additional modulation is needed.
         * row_x/row_y=0: document-space coords only matter for Dissolve dithering,
         * which is imperceptible at 52×52 thumbnail scale. */
        for (row = 0; row < task->thumb_h; row++) {
            blend_composite_row(src_px + row * task->thumb_w,
                                accum   + row * task->thumb_w,
                                task->thumb_w,
                                0, row,
                                255,
                                mode);
        }
    }

    task->result_pixels = (uint8_t*)accum;

    /* Deliver result to main thread */
    g_mutex_lock(&doc->thumbnail_completion_mutex);
    g_queue_push_tail(doc->thumbnail_completion_queue, task);
    g_mutex_unlock(&doc->thumbnail_completion_mutex);
}

void thumbnail_layer_snapshot_free(ThumbnailLayerSnapshot* snap) {
    if (!snap) {
        return;
    }
    g_free(snap->pixels);
    g_free(snap);
}

void thumbnail_task_free(ThumbnailTask* task) {
    if (!task) {
        return;
    }
    g_mutex_clear(&task->mutex);
    if (task->snapshots) {
        g_ptr_array_free(task->snapshots, TRUE);
    }
    g_free(task->result_pixels);
    g_free(task);
}
