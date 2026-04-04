#include "ui/filters/filter_color_halftone.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply color halftone filter to a layer using Ocular library
 */
gboolean filter_color_halftone_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    gint radius;
    gfloat dot_density;
    gfloat cyan_angle, magenta_angle, yellow_angle;

    if (!layer || !layer->surface || !values || num_values < 5) {
        return FALSE;
    }

    /* Extract parameters */
    radius = (gint)values[0];
    dot_density = values[1];
    cyan_angle = values[2];
    magenta_angle = values[3];
    yellow_angle = values[4];

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Color halftone filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Color halftone filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply color halftone filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       radius: halftone dot radius in pixels
       dotDensity: density of halftone dots (0.0-1.0)
       cyanAngle, magentaAngle, yellowAngle: screen angles in degrees */
    status = ocularColorHalftoneFilter(rgb_input, rgb_output, width, height, width * 3,
                                       radius, dot_density,
                                       cyan_angle, magenta_angle, yellow_angle);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Color halftone filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Color halftone filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
