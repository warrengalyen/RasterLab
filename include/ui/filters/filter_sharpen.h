#ifndef FILTER_SHARPEN_H
#define FILTER_SHARPEN_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply sharpen filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = strength, typically 0.1-10.0)
 * @param num_values Number of values (must be 1)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_sharpen_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_SHARPEN_H */
