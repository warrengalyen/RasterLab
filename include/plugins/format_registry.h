#ifndef FORMAT_REGISTRY_H
#define FORMAT_REGISTRY_H

#include "image_format_plugin.h"
#include "plugins/plugin_loader.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format handler entry
 */
typedef struct {
    PluginHandle* plugin_handle;
    ImageFormatPlugin* plugin;
    FormatInfo format_info;
    int32_t priority;
} FormatHandler;

/**
 * Initialize the format registry
 * Should be called once at application startup
 */
void format_registry_init(void);

/**
 * Shutdown the format registry and unload all plugins
 * Should be called once at application shutdown
 */
void format_registry_shutdown(void);

/**
 * Register a format plugin
 * @param plugin_handle Handle to loaded plugin (can be NULL for built-in plugins)
 * @param plugin Plugin structure from plugin_loader_get_plugin()
 * @return TRUE on success, FALSE on failure
 */
gboolean format_registry_register(PluginHandle* plugin_handle, ImageFormatPlugin* plugin);

/**
 * Register a built-in plugin directly (without plugin handle)
 * @param plugin Plugin structure (will be copied)
 * @return TRUE on success, FALSE on failure
 */
gboolean format_registry_register_builtin(ImageFormatPlugin* plugin);

/**
 * Find plugin handler for loading a file
 * Uses header bytes and filename extension to determine the best handler
 * @param filename File path/name
 * @param header Header bytes from file (at least 16 bytes recommended)
 * @param header_size Size of header bytes provided
 * @return FormatHandler* on success, NULL if no handler found
 */
FormatHandler* format_registry_find_loader(const char* filename,
                                           const uint8_t* header,
                                           size_t header_size);

/**
 * Find plugin handler for saving a file
 * Uses filename extension to determine the handler
 * @param filename File path/name
 * @return FormatHandler* on success, NULL if no handler found
 */
FormatHandler* format_registry_find_saver(const char* filename);

/**
 * Get all registered format handlers
 * @return List of FormatHandler* (GList), caller should not modify or free
 */
GList* format_registry_get_all_handlers(void);

/**
 * Get file filter string for GTK file chooser
 * Returns formatted string suitable for gtk_file_filter_add_pattern()
 * @return Newly allocated string, caller must free with g_free()
 */
gchar* format_registry_get_file_filter_patterns(void);

/**
 * Get display names for file formats (for UI)
 * @return Newly allocated string array, caller must free with g_strfreev()
 */
gchar** format_registry_get_format_names(void);

#ifdef __cplusplus
}
#endif

#endif /* FORMAT_REGISTRY_H */
