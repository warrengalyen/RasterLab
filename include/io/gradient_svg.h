/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_SVG_H
#define GRADIENT_SVG_H

#include "gradient.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes for SVG gradient files (.svg) containing SVG linearGradient
 * definitions under the standard SVG namespace.
 */
typedef enum {
    GRADIENT_SVG_ERROR_NONE                 = 0,
    GRADIENT_SVG_ERROR_INVALID_PARAMETERS   = 1,
    GRADIENT_SVG_ERROR_FILE_NOT_FOUND       = 2,
    GRADIENT_SVG_ERROR_FILE_READ_ERROR      = 3,
    GRADIENT_SVG_ERROR_FILE_WRITE_ERROR     = 4,
    GRADIENT_SVG_ERROR_CORRUPT_FILE         = 5,
    GRADIENT_SVG_ERROR_OUT_OF_MEMORY        = 6
} GradientSvgError;

/**
 * Load linearGradient definitions from an SVG file.
 *
 * Only \c <linearGradient> elements in the SVG namespace are converted.
 * \c <radialGradient> and other paint servers are skipped. Each gradient
 * becomes one \c GradientDef with linear RGB segments derived from its
 * \c <stop> list. \c xlink:href / \c href inheritance is resolved when the
 * referencing element has no child stops (per SVG).
 *
 * @param filename   Path to a .svg file
 * @param error_out  Optional error code (may be NULL)
 * @return New GradientSet, or NULL on failure. Free with gradient_set_free().
 */
GradientSet* gradient_svg_load(const char* filename, GradientSvgError* error_out);

/**
 * Save gradients as a minimal SVG document with \c <linearGradient> entries
 * in \c <defs>. Writes one \c <stop> per segment endpoint (merged at shared
 * positions). SVG interpolates linearly between stops; non-linear GIMP blend
 * curves are not preserved in the file, only endpoint colors.
 *
 * @param set        Gradients to write (may be empty)
 * @param filename   Destination .svg path
 * @param error_out  Optional error code (may be NULL)
 * @return TRUE on success
 */
gboolean gradient_svg_save(const GradientSet* set, const char* filename,
                           GradientSvgError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_SVG_H */
