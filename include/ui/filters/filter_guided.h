/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_GUIDED_H
#define FILTER_GUIDED_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply guided filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values (values[0] = radius, 1-64; values[1] = epsilon, 0.001-4.0)
 * @param num_values Number of values (must be 2)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_guided_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_GUIDED_H */
