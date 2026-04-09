/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_luminance_threshold.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply luminance threshold filter to a layer using Ocular library
 */
gboolean filter_luminance_threshold_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    guchar threshold;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    /* Extract parameters */
    /* Convert threshold from 0.0-1.0 range to 0-255 */
    threshold = (guchar)(values[0] * 255.0f);

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Luminance Threshold filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Luminance Threshold filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply luminance threshold filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       threshold: threshold value (0-255) */
    status = ocularLuminanceThresholdFilter(rgb_input, rgb_output, width, height, width * 3, threshold);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Luminance Threshold filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Luminance Threshold filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
