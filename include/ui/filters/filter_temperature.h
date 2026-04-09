/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

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

