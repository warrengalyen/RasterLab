/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include "app/settings.h"
#include "document.h"
#include "image_format_plugin.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load an image file using the plugin system
 * @param doc Document to load image into
 * @param filename Path to the image file
 * @param error_out Optional pointer to receive the plugin error code (can be NULL)
 * @param settings Optional color management settings (use embedded ICC, rendering intent, BPC). If NULL, defaults are used.
 * @return TRUE on success, FALSE on failure
 */
gboolean image_io_load(ImageDocument* doc, const char* filename, PluginError* error_out, const Settings* settings);

/**
 * Return TRUE if a file can be loaded by a registered format plugin (header + extension probe).
 */
gboolean image_io_is_supported_file(const char* filename);

/**
 * Get user-friendly error message from plugin error code
 * @param error The plugin error code
 * @param filename Optional filename for context in error messages
 * @return User-friendly error message string (static, do not free)
 */
const char* image_io_get_error_message(PluginError error, const char* filename);

/**
 * Save an image file using the plugin system
 * @param doc Document to save
 * @param filename Path to save the file
 * @param opts Save options (can be NULL for defaults)
 * @param error_out Optional pointer to receive the plugin error code (can be NULL)
 * @return TRUE on success, FALSE on failure
 */
gboolean image_io_save(ImageDocument* doc, const char* filename, const SaveOptions* opts, PluginError* error_out);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_IO_H */
