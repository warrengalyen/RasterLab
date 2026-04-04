#include "ui/filters/filter_zoom_blur.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply zoom blur filter to a layer using Ocular library
 */
gboolean filter_zoom_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    gint stride;
    OC_STATUS status;
    gint sample_radius, center_x, center_y;
    gfloat blur_amount;

    if (!layer || !layer->surface || !values || num_values < 4) {
        return FALSE;
    }

    sample_radius = (gint)(values[0]);
    blur_amount = values[1];
    center_x = (gint)(values[2]);
    center_y = (gint)(values[3]);

    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3;

    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Zoom blur filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Zoom blur filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    status = ocularZoomBlur(rgb_input, rgb_output, width, height, stride, sample_radius, blur_amount, center_x, center_y);
    
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Zoom blur filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Zoom blur filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

