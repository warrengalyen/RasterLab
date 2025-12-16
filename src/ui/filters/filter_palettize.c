#include "ui/filters/filter_palettize.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include <string.h>

/**
 * Apply palettize filter to a layer using Ocular library
 */
gboolean filter_palettize_apply(ImageLayer* layer, const PalettizeParams* params) {
    FilterRGBABuffers buffers;
    OC_STATUS status;
    gint channels = 4; /* RGBA format for Ocular (supports 3 or 4 channels) */

    if (!layer || !layer->surface || !params) {
        return FALSE;
    }

    /* Allocate and initialize RGBA buffers */
    if (!filter_utils_allocate_rgba_buffers(layer->surface, &buffers, "Palettize filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!filter_utils_cairo_to_rgba(layer->surface, &buffers, "Palettize filter")) {
        filter_utils_free_rgba_buffers(&buffers);
        return FALSE;
    }

    /* Apply palettize filter using Ocular library
       Input and output: RGBA format (Channels = 4) */
    if (params->use_file && params->palette_file) {
        status = ocularPalettetizeFromFile(
            buffers.rgba_input,
            buffers.rgba_output,
            buffers.width,
            buffers.height,
            channels,
            params->palette_file,
            params->dither_method,
            params->dither_amount);
    } else {
        status = ocularPalettetizeFromImage(
            buffers.rgba_input,
            buffers.rgba_output,
            buffers.width,
            buffers.height,
            channels,
            params->quantize_method,
            params->max_colors,
            params->dither_method,
            params->dither_amount);
    }

    if (status != OC_STATUS_OK) {
        g_warning("Palettize filter: Ocular filter returned error %d", status);
        filter_utils_free_rgba_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!filter_utils_rgba_to_cairo(layer->surface, &buffers, "Palettize filter")) {
        filter_utils_free_rgba_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgba_buffers(&buffers);

    return TRUE;
}
