#include "ui/filters/filter_guided.h"
#include "filters.h"
#include "ocular.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>

/**
 * Apply guided filter to a layer using Ocular library
 * Uses the input buffer as the guide image
 */
gboolean filter_guided_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    FilterRGBBuffers buffers;
    OC_STATUS status;
    gint radius;
    gfloat epsilon;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    /* Get parameters from values array */
    radius = (gint)values[0];
    epsilon = values[1];

    /* Allocate and initialize RGB buffers */
    if (!filter_utils_allocate_rgb_buffers(layer->surface, &buffers, "Guided filter")) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!filter_utils_cairo_to_rgb(layer->surface, &buffers, "Guided filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Apply guided filter using Ocular library
       Input and output: RGB format (stride = width * 3)
       Use input buffer as guide image (same buffer for Input and Guide) */
    status = ocularGuidedFilter(buffers.rgb_input, buffers.rgb_input, buffers.rgb_output,
                                buffers.width, buffers.height, buffers.stride,
                                radius, epsilon);

    if (status != OC_STATUS_OK) {
        g_warning("Guided filter: Ocular filter returned error %d", status);
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!filter_utils_rgb_to_cairo(layer->surface, &buffers, "Guided filter")) {
        filter_utils_free_rgb_buffers(&buffers);
        return FALSE;
    }

    /* Free temporary buffers */
    filter_utils_free_rgb_buffers(&buffers);

    return TRUE;
}
