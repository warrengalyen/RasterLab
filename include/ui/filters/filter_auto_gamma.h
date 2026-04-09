/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_AUTO_GAMMA_H
#define FILTER_AUTO_GAMMA_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply auto gamma correction filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_auto_gamma_apply(ImageLayer *layer);

#endif /* FILTER_AUTO_GAMMA_H */

