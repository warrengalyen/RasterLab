#ifndef FILTER_CRYSTALLIZE_H
#define FILTER_CRYSTALLIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply crystallize filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [cellSize]
 *               cellSize: cell size in pixels (integer)
 * @param num_values Number of values (should be 1)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_crystallize_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_CRYSTALLIZE_H */
