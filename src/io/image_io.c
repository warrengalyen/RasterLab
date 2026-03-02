#include "document.h"
#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#if HAVE_LCMS2
#include "color_manager.h"
#include "color_manager/icc_utils.h"
#endif
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
 * Get user-friendly error message from plugin error code
 */
const char* image_io_get_error_message(PluginError error, const char* filename) {
    static char error_buffer[512];

    switch (error) {
        case PLUGIN_ERROR_NONE:
            return "File loaded successfully";
        case PLUGIN_ERROR_INVALID_PARAMETERS:
            return "Invalid parameters provided";
        case PLUGIN_ERROR_FILE_NOT_FOUND:
            if (filename) {
                g_snprintf(error_buffer, sizeof(error_buffer), "File not found: %s", filename);
                return error_buffer;
            }
            return "File not found";
        case PLUGIN_ERROR_FILE_READ_ERROR:
            return "Failed to read file. The file may be locked or inaccessible.";
        case PLUGIN_ERROR_FILE_WRITE_ERROR:
            return "Failed to write file. The file may be locked or the disk may be full.";
        case PLUGIN_ERROR_UNSUPPORTED_FORMAT:
            return "Unsupported file format. The file format is not recognized or not supported.";
        case PLUGIN_ERROR_CORRUPT_FILE:
            return "File is corrupted or incomplete. The file may be damaged.";
        case PLUGIN_ERROR_OUT_OF_MEMORY:
            return "Out of memory. The file is too large to load.";
        case PLUGIN_ERROR_UNSUPPORTED_FEATURE:
            return "Unsupported feature. The file uses features that are not supported.";
        case PLUGIN_ERROR_USER_CANCELLED:
            return "Load cancelled.";
        case PLUGIN_ERROR_UNSUPPORTED_COMPRESSION:
            return "JPEG compression is not supported.";
        case PLUGIN_ERROR_UNKNOWN:
        default:
            return "An unknown error occurred while loading the file.";
    }
}

/**
 * Load image using plugin system
 * Returns TRUE on success, FALSE on failure.
 * If error_out is provided, it will be set to the plugin error code.
 */
/* Header probe size: must be at least 132 for DICOM (128-byte preamble + "DICM") */
#define IMAGE_IO_HEADER_PROBE_SIZE 256

gboolean image_io_load(ImageDocument* doc, const char* filename, PluginError* error_out) {
    FormatHandler* handler;
    uint8_t header[IMAGE_IO_HEADER_PROBE_SIZE];
    size_t header_size = 0;
    PluginError error = PLUGIN_ERROR_NONE;

    if (!doc || !filename) {
        g_warning("Invalid parameters for image_io_load");
        if (error_out) {
            *error_out = PLUGIN_ERROR_INVALID_PARAMETERS;
        }
        return FALSE;
    }

    /* Read file header for format detection */
    if (!read_file_header(filename, header, sizeof(header), &header_size)) {
        g_warning("Failed to read file header: %s", filename);
        if (error_out) {
            *error_out = PLUGIN_ERROR_FILE_READ_ERROR;
        }
        return FALSE;
    }

    /* Find appropriate plugin */
    handler = format_registry_find_loader(filename, header, header_size);
    if (!handler) {
        g_warning("No plugin found to load file: %s", filename);
        if (error_out) {
            *error_out = PLUGIN_ERROR_UNSUPPORTED_FORMAT;
        }
        return FALSE;
    }

    /* Call plugin's load function */
    error = handler->plugin->callbacks.load(doc, filename);

    if (error != PLUGIN_ERROR_NONE) {
        if (error != PLUGIN_ERROR_USER_CANCELLED)
            g_warning("Plugin failed to load file %s: error %d", filename, error);
        if (error_out) {
            *error_out = error;
        }
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

#if HAVE_LCMS2
    /* Apply load-time ICC conversion if plugin set a profile */
    if (doc->load_icc_profile && document_get_layer_count(doc) > 0) {
        ImageLayer* layer = document_get_layer(doc, 0);
        if (layer && layer->surface) {
            cairo_surface_flush(layer->surface);
            guchar* data = cairo_image_surface_get_data(layer->surface);
            if (data) {
                size_t pixel_count = (size_t)doc->width * (size_t)doc->height;
                cm_convert_sdr_to_srgb_argb32_from_profile(data, pixel_count, doc->load_icc_profile);
                cairo_surface_mark_dirty(layer->surface);
            }
            icc_destroy((cmsHPROFILE)doc->load_icc_profile);
        }
        doc->load_icc_profile = NULL;
    }
#endif

    if (error_out) {
        *error_out = PLUGIN_ERROR_NONE;
    }
    return TRUE;
}

/**
 * Save image using plugin system
 * Returns TRUE on success, FALSE on failure.
 * If error_out is provided, it will be set to the plugin error code.
 */
gboolean image_io_save(ImageDocument* doc, const char* filename, const SaveOptions* opts, PluginError* error_out) {
    FormatHandler* handler;
    SaveOptions default_opts;
    SaveOptions* actual_opts = NULL;
    PluginError error = PLUGIN_ERROR_NONE;
    size_t plugin_options_size = 0;
    void* plugin_data = NULL;

    if (!doc || !filename) {
        g_warning("Invalid parameters for image_io_save");
        if (error_out) {
            *error_out = PLUGIN_ERROR_INVALID_PARAMETERS;
        }
        return FALSE;
    }

    /* Find appropriate plugin */
    handler = format_registry_find_saver(filename);
    if (!handler) {
        g_warning("No plugin found to save file: %s", filename);
        if (error_out) {
            *error_out = PLUGIN_ERROR_UNSUPPORTED_FORMAT;
        }
        return FALSE;
    }

    /* Allocate options structure */
    actual_opts = g_malloc(sizeof(SaveOptions));
    if (!actual_opts) {
        g_warning("Failed to allocate memory for save options");
        if (error_out) {
            *error_out = PLUGIN_ERROR_OUT_OF_MEMORY;
        }
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
        if (error_out) {
            *error_out = PLUGIN_ERROR_UNSUPPORTED_FEATURE;
        }
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
        if (error_out) {
            *error_out = error;
        }
        return FALSE;
    }

    /* Update document filename */
    if (doc->file_path) {
        g_free(doc->file_path);
    }
    doc->file_path = g_strdup(filename);
    doc->modified = FALSE;

    if (error_out) {
        *error_out = PLUGIN_ERROR_NONE;
    }
    return TRUE;
}
