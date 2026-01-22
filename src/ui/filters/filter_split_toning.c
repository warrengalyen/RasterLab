#include "ui/filters/filter_split_toning.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply split toning filter to a layer using Ocular library
 */
gboolean filter_split_toning_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    OC_STATUS status;
    OcColor highlight_color;
    OcColor shadow_color;
    gfloat balance;
    gfloat strength;

    if (!layer || !layer->surface || !values || num_values < 8) {
        return FALSE;
    }

    /* Extract parameters */
    highlight_color.R = (unsigned char)(values[0] * 255.0f);
    highlight_color.G = (unsigned char)(values[1] * 255.0f);
    highlight_color.B = (unsigned char)(values[2] * 255.0f);
    balance = values[3]; /* balance is in -100.0 to 100.0 range */
    shadow_color.R = (unsigned char)(values[4] * 255.0f);
    shadow_color.G = (unsigned char)(values[5] * 255.0f);
    shadow_color.B = (unsigned char)(values[6] * 255.0f);
    strength = values[7]; /* strength is in 0.0-100.0 range */

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar*)g_malloc(width * height * 3);
    rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!rgb_input || !rgb_output) {
        g_warning("Split Toning filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Split Toning filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    status = ocularSplitToningFilter(rgb_input, rgb_output, width, height, width * 3,
                                     highlight_color, shadow_color, balance, strength);

    if (status != OC_STATUS_OK) {
        g_warning("Split Toning filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Split Toning filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
