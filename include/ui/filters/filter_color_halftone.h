#ifndef FILTER_COLOR_HALFTONE_H
#define FILTER_COLOR_HALFTONE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply color halftone filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [radius, dotDensity, cyanAngle, magentaAngle, yellowAngle]
 *               radius: halftone dot radius in pixels (integer)
 *               dotDensity: density of halftone dots (0.0-1.0)
 *               cyanAngle, magentaAngle, yellowAngle: screen angles in degrees (0.0-360.0)
 * @param num_values Number of values (should be 5)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_color_halftone_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_COLOR_HALFTONE_H */
