/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_SPLIT_TONING_H
#define FILTER_SPLIT_TONING_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply split toning filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [highlightR, highlightG, highlightB, balance, shadowR, shadowG, shadowB, strength]
 *               RGB values are in 0.0-1.0 range, balance is in -100.0 to 100.0 range, strength is in 0.0-100.0 range
 * @param num_values Number of values
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_split_toning_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_SPLIT_TONING_H */
