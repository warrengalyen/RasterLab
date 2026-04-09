/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_POSTERIZE_H
#define FILTER_POSTERIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply posterize filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of filter values: [levels]
 *               levels: number of levels (integer, typically 2-255)
 * @param num_values Number of values (should be 1)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_posterize_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_POSTERIZE_H */
