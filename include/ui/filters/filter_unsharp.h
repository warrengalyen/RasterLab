/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_UNSHARP_H
#define FILTER_UNSHARP_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply unsharp mask filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, values[1] = intensity, values[2] = threshold)
 *               radius: 0.1-200.0, intensity: 0.1-4.0, threshold: 0-100
 * @param num_values Number of values (must be 3)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_unsharp_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_UNSHARP_H */
