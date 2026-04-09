/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_GAMMA_H
#define FILTER_GAMMA_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply gamma correction filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of values (values[0] = red gamma, values[1] = green gamma, values[2] = blue gamma)
 * @param num_values Number of values (must be 3)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_gamma_apply(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_GAMMA_H */

