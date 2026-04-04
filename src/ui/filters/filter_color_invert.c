#include "ui/filters/filter_color_invert.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply color invert filter to a layer using Ocular library
 */
gboolean filter_color_invert_apply(ImageLayer* layer) {
    FilterRGBBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Color Invert filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Color Invert filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply color invert filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularColorInvertFilter(buffers.rgb_input, buffers.rgb_output,
                                     buffers.width, buffers.height, buffers.stride);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Color Invert filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Color Invert filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
