/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef LUT3D_IO_H
#define LUT3D_IO_H

#include "io/lut3d.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes for 3D color lookup file I/O.
 */
typedef enum {
    LUT3D_IO_ERROR_NONE = 0,
    LUT3D_IO_ERROR_INVALID_PARAMETERS = 1,
    LUT3D_IO_ERROR_FILE_NOT_FOUND = 2,
    LUT3D_IO_ERROR_FILE_READ_ERROR = 3,
    LUT3D_IO_ERROR_FILE_WRITE_ERROR = 4,
    LUT3D_IO_ERROR_UNSUPPORTED_FORMAT = 5,
    LUT3D_IO_ERROR_CORRUPT_FILE = 6,
    LUT3D_IO_ERROR_OUT_OF_MEMORY = 7,
    LUT3D_IO_ERROR_UNKNOWN = 99
} Lut3DIOError;

/**
 * Load a 3D color LUT. The format is selected from the file extension: .cube
 * (ASCII / Adobe) and .look (SpeedGrade / Iridas XML 3D LUT).
 *
 * @return Newly allocated ColorLut3D on success; call lut3d_free() when done.
 */
ColorLut3D* lut3d_io_load(const char* filename, Lut3DIOError* error_out);

/**
 * Save a 3D color LUT. The format is selected from the file extension.
 */
gboolean lut3d_io_save(const ColorLut3D* lut, const char* filename, Lut3DIOError* error_out);

/**
 * @return TRUE if the filename extension is a known 3D LUT format.
 */
gboolean lut3d_io_is_supported(const char* filename);

/**
 * User-readable message for a Lut3DIOError.
 */
const char* lut3d_io_get_error_message(Lut3DIOError error, const char* filename);

#ifdef __cplusplus
}
#endif

#endif /* LUT3D_IO_H */
