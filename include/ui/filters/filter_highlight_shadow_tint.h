#ifndef FILTER_HIGHLIGHT_SHADOW_TINT_H
#define FILTER_HIGHLIGHT_SHADOW_TINT_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply highlight/shadow tint filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [shadowR, shadowG, shadowB, highlightR, highlightG, highlightB, shadowIntensity, highlightIntensity]
 *               All values are in 0.0-1.0 range (will be converted to float for Ocular)
 * @param num_values Number of values (should be 8)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_highlight_shadow_tint_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_HIGHLIGHT_SHADOW_TINT_H */
