#ifndef FILTER_MOTION_BLUR_H
#define FILTER_MOTION_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply motion blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = distance, values[1] = angle)
 * @param num_values Number of values (must be 2)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_motion_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_MOTION_BLUR_H */

