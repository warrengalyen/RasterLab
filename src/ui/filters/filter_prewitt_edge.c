/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_prewitt_edge.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply Prewitt edge detection filter to a layer using Ocular library
 */
gboolean filter_prewitt_edge_apply(ImageLayer* layer) {
    cairo_surface_t* surface;
    gint width, height;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers:
       - rgb_input: RGB format (3 channels) for conversion
       - grayscale_input: Single channel grayscale for edge detection
       - edge_output: Single channel edge detection output */
    guchar* rgb_input = (guchar*)g_malloc(width * height * 3);
    guchar* grayscale_input = (guchar*)g_malloc(width * height);
    guchar* edge_output = (guchar*)g_malloc(width * height);

    if (!rgb_input || !grayscale_input || !edge_output) {
        debug_log("WRN", "Prewitt edge filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(edge_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Prewitt edge filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(edge_output);
        return FALSE;
    }

    /* Convert RGB to grayscale (single channel) */
    status = ocularGrayscaleFilter(rgb_input, grayscale_input, width, height, width * 3);
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Prewitt edge filter: Grayscale conversion returned error %d", status);
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(edge_output);
        return FALSE;
    }

    /* Apply Prewitt edge filter using Ocular library
       Input and output: Single channel grayscale (Channels = 1) */
    status = ocularPrewittEdgeDetect(grayscale_input, edge_output, width, height, 1);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Prewitt edge filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(edge_output);
        return FALSE;
    }

    /* Convert back from single channel grayscale to Cairo ARGB32 */
    if (!adjustments_grayscale_to_cairo(surface, edge_output)) {
        debug_log("WRN", "Prewitt edge filter: Failed to convert grayscale to surface");
        g_free(rgb_input);
        g_free(grayscale_input);
        g_free(edge_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(grayscale_input);
    g_free(edge_output);

    return TRUE;
}
