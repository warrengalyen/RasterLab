#ifndef FILTER_COLOR_INVERT_H
#define FILTER_COLOR_INVERT_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply color invert filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_color_invert_apply(ImageLayer* layer);

#endif /* FILTER_COLOR_INVERT_H */
