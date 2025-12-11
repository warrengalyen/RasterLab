#ifndef FILTER_BRIGHTNESS_CONTRAST_H
#define FILTER_BRIGHTNESS_CONTRAST_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply brightness and contrast filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values: [brightness, contrast]
 * @param num_values Number of values (should be 2)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_brightness_contrast_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_BRIGHTNESS_CONTRAST_H */

