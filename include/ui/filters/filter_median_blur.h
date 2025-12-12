#ifndef FILTER_MEDIAN_BLUR_H
#define FILTER_MEDIAN_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply median blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, 1-100)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_median_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_MEDIAN_BLUR_H */

