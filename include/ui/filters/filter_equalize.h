/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_EQUALIZE_H
#define FILTER_EQUALIZE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply histogram equalize filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_equalize_apply(ImageLayer *layer);

#endif /* FILTER_EQUALIZE_H */

