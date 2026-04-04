#include "ui/filters/filter_skin_smooth.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include <stdbool.h>
#include "debug_logger.h"

/**
 * Apply skin smoothing filter to a layer using Ocular library
 */
gboolean filter_skin_smooth_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gint smoothing_level;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    /* Get smoothing level from values array and cast to int */
    smoothing_level = (gint)values[0];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Skin smoothing filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Skin smoothing filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply skin smoothing filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       applySkinFilter is set to TRUE to enable skin detection */
    status = ocularSkinSmoothingFilter(buffers.rgb_input, buffers.rgb_output,
                                       buffers.width, buffers.height, buffers.stride,
                                       smoothing_level, true);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Skin smoothing filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Skin smoothing filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
