/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_SPHERIZE_H
#define FILTER_SPHERIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply spherize distortion filter to a layer using Ocular library.
 * @param values[0] amount (int, typically -100..100)
 * @param values[1] mode (0=Normal, 1=Horizontal, 2=Vertical)
 */
gboolean filter_spherize_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_SPHERIZE_H */
