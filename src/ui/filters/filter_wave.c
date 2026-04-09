/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_wave.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>
#include "debug_logger.h"

static OcWaveType wave_type_from_value(gint wave_type_value) {
    switch (wave_type_value) {
        case 1:
            return OC_WAVE_TRIANGLE;
        case 2:
            return OC_WAVE_SQUARE;
        case 3:
            return OC_WAVE_SAWTOOTH;
        case 0:
        default:
            return OC_WAVE_SINE;
    }
}

gboolean filter_wave_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface || !values || num_values < 9) {
        return FALSE;
    }

    const gint num_generators = (gint)values[0];
    const gint min_wavelength = (gint)values[1];
    const gint max_wavelength = (gint)values[2];
    const gint min_amplitude = (gint)values[3];
    const gint max_amplitude = (gint)values[4];
    const gint scale_x = (gint)values[5];
    const gint scale_y = (gint)values[6];
    const gint wave_type_value = (gint)values[7];
    const OcWaveType wave_type = wave_type_from_value(wave_type_value);
    const gint seed_value = (gint)values[8];
    const unsigned int seed = (seed_value < 0) ? 0u : (unsigned int)seed_value;

    if (!filter_distort_utils_prepare(layer, &buffers, "Wave distortion")) {
        return FALSE;
    }

    status = ocularWaveDistortionFilter(buffers.rgb_input, buffers.rgb_output,
                                        buffers.width, buffers.height, buffers.width * 3,
                                        num_generators,
                                        min_wavelength, max_wavelength,
                                        min_amplitude, max_amplitude,
                                        scale_x, scale_y,
                                        wave_type, seed);
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Wave distortion: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Wave distortion")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
