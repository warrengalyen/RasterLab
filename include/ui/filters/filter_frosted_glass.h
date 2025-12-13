#ifndef FILTER_FROSTED_GLASS_H
#define FILTER_FROSTED_GLASS_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply frosted glass filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of values (values[0] = radius, values[1] = range)
 * @param num_values Number of values (must be 2)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_frosted_glass_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_FROSTED_GLASS_H */
