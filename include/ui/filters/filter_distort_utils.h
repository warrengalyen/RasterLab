/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_DISTORT_UTILS_H
#define FILTER_DISTORT_UTILS_H

#include "render/layer.h"
#include <glib.h>

typedef struct {
    gint width;
    gint height;
    guchar* rgb_input;  /* width * height * 3 */
    guchar* rgb_output; /* width * height * 3 */
} DistortBuffers;

/**
 * Prepare buffers for distortion filters.
 * Produces straight (un-premultiplied) RGB input.
 * Note: For consistency with existing filters (e.g. vibrance), we preserve the layer's alpha
 * when writing results back (we do NOT warp alpha).
 */
gboolean filter_distort_utils_prepare(ImageLayer* layer, DistortBuffers* buffers, const gchar* filter_name);

/**
 * Commit distorted RGB back into the layer surface (preserving existing alpha).
 */
gboolean filter_distort_utils_commit(ImageLayer* layer, DistortBuffers* buffers, const gchar* filter_name);

/**
 * Free distortion buffers.
 */
void filter_distort_utils_free(DistortBuffers* buffers);

#endif /* FILTER_DISTORT_UTILS_H */
