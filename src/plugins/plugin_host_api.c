#include "app/settings.h"
#include "document.h"
#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * Host API implementation functions
 * These functions are called by plugins to interact with the host application
 */

/**
 * Create a new document (wrapper for document_new)
 */
static ImageDocument* host_document_create(uint32_t width, uint32_t height, bool has_alpha) {
    ImageDocument* doc = document_new("Untitled", TRUE, 10); /* Default 10 undo levels */
    if (!doc) {
        return NULL;
    }

    /* Set dimensions */
    doc->width = width;
    doc->height = height;
    doc->has_alpha = has_alpha ? TRUE : FALSE;
    doc->channels = has_alpha ? 4 : 3;
    doc->bit_depth = 8;

    return doc;
}

/**
 * Create a new layer (wrapper for document_add_layer)
 */
static ImageLayer* host_layer_create(ImageDocument* doc, const char* name,
                                     uint32_t width, uint32_t height, bool has_alpha) {
    if (!doc || !name) {
        return NULL;
    }

    return document_add_layer(doc, name,
                              has_alpha ? LAYER_BACKGROUND_TRANSPARENT : LAYER_BACKGROUND_WHITE,
                              LAYER_POSITION_ABOVE_CURRENT, NULL);
}

/**
 * Get pixel buffer for a layer
 */
static bool host_layer_get_pixel_buffer(ImageLayer* layer, PixelBuffer* out_buffer) {
    if (!layer || !out_buffer || !layer->surface) {
        return false;
    }

    cairo_surface_flush(layer->surface);
    guchar* surface_data = cairo_image_surface_get_data(layer->surface);
    gint stride = cairo_image_surface_get_stride(layer->surface);

    if (!surface_data) {
        return false;
    }

    out_buffer->pixels = surface_data;
    out_buffer->width = layer->width;
    out_buffer->height = layer->height;
    out_buffer->stride = stride;
    out_buffer->channels = 4; /* Cairo ARGB32 always has 4 channels */
    out_buffer->bit_depth = 8;
    out_buffer->has_alpha = TRUE;

    return true;
}

/**
 * Get composite pixel buffer for document
 */
static bool host_document_get_composite_pixels(ImageDocument* doc, PixelBuffer* out_buffer) {
    cairo_surface_t* composite;

    if (!doc || !out_buffer) {
        return false;
    }

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return false;
    }

    cairo_surface_flush(composite);
    guchar* surface_data = cairo_image_surface_get_data(composite);
    gint stride = cairo_image_surface_get_stride(composite);

    if (!surface_data) {
        cairo_surface_destroy(composite);
        return false;
    }

    out_buffer->pixels = surface_data;
    out_buffer->width = doc->width;
    out_buffer->height = doc->height;
    out_buffer->stride = stride;
    out_buffer->channels = doc->has_alpha ? 4 : 3;
    out_buffer->bit_depth = doc->bit_depth;
    out_buffer->has_alpha = doc->has_alpha ? true : false;

    /* Note: Caller should not free the composite surface while using this buffer */
    /* For plugin use, we need to create a copy - but that's expensive */
    /* For now, we'll document that the buffer is valid only during the callback */

    return true;
}

/**
 * Set document metadata
 */
static void host_document_set_metadata(ImageDocument* doc, uint32_t width, uint32_t height,
                                       uint32_t channels, uint32_t bit_depth, bool has_alpha) {
    if (!doc) {
        return;
    }

    doc->width = width;
    doc->height = height;
    doc->channels = channels;
    doc->bit_depth = bit_depth;
    doc->has_alpha = has_alpha ? TRUE : FALSE;
}

/**
 * Set load-time ICC profile (opaque cmsHPROFILE). Host applies conversion and destroys profile.
 */
static void host_document_set_load_icc_profile(ImageDocument* doc, void* profile) {
    if (!doc) {
        return;
    }
    doc->load_icc_profile = profile;
}

/** Settings pointer for color management (use embedded ICC). Set via plugin_host_api_set_cm_settings(). */
static Settings* s_cm_settings = NULL;

void plugin_host_api_set_cm_settings(void* settings) {
    s_cm_settings = (Settings*)settings;
}

/** Return true if embedded ICC profiles should be used when loading. Default true when no settings. */
static bool host_get_use_embedded_icc(void) {
    return !s_cm_settings || settings_get_cm_use_embedded_icc(s_cm_settings);
}

int plugin_host_api_get_cm_rendering_intent(void) {
    return s_cm_settings ? settings_get_cm_rendering_intent(s_cm_settings) : 1;
}

bool plugin_host_api_get_cm_bpc(void) {
    return !s_cm_settings || settings_get_cm_black_point_compensation(s_cm_settings);
}

/**
 * Get number of layers
 */
static uint32_t host_document_get_layer_count(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return (uint32_t)document_get_layer_count(doc);
}

/**
 * Get layer descriptor
 */
static LayerDescriptor* host_document_get_layer_descriptor(ImageDocument* doc, uint32_t index) {
    LayerDescriptor* desc;
    ImageLayer* layer;
    PixelBuffer* buffer;

    if (!doc) {
        return NULL;
    }

    layer = document_get_layer(doc, index);
    if (!layer) {
        return NULL;
    }

    desc = malloc(sizeof(LayerDescriptor));
    if (!desc) {
        return NULL;
    }

    buffer = malloc(sizeof(PixelBuffer));
    if (!buffer) {
        free(desc);
        return NULL;
    }

    memset(desc, 0, sizeof(LayerDescriptor));

    desc->name = layer->name;
    desc->opacity = layer->opacity;
    desc->visible = layer->visible ? true : false;
    desc->offset_x = layer->offset_x;
    desc->offset_y = layer->offset_y;

    /* Get pixel buffer */
    if (!host_layer_get_pixel_buffer(layer, buffer)) {
        free(buffer);
        free(desc);
        return NULL;
    }

    desc->buffer = buffer;

    return desc;
}

/**
 * Free layer descriptor
 */
static void host_layer_descriptor_free(LayerDescriptor* desc) {
    if (!desc) {
        return;
    }

    if (desc->buffer) {
        free(desc->buffer);
    }

    free(desc);
}

/**
 * Get host API structure with all callbacks filled in
 */
ImageFormatHostAPI* plugin_host_api_get(void) {
    static ImageFormatHostAPI host_api;
    static gboolean initialized = FALSE;

    if (initialized) {
        return &host_api;
    }

    memset(&host_api, 0, sizeof(ImageFormatHostAPI));
    host_api.api_version = IMAGE_FORMAT_PLUGIN_API_VERSION;

    /* Memory allocation */
    host_api.malloc = malloc;
    host_api.calloc = calloc;
    host_api.realloc = realloc;
    host_api.free = free;

    /* Logging - use g_log wrappers */
    /* Note: We can't directly assign variadic functions, so we'd need wrapper functions */
    /* For now, set to NULL and plugins should use host API logging through a helper */
    /* TODO: Implement proper logging wrappers */
    host_api.log_error = NULL;
    host_api.log_warning = NULL;
    host_api.log_info = NULL;
    host_api.log_debug = NULL;

    /* Document manipulation */
    host_api.document_create = host_document_create;
    host_api.layer_create = host_layer_create;
    host_api.layer_get_pixel_buffer = host_layer_get_pixel_buffer;
    host_api.document_get_composite_pixels = host_document_get_composite_pixels;
    host_api.document_set_metadata = host_document_set_metadata;
    host_api.document_set_load_icc_profile = host_document_set_load_icc_profile;
    host_api.get_use_embedded_icc = host_get_use_embedded_icc;
    host_api.document_get_layer_count = host_document_get_layer_count;
    host_api.document_get_layer_descriptor = host_document_get_layer_descriptor;
    host_api.layer_descriptor_free = host_layer_descriptor_free;

    initialized = TRUE;
    return &host_api;
}
