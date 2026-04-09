/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef SELECTION_RENDER_H
#define SELECTION_RENDER_H

#include "render/dirty.h"
#include "selection/selection_mask.h"
#include <glib.h>

/**
 * Feather quality settings for selection mask generation
 */
typedef enum {
    FEATHER_QUALITY_FAST,   /* Fast generation, may skip some feathering */
    FEATHER_QUALITY_NORMAL, /* Normal quality (default) */
    FEATHER_QUALITY_HIGH    /* High quality, full feathering */
} FeatherQuality;

/**
 * Build a combined, feathered selection mask for a specific region
 *
 * This function:
 * - Combines all selections using their combine modes
 * - Applies feathering dynamically per selection
 * - Generates mask ONLY for the specified dirty_region
 * - Returns a temporary mask that the caller owns (must free with selection_mask_free)
 *
 * @param selection_mask The document's selection mask (from doc->selection_mask)
 * @param dirty_region The region to generate mask for (in document coordinates)
 * @param quality Feather quality setting
 * @return Newly allocated SelectionMask for the region, or NULL if no selection or error
 *         Caller must free with selection_mask_free()
 */
SelectionMask* selection_build_combined_mask(
    SelectionMask* selection_mask,
    const DirtyRect* dirty_region,
    FeatherQuality quality,
    DirtyRect* out_actual_region);

#endif /* SELECTION_RENDER_H */
