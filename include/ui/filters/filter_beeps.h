#ifndef FILTER_BEEPS_H
#define FILTER_BEEPS_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply BEEPS filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param photometric_std_dev Photometric standard deviation (1.0-255.0)
 * @param spatial_decay Spatial decay (0.01-0.25)
 * @param range_filter Range filter mode (0=gentle, 1=balanced, 2=sharp)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_beeps_apply(ImageLayer* layer, gfloat photometric_std_dev, gfloat spatial_decay, gint range_filter);

#endif /* FILTER_BEEPS_H */
