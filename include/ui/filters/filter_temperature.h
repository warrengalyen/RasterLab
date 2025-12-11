#ifndef FILTER_TEMPERATURE_H
#define FILTER_TEMPERATURE_H

#include "document.h"

/**
 * Apply color temperature filter to a layer
 * 
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [temperature, strength]
 * @param num_values Number of values (must be 2)
 * @return TRUE on success, FALSE on error
 */
gboolean filter_temperature_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_TEMPERATURE_H */

