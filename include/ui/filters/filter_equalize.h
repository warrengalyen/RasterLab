#ifndef FILTER_EQUALIZE_H
#define FILTER_EQUALIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply histogram equalize filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_equalize_apply(ImageLayer *layer);

#endif /* FILTER_EQUALIZE_H */

