#ifndef ADJUSTMENTS_H
#define ADJUSTMENTS_H

#include <glib.h>
#include "render/layer.h"

/**
 * Apply grayscale filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_apply_grayscale(ImageLayer *layer);

#endif /* ADJUSTMENTS_H */

