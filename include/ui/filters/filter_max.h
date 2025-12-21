#ifndef FILTER_MAX_H
#define FILTER_MAX_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply max filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, 1-256)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_max_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_MAX_H */
