#include "ui/filters/filter_backlight.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply backlight repair filter to a layer using Ocular library
 */
gboolean filter_backlight_apply(ImageLayer *layer)
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
        g_warning("Backlight repair filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        g_warning("Backlight repair filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Apply backlight repair filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularBacklightRepair(rgb_input, rgb_output, width, height, width * 3);
    
    if (status != OC_STATUS_OK) {
        g_warning("Backlight repair filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        g_warning("Backlight repair filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(rgb_output);

    return TRUE;
}

