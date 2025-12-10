#ifndef FILTER_STRETCH_H
#define FILTER_STRETCH_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply histogram stretch filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_stretch_apply(ImageLayer *layer);

#endif /* FILTER_STRETCH_H */

