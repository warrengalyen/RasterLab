/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_IO_H
#define GRADIENT_IO_H

#include "gradient.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes returned by gradient I/O operations.
 */
typedef enum {
    GRADIENT_IO_ERROR_NONE               = 0,
    GRADIENT_IO_ERROR_INVALID_PARAMETERS = 1,
    GRADIENT_IO_ERROR_FILE_NOT_FOUND     = 2,
    GRADIENT_IO_ERROR_FILE_READ_ERROR    = 3,
    GRADIENT_IO_ERROR_FILE_WRITE_ERROR   = 4,
    GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT = 5,
    GRADIENT_IO_ERROR_CORRUPT_FILE       = 6,
    GRADIENT_IO_ERROR_OUT_OF_MEMORY      = 7,
    GRADIENT_IO_ERROR_UNKNOWN            = 99
} GradientIOError;

/**
 * Load a gradient file and return an allocated GradientSet.
 * The format is determined from the file extension (.ggr or .grd).
 *
 * @param filename   Path to the gradient file
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return Newly allocated GradientSet on success, NULL on failure.
 *         Caller must free with gradient_set_free().
 */
GradientSet* gradient_io_load(const char* filename, GradientIOError* error_out);

/**
 * Save a GradientSet to a file.
 * The format is determined from the file extension (.ggr or .grd).
 * For .ggr only the first gradient in the set is written (GGR is single-gradient).
 *
 * @param set        Gradient set to save (must not be NULL)
 * @param filename   Destination file path
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return TRUE on success, FALSE on failure
 */
gboolean gradient_io_save(const GradientSet* set, const char* filename,
                           GradientIOError* error_out);

/**
 * Return TRUE if the filename extension corresponds to a supported gradient format.
 * This is a fast extension-only check; it does not read the file.
 *
 * @param filename   File path to check
 * @return TRUE if the extension is .ggr or .grd (case-insensitive)
 */
gboolean gradient_io_is_supported(const char* filename);

/**
 * Return a user-readable error description for a GradientIOError code.
 *
 * @param error      The error code
 * @param filename   Optional filename for context (may be NULL)
 * @return Static string; do not free.
 */
const char* gradient_io_get_error_message(GradientIOError error, const char* filename);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_IO_H */
