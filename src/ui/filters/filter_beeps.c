/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_beeps.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply BEEPS filter to a layer using Ocular library
 */
gboolean filter_beeps_apply(ImageLayer* layer, gfloat photometric_std_dev, gfloat spatial_decay, gint range_filter) {
    FilterRGBBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "BEEPS filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "BEEPS filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply BEEPS filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularBEEPSFilter(buffers.rgb_input, buffers.rgb_output,
                               buffers.width, buffers.height, buffers.stride,
                               photometric_std_dev, spatial_decay, range_filter);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "BEEPS filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "BEEPS filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
