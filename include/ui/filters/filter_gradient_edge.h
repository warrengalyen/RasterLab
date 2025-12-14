#ifndef FILTER_GRADIENT_EDGE_H
#define FILTER_GRADIENT_EDGE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Gradient edge detection filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_gradient_edge_apply(ImageLayer* layer);

#endif /* FILTER_GRADIENT_EDGE_H */
