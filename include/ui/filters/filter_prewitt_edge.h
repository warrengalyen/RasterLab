/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_PREWITT_EDGE_H
#define FILTER_PREWITT_EDGE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply Prewitt edge detection filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_prewitt_edge_apply(ImageLayer* layer);

#endif /* FILTER_PREWITT_EDGE_H */
