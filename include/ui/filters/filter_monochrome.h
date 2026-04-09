/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_MONOCHROME_H
#define FILTER_MONOCHROME_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply monochrome filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [filterR, filterG, filterB, intensity]
 *               filterR, filterG, filterB: filter color components (0.0-1.0 range, converted to 0-255)
 *               intensity: filter intensity (0-100, converted to int)
 * @param num_values Number of values (should be 4)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_monochrome_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_MONOCHROME_H */
