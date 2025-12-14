#ifndef FILTER_FRAGMENT_H
#define FILTER_FRAGMENT_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply fragment filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_fragment_apply(ImageLayer* layer);

#endif /* FILTER_FRAGMENT_H */
