/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_LUT3D_H
#define FILTER_LUT3D_H

#include "document.h"
#include <cairo.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Apply a 3D color LUT to an ARGB32 cairo surface in place.
 * RGB channels (after un-premultiplying) are used as 0-1 table coordinates, then
 * the LUT result is combined with the original per BlendMode, then the effect is
 * mixed in using intensity.
 *
 * @param surface         Cairo image surface, format ARGB32, modified in place
 * @param lut_file_path   Path to a 3D LUT file
 * @param intensity       0-100, strength of the result
 * @param blend_mode      Same blend family as layer compositing (source = LUT, backdrop = original)
 * @return TRUE on success, FALSE on invalid input, I/O or allocation failure
 */
gboolean filter_apply_3d_lut(
    cairo_surface_t* surface,
    const char* lut_file_path,
    gint intensity,
    BlendMode blend_mode);

/** Minimum and maximum grid_points for filter_save_3d_lut_from_image (LUT_3D_SIZE in the file). */
#define FILTER_LUT3D_PHOTO_GRID_MIN 2
#define FILTER_LUT3D_PHOTO_GRID_MAX 64

/**
 * Build and save a 3D LUT that approximates the color character of a photograph. The
 * image is not required to be square or a HALD. Un-premultiplied sRGB pixels are
 * compared to a synthetic identity color distribution: each LUT entry maps an input
 * sRGB in DOMAIN 0–1 (grid vertex) to an output using Reinhard-style per-channel
 * mean/std transfer in CIELAB (D65), then saved via lut3d_io_save() (.cube or .look;
 * see io/lut3d_io). This yields a global “look” (cast and contrast in Lab), not a
 * true scene-referred match; for an exact effect from all inputs, a HALD workflow is
 * still appropriate.
 *
 * @param file_description  .cube TITLE and .look Description: NULL → default title; "" → blank
 * @param file_copyright      Optional copyright; written as a number-sign comment in .cube
 *                            and a Copyright child in .look. NULL or empty → omitted
 * @param grid_points         2–64, LUT_3D_SIZE and synthetic identity-stat sample grid
 * @return TRUE on success, FALSE on invalid input, I/O, or memory failure
 */
gboolean filter_save_3d_lut_from_image(
    cairo_surface_t* surface,
    const char* lut_file_path,
    const char* file_description,
    const char* file_copyright,
    gint grid_points);

/**
 * Build and save a 3D LUT by sampling pixel-to-pixel colour correspondences between
 * two surfaces of the same dimensions (PhotoDemon-style approach).
 *
 * For every non-transparent pixel pair at the same (x, y), the base colour is
 * quantised to the nearest LUT grid vertex and the modified colour is accumulated
 * there.  After scanning all pixels, grid vertices with no samples fall back to the
 * identity mapping (no change), so the LUT is always fully populated.  A final
 * diffusion pass propagates sampled transforms into unsampled vertices, producing a
 * smooth result even when the image does not cover the entire colour cube.
 *
 * Typical use: base_surface = original (pre-adjustment) image,
 *              mod_surface  = composited result after all layer adjustments.
 * Both surfaces must be ARGB32, premultiplied, and the same width × height.
 *
 * @param base_surface        Original pixels  (before adjustments)
 * @param mod_surface         Modified pixels  (after  adjustments)
 * @param lut_file_path       Destination .cube or .look path
 * @param file_description    Optional TITLE / Description string (NULL → default)
 * @param file_copyright      Optional copyright string (NULL or "" → omitted)
 * @param grid_points         LUT_3D_SIZE in [FILTER_LUT3D_PHOTO_GRID_MIN, FILTER_LUT3D_PHOTO_GRID_MAX]
 * @return TRUE on success
 */
gboolean filter_save_3d_lut_from_two_surfaces(
    cairo_surface_t* base_surface,
    cairo_surface_t* mod_surface,
    const char* lut_file_path,
    const char* file_description,
    const char* file_copyright,
    gint grid_points);

/**
 * Export a 3D LUT from a square HALD (ImageMagick / HaldCLUT) image: side S = L³,
 * LUT_3D_SIZE n = L², standard Hald (x, y) → (r, g, b) mapping.
 * @return TRUE on success, FALSE on invalid size, format, I/O, or memory failure
 */
gboolean filter_save_3d_lut_from_hald_image(cairo_surface_t* surface, const char* lut_file_path);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_LUT3D_H */
