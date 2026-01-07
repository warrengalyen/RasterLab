#include "ui/filters/filter_ripple.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>

gboolean filter_ripple_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface || !values || num_values < 6) {
        return FALSE;
    }

    const gfloat wavelength = values[0];
    const gfloat amplitude = values[1];
    const gfloat center_x = values[2];
    const gfloat center_y = values[3];
    const gfloat radius_percentage = values[4];
    const gfloat phase = values[5];

    if (!filter_distort_utils_prepare(layer, &buffers, "Ripple distortion")) {
        return FALSE;
    }

    status = ocularRippleDistortionFilter(buffers.rgb_input, buffers.rgb_output,
                                          buffers.width, buffers.height, buffers.width * 3,
                                          wavelength, amplitude,
                                          center_x, center_y,
                                          radius_percentage, phase);
    if (status != OC_STATUS_OK) {
        g_warning("Ripple distortion: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Ripple distortion")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
