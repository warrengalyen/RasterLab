#ifndef FILTER_ROBERTS_EDGE_H
#define FILTER_ROBERTS_EDGE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Roberts edge detection filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_roberts_edge_apply(ImageLayer* layer);

#endif /* FILTER_ROBERTS_EDGE_H */
