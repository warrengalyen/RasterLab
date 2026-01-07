#include "ui/filters/filter_twirl.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>

gboolean filter_twirl_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;
    gfloat angle;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    angle = values[0];

    if (!filter_distort_utils_prepare(layer, &buffers, "Twirl distortion")) {
        return FALSE;
    }

    status = ocularTwirlDistortionFilter(buffers.rgb_input, buffers.rgb_output,
                                         buffers.width, buffers.height, buffers.width * 3,
                                         angle);
    if (status != OC_STATUS_OK) {
        g_warning("Twirl distortion: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Twirl distortion")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
