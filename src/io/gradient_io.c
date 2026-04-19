/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "io/gradient_io.h"
#include "io/gradient_ggr.h"
#include "io/gradient_grd.h"
#include "io/gradient_rgr.h"
#include "io/gradient_svg.h"
#include "debug_logger.h"
#include "i18n.h"
#include <glib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

typedef enum {
    GRADIENT_FORMAT_UNKNOWN = 0,
    GRADIENT_FORMAT_GGR,
    GRADIENT_FORMAT_GRD,
    GRADIENT_FORMAT_RGR,
    GRADIENT_FORMAT_SVG
} GradientFormat;

static GradientFormat detect_format(const char* filename) {
    if (!filename) return GRADIENT_FORMAT_UNKNOWN;
    const char* ext = strrchr(filename, '.');
    if (!ext) return GRADIENT_FORMAT_UNKNOWN;
    ext++; /* skip the dot */
    if (g_ascii_strcasecmp(ext, "ggr") == 0) return GRADIENT_FORMAT_GGR;
    if (g_ascii_strcasecmp(ext, "grd") == 0) return GRADIENT_FORMAT_GRD;
    if (g_ascii_strcasecmp(ext, "rgr") == 0) return GRADIENT_FORMAT_RGR;
    if (g_ascii_strcasecmp(ext, "svg") == 0) return GRADIENT_FORMAT_SVG;
    return GRADIENT_FORMAT_UNKNOWN;
}

static GradientIOError map_ggr_error(GradientGgrError err) {
    switch (err) {
        case GRADIENT_GGR_ERROR_NONE:               return GRADIENT_IO_ERROR_NONE;
        case GRADIENT_GGR_ERROR_INVALID_PARAMETERS: return GRADIENT_IO_ERROR_INVALID_PARAMETERS;
        case GRADIENT_GGR_ERROR_FILE_NOT_FOUND:     return GRADIENT_IO_ERROR_FILE_NOT_FOUND;
        case GRADIENT_GGR_ERROR_FILE_READ_ERROR:    return GRADIENT_IO_ERROR_FILE_READ_ERROR;
        case GRADIENT_GGR_ERROR_FILE_WRITE_ERROR:   return GRADIENT_IO_ERROR_FILE_WRITE_ERROR;
        case GRADIENT_GGR_ERROR_CORRUPT_FILE:       return GRADIENT_IO_ERROR_CORRUPT_FILE;
        case GRADIENT_GGR_ERROR_OUT_OF_MEMORY:      return GRADIENT_IO_ERROR_OUT_OF_MEMORY;
        default:                                     return GRADIENT_IO_ERROR_UNKNOWN;
    }
}

static GradientIOError map_grd_error(GradientGrdError err) {
    switch (err) {
        case GRADIENT_GRD_ERROR_NONE:               return GRADIENT_IO_ERROR_NONE;
        case GRADIENT_GRD_ERROR_INVALID_PARAMETERS: return GRADIENT_IO_ERROR_INVALID_PARAMETERS;
        case GRADIENT_GRD_ERROR_FILE_NOT_FOUND:     return GRADIENT_IO_ERROR_FILE_NOT_FOUND;
        case GRADIENT_GRD_ERROR_FILE_READ_ERROR:    return GRADIENT_IO_ERROR_FILE_READ_ERROR;
        case GRADIENT_GRD_ERROR_FILE_WRITE_ERROR:   return GRADIENT_IO_ERROR_FILE_WRITE_ERROR;
        case GRADIENT_GRD_ERROR_CORRUPT_FILE:       return GRADIENT_IO_ERROR_CORRUPT_FILE;
        case GRADIENT_GRD_ERROR_OUT_OF_MEMORY:      return GRADIENT_IO_ERROR_OUT_OF_MEMORY;
        case GRADIENT_GRD_ERROR_UNSUPPORTED_FORMAT: return GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT;
        default:                                     return GRADIENT_IO_ERROR_UNKNOWN;
    }
}

static GradientIOError map_rgr_error(GradientRgrError err) {
    switch (err) {
        case GRADIENT_RGR_ERROR_NONE:               return GRADIENT_IO_ERROR_NONE;
        case GRADIENT_RGR_ERROR_INVALID_PARAMETERS: return GRADIENT_IO_ERROR_INVALID_PARAMETERS;
        case GRADIENT_RGR_ERROR_FILE_NOT_FOUND:     return GRADIENT_IO_ERROR_FILE_NOT_FOUND;
        case GRADIENT_RGR_ERROR_FILE_READ_ERROR:    return GRADIENT_IO_ERROR_FILE_READ_ERROR;
        case GRADIENT_RGR_ERROR_FILE_WRITE_ERROR:   return GRADIENT_IO_ERROR_FILE_WRITE_ERROR;
        case GRADIENT_RGR_ERROR_CORRUPT_FILE:       return GRADIENT_IO_ERROR_CORRUPT_FILE;
        case GRADIENT_RGR_ERROR_OUT_OF_MEMORY:      return GRADIENT_IO_ERROR_OUT_OF_MEMORY;
        default:                                     return GRADIENT_IO_ERROR_UNKNOWN;
    }
}

static GradientIOError map_svg_error(GradientSvgError err) {
    switch (err) {
        case GRADIENT_SVG_ERROR_NONE:               return GRADIENT_IO_ERROR_NONE;
        case GRADIENT_SVG_ERROR_INVALID_PARAMETERS: return GRADIENT_IO_ERROR_INVALID_PARAMETERS;
        case GRADIENT_SVG_ERROR_FILE_NOT_FOUND:     return GRADIENT_IO_ERROR_FILE_NOT_FOUND;
        case GRADIENT_SVG_ERROR_FILE_READ_ERROR:    return GRADIENT_IO_ERROR_FILE_READ_ERROR;
        case GRADIENT_SVG_ERROR_FILE_WRITE_ERROR:   return GRADIENT_IO_ERROR_FILE_WRITE_ERROR;
        case GRADIENT_SVG_ERROR_CORRUPT_FILE:       return GRADIENT_IO_ERROR_CORRUPT_FILE;
        case GRADIENT_SVG_ERROR_OUT_OF_MEMORY:      return GRADIENT_IO_ERROR_OUT_OF_MEMORY;
        default:                                     return GRADIENT_IO_ERROR_UNKNOWN;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

GradientSet* gradient_io_load(const char* filename, GradientIOError* error_out) {
    GradientFormat fmt;
    GradientSet* set = NULL;

    if (!filename) {
        debug_log("WRN", "gradient_io_load: NULL filename");
        if (error_out) *error_out = GRADIENT_IO_ERROR_INVALID_PARAMETERS;
        return NULL;
    }

    fmt = detect_format(filename);
    if (fmt == GRADIENT_FORMAT_UNKNOWN) {
        debug_log("WRN", "gradient_io_load: unsupported extension for '%s'", filename);
        if (error_out) *error_out = GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT;
        return NULL;
    }

    if (fmt == GRADIENT_FORMAT_GGR) {
        GradientGgrError ggr_err = GRADIENT_GGR_ERROR_NONE;
        set = gradient_ggr_load(filename, &ggr_err);
        if (error_out) *error_out = map_ggr_error(ggr_err);
        return set;
    }

    if (fmt == GRADIENT_FORMAT_GRD) {
        GradientGrdError grd_err = GRADIENT_GRD_ERROR_NONE;
        set = gradient_grd_load(filename, &grd_err);
        if (error_out) *error_out = map_grd_error(grd_err);
        return set;
    }

    if (fmt == GRADIENT_FORMAT_RGR) {
        GradientRgrError rgr_err = GRADIENT_RGR_ERROR_NONE;
        set = gradient_rgr_load(filename, &rgr_err);
        if (error_out) *error_out = map_rgr_error(rgr_err);
        return set;
    }

    if (fmt == GRADIENT_FORMAT_SVG) {
        GradientSvgError svg_err = GRADIENT_SVG_ERROR_NONE;
        set = gradient_svg_load(filename, &svg_err);
        if (error_out) *error_out = map_svg_error(svg_err);
        return set;
    }

    if (error_out) *error_out = GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT;
    return NULL;
}

gboolean gradient_io_save(const GradientSet* set, const char* filename,
                           GradientIOError* error_out) {
    GradientFormat fmt;

    if (!set || !filename) {
        debug_log("WRN", "gradient_io_save: NULL argument");
        if (error_out) *error_out = GRADIENT_IO_ERROR_INVALID_PARAMETERS;
        return FALSE;
    }

    fmt = detect_format(filename);
    if (fmt == GRADIENT_FORMAT_UNKNOWN) {
        debug_log("WRN", "gradient_io_save: unsupported extension for '%s'", filename);
        if (error_out) *error_out = GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT;
        return FALSE;
    }

    if (fmt == GRADIENT_FORMAT_GGR) {
        GradientGgrError ggr_err = GRADIENT_GGR_ERROR_NONE;
        gboolean ok = gradient_ggr_save(set, filename, &ggr_err);
        if (error_out) *error_out = map_ggr_error(ggr_err);
        return ok;
    }

    if (fmt == GRADIENT_FORMAT_GRD) {
        GradientGrdError grd_err = GRADIENT_GRD_ERROR_NONE;
        gboolean ok = gradient_grd_save(set, filename, &grd_err);
        if (error_out) *error_out = map_grd_error(grd_err);
        return ok;
    }

    if (fmt == GRADIENT_FORMAT_RGR) {
        GradientRgrError rgr_err = GRADIENT_RGR_ERROR_NONE;
        gboolean ok = gradient_rgr_save(set, filename, &rgr_err);
        if (error_out) *error_out = map_rgr_error(rgr_err);
        return ok;
    }

    if (fmt == GRADIENT_FORMAT_SVG) {
        GradientSvgError svg_err = GRADIENT_SVG_ERROR_NONE;
        gboolean ok = gradient_svg_save(set, filename, &svg_err);
        if (error_out) *error_out = map_svg_error(svg_err);
        return ok;
    }

    if (error_out) *error_out = GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT;
    return FALSE;
}

gboolean gradient_io_is_supported(const char* filename) {
    return detect_format(filename) != GRADIENT_FORMAT_UNKNOWN;
}

const char* gradient_io_get_error_message(GradientIOError error, const char* filename) {
    static char error_buffer[512];

    switch (error) {
        case GRADIENT_IO_ERROR_NONE:
            return _("Gradient file loaded successfully");
        case GRADIENT_IO_ERROR_INVALID_PARAMETERS:
            return _("Invalid parameters provided");
        case GRADIENT_IO_ERROR_FILE_NOT_FOUND:
            if (filename) {
                g_snprintf(error_buffer, sizeof(error_buffer),
                           _("Gradient file not found: %s"), filename);
                return error_buffer;
            }
            return _("Gradient file not found");
        case GRADIENT_IO_ERROR_FILE_READ_ERROR:
            return _("Failed to read gradient file. The file may be locked or inaccessible.");
        case GRADIENT_IO_ERROR_FILE_WRITE_ERROR:
            return _("Failed to write gradient file. The file may be locked or the disk may be full.");
        case GRADIENT_IO_ERROR_UNSUPPORTED_FORMAT:
            return _("Unsupported gradient file format. Supported formats: .ggr, .grd, .rgr, .svg");
        case GRADIENT_IO_ERROR_CORRUPT_FILE:
            return _("Gradient file is corrupted or incomplete.");
        case GRADIENT_IO_ERROR_OUT_OF_MEMORY:
            return _("Out of memory loading gradient file.");
        case GRADIENT_IO_ERROR_UNKNOWN:
        default:
            return _("An unknown error occurred while loading the gradient file.");
    }
}
