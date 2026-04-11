/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef THUMBNAIL_WORKER_H
#define THUMBNAIL_WORKER_H

#include <glib.h>
#include <stdint.h>

/*
 * Asynchronous undo thumbnail generation
 *
 * Architecture follows the same Cairo-safe pattern as TileWorkerPool:
 *   - Main thread pre-scales layer pixel data into small raw buffers (microseconds)
 *   - Worker thread composites those buffers using pixel math (no Cairo)
 *   - Main thread creates the final cairo_surface_t from the result in the draw cycle
 *
 * This ensures undo commits never block the main thread.
 */

/* Forward declarations */
typedef struct _Command Command;

/**
 * Pre-scaled pixel snapshot of one layer for thumbnail compositing.
 * Created on the main thread; consumed by the worker thread.
 * Pixel data is in CAIRO_FORMAT_ARGB32 (premultiplied, native-endian).
 */
typedef struct {
    uint8_t* pixels;   /* Owned. Pre-scaled ARGB32 buffer (thumb_w * thumb_h * 4 bytes) */
    gint offset_x;     /* Layer offset_x mapped to thumbnail space (in thumb pixels) */
    gint offset_y;     /* Layer offset_y mapped to thumbnail space (in thumb pixels) */
    gint blend_mode;   /* BlendMode enum value cast to int; drives compositing formula */
} ThumbnailLayerSnapshot;

/**
 * Thumbnail generation task.
 * Allocated on the main thread, pushed to the thumbnail GThreadPool.
 * Ownership: the pool worker frees snapshots and result_pixels when done;
 * document_process_thumbnail_completions frees the task itself.
 */
typedef struct ThumbnailTask {
    Command*   cmd;           /* Target command. NULLed under mutex by command_free to cancel. */
    GMutex     mutex;         /* Protects cmd field across main/worker threads */
    GPtrArray* snapshots;     /* Array of ThumbnailLayerSnapshot* (owned) */
    gint       thumb_w;       /* Output width in pixels (always UNDO_THUMB_SIZE) */
    gint       thumb_h;       /* Output height in pixels (always UNDO_THUMB_SIZE) */
    uint8_t*   result_pixels; /* Set by worker on completion. Owned by task. */
    gboolean   is_initial;    /* TRUE → represents the pre-command "Original image" state;
                               * cmd is NULL but task is not cancelled. Result delivered to
                               * ImageDocument.initial_thumbnail instead of cmd->thumbnail. */
} ThumbnailTask;

/** Maximum dimension of undo history thumbnails */
#define UNDO_THUMB_SIZE 52

/**
 * Worker thread function — composites layer snapshots into result_pixels,
 * then pushes the task onto the document's completion queue.
 * This function signature matches GFunc for GThreadPool.
 * @param data   ThumbnailTask* to process
 * @param user_data  ImageDocument* (for completion queue delivery)
 */
void thumbnail_worker_func(gpointer data, gpointer user_data);

/**
 * Free a ThumbnailLayerSnapshot (used as GPtrArray element destructor).
 */
void thumbnail_layer_snapshot_free(ThumbnailLayerSnapshot* snap);

/**
 * Free a ThumbnailTask after delivery has been completed.
 * Caller must ensure the task is no longer referenced by any worker.
 */
void thumbnail_task_free(ThumbnailTask* task);

#endif  /* THUMBNAIL_WORKER_H */
