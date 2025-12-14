#ifndef FILTER_POINTILLIZE_H
#define FILTER_POINTILLIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply pointillize filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [cellSize, r, g, b]
 *               cellSize: cell size in pixels (integer)
 *               r, g, b: background color components (0.0-1.0 range, will be converted to 0-255)
 * @param num_values Number of values (should be 4: cellSize + RGB)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_pointillize_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_POINTILLIZE_H */
