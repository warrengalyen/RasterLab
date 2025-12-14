#ifndef FILTER_LUMINANCE_THRESHOLD_H
#define FILTER_LUMINANCE_THRESHOLD_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply luminance threshold filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [threshold]
 *               threshold: threshold value (0.0-1.0 range, converted to 0-255)
 * @param num_values Number of values (should be 1)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_luminance_threshold_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_LUMINANCE_THRESHOLD_H */
