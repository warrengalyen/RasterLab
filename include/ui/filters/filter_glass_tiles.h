/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_GLASS_TILES_H
#define FILTER_GLASS_TILES_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Glass Tiles filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [angle, size, curvature, quality, edge_mode_index]
 *               angle: rotation in degrees (float, -45 to 45)
 *               size: tile size 1-100 (float, cast to int)
 *               curvature: curvature amount (float, -20 to 20)
 *               quality: quality 1-5 (float, cast to int)
 *               edge_mode_index: combo index 0-4 (clamp, reflect, wrap, erase, ignore)
 * @param num_values Number of values (should be 5)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_glass_tiles_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_GLASS_TILES_H */
