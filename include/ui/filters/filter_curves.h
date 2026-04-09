/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_CURVES_H
#define FILTER_CURVES_H

#include "render/layer.h"
#include "ui/widgets/curves_widget.h"
#include <glib.h>

/**
 * Apply curves filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param curves The curves widget containing the curve data
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_curves_apply(ImageLayer* layer, CurvesWidget* curves);

#endif /* FILTER_CURVES_H */
