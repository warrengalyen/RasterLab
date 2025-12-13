#ifndef FILTER_DEHAZE_H
#define FILTER_DEHAZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply dehaze filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of values (values[0] = radius, values[1] = guideRadius, 
 *               values[2] = maxAtm, values[3] = omega, values[4] = epsilon, values[5] = t0)
 * @param num_values Number of values (must be 6)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_dehaze_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_DEHAZE_H */
