/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_GRD_H
#define GRADIENT_GRD_H

#include "gradient.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes specific to Photoshop .grd file operations.
 */
typedef enum {
    GRADIENT_GRD_ERROR_NONE               = 0,
    GRADIENT_GRD_ERROR_INVALID_PARAMETERS = 1,
    GRADIENT_GRD_ERROR_FILE_NOT_FOUND     = 2,
    GRADIENT_GRD_ERROR_FILE_READ_ERROR    = 3,
    GRADIENT_GRD_ERROR_FILE_WRITE_ERROR   = 4,
    GRADIENT_GRD_ERROR_CORRUPT_FILE       = 5,
    GRADIENT_GRD_ERROR_OUT_OF_MEMORY      = 6,
    GRADIENT_GRD_ERROR_UNSUPPORTED_FORMAT = 7
} GradientGrdError;

/**
 * Load a Photoshop gradient file (.grd).
 *
 * Both version 3 (flat binary, Photoshop 5 and earlier) and version 5
 * (descriptor-based, Photoshop 6+) are supported.  A single file may contain
 * multiple gradients.
 *
 * Noise gradients (GrdF = ClNs) are skipped with a warning; all other gradient
 * types are loaded into the returned GradientSet.
 *
 * @param filename   Path to a .grd file
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return Newly allocated GradientSet on success, NULL on failure.
 *         Caller must free with gradient_set_free().
 */
GradientSet* gradient_grd_load(const char* filename, GradientGrdError* error_out);

/**
 * Save a GradientSet as a Photoshop gradient file (.grd).
 *
 * The file is written in version 5 (descriptor) format, which is compatible
 * with Photoshop 6 and later.  Gradients that have only segment data (from a
 * .ggr source) are converted to separate color and transparency stop lists.
 *
 * @param set        Gradient set to save (must not be NULL)
 * @param filename   Destination .grd file path
 * @param error_out  Optional pointer to receive the error code (may be NULL)
 * @return TRUE on success, FALSE on failure
 */
gboolean gradient_grd_save(const GradientSet* set, const char* filename,
                             GradientGrdError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_GRD_H */
