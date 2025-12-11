#ifndef FILTER_EXPOSURE_H
#define FILTER_EXPOSURE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply exposure filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values: [exposure]
 * @param num_values Number of values (should be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_exposure_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_EXPOSURE_H */

