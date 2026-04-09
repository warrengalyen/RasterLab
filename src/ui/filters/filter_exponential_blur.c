/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_exponential_blur.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply exponential blur filter to a layer using Ocular library
 */
gboolean filter_exponential_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    OC_STATUS status;
    gfloat radius;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    radius = values[0];

    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Exponential blur filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Exponential blur filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* ocularExponentialBlur uses Channels instead of Stride */
    status = ocularExponentialBlur(rgb_input, rgb_output, width, height, 3, radius);
    
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Exponential blur filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Exponential blur filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

