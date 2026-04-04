#include "ui/filters/filter_chroma_key.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply chroma key filter to a layer using Ocular library
 */
gboolean filter_chroma_key_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBABuffers buffers;
    OC_STATUS status;
    guchar color_r, color_g, color_b;
    gfloat threshold, smoothing;

    if (!layer || !layer->surface || !values || num_values < 5) {
        return FALSE;
    }

    /* Extract parameters */
    color_r = (guchar)(values[0] * 255.0f);
    color_g = (guchar)(values[1] * 255.0f);
    color_b = (guchar)(values[2] * 255.0f);
    threshold = values[3];
    smoothing = values[4];

    /* Allocate and initialize RGBA buffers */
    if (!filter_utils_allocate_rgba_buffers(layer->surface, &buffers, "Chroma key filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!filter_utils_cairo_to_rgba(layer->surface, &buffers, "Chroma key filter")) {
        filter_utils_free_rgba_buffers(&buffers);
        return FALSE;
    }

    /* Apply chroma key filter using Ocular library
       Input and output: RGBA format (4 channels, Stride = width * 4)
       This allows the filter to modify the alpha channel for transparency */
    status = ocularChromaKeyFilter(buffers.rgba_input, buffers.rgba_output,
                                   buffers.width, buffers.height, buffers.stride,
                                   color_r, color_g, color_b, threshold, smoothing);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Chroma key filter: Ocular filter returned error %d", status);
        filter_utils_free_rgba_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!filter_utils_rgba_to_cairo(layer->surface, &buffers, "Chroma key filter")) {
        filter_utils_free_rgba_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgba_buffers(&buffers);

    return TRUE;
}
