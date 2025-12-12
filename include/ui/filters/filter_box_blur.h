#ifndef FILTER_BOX_BLUR_H
#define FILTER_BOX_BLUR_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply box blur filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, 1-100)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_box_blur_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_BOX_BLUR_H */

