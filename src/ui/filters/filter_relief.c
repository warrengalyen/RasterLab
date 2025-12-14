#include "ui/filters/filter_relief.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply relief filter to a layer using Ocular library
 */
gboolean filter_relief_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    gfloat angle;
    gint offset;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    angle = values[0];
    offset = (gint)values[1];

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        g_warning("Relief filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Relief filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply relief filter using Ocular library
       Input and output: RGB format (Stride = width * 3)
       Angle: relief angle in degrees (float)
       Offset: relief offset (integer) */
    status = ocularReliefFilter(rgb_input, rgb_output, width, height, width * 3, angle, offset);

    if (status != OC_STATUS_OK) {
        g_warning("Relief filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Relief filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
