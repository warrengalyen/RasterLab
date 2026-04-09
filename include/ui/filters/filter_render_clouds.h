/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_RENDER_CLOUDS_H
#define FILTER_RENDER_CLOUDS_H

#include "ocular.h"
#include "render/layer.h"
#include <glib.h>

/**
 * Apply render clouds filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param params CloudParams structure with all cloud parameters
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_render_clouds_apply(ImageLayer* layer, const CloudParams* params);

#endif /* FILTER_RENDER_CLOUDS_H */
