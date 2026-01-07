#ifndef FILTER_PINCH_H
#define FILTER_PINCH_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply pinch distortion filter to a layer using Ocular library.
 * @param values[0] amount (float)
 */
gboolean filter_pinch_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_PINCH_H */
