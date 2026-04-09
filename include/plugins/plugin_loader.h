/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include "image_format_plugin.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Plugin handle - opaque pointer to loaded plugin
 */
typedef struct PluginHandle PluginHandle;

/**
 * Load a plugin from a shared library file
 * @param plugin_path Path to the plugin shared library (.so, .dylib, or .dll)
 * @return Plugin handle on success, NULL on failure
 */
PluginHandle* plugin_loader_load(const char* plugin_path);

/**
 * Unload a plugin
 * @param handle Plugin handle returned by plugin_loader_load
 */
void plugin_loader_unload(PluginHandle* handle);

/**
 * Get the plugin structure from a loaded plugin
 * @param handle Plugin handle
 * @return Pointer to ImageFormatPlugin structure, or NULL if invalid
 */
ImageFormatPlugin* plugin_loader_get_plugin(PluginHandle* handle);

/**
 * Scan directory for plugins and load them
 * @param directory_path Path to directory containing plugin files
 * @return List of PluginHandle* (GList), caller must free with g_list_free()
 */
GList* plugin_loader_scan_directory(const char* directory_path);

/**
 * Free a list of plugin handles (unloads all plugins)
 * @param plugin_list List of PluginHandle* returned by plugin_loader_scan_directory
 */
void plugin_loader_free_list(GList* plugin_list);

/**
 * Initialize plugin loader system (should be called once at startup)
 */
void plugin_loader_init(void);

/**
 * Shutdown plugin loader system (should be called once at shutdown)
 */
void plugin_loader_shutdown(void);

/**
 * Initialize plugin with host API (internal, for use by format registry)
 * @param handle Plugin handle
 * @param host_api Host API structure
 * @return TRUE on success, FALSE on failure
 */
gboolean plugin_loader_init_with_host(PluginHandle* handle, const ImageFormatHostAPI* host_api);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_LOADER_H */
