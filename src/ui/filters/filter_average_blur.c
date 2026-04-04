#include "ui/filters/filter_average_blur.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply average blur filter to a layer using Ocular library
 */
gboolean filter_average_blur_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gint radius;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    /* Get radius from values array */
    radius = (gint)(values[0]);

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Average blur filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Average blur filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply average blur filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularAverageBlur(buffers.rgb_input, buffers.rgb_output,
                               buffers.width, buffers.height, buffers.stride, radius);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Average blur filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Average blur filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
