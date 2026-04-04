#include "ui/filters/filter_gamma.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply gamma correction filter to a layer using Ocular library
 */
gboolean filter_gamma_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    OC_STATUS status;
    gfloat gamma_array[3];

    if (!layer || !layer->surface || !values || num_values < 3) {
        return FALSE;
    }

    gamma_array[0] = values[0];  /* Red */
    gamma_array[1] = values[1];  /* Green */
    gamma_array[2] = values[2];  /* Blue */

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Gamma filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Gamma filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply gamma filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       gamma_array contains [red, green, blue] gamma values */
    status = ocularGammaFilter(rgb_input, rgb_output, width, height, width * 3, gamma_array);
    
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Gamma filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Gamma filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

