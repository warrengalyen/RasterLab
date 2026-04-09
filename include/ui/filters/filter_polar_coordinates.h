/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_POLAR_COORDINATES_H
#define FILTER_POLAR_COORDINATES_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply polar coordinates distortion to a layer using Ocular library.
 * @param values[0] mode (0=Rect->Polar, 1=Polar->Rect)
 */
gboolean filter_polar_coordinates_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_POLAR_COORDINATES_H */
