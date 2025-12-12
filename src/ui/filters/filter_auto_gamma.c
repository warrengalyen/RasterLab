#include "ui/filters/filter_auto_gamma.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply auto gamma correction filter to a layer using Ocular library
 */
gboolean filter_auto_gamma_apply(ImageLayer *layer)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    gint stride;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3; /* RGB stride */

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        g_warning("Auto gamma filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Auto gamma filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply auto gamma correction filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularAutoGammaCorrection(rgb_input, rgb_output, width, height, stride);
    
    if (status != OC_STATUS_OK) {
        g_warning("Auto gamma filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Auto gamma filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

