#include "ui/filters/filter_monochrome.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply monochrome filter to a layer using Ocular library
 */
gboolean filter_monochrome_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    guchar filter_r, filter_g, filter_b;
    gint intensity;

    if (!layer || !layer->surface || !values || num_values < 4) {
        return FALSE;
    }

    /* Extract parameters */
    /* Convert RGB from 0.0-1.0 range to 0-255 */
    filter_r = (guchar)(values[0] * 255.0f);
    filter_g = (guchar)(values[1] * 255.0f);
    filter_b = (guchar)(values[2] * 255.0f);
    intensity = (gint)values[3]; /* intensity is 0-100 */

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        g_warning("Monochrome filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Monochrome filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply monochrome filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       filterColorR, filterColorG, filterColorB: filter color components (0-255)
       intensity: filter intensity (0-100) */
    status = ocularMonochromeFilter(rgb_input, rgb_output, width, height, width * 3,
                                    filter_r, filter_g, filter_b, intensity);

    if (status != OC_STATUS_OK) {
        g_warning("Monochrome filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Monochrome filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
