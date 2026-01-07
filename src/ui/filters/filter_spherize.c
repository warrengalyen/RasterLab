#include "ui/filters/filter_spherize.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>

static OcSpherizeMode spherize_mode_from_value(gint mode_value) {
    switch (mode_value) {
        case 1:
            return OC_SPHERIZE_HORIZONTAL;
        case 2:
            return OC_SPHERIZE_VERTICAL;
        case 0:
        default:
            return OC_SPHERIZE_NORMAL;
    }
}

gboolean filter_spherize_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface || !values || num_values < 2) {
        return FALSE;
    }

    const gint amount = (gint)values[0];
    const gint mode_value = (gint)values[1];
    const OcSpherizeMode mode = spherize_mode_from_value(mode_value);

    if (!filter_distort_utils_prepare(layer, &buffers, "Spherize distortion")) {
        return FALSE;
    }

    status = ocularSpherizeDistortionFilter(buffers.rgb_input, buffers.rgb_output,
                                            buffers.width, buffers.height, buffers.width * 3,
                                            amount, mode);
    if (status != OC_STATUS_OK) {
        g_warning("Spherize distortion: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Spherize distortion")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
