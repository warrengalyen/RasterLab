#ifndef FILTER_ZOOM_BLUR_H
#define FILTER_ZOOM_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply zoom blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = sampleRadius, values[1] = blurAmount, values[2] = centerX, values[3] = centerY)
 * @param num_values Number of values (must be 4)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_zoom_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_ZOOM_BLUR_H */

