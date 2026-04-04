#include "ui/filters/filter_brightness_contrast.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include "debug_logger.h"

/**
 * Apply brightness and contrast filter to a layer using Ocular library
 */
gboolean filter_brightness_contrast_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gfloat brightness, contrast;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    brightness = values[0];
    contrast = values[1];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Brightness/Contrast filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Brightness/Contrast filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply brightness and contrast filter using Ocular library
       Input and output: RGB format (stride = width * 3) */
    status = ocularBrightnessAndContrastFilter(buffers.rgb_input, buffers.rgb_output,
                                               buffers.width, buffers.height, buffers.stride,
                                               brightness, contrast);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Brightness/Contrast filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Brightness/Contrast filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
