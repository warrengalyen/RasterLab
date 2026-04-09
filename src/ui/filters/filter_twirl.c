/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/filters/filter_twirl.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>
#include "debug_logger.h"

gboolean filter_twirl_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;
    gfloat angle;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    angle = values[0];

    if (!filter_distort_utils_prepare(layer, &buffers, "Twirl distortion")) {
        return FALSE;
    }

    status = ocularTwirlDistortionFilter(buffers.rgb_input, buffers.rgb_output,
                                         buffers.width, buffers.height, buffers.width * 3,
                                         angle);
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Twirl distortion: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Twirl distortion")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
