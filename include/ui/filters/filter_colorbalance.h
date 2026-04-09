/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_COLORBALANCE_H
#define FILTER_COLORBALANCE_H

#include "render/layer.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply color balance filter to a layer using Ocular library
 * @param layer The image layer to apply the filter to
 * @param red_balance Red balance value (-100 to 100)
 * @param green_balance Green balance value (-100 to 100)
 * @param blue_balance Blue balance value (-100 to 100)
 * @param mode Tone balance mode (OC_TONE_SHADOWS, OC_TONE_MIDTONES, OC_TONE_HIGHLIGHTS)
 * @param preserve_luminosity Whether to preserve luminosity
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_colorbalance_apply(ImageLayer *layer, 
                                    gint red_balance, gint green_balance, gint blue_balance,
                                    OcToneBalanceMode mode, gboolean preserve_luminosity);

/**
 * Apply color balance filter using standard filter signature
 * Values array: [red_balance, green_balance, blue_balance, mode, preserve_luminosity]
 * @param layer The image layer to apply the filter to
 * @param values Array of values: [red, green, blue, mode, preserve_luminosity]
 * @param num_values Number of values (should be 5)
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean filter_colorbalance_apply_values(ImageLayer *layer, const gfloat *values, gint num_values);

#endif /* FILTER_COLORBALANCE_H */

