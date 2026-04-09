/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_RETINEX_H
#define FILTER_RETINEX_H

#include "ocular.h"
#include "render/layer.h"
#include <glib.h>

/**
 * Apply retinex filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param mode Retinex mode (OC_RETINEX_UNIFORM, OC_RETINEX_LOW, OC_RETINEX_HIGH)
 * @param scale Scale parameter (integer)
 * @param numScales Number of scales (float)
 * @param dynamic Dynamic parameter (float)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_retinex_apply(ImageLayer* layer, OcRetinexMode mode, gint scale, gfloat numScales, gfloat dynamic);

#endif /* FILTER_RETINEX_H */
