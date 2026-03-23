/**
 * Stub implementations of application functions referenced by plugin_rli.c.
 *
 * These provide just enough for the linker to resolve symbols without
 * pulling in the full application.  Only used by the test_rli_plugin
 * test executable.
 */

#include "document.h"
#include "render/layer.h"
#include "render/text_layer.h"

#include <cairo/cairo.h>
#include <glib.h>
#include <string.h>

/* ---------- render/layer.h stubs ---------- */

ImageLayer*
layer_new(const gchar* name, guint width, guint height, gboolean has_alpha,
          LayerBackgroundType background, LayerPosition position,
          const gdouble* custom_color, struct ImageDocument* doc) {
    (void)has_alpha;
    (void)background;
    (void)position;
    (void)custom_color;
    (void)doc;

    ImageLayer* layer = g_new0(ImageLayer, 1);
    layer->name = g_strdup(name ? name : "Layer");
    layer->width = width;
    layer->height = height;
    layer->opacity = 1.0;
    layer->visible = TRUE;
    layer->blend_mode = BLEND_MODE_NORMAL;
    layer->layer_type = LAYER_TYPE_RASTER;

    if (width > 0 && height > 0)
        layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    return layer;
}

void
layer_free(ImageLayer* layer) {
    if (!layer) return;
    g_free(layer->name);
    if (layer->surface)
        cairo_surface_destroy(layer->surface);
    if (layer->text_data)
        text_layer_free((TextLayer*)layer->text_data);
    g_free(layer);
}

/* ---------- render/text_layer.h stubs ---------- */

void
text_layer_render_to_surface(ImageLayer* layer) {
    (void)layer;
}

void
text_layer_free(TextLayer* text) {
    if (!text) return;
    g_free(text->text);
    g_free(text->font_family);
    g_free(text->opentype_features);
    g_free(text);
}

/* ---------- color_manager/icc_utils.h stubs ---------- */

#if HAVE_LCMS2
#include <lcms2.h>

cmsHPROFILE
icc_profile_from_memory(const void* data, size_t size) {
    (void)data;
    (void)size;
    return NULL;
}
#endif

/* ---------- document.h stubs ---------- */

gboolean
document_render_composite(ImageDocument* doc) {
    (void)doc;
    return TRUE;
}
