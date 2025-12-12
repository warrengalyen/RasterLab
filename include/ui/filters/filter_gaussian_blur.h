#ifndef FILTER_GAUSSIAN_BLUR_H
#define FILTER_GAUSSIAN_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Gaussian blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = sigma, typically 0.1-10.0)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_gaussian_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_GAUSSIAN_BLUR_H */

