#include "ui/filters/filter_gaussian_blur.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply Gaussian blur filter to a layer using Ocular library
 */
gboolean filter_gaussian_blur_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gfloat sigma;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    /* Get sigma from values array */
    sigma = values[0];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Gaussian blur filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Gaussian blur filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply Gaussian blur filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularGaussianBlurFilter(buffers.rgb_input, buffers.rgb_output,
                                      buffers.width, buffers.height, buffers.stride, sigma);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Gaussian blur filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Gaussian blur filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
