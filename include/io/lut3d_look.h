/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef LUT3D_LOOK_H
#define LUT3D_LOOK_H

#include "io/lut3d.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes for Adobe SpeedGrade / Iridas .look 3D LUT (XML) I/O.
 */
typedef enum {
    LUT3D_LOOK_ERROR_NONE = 0,
    LUT3D_LOOK_ERROR_INVALID_PARAMETERS = 1,
    LUT3D_LOOK_ERROR_FILE_NOT_FOUND = 2,
    LUT3D_LOOK_ERROR_FILE_READ_ERROR = 3,
    LUT3D_LOOK_ERROR_FILE_WRITE_ERROR = 4,
    LUT3D_LOOK_ERROR_CORRUPT_FILE = 5,
    LUT3D_LOOK_ERROR_OUT_OF_MEMORY = 6,
    LUT3D_LOOK_ERROR_UNSUPPORTED_FORMAT = 7
} Lut3dLookError;

/**
 * Load a 3D LUT from a SpeedGrade / Iridas .look (XML) file.
 * Expects a LUT element with size and data children: data is
 * hex-encoded, little-endian IEEE-754 float triples in red-fastest scan order.
 */
ColorLut3D* lut3d_look_load(const char* filename, Lut3dLookError* error_out);

/**
 * Save a 3D LUT in the same .look (XML) layout.
 */
gboolean lut3d_look_save(const ColorLut3D* lut, const char* filename, Lut3dLookError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* LUT3D_LOOK_H */
