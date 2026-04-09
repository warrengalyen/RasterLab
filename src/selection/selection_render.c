/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "selection/selection_render.h"
#include "render/dirty.h"
#include "selection/selection_mask.h"
#include <stdlib.h>
#include <string.h>

/**
 * Build a combined, feathered selection mask for a specific region
 */
SelectionMask* selection_build_combined_mask(
    SelectionMask* selection_mask,
    const DirtyRect* dirty_region,
    FeatherQuality quality,
    DirtyRect* out_actual_region) {

    if (!selection_mask || !dirty_region || dirty_rect_is_empty(dirty_region)) {
        if (out_actual_region) {
            dirty_rect_init(out_actual_region);
        }
        return NULL;
    }

    /* Check if selection is empty */
    if (selection_mask_is_empty(selection_mask)) {
        if (out_actual_region) {
            dirty_rect_init(out_actual_region);
        }
        return NULL;
    }

    /* Clamp region to selection mask bounds */
    DirtyRect clamped_region = *dirty_region;
    dirty_rect_clamp(&clamped_region, selection_mask->width, selection_mask->height);

    if (dirty_rect_is_empty(&clamped_region)) {
        if (out_actual_region) {
            dirty_rect_init(out_actual_region);
        }
        return NULL;
    }

    /* Return actual region if requested */
    if (out_actual_region) {
        *out_actual_region = clamped_region;
    }

    /* Create a new mask for just this region */
    SelectionMask* region_mask = selection_mask_new(clamped_region.width, clamped_region.height);
    if (!region_mask) {
        return NULL;
    }

    /* Check if any selections need feathering */
    gboolean any_feathering = FALSE;
    GList* iter;
    for (iter = selection_mask->selections; iter != NULL; iter = iter->next) {
        Selection* sel = (Selection*)iter->data;
        if (sel && sel->feather_mode == SELECTION_SMOOTH_FEATHERED && sel->feather_radius > 0.0f) {
            any_feathering = TRUE;
            break;
        }
    }

    /* If no feathering needed, extract base_mask directly */
    if (!any_feathering) {
        /* Copy base_mask region */
        for (int y = 0; y < clamped_region.height; y++) {
            int src_y = clamped_region.y + y;
            if (src_y < 0 || src_y >= selection_mask->height) {
                continue;
            }

            uint8_t* src_row = selection_mask->base_mask + src_y * selection_mask->stride + clamped_region.x;
            uint8_t* dst_row = region_mask->base_mask + y * region_mask->stride;

            memcpy(dst_row, src_row, clamped_region.width);
        }
        region_mask->data = region_mask->base_mask;
        return region_mask;
    }

    /* Feathering needed - regenerate combined preview if dirty */
    if (selection_mask->feather_dirty) {
        selection_mask_regenerate_combined_feather_preview(selection_mask);
    }

    /* Use feathered_preview if available, otherwise base_mask */
    uint8_t* source_data = selection_mask->feathered_preview ? selection_mask->feathered_preview : selection_mask->base_mask;

    /* Allocate feathered preview for region mask */
    region_mask->feathered_preview = g_malloc0(region_mask->stride * region_mask->height);
    if (!region_mask->feathered_preview) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy feathered region from source */
    for (int y = 0; y < clamped_region.height; y++) {
        int src_y = clamped_region.y + y;
        if (src_y < 0 || src_y >= selection_mask->height) {
            continue;
        }

        uint8_t* src_row = source_data + src_y * selection_mask->stride + clamped_region.x;
        uint8_t* dst_row = region_mask->feathered_preview + y * region_mask->stride;

        memcpy(dst_row, src_row, clamped_region.width);
    }

    region_mask->data = region_mask->feathered_preview;
    region_mask->feather_dirty = FALSE;

    return region_mask;
}
