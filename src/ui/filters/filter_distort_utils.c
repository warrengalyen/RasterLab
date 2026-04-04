#include "ui/filters/filter_distort_utils.h"
#include "filters.h"
#include <cairo.h>
#include "debug_logger.h"

static void distort_buffers_init(DistortBuffers* b) {
    if (!b)
        return;
    b->width = 0;
    b->height = 0;
    b->rgb_input = NULL;
    b->rgb_output = NULL;
}

gboolean filter_distort_utils_prepare(ImageLayer* layer, DistortBuffers* buffers, const gchar* filter_name) {
    cairo_surface_t* surface;
    gint width, height;

    if (!layer || !layer->surface || !buffers || !filter_name) {
        return FALSE;
    }

    distort_buffers_init(buffers);
    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    buffers->width = width;
    buffers->height = height;

    buffers->rgb_input = (guchar*)g_malloc((gsize)width * (gsize)height * 3);
    buffers->rgb_output = (guchar*)g_malloc((gsize)width * (gsize)height * 3);

    if (!buffers->rgb_input || !buffers->rgb_output) {
        debug_log("WRN", "%s: Failed to allocate memory", filter_name);
        filter_distort_utils_free(buffers);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to straight RGB */
    if (!adjustments_cairo_to_rgb(surface, buffers->rgb_input)) {
        debug_log("WRN", "%s: Failed to convert surface to RGB", filter_name);
        filter_distort_utils_free(buffers);
        return FALSE;
    }

    return TRUE;
}

gboolean filter_distort_utils_commit(ImageLayer* layer, DistortBuffers* buffers, const gchar* filter_name) {
    if (!layer || !layer->surface || !buffers || !filter_name) {
        return FALSE;
    }

    if (!buffers->rgb_output) {
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 (preserves existing alpha) */
    if (!adjustments_rgb_to_cairo(layer->surface, buffers->rgb_output)) {
        debug_log("WRN", "%s: Failed to convert RGB to surface", filter_name);
        return FALSE;
    }

    return TRUE;
}

void filter_distort_utils_free(DistortBuffers* buffers) {
    if (!buffers) {
        return;
    }

    g_free(buffers->rgb_input);
    g_free(buffers->rgb_output);

    distort_buffers_init(buffers);
}
