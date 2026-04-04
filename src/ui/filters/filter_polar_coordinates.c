#include "ui/filters/filter_polar_coordinates.h"
#include "ocular.h"
#include "ui/filters/filter_distort_utils.h"
#include <glib.h>
#include "debug_logger.h"

static OcPolarMode polar_mode_from_value(gint mode_value) {
    return (mode_value != 0) ? OC_POLAR_POLAR_TO_RECT : OC_POLAR_RECT_TO_POLAR;
}

gboolean filter_polar_coordinates_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    DistortBuffers buffers;
    OC_STATUS status;

    if (!layer || !layer->surface || !values || num_values < 1) {
        return FALSE;
    }

    const gint mode_value = (gint)values[0];
    const OcPolarMode mode = polar_mode_from_value(mode_value);

    if (!filter_distort_utils_prepare(layer, &buffers, "Polar coordinates")) {
        return FALSE;
    }

    status = ocularPolarCoordinatesFilter(buffers.rgb_input, buffers.rgb_output,
                                          buffers.width, buffers.height, buffers.width * 3,
                                          mode);
    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Polar coordinates: Ocular filter returned error %d", status);
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    if (!filter_distort_utils_commit(layer, &buffers, "Polar coordinates")) {
        filter_distort_utils_free(&buffers);
        return FALSE;
    }

    filter_distort_utils_free(&buffers);
    return TRUE;
}
