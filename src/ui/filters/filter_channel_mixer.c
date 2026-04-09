/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_channel_mixer.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include <string.h>
#include "debug_logger.h"

#define MIXER_SIZE 16

gboolean filter_channel_mixer_apply(ImageLayer* layer,
                                    const gfloat* mixer,
                                    gboolean monochrome,
                                    gboolean preserve_luminance) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    gint stride;
    OC_STATUS status;

    if (!layer || !layer->surface || !mixer) {
        return FALSE;
    }

    surface = layer->surface;
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3;
    rgb_input = (guchar*)g_malloc((size_t)(width * height * 3));
    rgb_output = (guchar*)g_malloc((size_t)(width * height * 3));
    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Channel mixer filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Channel mixer filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    {
        gfloat scaled_mixer[MIXER_SIZE];
        int i;
        for (i = 0; i < MIXER_SIZE; i++) {
            /* Scale RGB cofficients */
            scaled_mixer[i] = ((i % 4) == 3) ? mixer[i] : (mixer[i] / 100.0f);
        }
        status = ocularChannelMixerFilter(rgb_input, rgb_output, width, height, stride,
                                          scaled_mixer, monochrome, preserve_luminance);
    }

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Channel mixer filter: Ocular returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Channel mixer filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);
    cairo_surface_mark_dirty(surface);
    return TRUE;
}

gboolean filter_channel_mixer_apply_values(ImageLayer* layer, const gfloat* values, gint num_values) {
    gfloat mixer[MIXER_SIZE];
    gboolean monochrome;
    gboolean preserve_luminance;

    if (!layer || !values || num_values < 18) {
        return FALSE;
    }

    memcpy(mixer, values, MIXER_SIZE * sizeof(gfloat));
    monochrome = (gboolean)(int)values[16];
    preserve_luminance = (gboolean)(int)values[17];

    return filter_channel_mixer_apply(layer, mixer, monochrome, preserve_luminance);
}
