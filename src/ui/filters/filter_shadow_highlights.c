/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_shadow_highlights.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply shadow/highlights filter to a layer using Ocular library
 */
gboolean filter_shadow_highlights_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gfloat shadows, highlights, midtone_contrast;

    if (!layer || !layer->surface || !values || num_values < 3) {
        return FALSE;
    }

    shadows = values[0];
    midtone_contrast = values[1];
    highlights = values[2];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Shadow/Highlights filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Shadow/Highlights filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply shadow/highlights filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       Parameter order: shadows, midtone contrast, highlights */
    status = ocularHighlightShadowFilter(buffers.rgb_input, buffers.rgb_output,
                                         buffers.width, buffers.height, buffers.stride,
                                         shadows, midtone_contrast, highlights);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Shadow/Highlights filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Shadow/Highlights filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
