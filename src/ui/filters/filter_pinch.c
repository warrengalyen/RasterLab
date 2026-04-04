#include "ui/filters/filter_pinch.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>
#include "debug_logger.h"

gboolean filter_pinch_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;
    gfloat amount;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    amount = values[0];

    if (!filter_distort_utils_prepare(layer, &buffers, "Pinch distortion")) {
        return FALSE;
    }

    status = ocularPinchDistortionFilter(buffers.rgb_input, buffers.rgb_output,
                                         buffers.width, buffers.height, buffers.width * 3,
                                         amount);
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Pinch distortion: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Pinch distortion")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
