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
    PluginError error;

    if (!doc || !filename) {
        g_warning("Invalid parameters for image_io_save");
        return FALSE;
    }

    /* Use default options if not provided */
    if (!opts) {
        memset(&default_opts, 0, sizeof(SaveOptions));
        default_opts.quality = -1;
        default_opts.compression_level = -1;
        default_opts.preserve_alpha = doc->has_alpha ? true : false;
        default_opts.flatten_layers = FALSE;
        opts = &default_opts;
    }

    /* Find appropriate plugin */
    handler = format_registry_find_saver(filename);
    if (!handler) {
        g_warning("No plugin found to save file: %s", filename);
        return FALSE;
    }

    /* Call plugin's save function */
    error = handler->plugin->callbacks.save(doc, filename, opts);

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
