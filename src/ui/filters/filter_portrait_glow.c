/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_portrait_glow.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply portrait glow filter to a layer using Ocular library
 */
gboolean filter_portrait_glow_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    OcGlowStyle style;
    gint glow_radius;
    gint exposure_boost;
    gint strength;

    if (!layer || !layer->surface || !values || num_values < 4) {
        return FALSE;
    }

    style = (OcGlowStyle)(gint)values[0];
    glow_radius = (gint)values[1];
    exposure_boost = (gint)values[2];
    strength = (gint)values[3];

    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Portrait glow filter")) {
        return FALSE;
    }

    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Portrait glow filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    status = ocularPortraitGlowFilter(buffers.rgb_input, buffers.rgb_output,
                                      buffers.width, buffers.height, buffers.stride,
                                      style, glow_radius, exposure_boost, strength);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Portrait glow filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Portrait glow filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
