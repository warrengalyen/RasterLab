/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_PORTRAIT_GLOW_H
#define FILTER_PORTRAIT_GLOW_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply portrait glow filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param values Array of values:
 *               values[0] = style (0=classic, 1=modern, 2=subtle)
 *               values[1] = glow radius (1-100)
 *               values[2] = exposure boost (0-200)
 *               values[3] = strength (0-100)
 * @param num_values Number of values (must be 4)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_portrait_glow_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_PORTRAIT_GLOW_H */
