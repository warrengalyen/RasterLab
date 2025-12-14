#include "ui/filters/filter_chroma_key.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply chroma key filter to a layer using Ocular library
 */
gboolean filter_chroma_key_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;
    guchar color_r, color_g, color_b;
    gfloat threshold, smoothing;

    if (!layer || !layer->surface || !values || num_values < 5) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Extract parameters */
    color_r = (guchar)(values[0] * 255.0f);
    color_g = (guchar)(values[1] * 255.0f);
    color_b = (guchar)(values[2] * 255.0f);
    threshold = values[3];
    smoothing = values[4];

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Chroma key filter: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        g_warning("Chroma key filter: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply chroma key filter using Ocular library
       Input and output: RGBA format (4 channels, Stride = width * 4)
       This allows the filter to modify the alpha channel for transparency */
    status = ocularChromaKeyFilter(rgba_input, rgba_output, width, height, width * 4,
                                   color_r, color_g, color_b, threshold, smoothing);

    if (status != OC_STATUS_OK) {
        g_warning("Chroma key filter: Ocular filter returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        g_warning("Chroma key filter: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    return TRUE;
}
