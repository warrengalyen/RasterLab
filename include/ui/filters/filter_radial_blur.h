#ifndef FILTER_RADIAL_BLUR_H
#define FILTER_RADIAL_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply radial blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = centerX, values[1] = centerY, values[2] = intensity)
 * @param num_values Number of values (must be 3)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_radial_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_RADIAL_BLUR_H */

