#include "ui/filters/filter_kaleidoscope.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>

gboolean filter_kaleidoscope_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface || !values || num_values < 6) {
        return FALSE;
    }

    const gint mirrors = (gint)values[0];
    const gfloat angle = values[1];
    const gfloat angle2 = values[2];
    const gfloat center_x = values[3];
    const gfloat center_y = values[4];
    const gfloat radius_percent = values[5];

    if (!filter_distort_utils_prepare(layer, &buffers, "Kaleidoscope")) {
        return FALSE;
    }

    status = ocularKaleidoscopeFilter(buffers.rgb_input, buffers.rgb_output,
                                      buffers.width, buffers.height, buffers.width * 3,
                                      mirrors, angle, angle2, center_x, center_y, radius_percent);
    if (status != OC_STATUS_OK) {
        g_warning("Kaleidoscope: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Kaleidoscope")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
