/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_bilateral.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply bilateral filter to a layer using Ocular library
 */
gboolean filter_bilateral_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gfloat sigma_spatial, sigma_range;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    /* Get parameters from values array */
    sigma_spatial = values[0];
    sigma_range = values[1];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Bilateral filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Bilateral filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply bilateral filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularBilateralFilter(buffers.rgb_input, buffers.rgb_output,
                                   buffers.width, buffers.height, buffers.stride,
                                   sigma_spatial, sigma_range);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Bilateral filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Bilateral filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
