#include "ui/filters/filter_radial_blur.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply radial blur filter to a layer using Ocular library
 */
gboolean filter_radial_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    gint stride;
    OC_STATUS status;
    gint center_x, center_y, intensity;

    if (!layer || !layer->surface || !values || num_values < 3) {
        return FALSE;
    }

    center_x = (gint)(values[0]);
    center_y = (gint)(values[1]);
    intensity = (gint)(values[2]);

    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3;

    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        g_warning("Radial blur filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Radial blur filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    status = ocularRadialBlur(rgb_input, rgb_output, width, height, stride, center_x, center_y, intensity);
    
    if (status != OC_STATUS_OK) {
        g_warning("Radial blur filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Radial blur filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

