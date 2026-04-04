#include "ui/filters/filter_pointillize.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply pointillize filter to a layer using Ocular library
 */
gboolean filter_pointillize_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    gint cell_size;
    guchar bg_r, bg_g, bg_b;

    if (!layer || !layer->surface || !values || num_values < 4) {
        return FALSE;
    }

    /* Extract parameters */
    cell_size = (gint)values[0];
    /* Convert RGB from 0.0-1.0 range to 0-255 */
    bg_r = (guchar)(values[1] * 255.0f);
    bg_g = (guchar)(values[2] * 255.0f);
    bg_b = (guchar)(values[3] * 255.0f);

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Pointillize filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Pointillize filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply pointillize filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       cellSize: size of pointillize cells in pixels
       bgR, bgG, bgB: background color components (0-255) */
    status = ocularPointillizeFilter(rgb_input, rgb_output, width, height, width * 3,
                                     cell_size, bg_r, bg_g, bg_b);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Pointillize filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Pointillize filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
