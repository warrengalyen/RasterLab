/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_RGR_H
#define GRADIENT_RGR_H

#include "gradient.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes specific to RasterLab .rgr binary gradient file operations.
 */
typedef enum {
    GRADIENT_RGR_ERROR_NONE               = 0,
    GRADIENT_RGR_ERROR_INVALID_PARAMETERS = 1,
    GRADIENT_RGR_ERROR_FILE_NOT_FOUND     = 2,
    GRADIENT_RGR_ERROR_FILE_READ_ERROR    = 3,
    GRADIENT_RGR_ERROR_FILE_WRITE_ERROR   = 4,
    GRADIENT_RGR_ERROR_CORRUPT_FILE       = 5,
    GRADIENT_RGR_ERROR_OUT_OF_MEMORY      = 6
} GradientRgrError;

/**
 * Load a RasterLab binary gradient file (.rgr).
 *
 * The RGR format supports multiple gradients per file.  The returned
 * GradientSet contains all gradients stored in the file.
 *
 * @param filename   Path to a .rgr file
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return Newly allocated GradientSet on success, NULL on failure.
 *         Caller must free with gradient_set_free().
 */
GradientSet* gradient_rgr_load(const char* filename, GradientRgrError* error_out);

/**
 * Save all gradients in a GradientSet as a RasterLab binary gradient file (.rgr).
 *
 * All gradients in the set are written.  If the set contains no gradients
 * the file is created with a zero gradient count.
 *
 * @param set        Gradient set to write
 * @param filename   Destination .rgr file path
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return TRUE on success, FALSE on failure
 */
gboolean gradient_rgr_save(const GradientSet* set, const char* filename,
                             GradientRgrError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_RGR_H */
