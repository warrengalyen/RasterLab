#ifndef FILTER_BILATERAL_H
#define FILTER_BILATERAL_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply bilateral filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = sigmaSpatial, values[1] = sigmaRange, both 0.0-1.0)
 * @param num_values Number of values (must be 2)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_bilateral_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_BILATERAL_H */
