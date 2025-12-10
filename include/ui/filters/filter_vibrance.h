#ifndef FILTER_VIBRANCE_H
#define FILTER_VIBRANCE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply vibrance filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of values (values[0] = vibrance, 0.0-1.0)
 * @param num_values Number of values (must be 1)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_vibrance_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_VIBRANCE_H */

