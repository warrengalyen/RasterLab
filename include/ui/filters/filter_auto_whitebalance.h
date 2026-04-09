/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_AUTO_WHITEBALANCE_H
#define FILTER_AUTO_WHITEBALANCE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply auto white balance filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_auto_whitebalance_apply(ImageLayer *layer);

#endif /* FILTER_AUTO_WHITEBALANCE_H */

