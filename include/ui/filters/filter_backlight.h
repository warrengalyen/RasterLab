#ifndef FILTER_BACKLIGHT_H
#define FILTER_BACKLIGHT_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply backlight repair filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_backlight_apply(ImageLayer *layer);

#endif /* FILTER_BACKLIGHT_H */

