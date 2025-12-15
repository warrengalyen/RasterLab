#include "ui/filters/filter_palettize.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include <string.h>

/**
 * Apply palettize filter to a layer using Ocular library
 */
gboolean filter_palettize_apply(ImageLayer* layer, const PalettizeParams* params) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;
    gint channels = 4; /* RGBA format for Ocular (supports 3 or 4 channels) */

    if (!layer || !layer->surface || !params) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Palettize filter: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        g_warning("Palettize filter: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply palettize filter using Ocular library
       Input and output: RGBA format (Channels = 4) */
    if (params->use_file && params->palette_file) {
        status = ocularPalettetizeFromFile(
            rgba_input,
            rgba_output,
            width,
            height,
            channels,
            params->palette_file,
            params->dither_method,
            params->dither_amount);
    } else {
        status = ocularPalettetizeFromImage(
            rgba_input,
            rgba_output,
            width,
            height,
            channels,
            params->quantize_method,
            params->max_colors,
            params->dither_method,
            params->dither_amount);
    }

    if (status != OC_STATUS_OK) {
        g_warning("Palettize filter: Ocular filter returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        g_warning("Palettize filter: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    return TRUE;
}
