#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <string.h>

/* PNG file signature */
static const uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

/* JPEG file signatures */
static const uint8_t JPEG_SIGNATURE_SOI[2] = {0xFF, 0xD8};

/**
 * Check if file is PNG format
 */
static bool can_load_png(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 8) {
        return false;
    }

    return memcmp(header, PNG_SIGNATURE, 8) == 0;
}

/**
 * Check if file is JPEG format
 */
static bool can_load_jpeg(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 2) {
        return false;
    }

    return memcmp(header, JPEG_SIGNATURE_SOI, 2) == 0;
}

/**
 * Load PNG image
 */
static PluginError load_png(ImageDocument* doc, const char* filename) {
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    ImageLayer* base_layer;
    cairo_surface_t* temp_surface;
    cairo_t* cr;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Load image with GdkPixbuf */
    pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (!pixbuf) {
        if (error) {
            g_error_free(error);
        }
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Set document metadata */
    doc->width = gdk_pixbuf_get_width(pixbuf);
    doc->height = gdk_pixbuf_get_height(pixbuf);
    doc->channels = gdk_pixbuf_get_n_channels(pixbuf);
    doc->bit_depth = 8;
    doc->has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    /* Free old layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Create base layer */
    base_layer = layer_new("Background", doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        g_object_unref(pixbuf);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert pixbuf to Cairo surface and copy to layer */
    temp_surface = pixbuf_to_cairo_surface(pixbuf);
    if (!temp_surface) {
        g_object_unref(pixbuf);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cr = cairo_create(base_layer->surface);
    cairo_set_source_surface(cr, temp_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(temp_surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    g_object_unref(pixbuf);
    return PLUGIN_ERROR_NONE;
}

/**
 * Load JPEG image (same as PNG but without alpha)
 */
static PluginError load_jpeg(ImageDocument* doc, const char* filename) {
    return load_png(doc, filename); /* Same implementation */
}

/**
 * Check if we can save as PNG
 */
static bool can_save_png(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "png") == 0;
}

/**
 * Check if we can save as JPEG
 */
static bool can_save_jpeg(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "jpg") == 0 ||
           g_ascii_strcasecmp(ext + 1, "jpeg") == 0;
}

/**
 * Save PNG image
 */
static PluginError save_png(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result;

    (void)opts; /* Options not used for PNG */

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Convert to pixbuf with alpha */
    pixbuf = cairo_surface_to_pixbuf(composite, TRUE);
    cairo_surface_destroy(composite);

    if (!pixbuf) {
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Save as PNG */
    result = gdk_pixbuf_save(pixbuf, filename, "png", &error, NULL);
    g_object_unref(pixbuf);

    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    return PLUGIN_ERROR_NONE;
}

/**
 * Save JPEG image
 */
static PluginError save_jpeg(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite;
    cairo_surface_t* flattened;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result;
    gchar quality_str[4];
    gint quality;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get quality from options */
    quality = (opts && opts->quality >= 0) ? opts->quality : 85;
    if (quality < 0)
        quality = 0;
    if (quality > 100)
        quality = 100;

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Flatten to white background */
    flattened = compositor_flatten_to_white_background(composite, doc->width, doc->height);
    cairo_surface_destroy(composite);

    if (!flattened) {
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert to pixbuf without alpha */
    pixbuf = cairo_surface_to_pixbuf(flattened, FALSE);
    cairo_surface_destroy(flattened);

    if (!pixbuf) {
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Save as JPEG */
    g_snprintf(quality_str, sizeof(quality_str), "%d", quality);
    result = gdk_pixbuf_save(pixbuf, filename, "jpeg", &error, "quality", quality_str, NULL);
    g_object_unref(pixbuf);

    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    return PLUGIN_ERROR_NONE;
}

/**
 * PNG plugin initialization
 */
bool plugin_init_png(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "PNG";
    out_plugin->format_info.extensions = "png";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_png;
    out_plugin->callbacks.load = load_png;
    out_plugin->callbacks.can_save = can_save_png;
    out_plugin->callbacks.save = save_png;

    return true;
}

/**
 * JPEG plugin initialization
 */
bool plugin_init_jpeg(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "JPEG";
    out_plugin->format_info.extensions = "jpg,jpeg";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_jpeg;
    out_plugin->callbacks.load = load_jpeg;
    out_plugin->callbacks.can_save = can_save_jpeg;
    out_plugin->callbacks.save = save_jpeg;

    return true;
}
