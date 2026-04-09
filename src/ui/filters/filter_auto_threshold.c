/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_auto_threshold.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply auto threshold filter to a layer using Ocular library
 * Note: Auto threshold requires a single-channel grayscale image
 */
gboolean filter_auto_threshold_apply(ImageLayer *layer)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *grayscale_input, *threshold_output;
    gint stride;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width; /* Single channel stride */

    /* Allocate buffers:
       - rgb_input: RGB format (3 channels) for conversion from ARGB32
       - grayscale_input: Single channel grayscale for threshold input
       - threshold_output: Single channel threshold output */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    grayscale_input = (guchar *)g_malloc(width * height);
    threshold_output = (guchar *)g_malloc(width * height);
    
    if (!rgb_input || !grayscale_input || !threshold_output) {
        debug_log("WRN", "Auto threshold filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(threshold_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Auto threshold filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(threshold_output);
        return FALSE;
    }

    /* Convert RGB to grayscale (single channel) using Ocular library
       Input: RGB format (stride = width * 3)
       Output: Single channel grayscale (stride = width) */
    status = ocularGrayscaleFilter(rgb_input, grayscale_input, width, height, width * 3);
    
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Auto threshold filter: Failed to convert to grayscale, error %d", status);
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(threshold_output);
        return FALSE;
    }

    /* Apply auto threshold filter using Ocular library
       Input and output: Single channel grayscale (stride = width) */
    status = ocularAutoThreshold(grayscale_input, threshold_output, width, height, stride, OC_AUTO_THRESHOLD_OTSU);
    
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Auto threshold filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(threshold_output);
        return FALSE;
    }

    /* Convert back from single channel grayscale to Cairo ARGB32 */
    if (!adjustments_grayscale_to_cairo(surface, threshold_output)) {
        debug_log("WRN", "Auto threshold filter: Failed to convert grayscale to surface");
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(threshold_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(grayscale_input);
    g_free(threshold_output);

    return TRUE;
}

