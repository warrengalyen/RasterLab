#ifndef SELECTION_MASK_H
#define SELECTION_MASK_H

#include "selection.h" /* For SelectionSmoothing and SelectionCombineMode enums */
#include <cairo.h>
#include <glib.h>
#include <stdint.h>

/**
 * Individual selection with per-selection feathering parameters
 * Each selection maintains its own feathering state independently
 */
typedef struct Selection {
    /* Selection geometry (hard-edged mask region) */
    int x, y, width, height; /* Bounding rectangle */
    uint8_t* mask;           /* Hard-edged mask for this selection (0/255 only, owned) */

    /* Per-selection feathering parameters */
    float feather_radius;                /* Feather radius in pixels (0.0 = no feathering) */
    SelectionSmoothingMode feather_mode; /* Feathering algorithm */

    /* Combine mode used when this selection was created */
    SelectionCombineMode combine_mode;

    /* Cached feathered preview for this selection (lazy-generated) */
    uint8_t* feathered_preview; /* Feathered mask (owned, NULL if not generated) */
    gboolean feather_dirty;     /* TRUE if feathered_preview needs regeneration */

    /* Reference count for memory management */
    int ref_count;
} Selection;

/**
 * Pixel mask representation of selection
 *
 * NEW ARCHITECTURE:
 * - base_mask is the COMBINED result of all selections (hard 0/255 edges only)
 * - selections list stores individual Selection objects with per-selection feathering
 * - feathered_preview is the combined feathered result of all selections
 * - feathering is NEVER baked into base_mask
 * - Each selection has independent feather_radius and feather_mode
 */
typedef struct SelectionMask {
    int width;  /* Mask width in pixels */
    int height; /* Mask height in pixels */
    int stride; /* Bytes per row (aligned) */

    /* Authoritative selection data */
    uint8_t* base_mask; /* COMBINED: Hard-edged mask 0/255 only (never feathered, owned) */
    uint8_t* temp_data; /* Temporary buffer for operations (owned) */

    /* Individual selections with per-selection feathering */
    GList* selections; /* List of Selection* objects (each with own feathering) */

    /* Derived views */
    uint8_t* data;              /* Current mask pointer (either base_mask or feathered_preview) */
    uint8_t* feathered_preview; /* Cached combined feathered preview (owned, regenerated on demand) */

    /* Cairo surface cache */
    cairo_surface_t* surface; /* ARGB32 cached surface (owned) */
    gboolean dirty;           /* TRUE if surface needs rebuild from mask */

    /* Feathering preview state */
    gboolean feather_dirty; /* TRUE if feathered_preview needs recompute */
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
 * @param direct_modify If TRUE, directly modify base_mask without creating Selection objects
 *                      (useful for programmatic operations like Select All that shouldn't trigger tool commands)
 */
void selection_mask_fill_rect(
    SelectionMask* mask,
    int x, int y, int width, int height,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius,
    gboolean direct_modify);

/**
 * Fill elliptical region in mask with optional smoothing
 * @param mask Target mask to modify
 * @param x Left coordinate of bounding rectangle
 * @param y Top coordinate of bounding rectangle
 * @param width Bounding rectangle width
 * @param height Bounding rectangle height
 * @param combine How to combine with existing mask
 * @param smoothing Edge smoothing mode
 * @param feather_radius Feather radius for SMOOTH_FEATHERED (pixels)
 * @param direct_modify If TRUE, directly modify base_mask without creating Selection objects
 *                      (useful for programmatic operations like Select All that shouldn't trigger tool commands)
 */
void selection_mask_fill_ellipse(
    SelectionMask* mask,
    int x, int y, int width, int height,
    SelectionCombineMode combine,
    SelectionSmoothingMode smoothing,
    float feather_radius,
    gboolean direct_modify);

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
 * Commit feathering parameters to selection
 * Does NOT bake into base_mask (base_mask stays hard-edged)
 * Only stores feathering parameters for non-destructive rendering
 * Call this when finalizing a selection to lock in the feather parameters
 * @param mask The selection mask
 * @param feather_mode Feathering algorithm (none / antialiased / feathered)
 * @param feather_radius Feather radius in pixels
 */
void selection_mask_commit_feathering(SelectionMask* mask,
                                      SelectionSmoothingMode feather_mode,
                                      float feather_radius);

/**
 * Regenerate feathered preview from base_mask and feather parameters
 * Called after feathering parameters are changed to update the cached preview
 * @param mask The selection mask
 */
void selection_mask_regenerate_feather_preview(SelectionMask* mask);

/* ============================================================
 * Per-Selection API (NEW - for per-selection feathering)
 * ============================================================ */

/**
 * Create a new Selection object
 * @param x Left coordinate of selection rectangle
 * @param y Top coordinate of selection rectangle
 * @param width Width of selection rectangle
 * @param height Height of selection rectangle
 * @param combine_mode Combine mode used when creating this selection
 * @param feather_mode Feathering mode for this selection
 * @param feather_radius Feather radius in pixels (0.0 = no feathering)
 * @return New Selection object with ref_count=1, or NULL on error
 */
Selection* selection_new(int x, int y, int width, int height,
                         SelectionCombineMode combine_mode,
                         SelectionSmoothingMode feather_mode,
                         float feather_radius);

/**
 * Increment reference count for a Selection
 * @param sel The selection to reference
 * @return The same Selection pointer
 */
Selection* selection_ref(Selection* sel);

/**
 * Decrement reference count for a Selection (frees if count reaches 0)
 * @param sel The selection to unreference
 */
void selection_unref(Selection* sel);

/**
 * Get the list of all selections in a mask
 * @param mask The selection mask
 * @return GList of Selection* objects (do not modify, do not free)
 */
GList* selection_mask_get_selections(SelectionMask* mask);

/**
 * Add a selection to the mask
 * Updates base_mask to include the new selection according to its combine_mode
 * @param mask The selection mask
 * @param sel The selection to add (ownership transferred, will be freed when mask is freed)
 */
void selection_mask_add_selection(SelectionMask* mask, Selection* sel);

/**
 * Remove a selection from the mask
 * Updates base_mask to remove the selection
 * @param mask The selection mask
 * @param sel The selection to remove
 */
void selection_mask_remove_selection(SelectionMask* mask, Selection* sel);

/**
 * Rebuild base_mask from all selections in the list
 * Combines all selections according to their combine_mode
 * @param mask The selection mask
 */
void selection_mask_rebuild_from_selections(SelectionMask* mask);

/**
 * Regenerate combined feathered preview from all selections
 * Applies per-selection feathering and combines results
 * @param mask The selection mask
 */
void selection_mask_regenerate_combined_feather_preview(SelectionMask* mask);

/* ============================================================
 * Selection Modification Operations
 * ============================================================ */

/**
 * Progress callback for selection operations
 * Called periodically during long operations to update progress
 * @param current Current selection index (0-based)
 * @param total Total number of selections
 * @param user_data User data passed to the operation
 * @return TRUE to continue, FALSE to cancel
 */
typedef gboolean (*SelectionOperationProgressCallback)(gint current, gint total, gpointer user_data);

/**
 * Grow selection by specified radius (dilate)
 * Expands each selection outward by the given number of pixels
 * Preserves each selection's individual feathering parameters
 * @param mask The selection mask to modify
 * @param radius Radius in pixels (1-500)
 * @param progress_callback Optional callback for progress updates (can be NULL)
 * @param progress_user_data User data for progress callback
 * @return TRUE if successful, FALSE otherwise
 */
gboolean selection_mask_grow(SelectionMask* mask, gint radius,
                             SelectionOperationProgressCallback progress_callback,
                             gpointer progress_user_data);

/**
 * Shrink selection by specified radius (erode)
 * Contracts each selection inward by the given number of pixels
 * Preserves each selection's individual feathering parameters
 * @param mask The selection mask to modify
 * @param radius Radius in pixels (1-500)
 * @param progress_callback Optional callback for progress updates (can be NULL)
 * @param progress_user_data User data for progress callback
 * @return TRUE if successful, FALSE otherwise
 */
gboolean selection_mask_shrink(SelectionMask* mask, gint radius,
                               SelectionOperationProgressCallback progress_callback,
                               gpointer progress_user_data);

/**
 * Create border selection (dilate then subtract original)
 * Creates a selection that is only the border/edge of each original selection
 * Preserves each selection's individual feathering parameters
 * @param mask The selection mask to modify
 * @param radius Border width in pixels (1-500)
 * @param progress_callback Optional callback for progress updates (can be NULL)
 * @param progress_user_data User data for progress callback
 * @return TRUE if successful, FALSE otherwise
 */
gboolean selection_mask_border(SelectionMask* mask, gint radius,
                               SelectionOperationProgressCallback progress_callback,
                               gpointer progress_user_data);

/**
 * Feather selection edges (modify base_mask with feathering)
 * Applies feathering directly to each selection's mask
 * Preserves each selection's individual feathering parameters
 * @param mask The selection mask to modify
 * @param radius Feather radius in pixels (1-500)
 * @param progress_callback Optional callback for progress updates (can be NULL)
 * @param progress_user_data User data for progress callback
 * @return TRUE if successful, FALSE otherwise
 */
gboolean selection_mask_feather(SelectionMask* mask, gint radius,
                                SelectionOperationProgressCallback progress_callback,
                                gpointer progress_user_data);

/**
 * Sharpen selection edges (contract then expand to make edges harder)
 * Makes each selection's edges more defined by shrinking then growing
 * Preserves each selection's individual feathering parameters
 * @param mask The selection mask to modify
 * @param radius Sharpen radius in pixels (1-500)
 * @param progress_callback Optional callback for progress updates (can be NULL)
 * @param progress_user_data User data for progress callback
 * @return TRUE if successful, FALSE otherwise
 */
gboolean selection_mask_sharpen(SelectionMask* mask, gint radius,
                                SelectionOperationProgressCallback progress_callback,
                                gpointer progress_user_data);

#endif /* SELECTION_MASK_H */
