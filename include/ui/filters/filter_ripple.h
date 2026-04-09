/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_RIPPLE_H
#define FILTER_RIPPLE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply ripple distortion filter to a layer using Ocular library.
 * @param values[0] wavelength
 * @param values[1] amplitude
 * @param values[2] centerX (normalized 0..1)
 * @param values[3] centerY (normalized 0..1)
 * @param values[4] radius_percentage (implementation expects 0..1)
 * @param values[5] phase (degrees)
 */
gboolean filter_ripple_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_RIPPLE_H */
