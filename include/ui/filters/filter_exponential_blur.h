#ifndef FILTER_EXPONENTIAL_BLUR_H
#define FILTER_EXPONENTIAL_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply exponential blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, 0.1-100.0)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_exponential_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_EXPONENTIAL_BLUR_H */

