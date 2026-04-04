#include "ui/filters/filter_despeckle.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply despeckle filter to a layer using Ocular library
 */
gboolean filter_despeckle_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gint max_window_size, threshold;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    /* Get parameters from values array and cast to int */
    max_window_size = (gint)values[0];
    threshold = (gint)values[1];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Despeckle filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Despeckle filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply despeckle filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularDespeckle(buffers.rgb_input, buffers.rgb_output,
                             buffers.width, buffers.height, buffers.stride,
                             max_window_size, threshold);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Despeckle filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Despeckle filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
