#include "ui/filters/filter_frosted_glass.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply frosted glass filter to a layer using Ocular library
 */
gboolean filter_frosted_glass_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    gint stride;
    OC_STATUS status;
    gint radius, range;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    radius = (gint)values[0];
    range = (gint)values[1];

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
        g_warning("Frosted glass filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Frosted glass filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply frosted glass filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       Radius: blur radius, Range: color range */
    status = ocularFrostedGlassEffect(rgb_input, rgb_output, width, height, stride, radius, range);
    
    if (status != OC_STATUS_OK) {
        g_warning("Frosted glass filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Frosted glass filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
