/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_KALEIDOSCOPE_H
#define FILTER_KALEIDOSCOPE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply kaleidoscope distortion filter to a layer using Ocular library.
 * @param values[0] mirrors (int)
 * @param values[1] angle (degrees, float)
 * @param values[2] angle2 (degrees, float)
 * @param values[3] centerX (normalized 0..1)
 * @param values[4] centerY (normalized 0..1)
 * @param values[5] radius_percent (0..1)
 */
gboolean filter_kaleidoscope_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_KALEIDOSCOPE_H */
