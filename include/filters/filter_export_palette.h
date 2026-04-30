/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_EXPORT_PALETTE_H
#define FILTER_EXPORT_PALETTE_H

#include "ocular.h"
#include <cairo.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimum / maximum `custom_max_colors` when count_mode is CUSTOM. */
#define FILTER_EXPORT_PALETTE_CUSTOM_MIN 2
#define FILTER_EXPORT_PALETTE_CUSTOM_MAX 256

typedef enum {
    /** Quantize with median cut up to 256 colors, then export distinct colors found. */
    FILTER_PALETTE_EXPORT_COUNT_AUTO = 0,
    /** Quantize with median cut to exactly this many colors (see custom_max_colors). */
    FILTER_PALETTE_EXPORT_COUNT_CUSTOM = 1,
} FilterPaletteExportCountMode;

/**
 * Build an OcPalette from an ARGB32 Cairo surface without saving to disk.
 * Used by the preview widget before the user picks a file path.
 * The caller must free the result with ocularFreePalette() when done.
 *
 * @param surface           Cairo image surface (ARGB32)
 * @param count_mode        AUTO or CUSTOM
 * @param custom_max_colors Used only when count_mode is CUSTOM (2–256)
 * @param out_palette       Palette to fill; must be zero-initialized by caller
 * @return TRUE on success
 */
gboolean filter_build_preview_palette(cairo_surface_t* surface,
                                      FilterPaletteExportCountMode count_mode,
                                      gint custom_max_colors,
                                      OcPalette* out_palette);

/**
 * Export an ARGB32 image to an Ocular-supported palette file by quantizing with
 * ocularPalettetizeFromImage (same path as the Palettize filter), collecting distinct
 * opaque RGB values from the result, and writing via save_gimp_palette, save_riff_palette,
 * save_aco_palette, save_paintnet_palette, or save_act_palette based on @a file_path
 * extension (.gpl, .pal, .aco, .txt, .act). Adobe Swatch Exchange (.ase) is import-only in Ocular.
 *
 * @param surface           Cairo image surface (ARGB32)
 * @param file_path         Destination path; extension selects format (case-insensitive)
 * @param count_mode        AUTO uses up to 256 palette slots; CUSTOM uses custom_max_colors (2–256)
 * @param custom_max_colors Ignored when count_mode is AUTO; otherwise must be in
 *                          [FILTER_EXPORT_PALETTE_CUSTOM_MIN, FILTER_EXPORT_PALETTE_CUSTOM_MAX]
 * @param palette_name      Palette name stored in the file; NULL uses "RasterLab Export"
 * @return TRUE on success
 */
gboolean filter_export_palette_from_surface(cairo_surface_t* surface,
                                            const char* file_path,
                                            FilterPaletteExportCountMode count_mode,
                                            gint custom_max_colors,
                                            const char* palette_name);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_EXPORT_PALETTE_H */
