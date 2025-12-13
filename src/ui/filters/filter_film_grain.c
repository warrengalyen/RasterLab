#include "ui/filters/filter_film_grain.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply film grain filter to a layer using Ocular library
 */
gboolean filter_film_grain_apply(ImageLayer *layer, const gfloat *values, gint num_values)
{
    cairo_surface_t *surface;
    gint width, height;
    guchar *rgb_input, *rgb_output;
    OC_STATUS status;
    gfloat strength, softness;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    strength = values[0];
    softness = values[1];

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGB input and output */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    rgb_output = (guchar *)g_malloc(width * height * 3);
    
    if (!rgb_input || !rgb_output) {
        g_warning("Film grain filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Film grain filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply film grain filter using Ocular library
       Input and output: RGB format (Channels = 3)
       Strength: 0.0-1.0, Softness: 0.0-1.0 */
    status = ocularFilmGrainEffect(rgb_input, rgb_output, width, height, 3, strength, softness);
    
    if (status != OC_STATUS_OK) {
        g_warning("Film grain filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Film grain filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}
