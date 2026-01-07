#ifndef FILTER_TWIRL_H
#define FILTER_TWIRL_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply twirl distortion filter to a layer using Ocular library.
 * @param values[0] angle (degrees, float)
 */
gboolean filter_twirl_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_TWIRL_H */
