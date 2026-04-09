/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_highlight_shadow_tint.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply highlight/shadow tint filter to a layer using Ocular library
 */
gboolean filter_highlight_shadow_tint_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    gfloat shadow_r, shadow_g, shadow_b;
    gfloat highlight_r, highlight_g, highlight_b;
    gfloat shadow_intensity, highlight_intensity;

    if (!layer || !layer->surface || !values || num_values < 8) {
        return FALSE;
    }

    /* Extract parameters (values are already in 0.0-1.0 range) */
    shadow_r = values[0];
    shadow_g = values[1];
    shadow_b = values[2];
    highlight_r = values[3];
    highlight_g = values[4];
    highlight_b = values[5];
    shadow_intensity = values[6];
    highlight_intensity = values[7];

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Highlight/Shadow Tint filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Highlight/Shadow Tint filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply highlight/shadow tint filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       All tint values and intensities are floats in 0.0-1.0 range */
    status = ocularHighlightShadowTintFilter(rgb_input, rgb_output, width, height, width * 3,
                                             shadow_r, shadow_g, shadow_b,
                                             highlight_r, highlight_g, highlight_b,
                                             shadow_intensity, highlight_intensity);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Highlight/Shadow Tint filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Highlight/Shadow Tint filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
