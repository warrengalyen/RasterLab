#ifndef FILTER_AUTO_LEVEL_H
#define FILTER_AUTO_LEVEL_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply auto level filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_auto_level_apply(ImageLayer *layer);

#endif /* FILTER_AUTO_LEVEL_H */

