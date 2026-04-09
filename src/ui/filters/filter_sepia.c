/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_sepia.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply sepia filter to a layer using Ocular library
 */
gboolean filter_sepia_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gint intensity;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    intensity = (gint)values[0];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Sepia filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Sepia filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply sepia filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       Intensity: 0-100, using default 100 for full effect */
    status = ocularSepiaFilter(buffers.rgb_input, buffers.rgb_output,
                               buffers.width, buffers.height, buffers.stride, intensity);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Sepia filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Sepia filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
