/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_HSL_H
#define FILTER_HSL_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply HSL filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values: [hue, saturation, lightness]
 * @param num_values Number of values (should be 3)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_hsl_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_HSL_H */

