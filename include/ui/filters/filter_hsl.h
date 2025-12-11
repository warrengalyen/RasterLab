#ifndef FILTER_HSL_H
#define FILTER_HSL_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply HSL filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values: [hue, saturation, lightness]
 * @param num_values Number of values (should be 3)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_hsl_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_HSL_H */

