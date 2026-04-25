/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "io/lut3d.h"
#include "debug_logger.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

ColorLut3D* lut3d_new(int size) {
    if (size < LUT3D_SIZE_MIN || size > LUT3D_SIZE_MAX) {
        debug_log("WRN", "lut3d_new: size %d out of range", size);
        return NULL;
    }
    int64_t n3 = (int64_t)size * (int64_t)size * (int64_t)size;
    if (n3 * 3 > (int64_t)(SIZE_MAX / sizeof(float)))
        return NULL;
    size_t     total_floats = (size_t)(n3 * 3);
    ColorLut3D* lut         = g_new0(ColorLut3D, 1);
    if (!lut)
        return NULL;
    lut->size = size;
    lut->domain_min[0] = lut->domain_min[1] = lut->domain_min[2] = 0.0;
    lut->domain_max[0] = lut->domain_max[1] = lut->domain_max[2] = 1.0;
    lut->rgb = (float*)g_malloc0(total_floats * sizeof(float));
    if (!lut->rgb) {
        g_free(lut);
        return NULL;
    }
    return lut;
}

void lut3d_free(ColorLut3D* lut) {
    if (!lut)
        return;
    g_free(lut->title);
    g_free(lut->copyright);
    g_free(lut->rgb);
    g_free(lut);
}

ColorLut3D* lut3d_copy(const ColorLut3D* src) {
    if (!src || !src->rgb)
        return NULL;
    if (src->size < LUT3D_SIZE_MIN || src->size > LUT3D_SIZE_MAX)
        return NULL;
    int     n  = src->size;
    int64_t n3 = (int64_t)n * (int64_t)n * (int64_t)n;
    size_t  nf = (size_t)(n3 * 3);
    if (n3 * 3 > (int64_t)(SIZE_MAX / sizeof(float)))
        return NULL;
    ColorLut3D* d = g_new0(ColorLut3D, 1);
    if (!d)
        return NULL;
    d->size = src->size;
    d->domain_min[0] = src->domain_min[0];
    d->domain_min[1] = src->domain_min[1];
    d->domain_min[2] = src->domain_min[2];
    d->domain_max[0] = src->domain_max[0];
    d->domain_max[1] = src->domain_max[1];
    d->domain_max[2] = src->domain_max[2];
    d->title     = (src->title) ? g_strdup(src->title) : NULL;
    d->copyright = (src->copyright) ? g_strdup(src->copyright) : NULL;
    d->rgb       = (float*)g_malloc(nf * sizeof(float));
    if (!d->rgb) {
        g_free(d->title);
        g_free(d->copyright);
        g_free(d);
        return NULL;
    }
    memcpy(d->rgb, src->rgb, nf * sizeof(float));
    return d;
}
