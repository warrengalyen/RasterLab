#ifndef FILTER_SHADOW_HIGHLIGHTS_H
#define FILTER_SHADOW_HIGHLIGHTS_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply shadow/highlights filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values: [shadows, highlights, midtoneContrast]
 * @param num_values Number of values (should be 3)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_shadow_highlights_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_SHADOW_HIGHLIGHTS_H */

