#ifndef FILTER_MARBLE_H
#define FILTER_MARBLE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Marble filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [scale, turbulence, quality, edge_mode_index, seed]
 *               scale: marble scale 0-100 (float)
 *               turbulence: turbulence 0-1 (float)
 *               quality: quality 1-5 (float, cast to int)
 *               edge_mode_index: combo index 0-4 (clamp, reflect, wrap, erase, ignore)
 *               seed: random seed (float, cast to unsigned int)
 * @param num_values Number of values (should be 5)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_marble_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_MARBLE_H */
