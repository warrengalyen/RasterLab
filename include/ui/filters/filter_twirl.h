/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_TWIRL_H
#define FILTER_TWIRL_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply twirl distortion filter to a layer using Ocular library.
 * @param values[0] angle (degrees, float)
 */
gboolean filter_twirl_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_TWIRL_H */
