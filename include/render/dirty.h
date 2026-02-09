#ifndef DIRTY_H
#define DIRTY_H

#include <glib.h>
#include <gtk/gtk.h>

/**
 * Structure to represent a dirty rectangle region
 */
typedef struct {
    gint x;      /* Left edge */
    gint y;      /* Top edge */
    gint width;  /* Width */
    gint height; /* Height */
    gboolean valid; /* Is this rectangle valid? */
} DirtyRect;

/**
 * Configuration constants for dirty region coalescing
 */
#define DIRTY_REGION_MAX_RECTS 32       /* Max rects before auto-coalesce */
#define DIRTY_REGION_MERGE_DISTANCE 16  /* Pixels distance to merge nearby rects */
#define DIRTY_REGION_OVERLAP_THRESHOLD 0.5 /* Merge if overlap area > 50% of smaller rect */

/**
 * Structure to manage a list of dirty rectangles with coalescing support.
 * Instead of immediately unioning all dirty rects into one bounding box,
 * this maintains a list of individual rects and coalesces them intelligently
 * to reduce per-tile overhead while avoiding excessive over-invalidation.
 */
typedef struct {
    DirtyRect* rects;       /* Array of dirty rectangles */
    gint count;             /* Number of valid rectangles in array */
    gint capacity;          /* Allocated capacity of array */
    gint merge_distance;    /* Distance threshold for merging nearby rects */
    gint max_rects;         /* Max rects before forcing coalesce */
} DirtyRegionList;

/**
 * Initialize a dirty rectangle to empty/invalid
 */
void dirty_rect_init(DirtyRect *rect);

/**
 * Set a dirty rectangle to specific coordinates
 */
void dirty_rect_set(DirtyRect *rect, gint x, gint y, gint width, gint height);

/**
 * Union two dirty rectangles (combine them)
 * Result is stored in result
 */
void dirty_rect_union(const DirtyRect *a, const DirtyRect *b, DirtyRect *result);

/**
 * Intersect two dirty rectangles
 * Result is stored in result, returns FALSE if no intersection
 */
gboolean dirty_rect_intersect(const DirtyRect *a, const DirtyRect *b, DirtyRect *result);

/**
 * Check if a dirty rectangle is empty/invalid
 */
gboolean dirty_rect_is_empty(const DirtyRect *rect);

/**
 * Clamp a dirty rectangle to document bounds
 */
void dirty_rect_clamp(DirtyRect *rect, gint doc_width, gint doc_height);

/**
 * Expand dirty rectangle by a margin (useful for anti-aliasing)
 */
void dirty_rect_expand(DirtyRect *rect, gint margin);

/**
 * Convert dirty rectangle to GdkRectangle
 */
void dirty_rect_to_gdk(const DirtyRect *rect, GdkRectangle *gdk_rect);

/**
 * Check if two rectangles overlap
 */
gboolean dirty_rect_overlaps(const DirtyRect *a, const DirtyRect *b);

/**
 * Check if two rectangles are nearby (within merge distance)
 */
gboolean dirty_rect_is_nearby(const DirtyRect *a, const DirtyRect *b, gint distance);

/**
 * Calculate the area of a dirty rectangle
 */
gint64 dirty_rect_area(const DirtyRect *rect);

/**
 * Calculate the overlap area between two rectangles
 */
gint64 dirty_rect_overlap_area(const DirtyRect *a, const DirtyRect *b);

/* ============================================================================
 * DirtyRegionList - Dirty region coalescing for improved tile performance
 * ============================================================================
 * 
 * Problem: During drawing operations, many small dirty regions are generated.
 * Immediately unioning them into one bounding box can over-invalidate (marking
 * tiles dirty that didn't actually change). Processing each region individually
 * creates overhead from per-tile setup costs.
 *
 * Solution: Collect dirty regions and coalesce them intelligently:
 * - Merge overlapping rectangles into their union
 * - Merge nearby rectangles (within merge_distance pixels)
 * - Merge small rectangles that would create minimal over-invalidation
 * - Auto-coalesce when list exceeds max_rects threshold
 *
 * This reduces per-tile overhead while minimizing over-invalidation.
 * ============================================================================
 */

/**
 * Create a new dirty region list
 * @return New DirtyRegionList, or NULL on error. Caller must call dirty_region_list_free().
 */
DirtyRegionList* dirty_region_list_create(void);

/**
 * Create a new dirty region list with custom parameters
 * @param max_rects Maximum rectangles before auto-coalescing
 * @param merge_distance Distance threshold for merging nearby rectangles
 * @return New DirtyRegionList, or NULL on error. Caller must call dirty_region_list_free().
 */
DirtyRegionList* dirty_region_list_create_with_params(gint max_rects, gint merge_distance);

/**
 * Free a dirty region list
 * @param list List to free
 */
void dirty_region_list_free(DirtyRegionList* list);

/**
 * Clear all rectangles from the list
 * @param list List to clear
 */
void dirty_region_list_clear(DirtyRegionList* list);

/**
 * Add a dirty rectangle to the list
 * May trigger auto-coalescing if list exceeds max_rects.
 * @param list List to add to
 * @param rect Rectangle to add
 * @return TRUE if rect was added, FALSE if invalid or error
 */
gboolean dirty_region_list_add(DirtyRegionList* list, const DirtyRect* rect);

/**
 * Add a dirty rectangle by coordinates
 * @param list List to add to
 * @param x Left edge
 * @param y Top edge
 * @param width Width
 * @param height Height
 * @return TRUE if rect was added, FALSE if invalid or error
 */
gboolean dirty_region_list_add_rect(DirtyRegionList* list, gint x, gint y, gint width, gint height);

/**
 * Coalesce all rectangles in the list
 * Merges overlapping and nearby rectangles to reduce the total count.
 * @param list List to coalesce
 */
void dirty_region_list_coalesce(DirtyRegionList* list);

/**
 * Get the number of rectangles in the list
 * @param list List to query
 * @return Number of rectangles
 */
gint dirty_region_list_get_count(const DirtyRegionList* list);

/**
 * Get a rectangle from the list by index
 * @param list List to query
 * @param index Index of rectangle (0-based)
 * @return Pointer to rectangle, or NULL if index out of bounds
 */
const DirtyRect* dirty_region_list_get_rect(const DirtyRegionList* list, gint index);

/**
 * Check if the list is empty
 * @param list List to query
 * @return TRUE if list is empty or NULL
 */
gboolean dirty_region_list_is_empty(const DirtyRegionList* list);

/**
 * Get the bounding box of all rectangles in the list
 * @param list List to query
 * @param result Output rectangle containing the bounding box
 * @return TRUE if bounding box was calculated, FALSE if list is empty
 */
gboolean dirty_region_list_get_bounds(const DirtyRegionList* list, DirtyRect* result);

/**
 * Iterate over all rectangles in the list, calling a callback for each
 * @param list List to iterate
 * @param callback Function to call for each rectangle
 * @param user_data User data passed to callback
 */
typedef void (*DirtyRegionCallback)(const DirtyRect* rect, gpointer user_data);
void dirty_region_list_foreach(const DirtyRegionList* list, DirtyRegionCallback callback, gpointer user_data);

#endif /* DIRTY_H */

