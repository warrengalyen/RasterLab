#ifndef FILTER_CANNY_EDGE_H
#define FILTER_CANNY_EDGE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Canny edge detection filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_canny_edge_apply(ImageLayer* layer);

#endif /* FILTER_CANNY_EDGE_H */
