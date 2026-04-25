/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "io/lut3d_io.h"
#include "debug_logger.h"
#include "i18n.h"
#include "io/lut3d_cube.h"
#include "io/lut3d_look.h"
#include <glib.h>
#include <string.h>

typedef enum {
    LUT3D_FORMAT_UNKNOWN = 0,
    LUT3D_FORMAT_CUBE,
    LUT3D_FORMAT_LOOK
} Lut3dFormat;

static Lut3dFormat detect_format(const char* filename) {
    if (!filename)
        return LUT3D_FORMAT_UNKNOWN;
    const char* ext = strrchr(filename, '.');
    if (!ext)
        return LUT3D_FORMAT_UNKNOWN;
    ext++;
    if (g_ascii_strcasecmp(ext, "cube") == 0)
        return LUT3D_FORMAT_CUBE;
    if (g_ascii_strcasecmp(ext, "look") == 0)
        return LUT3D_FORMAT_LOOK;
    return LUT3D_FORMAT_UNKNOWN;
}

static Lut3DIOError map_cube_error(Lut3dCubeError e) {
    switch (e) {
        case LUT3D_CUBE_ERROR_NONE:
            return LUT3D_IO_ERROR_NONE;
        case LUT3D_CUBE_ERROR_INVALID_PARAMETERS:
            return LUT3D_IO_ERROR_INVALID_PARAMETERS;
        case LUT3D_CUBE_ERROR_FILE_NOT_FOUND:
            return LUT3D_IO_ERROR_FILE_NOT_FOUND;
        case LUT3D_CUBE_ERROR_FILE_READ_ERROR:
            return LUT3D_IO_ERROR_FILE_READ_ERROR;
        case LUT3D_CUBE_ERROR_FILE_WRITE_ERROR:
            return LUT3D_IO_ERROR_FILE_WRITE_ERROR;
        case LUT3D_CUBE_ERROR_CORRUPT_FILE:
            return LUT3D_IO_ERROR_CORRUPT_FILE;
        case LUT3D_CUBE_ERROR_OUT_OF_MEMORY:
            return LUT3D_IO_ERROR_OUT_OF_MEMORY;
        case LUT3D_CUBE_ERROR_UNSUPPORTED_FORMAT:
            return LUT3D_IO_ERROR_UNSUPPORTED_FORMAT;
        default:
            return LUT3D_IO_ERROR_UNKNOWN;
    }
}

static Lut3DIOError map_look_error(Lut3dLookError e) {
    switch (e) {
        case LUT3D_LOOK_ERROR_NONE:
            return LUT3D_IO_ERROR_NONE;
        case LUT3D_LOOK_ERROR_INVALID_PARAMETERS:
            return LUT3D_IO_ERROR_INVALID_PARAMETERS;
        case LUT3D_LOOK_ERROR_FILE_NOT_FOUND:
            return LUT3D_IO_ERROR_FILE_NOT_FOUND;
        case LUT3D_LOOK_ERROR_FILE_READ_ERROR:
            return LUT3D_IO_ERROR_FILE_READ_ERROR;
        case LUT3D_LOOK_ERROR_FILE_WRITE_ERROR:
            return LUT3D_IO_ERROR_FILE_WRITE_ERROR;
        case LUT3D_LOOK_ERROR_CORRUPT_FILE:
            return LUT3D_IO_ERROR_CORRUPT_FILE;
        case LUT3D_LOOK_ERROR_OUT_OF_MEMORY:
            return LUT3D_IO_ERROR_OUT_OF_MEMORY;
        case LUT3D_LOOK_ERROR_UNSUPPORTED_FORMAT:
            return LUT3D_IO_ERROR_UNSUPPORTED_FORMAT;
        default:
            return LUT3D_IO_ERROR_UNKNOWN;
    }
}

ColorLut3D* lut3d_io_load(const char* filename, Lut3DIOError* error_out) {
    Lut3dFormat fmt;

    if (!filename) {
        debug_log("WRN", "lut3d_io_load: NULL filename");
        if (error_out)
            *error_out = LUT3D_IO_ERROR_INVALID_PARAMETERS;
        return NULL;
    }
    fmt = detect_format(filename);
    if (fmt == LUT3D_FORMAT_UNKNOWN) {
        debug_log("WRN", "lut3d_io_load: unsupported extension for '%s'", filename);
        if (error_out)
            *error_out = LUT3D_IO_ERROR_UNSUPPORTED_FORMAT;
        return NULL;
    }
    if (fmt == LUT3D_FORMAT_CUBE) {
        Lut3dCubeError ce = LUT3D_CUBE_ERROR_NONE;
        ColorLut3D* lut = lut3d_cube_load(filename, &ce);
        if (error_out)
            *error_out = map_cube_error(ce);
        return lut;
    }
    if (fmt == LUT3D_FORMAT_LOOK) {
        Lut3dLookError le = LUT3D_LOOK_ERROR_NONE;
        ColorLut3D* lut = lut3d_look_load(filename, &le);
        if (error_out)
            *error_out = map_look_error(le);
        return lut;
    }
    if (error_out)
        *error_out = LUT3D_IO_ERROR_UNSUPPORTED_FORMAT;
    return NULL;
}

gboolean lut3d_io_save(const ColorLut3D* lut, const char* filename, Lut3DIOError* error_out) {
    Lut3dFormat fmt;
    if (!lut || !filename) {
        debug_log("WRN", "lut3d_io_save: NULL argument");
        if (error_out)
            *error_out = LUT3D_IO_ERROR_INVALID_PARAMETERS;
        return FALSE;
    }
    fmt = detect_format(filename);
    if (fmt == LUT3D_FORMAT_UNKNOWN) {
        debug_log("WRN", "lut3d_io_save: unsupported extension for '%s'", filename);
        if (error_out)
            *error_out = LUT3D_IO_ERROR_UNSUPPORTED_FORMAT;
        return FALSE;
    }
    if (fmt == LUT3D_FORMAT_CUBE) {
        Lut3dCubeError ce = LUT3D_CUBE_ERROR_NONE;
        gboolean ok = lut3d_cube_save(lut, filename, &ce);
        if (error_out)
            *error_out = map_cube_error(ce);
        return ok;
    }
    if (fmt == LUT3D_FORMAT_LOOK) {
        Lut3dLookError le = LUT3D_LOOK_ERROR_NONE;
        gboolean ok = lut3d_look_save(lut, filename, &le);
        if (error_out)
            *error_out = map_look_error(le);
        return ok;
    }
    if (error_out)
        *error_out = LUT3D_IO_ERROR_UNSUPPORTED_FORMAT;
    return FALSE;
}

gboolean lut3d_io_is_supported(const char* filename) {
    return detect_format(filename) != LUT3D_FORMAT_UNKNOWN;
}

const char* lut3d_io_get_error_message(Lut3DIOError error, const char* filename) {
    static char error_buffer[512];

    switch (error) {
        case LUT3D_IO_ERROR_NONE:
            return _("3D color LUT file loaded successfully");
        case LUT3D_IO_ERROR_INVALID_PARAMETERS:
            return _("Invalid parameters provided");
        case LUT3D_IO_ERROR_FILE_NOT_FOUND:
            if (filename) {
                g_snprintf(error_buffer, sizeof(error_buffer), _("3D LUT file not found: %s"), filename);
                return error_buffer;
            }
            return _("3D LUT file not found");
        case LUT3D_IO_ERROR_FILE_READ_ERROR:
            return _("Failed to read 3D LUT file. The file may be locked or inaccessible.");
        case LUT3D_IO_ERROR_FILE_WRITE_ERROR:
            return _("Failed to write 3D LUT file. The file may be locked or the disk may be full.");
        case LUT3D_IO_ERROR_UNSUPPORTED_FORMAT:
            return _("Unsupported 3D LUT file format.");
        case LUT3D_IO_ERROR_CORRUPT_FILE:
            return _("3D LUT file is corrupted or incomplete.");
        case LUT3D_IO_ERROR_OUT_OF_MEMORY:
            return _("Out of memory while loading 3D LUT file.");
        case LUT3D_IO_ERROR_UNKNOWN:
        default:
            return _("An unknown error occurred while reading the 3D LUT file.");
    }
}
