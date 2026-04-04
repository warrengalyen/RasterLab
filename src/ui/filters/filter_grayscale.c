#include "ui/filters/filter_grayscale.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply grayscale filter to a layer using Ocular library
 */
gboolean filter_grayscale_apply(ImageLayer *layer)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *grayscale_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers:
       - rgb_input: RGB format (3 channels) for Ocular input
       - grayscale_output: Single channel grayscale output from Ocular */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    grayscale_output = (guchar *)g_malloc(width * height);
    
    if (!rgb_input || !grayscale_output) {
        debug_log("WRN", "Grayscale filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(grayscale_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Grayscale filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(grayscale_output);
        return FALSE;
    }

    /* Apply grayscale filter using Ocular library
       Input: RGB format (stride = width * 3)
       Output: Single channel grayscale (stride = width) */
    status = ocularGrayscaleFilter(rgb_input, grayscale_output, width, height, width * 3);
    
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Grayscale filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(grayscale_output);
        return FALSE;
    }

    /* Convert back from single channel grayscale to Cairo ARGB32 */
    if (!adjustments_grayscale_to_cairo(surface, grayscale_output)) {
        debug_log("WRN", "Grayscale filter: Failed to convert grayscale to surface");
        g_free(rgb_input);
        g_free(grayscale_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(grayscale_output);

    return TRUE;
}

