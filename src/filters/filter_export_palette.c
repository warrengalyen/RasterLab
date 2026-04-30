/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "filters/filter_export_palette.h"
#include "debug_logger.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

static int oc_palette_color_cmp(const void* a, const void* b) {
    const OcPaletteColor* ca = (const OcPaletteColor*)a;
    const OcPaletteColor* cb = (const OcPaletteColor*)b;
    if (ca->r != cb->r)
        return ca->r - cb->r;
    if (ca->g != cb->g)
        return ca->g - cb->g;
    return ca->b - cb->b;
}

/**
 * Collect distinct RGB colors from RGBA buffer (opaque pixels only), sorted for stable output.
 */
static gboolean build_palette_from_quantized_rgba(const guchar* rgba,
                                                  gint width,
                                                  gint height,
                                                  OcPalette* out_palette,
                                                  gint capacity_hint) {
    gint x, y;
    gint cap = capacity_hint > 0 ? capacity_hint : 256;
    if (cap < 1)
        cap = 256;

    out_palette->colors = (OcPaletteColor*)g_malloc0((gsize)cap * sizeof(OcPaletteColor));
    if (!out_palette->colors)
        return FALSE;
    out_palette->capacity = cap;
    out_palette->num_colors = 0;

    for (y = 0; y < height; y++) {
        const guchar* row = rgba + y * width * 4;
        for (x = 0; x < width; x++) {
            gint r = row[x * 4 + 0];
            gint g = row[x * 4 + 1];
            gint b = row[x * 4 + 2];
            gint a = row[x * 4 + 3];
            gint i;

            if (a == 0)
                continue;

            for (i = 0; i < out_palette->num_colors; i++) {
                if (out_palette->colors[i].r == r && out_palette->colors[i].g == g &&
                    out_palette->colors[i].b == b)
                    break;
            }
            if (i < out_palette->num_colors)
                continue;

            if (out_palette->num_colors >= out_palette->capacity) {
                gint new_cap = out_palette->capacity * 2;
                OcPaletteColor* grown =
                    (OcPaletteColor*)g_realloc(out_palette->colors, (gsize)new_cap * sizeof(OcPaletteColor));
                if (!grown) {
                    ocularFreePalette(out_palette);
                    return FALSE;
                }
                out_palette->colors = grown;
                out_palette->capacity = new_cap;
            }

            out_palette->colors[out_palette->num_colors].r = r;
            out_palette->colors[out_palette->num_colors].g = g;
            out_palette->colors[out_palette->num_colors].b = b;
            out_palette->colors[out_palette->num_colors].name[0] = '\0';
            out_palette->num_colors++;
        }
    }

    if (out_palette->num_colors == 0) {
        ocularFreePalette(out_palette);
        return FALSE;
    }

    qsort(out_palette->colors, (size_t)out_palette->num_colors, sizeof(OcPaletteColor),
          oc_palette_color_cmp);

    return TRUE;
}

static gboolean save_palette_for_path(const char* file_path, const OcPalette* palette) {
    const char* ext;
    OC_STATUS st;

    if (!file_path)
        return FALSE;

    ext = strrchr(file_path, '.');
    if (!ext)
        return FALSE;

    if (g_ascii_strcasecmp(ext, ".gpl") == 0)
        st = save_gimp_palette(file_path, palette);
    else if (g_ascii_strcasecmp(ext, ".pal") == 0)
        st = save_riff_palette(file_path, palette);
    else if (g_ascii_strcasecmp(ext, ".aco") == 0)
        st = save_aco_palette(file_path, palette);
    else if (g_ascii_strcasecmp(ext, ".txt") == 0)
        st = save_paintnet_palette(file_path, palette);
    else if (g_ascii_strcasecmp(ext, ".act") == 0)
        st = save_act_palette(file_path, palette);
    else {
        debug_log("WRN", "Palette export: unsupported extension (use .gpl .pal .aco .txt .act)");
        return FALSE;
    }

    if (st != OC_STATUS_OK) {
        debug_log("WRN", "Palette export: Ocular save failed with status %d", st);
        return FALSE;
    }

    return TRUE;
}

gboolean filter_build_preview_palette(cairo_surface_t* surface,
                                      FilterPaletteExportCountMode count_mode,
                                      gint custom_max_colors,
                                      OcPalette* out_palette) {
    gint width, height;
    guchar* rgba_in = NULL;
    guchar* rgba_out = NULL;
    gint max_colors;

    if (!surface || !out_palette) {
        return FALSE;
    }

    if (count_mode == FILTER_PALETTE_EXPORT_COUNT_CUSTOM) {
        if (custom_max_colors < FILTER_EXPORT_PALETTE_CUSTOM_MIN ||
            custom_max_colors > FILTER_EXPORT_PALETTE_CUSTOM_MAX) {
            debug_log("WRN", "Palette preview: custom_max_colors must be %d-%d",
                      FILTER_EXPORT_PALETTE_CUSTOM_MIN, FILTER_EXPORT_PALETTE_CUSTOM_MAX);
            return FALSE;
        }
        max_colors = custom_max_colors;
    } else {
        max_colors = FILTER_EXPORT_PALETTE_CUSTOM_MAX;
    }

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    rgba_in = (guchar*)g_malloc((gsize)width * height * 4);
    rgba_out = (guchar*)g_malloc((gsize)width * height * 4);
    if (!rgba_in || !rgba_out) {
        g_free(rgba_in);
        g_free(rgba_out);
        debug_log("WRN", "Palette preview: allocation failed");
        return FALSE;
    }

    if (!adjustments_cairo_to_rgba(surface, rgba_in)) {
        g_free(rgba_in);
        g_free(rgba_out);
        return FALSE;
    }

    {
        OC_STATUS pst = ocularPalettetizeFromImage(rgba_in,
                                                   rgba_out,
                                                   width,
                                                   height,
                                                   4,
                                                   OC_QUANTIZE_MEDIAN_CUT,
                                                   max_colors,
                                                   OC_DITHER_NONE,
                                                   0);
        if (pst != OC_STATUS_OK) {
            debug_log("WRN", "Palette preview: ocularPalettetizeFromImage failed (%d)", pst);
            g_free(rgba_in);
            g_free(rgba_out);
            return FALSE;
        }
    }

    g_free(rgba_in);
    rgba_in = NULL;

    if (!build_palette_from_quantized_rgba(rgba_out, width, height, out_palette, max_colors)) {
        debug_log("WRN", "Palette preview: no opaque colors found");
        g_free(rgba_out);
        return FALSE;
    }

    g_free(rgba_out);
    return TRUE;
}

gboolean filter_export_palette_from_surface(cairo_surface_t* surface,
                                            const char* file_path,
                                            FilterPaletteExportCountMode count_mode,
                                            gint custom_max_colors,
                                            const char* palette_name) {
    OcPalette palette = {0};
    gboolean ok = FALSE;

    if (!surface || !file_path) {
        return FALSE;
    }

    if (!filter_build_preview_palette(surface, count_mode, custom_max_colors, &palette)) {
        return FALSE;
    }

    if (palette_name && palette_name[0] != '\0') {
        strncpy(palette.name, palette_name, sizeof(palette.name) - 1);
        palette.name[sizeof(palette.name) - 1] = '\0';
    } else {
        strncpy(palette.name, "RasterLab Export", sizeof(palette.name) - 1);
        palette.name[sizeof(palette.name) - 1] = '\0';
    }

    ok = save_palette_for_path(file_path, &palette);
    ocularFreePalette(&palette);
    return ok;
}
