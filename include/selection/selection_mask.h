#ifndef SELECTION_MASK_H
#define SELECTION_MASK_H

#include "selection.h" /* For SelectionSmoothing and SelectionCombineMode enums */
#include <cairo.h>
#include <glib.h>
#include <stdint.h>

/**
 * Pixel mask representation of selection
 * - Authoritative data is the uint8_t array
 * - Cairo surface is cached visualization only
 * - Values: 0 = unselected, 255 = fully selected, 0-255 = partially selected
 */
typedef struct SelectionMask {
    int width;                /* Mask width in pixels */
    int height;               /* Mask height in pixels */
    int stride;               /* Bytes per row (aligned) */
    uint8_t* base_mask;       /* Hard mask: 0 = unselected, 255 = selected (owned) */
    uint8_t* data;            /* Current mask data (either base_mask or feathered preview) */
    cairo_surface_t* surface; /* ARGB32 cached surface (owned) */
    gboolean dirty;           /* TRUE if surface needs rebuild from mask */
    uint8_t* temp_data;       /* Temporary buffer for operations */

    /* Feathering support */
    uint8_t* preview_feather_mask; /* Preview feather mask for rendering (owned, lower res or cached) */
    float feather_radius;          /* Current feather radius (0 = no feathering) */
    gboolean feather_dirty;        /* TRUE if preview_feather_mask needs recompute */
} SelectionMask;

/**
 * Create a new selection mask
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return Newly allocated SelectionMask, initially all zeros (empty)
 */
SelectionMask* selection_mask_new(int width, int height);

/**
 * Free selection mask and all resources
 * @param mask The mask to free
 */
void selection_mask_free(SelectionMask* mask);

/**
 * Clear selection mask to all zeros (empty)
 * @param mask The mask to clear
 */
void selection_mask_clear(SelectionMask* mask);

/**
 * Check if selection mask is completely empty
 * @param mask The mask to check
 * @return TRUE if all pixels are 0
 */
gboolean selection_mask_is_empty(SelectionMask* mask);

/**
 * Fill rectangular region in mask with optional smoothing
 * @param mask Target mask to modify
 * @param x Left coordinate
 * @param y Top coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param combine How to combine with existing mask
 * @param smoothing Edge smoothing mode
 * @param feather_radius Feather radius for SMOOTH_FEATHERED (pixels)
 */
void selection_mask_fill_rect(
    SelectionMask* mask,
    int x, int y, int width, int height,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius);

/**
 * Apply one mask to another using combine mode
 * @param dest Destination mask (modified in place)
 * @param src Source mask to apply
 * @param combine How to combine the masks
 */
void selection_mask_apply(
    SelectionMask* dest,
    SelectionMask* src,
    SelectionCombineMode combine);

/**
 * Get alpha value at specific pixel
 * @param mask The mask
 * @param x Pixel X coordinate
 * @param y Pixel Y coordinate
 * @return Alpha value (0-255), or 0 if out of bounds
 */
uint8_t selection_mask_get_alpha(SelectionMask* mask, int x, int y);

/**
 * Get Cairo surface for rendering (creates if needed)
 * Rebuilds surface from mask data if dirty flag is set
 * @param mask The mask
 * @return Cairo surface, or NULL on error
 */
cairo_surface_t* selection_mask_get_surface(SelectionMask* mask);

/**
 * Mark a region as dirty (surface needs rebuild)
 * @param mask The mask
 * @param x Left coordinate of dirty region
 * @param y Top coordinate of dirty region
 * @param width Width of dirty region
 * @param height Height of dirty region
 */
void selection_mask_mark_dirty(SelectionMask* mask, int x, int y, int width, int height);

/**
 * Render animated marching ants outline
 * Detects edges from mask threshold (alpha >= 128) and draws animated outline
 * @param cr Cairo context to draw on
 * @param mask The selection mask
 * @param dash_phase Animation phase (0-3 for 4-pixel dashes)
 * @param zoom_factor Current zoom level (for line width)
 */
void selection_mask_render_outline(
    cairo_t* cr,
    SelectionMask* mask,
    int dash_phase,
    gdouble zoom_factor);

/**
 * Apply feathering permanently to base_mask
 * Converts preview feathering to the actual base_mask
 * Call this when finalizing a selection
 * @param mask The selection mask
 */
void selection_mask_commit_feathering(SelectionMask* mask);

#endif /* SELECTION_MASK_H */
