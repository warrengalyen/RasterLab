#ifndef FILTER_SPHERIZE_H
#define FILTER_SPHERIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply spherize distortion filter to a layer using Ocular library.
 * @param values[0] amount (int, typically -100..100)
 * @param values[1] mode (0=Normal, 1=Horizontal, 2=Vertical)
 */
gboolean filter_spherize_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_SPHERIZE_H */
