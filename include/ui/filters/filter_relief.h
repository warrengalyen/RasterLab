#ifndef FILTER_RELIEF_H
#define FILTER_RELIEF_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply relief filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [angle, offset]
 *               angle: relief angle in degrees (float)
 *               offset: relief offset (integer)
 * @param num_values Number of values (should be 2)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_relief_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_RELIEF_H */
