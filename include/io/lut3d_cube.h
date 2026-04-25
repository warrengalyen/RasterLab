/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef LUT3D_CUBE_H
#define LUT3D_CUBE_H

#include "io/lut3d.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes for Adobe / ASC CDL .cube 3D LUT I/O.
 */
typedef enum {
    LUT3D_CUBE_ERROR_NONE = 0,
    LUT3D_CUBE_ERROR_INVALID_PARAMETERS = 1,
    LUT3D_CUBE_ERROR_FILE_NOT_FOUND = 2,
    LUT3D_CUBE_ERROR_FILE_READ_ERROR = 3,
    LUT3D_CUBE_ERROR_FILE_WRITE_ERROR = 4,
    LUT3D_CUBE_ERROR_CORRUPT_FILE = 5,
    LUT3D_CUBE_ERROR_OUT_OF_MEMORY = 6,
    LUT3D_CUBE_ERROR_UNSUPPORTED_FORMAT = 7
} Lut3dCubeError;

/**
 * Load a 3D LUT from an Adobe .cube (ASCII) file.
 * Only 3D LUTs are supported (files that declare LUT_1D_SIZE without LUT_3D_SIZE
 * or that contain only 1D data are rejected with LUT3D_CUBE_ERROR_UNSUPPORTED_FORMAT).
 *
 * @return Newly allocated ColorLut3D on success; caller must use lut3d_free().
 */
ColorLut3D* lut3d_cube_load(const char* filename, Lut3dCubeError* error_out);

/**
 * Save a 3D LUT in .cube (ASCII) format. The lut and filename parameters must be non-NULL;
 * lut->size and lut->rgb must be valid.
 */
gboolean lut3d_cube_save(const ColorLut3D* lut, const char* filename, Lut3dCubeError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* LUT3D_CUBE_H */
