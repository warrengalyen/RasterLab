/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_OIL_PAINT_H
#define FILTER_OIL_PAINT_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply oil paint filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of values (values[0] = radius, values[1] = intensity)
 * @param num_values Number of values (must be 2)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_oil_paint_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_OIL_PAINT_H */
