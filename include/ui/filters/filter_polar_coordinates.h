#ifndef FILTER_POLAR_COORDINATES_H
#define FILTER_POLAR_COORDINATES_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply polar coordinates distortion to a layer using Ocular library.
 * @param values[0] mode (0=Rect->Polar, 1=Polar->Rect)
 */
gboolean filter_polar_coordinates_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_POLAR_COORDINATES_H */
