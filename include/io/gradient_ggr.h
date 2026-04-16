/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_GGR_H
#define GRADIENT_GGR_H

#include "gradient.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes specific to GIMP .ggr file operations.
 */
typedef enum {
    GRADIENT_GGR_ERROR_NONE               = 0,
    GRADIENT_GGR_ERROR_INVALID_PARAMETERS = 1,
    GRADIENT_GGR_ERROR_FILE_NOT_FOUND     = 2,
    GRADIENT_GGR_ERROR_FILE_READ_ERROR    = 3,
    GRADIENT_GGR_ERROR_FILE_WRITE_ERROR   = 4,
    GRADIENT_GGR_ERROR_CORRUPT_FILE       = 5,
    GRADIENT_GGR_ERROR_OUT_OF_MEMORY      = 6
} GradientGgrError;

/**
 * Load a GIMP gradient file (.ggr).
 *
 * The GGR format stores exactly one gradient per file.  The returned
 * GradientSet therefore always contains num_gradients == 1 on success.
 *
 * @param filename   Path to a .ggr file
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return Newly allocated GradientSet on success, NULL on failure.
 *         Caller must free with gradient_set_free().
 */
GradientSet* gradient_ggr_load(const char* filename, GradientGgrError* error_out);

/**
 * Save the first gradient in a GradientSet as a GIMP gradient file (.ggr).
 *
 * Only the first gradient (index 0) is written because the GGR format is
 * single-gradient.  If the set contains no gradients, the call fails.
 *
 * @param set        Gradient set whose first gradient is written
 * @param filename   Destination .ggr file path
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return TRUE on success, FALSE on failure
 */
gboolean gradient_ggr_save(const GradientSet* set, const char* filename,
                             GradientGgrError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_GGR_H */
