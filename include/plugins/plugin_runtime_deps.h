/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_RUNTIME_DEPS_H
#define PLUGIN_RUNTIME_DEPS_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return TRUE if all shared libraries required for the format are present in the
 * application directory (directory containing the executable). Implementations
 * exist for Windows (_WIN32, GCC/MinGW DLL layout) and for Unix (Linux, macOS;
 * .so/.dylib next to the executable).
 */
gboolean plugin_runtime_deps_jpeg_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_png_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_webp_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_tiff_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_heic_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_jxl_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_exr_ok(const gchar* app_dir);

/**
 * Shared libraries used by the debug log summary.
 */
gboolean plugin_runtime_deps_zlib_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_libde265_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_libaom_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_libheif_ok(const gchar* app_dir);
gboolean plugin_runtime_deps_lcms2_ok(const gchar* app_dir);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_RUNTIME_DEPS_H */
