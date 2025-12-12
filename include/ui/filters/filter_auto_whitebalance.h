#ifndef FILTER_AUTO_WHITEBALANCE_H
#define FILTER_AUTO_WHITEBALANCE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply auto white balance filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_auto_whitebalance_apply(ImageLayer *layer);

#endif /* FILTER_AUTO_WHITEBALANCE_H */

