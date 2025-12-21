#ifndef FILTER_DESPECKLE_H
#define FILTER_DESPECKLE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply despeckle filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = maxWindowSize, 1-127; values[1] = threshold, 2-255)
 * @param num_values Number of values (must be 2)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_despeckle_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_DESPECKLE_H */
