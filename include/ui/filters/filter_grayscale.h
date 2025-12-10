#ifndef FILTER_GRAYSCALE_H
#define FILTER_GRAYSCALE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply grayscale filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_grayscale_apply(ImageLayer *layer);

#endif /* FILTER_GRAYSCALE_H */

