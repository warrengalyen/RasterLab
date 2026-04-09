/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_CHANNEL_MIXER_H
#define FILTER_CHANNEL_MIXER_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply channel mixer filter to a layer.
 * @param layer The image layer (ARGB32 surface)
 * @param mixer 16 floats: 4 output rows (R, G, B, Gray) × 4 inputs (R, G, B, Constant)
 * @param monochrome TRUE for monochrome output
 * @param preserve_luminance TRUE to preserve luminance
 * @return TRUE on success
 */
gboolean filter_channel_mixer_apply(ImageLayer* layer,
                                    const gfloat* mixer,
                                    gboolean monochrome,
                                    gboolean preserve_luminance);

/**
 * Apply channel mixer using standard filter signature (for undo/command).
 * values: 16 floats (mixer) + (float)monochrome + (float)preserve_luminance => 18 values
 */
gboolean filter_channel_mixer_apply_values(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_CHANNEL_MIXER_H */
