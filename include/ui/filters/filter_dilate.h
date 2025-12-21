#ifndef FILTER_DILATE_H
#define FILTER_DILATE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply dilate filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, 1-500)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_dilate_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_DILATE_H */
