/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef LUT3D_H
#define LUT3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUT3D_SIZE_MIN 2
#define LUT3D_SIZE_MAX 256

/**
 * In-memory 3D color lookup table (RGB cuboid, equal resolution per axis).
 *
 * The rgb array layout matches Adobe / Resolve .cube file order: lines are written with
 * input red index moving fastest, then green, then blue. Equivalently, the flat index
 * is idx = b * n * n + g * n + r (with size n), and each entry is three
 * consecutive floats: output R, G, B in [0,1] (values may exceed that range in file).
 */
typedef struct {
    int size; /* grid edge n (2–LUT3D_SIZE_MAX), same for R, G, B */
    double domain_min[3];
    double domain_max[3];
    char* title;     /* optional; e.g. TITLE / file description in .cube, Description in .look; may be NULL */
    char* copyright; /* optional; written as # line in .cube, Copyright element in .look; may be NULL */
    float* rgb;      /* n*n*n*3, linearized as b slowest, g, r fastest */
} ColorLut3D;

/**
 * Allocate a 3D LUT with uninitialized rgb samples (all zeros in practice
 * if using calloc via lut3d_new). The size parameter must be in [LUT3D_SIZE_MIN, LUT3D_SIZE_MAX].
 *
 * @return new ColorLut3D or NULL on failure
 */
ColorLut3D* lut3d_new(int size);

/**
 * Release all storage held by a LUT, including the ColorLut3D itself.
 * Safe to call with NULL.
 */
void lut3d_free(ColorLut3D* lut);

/**
 * @return a deep copy of src, or NULL on failure
 */
ColorLut3D* lut3d_copy(const ColorLut3D* src);

/**
 * @return the flat array index in lut->rgb for the cube cell at integer
 *         coordinates r, g, b in [0, n-1].
 */
static inline size_t lut3d_cell_index(int r, int g, int b, int n) {
    return (size_t)(b * n * n + g * n + r) * 3U;
}

#ifdef __cplusplus
}
#endif

#endif /* LUT3D_H */
