#include "document.h"
#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Read header bytes from a file
 */
static gboolean read_file_header(const char* filename, uint8_t* header, size_t header_size, size_t* bytes_read) {
    FILE* file;

    if (!filename || !header || header_size == 0) {
        return FALSE;
    }

    file = g_fopen(filename, "rb");
    if (!file) {
        return FALSE;
    }

    *bytes_read = fread(header, 1, header_size, file);
    fclose(file);

    return *bytes_read > 0;
}

/**
 * Load image using plugin system
 */
gboolean image_io_load(ImageDocument* doc, const char* filename) {
    FormatHandler* handler;
    uint8_t header[64];
    size_t header_size = 0;
    PluginError error;

    if (!doc || !filename) {
        g_warning("Invalid parameters for image_io_load");
        return FALSE;
    }

    /* Read file header for format detection */
    if (!read_file_header(filename, header, sizeof(header), &header_size)) {
        g_warning("Failed to read file header: %s", filename);
        return FALSE;
    }

    /* Find appropriate plugin */
    handler = format_registry_find_loader(filename, header, header_size);
    if (!handler) {
        g_warning("No plugin found to load file: %s", filename);
        return FALSE;
    }

    /* Call plugin's load function */
    error = handler->plugin->callbacks.load(doc, filename);

    if (error != PLUGIN_ERROR_NONE) {
        g_warning("Plugin failed to load file %s: error %d", filename, error);
        return FALSE;
    }

    /* Update document filename */
    if (doc->file_path) {
        g_free(doc->file_path);
    }
    doc->file_path = g_strdup(filename);

    gchar* basename = g_path_get_basename(filename);
    g_free(doc->filename);
    doc->filename = basename;

    return TRUE;
}

/**
 * Save image using plugin system
 */
gboolean image_io_save(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    FormatHandler* handler;
    SaveOptions default_opts;
    SaveOptions* actual_opts = NULL;
    PluginError error;
    size_t plugin_options_size = 0;
    void* plugin_data = NULL;

    if (!doc || !filename) {
        g_warning("Invalid parameters for image_io_save");
        return FALSE;
    }

    /* Find appropriate plugin */
    handler = format_registry_find_saver(filename);
    if (!handler) {
        g_warning("No plugin found to save file: %s", filename);
        return FALSE;
    }

    /* Allocate options structure */
    actual_opts = g_malloc(sizeof(SaveOptions));
    if (!actual_opts) {
        g_warning("Failed to allocate memory for save options");
        return FALSE;
    }

    /* Initialize with provided options or defaults */
    if (opts) {
        /* Copy base options */
        actual_opts->quality = opts->quality;
        actual_opts->compression_level = opts->compression_level;
        actual_opts->preserve_alpha = opts->preserve_alpha;
        actual_opts->flatten_layers = opts->flatten_layers;

        /* Use existing plugin_data if provided, otherwise allocate new one */
        if (opts->plugin_data) {
            /* Use existing plugin_data (e.g., from save options dialog) */
            actual_opts->plugin_data = opts->plugin_data;
            /* Don't free this - it's owned by the caller */
            plugin_data = NULL; /* Mark as not allocated here */
        } else {
            /* Allocate and initialize plugin-specific options if plugin supports them */
            if (handler && handler->plugin && handler->plugin->callbacks.get_save_options_size) {
                plugin_options_size = handler->plugin->callbacks.get_save_options_size();
                if (plugin_options_size > 0) {
                    plugin_data = g_malloc0(plugin_options_size);
                    if (plugin_data) {
                        /* Initialize with plugin's default values */
                        if (handler->plugin->callbacks.init_save_options) {
                            handler->plugin->callbacks.init_save_options(plugin_data);
                        }
                        actual_opts->plugin_data = plugin_data;
                    }
                }
            } else {
                actual_opts->plugin_data = NULL;
            }
        }
    } else {
        actual_opts->quality = -1;
        actual_opts->compression_level = -1;
        actual_opts->preserve_alpha = doc->has_alpha ? true : false;
        actual_opts->flatten_layers = FALSE;
        actual_opts->plugin_data = NULL;

        /* Allocate and initialize plugin-specific options if plugin supports them */
        if (handler && handler->plugin && handler->plugin->callbacks.get_save_options_size) {
            plugin_options_size = handler->plugin->callbacks.get_save_options_size();
            if (plugin_options_size > 0) {
                plugin_data = g_malloc0(plugin_options_size);
                if (plugin_data) {
                    /* Initialize with plugin's default values */
                    if (handler->plugin->callbacks.init_save_options) {
                        handler->plugin->callbacks.init_save_options(plugin_data);
                    }
                    actual_opts->plugin_data = plugin_data;
                }
            }
        }
    }

    /* Call plugin's save function */
    if (!handler || !handler->plugin || !handler->plugin->callbacks.save) {
        g_warning("Invalid plugin handler for saving file: %s", filename);
        if (plugin_data) {
            g_free(plugin_data);
        }
        g_free(actual_opts);
        return FALSE;
    }
    error = handler->plugin->callbacks.save(doc, filename, actual_opts);

    /* Clean up plugin-specific options */
    if (plugin_data) {
        g_free(plugin_data);
    }

    /* Clean up options structure */
    g_free(actual_opts);

    if (error != PLUGIN_ERROR_NONE) {
        g_warning("Plugin failed to save file %s: error %d", filename, error);
        return FALSE;
    }

    /* Update document filename */
    if (doc->file_path) {
        g_free(doc->file_path);
    }
    doc->file_path = g_strdup(filename);
    doc->modified = FALSE;

    return TRUE;
}
