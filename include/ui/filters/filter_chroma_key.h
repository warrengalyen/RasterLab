/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_CHROMA_KEY_H
#define FILTER_CHROMA_KEY_H

#include "ocular.h"
#include "render/layer.h"
#include <glib.h>

/**
 * Apply chroma key filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param values Array of values: [r, g, b, threshold, smoothing]
 *               r, g, b: Color to replace (0.0-1.0 range, will be converted to 0-255)
 *               threshold: Threshold sensitivity (0.0-1.0)
 *               smoothing: Smoothing amount (0.0-1.0)
 * @param num_values Number of values (should be 5)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_chroma_key_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_CHROMA_KEY_H */
