#ifndef FILTER_SOBEL_EDGE_H
#define FILTER_SOBEL_EDGE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Sobel edge detection filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_sobel_edge_apply(ImageLayer* layer);

#endif /* FILTER_SOBEL_EDGE_H */
