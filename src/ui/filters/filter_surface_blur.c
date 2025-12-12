#include "ui/filters/filter_surface_blur.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply surface blur filter to a layer using Ocular library
 */
gboolean filter_surface_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    gint stride;
    OC_STATUS status;
    gint radius, threshold;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    radius = (gint)(values[0]);
    threshold = (gint)(values[1]);

    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3;

    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        g_warning("Surface blur filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Surface blur filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    status = ocularSurfaceBlurFilter(rgb_input, rgb_output, width, height, stride, radius, threshold);
    
    if (status != OC_STATUS_OK) {
        g_warning("Surface blur filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Surface blur filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

