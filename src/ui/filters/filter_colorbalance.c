#include "ui/filters/filter_colorbalance.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply color balance filter to a layer using Ocular library
 */
gboolean filter_colorbalance_apply(ImageLayer *layer, 
                                    gint red_balance, gint green_balance, gint blue_balance,
                                    OcToneBalanceMode mode, gboolean preserve_luminosity)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        g_warning("Color balance filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Color balance filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply color balance filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularColorBalance(rgb_input, rgb_output, width, height, width * 3,
                                red_balance, green_balance, blue_balance, mode, preserve_luminosity);
    
    if (status != OC_STATUS_OK) {
        g_warning("Color balance filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Color balance filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

/**
 * Apply color balance filter using standard filter signature
 */
gboolean filter_colorbalance_apply_values(ImageLayer *layer, const gfloat *values, gint num_values)
{
    gint red_balance, green_balance, blue_balance;
    OcToneBalanceMode mode;
    gboolean preserve_luminosity;

    if (!layer || !values || num_values < 5) {
        return FALSE;
    }

    red_balance = (gint)values[0];
    green_balance = (gint)values[1];
    blue_balance = (gint)values[2];
    mode = (OcToneBalanceMode)(gint)values[3];
    preserve_luminosity = (gboolean)(gint)values[4];

    return filter_colorbalance_apply(layer, red_balance, green_balance, blue_balance, 
                                     mode, preserve_luminosity);
}

