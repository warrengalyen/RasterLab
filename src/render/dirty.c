/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "render/dirty.h"
#include <string.h>
#include <stdlib.h>

/**
 * Initialize a dirty rectangle to empty/invalid
 */
void dirty_rect_init(DirtyRect *rect)
{
    if (!rect) {
        return;
    }
    memset(rect, 0, sizeof(DirtyRect));
    rect->valid = FALSE;
}

/**
 * Set a dirty rectangle to specific coordinates
 */
void dirty_rect_set(DirtyRect *rect, gint x, gint y, gint width, gint height)
{
    if (!rect) {
        return;
    }
    
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    rect->valid = (width > 0 && height > 0);
}

/**
 * Union two dirty rectangles (combine them)
 * Result is stored in result
 */
void dirty_rect_union(const DirtyRect *a, const DirtyRect *b, DirtyRect *result)
{
    if (!a || !b || !result) {
        return;
    }
    
    /* If one is empty, return the other */
    if (!a->valid) {
        *result = *b;
        return;
    }
    if (!b->valid) {
        *result = *a;
        return;
    }
    
    /* Calculate bounding box */
    gint left = (a->x < b->x) ? a->x : b->x;
    gint top = (a->y < b->y) ? a->y : b->y;
    gint right = ((a->x + a->width) > (b->x + b->width)) 
                 ? (a->x + a->width) : (b->x + b->width);
    gint bottom = ((a->y + a->height) > (b->y + b->height))
                  ? (a->y + a->height) : (b->y + b->height);
    
    result->x = left;
    result->y = top;
    result->width = right - left;
    result->height = bottom - top;
    result->valid = TRUE;
}

/**
 * Intersect two dirty rectangles
 * Result is stored in result, returns FALSE if no intersection
 */
gboolean dirty_rect_intersect(const DirtyRect *a, const DirtyRect *b, DirtyRect *result)
{
    if (!a || !b || !result) {
        return FALSE;
    }
    
    if (!a->valid || !b->valid) {
        result->valid = FALSE;
        return FALSE;
    }
    
    gint left = (a->x > b->x) ? a->x : b->x;
    gint top = (a->y > b->y) ? a->y : b->y;
    gint right = ((a->x + a->width) < (b->x + b->width))
                 ? (a->x + a->width) : (b->x + b->width);
    gint bottom = ((a->y + a->height) < (b->y + b->height))
                  ? (a->y + a->height) : (b->y + b->height);
    
    if (left < right && top < bottom) {
        result->x = left;
        result->y = top;
        result->width = right - left;
        result->height = bottom - top;
        result->valid = TRUE;
        return TRUE;
    }
    
    result->valid = FALSE;
    return FALSE;
}

/**
 * Check if a dirty rectangle is empty/invalid
 */
gboolean dirty_rect_is_empty(const DirtyRect *rect)
{
    if (!rect) {
        return TRUE;
    }
    return !rect->valid || rect->width <= 0 || rect->height <= 0;
}

/**
 * Clamp a dirty rectangle to document bounds
 */
void dirty_rect_clamp(DirtyRect *rect, gint doc_width, gint doc_height)
{
    if (!rect || !rect->valid) {
        return;
    }
    
    /* Clamp position */
    if (rect->x < 0) {
        rect->width += rect->x;
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->height += rect->y;
        rect->y = 0;
    }
    
    /* Clamp size */
    if (rect->x + rect->width > doc_width) {
        rect->width = doc_width - rect->x;
    }
    if (rect->y + rect->height > doc_height) {
        rect->height = doc_height - rect->y;
    }
    
    /* Mark invalid if clamped to nothing */
    if (rect->width <= 0 || rect->height <= 0) {
        rect->valid = FALSE;
    }
}

/**
 * Expand dirty rectangle by a margin (useful for anti-aliasing)
 */
void dirty_rect_expand(DirtyRect *rect, gint margin)
{
    if (!rect || !rect->valid || margin <= 0) {
        return;
    }
    
    rect->x -= margin;
    rect->y -= margin;
    rect->width += 2 * margin;
    rect->height += 2 * margin;
}

/**
 * Convert dirty rectangle to GdkRectangle
 */
void dirty_rect_to_gdk(const DirtyRect *rect, GdkRectangle *gdk_rect)
{
    if (!rect || !gdk_rect) {
        return;
    }
    
    if (!rect->valid) {
        gdk_rect->x = 0;
        gdk_rect->y = 0;
        gdk_rect->width = 0;
        gdk_rect->height = 0;
        return;
    }
    
    gdk_rect->x = rect->x;
    gdk_rect->y = rect->y;
    gdk_rect->width = rect->width;
    gdk_rect->height = rect->height;
}

/**
 * Check if two rectangles overlap
 */
gboolean dirty_rect_overlaps(const DirtyRect *a, const DirtyRect *b)
{
    if (!a || !b || !a->valid || !b->valid) {
        return FALSE;
    }
    
    /* Check for no overlap conditions */
    if (a->x + a->width <= b->x || b->x + b->width <= a->x ||
        a->y + a->height <= b->y || b->y + b->height <= a->y) {
        return FALSE;
    }
    
    return TRUE;
}

/**
 * Check if two rectangles are nearby (within merge distance)
 * "Nearby" means the gap between them is <= distance pixels
 */
gboolean dirty_rect_is_nearby(const DirtyRect *a, const DirtyRect *b, gint distance)
{
    gint a_right, a_bottom, b_right, b_bottom;
    gint gap_x, gap_y;
    
    if (!a || !b || !a->valid || !b->valid || distance < 0) {
        return FALSE;
    }
    
    a_right = a->x + a->width;
    a_bottom = a->y + a->height;
    b_right = b->x + b->width;
    b_bottom = b->y + b->height;
    
    /* Calculate gap in X direction */
    if (a_right < b->x) {
        gap_x = b->x - a_right;
    } else if (b_right < a->x) {
        gap_x = a->x - b_right;
    } else {
        gap_x = 0; /* Overlapping in X */
    }
    
    /* Calculate gap in Y direction */
    if (a_bottom < b->y) {
        gap_y = b->y - a_bottom;
    } else if (b_bottom < a->y) {
        gap_y = a->y - b_bottom;
    } else {
        gap_y = 0; /* Overlapping in Y */
    }
    
    /* Rectangles are nearby if both gaps are within distance */
    return (gap_x <= distance && gap_y <= distance);
}

/**
 * Calculate the area of a dirty rectangle
 */
gint64 dirty_rect_area(const DirtyRect *rect)
{
    if (!rect || !rect->valid || rect->width <= 0 || rect->height <= 0) {
        return 0;
    }
    return (gint64)rect->width * (gint64)rect->height;
}

/**
 * Calculate the overlap area between two rectangles
 */
gint64 dirty_rect_overlap_area(const DirtyRect *a, const DirtyRect *b)
{
    DirtyRect intersection;
    
    if (!dirty_rect_intersect(a, b, &intersection)) {
        return 0;
    }
    
    return dirty_rect_area(&intersection);
}

/* ============================================================================
 * DirtyRegionList Implementation
 * ============================================================================ */

#define DIRTY_REGION_INITIAL_CAPACITY 8

/**
 * Create a new dirty region list with default parameters
 */
DirtyRegionList* dirty_region_list_create(void)
{
    return dirty_region_list_create_with_params(DIRTY_REGION_MAX_RECTS,
                                                 DIRTY_REGION_MERGE_DISTANCE);
}

/**
 * Create a new dirty region list with custom parameters
 */
DirtyRegionList* dirty_region_list_create_with_params(gint max_rects, gint merge_distance)
{
    DirtyRegionList* list;
    
    list = g_malloc(sizeof(DirtyRegionList));
    if (!list) {
        return NULL;
    }
    
    list->capacity = DIRTY_REGION_INITIAL_CAPACITY;
    list->rects = g_malloc(sizeof(DirtyRect) * list->capacity);
    if (!list->rects) {
        g_free(list);
        return NULL;
    }
    
    list->count = 0;
    list->max_rects = (max_rects > 0) ? max_rects : DIRTY_REGION_MAX_RECTS;
    list->merge_distance = (merge_distance >= 0) ? merge_distance : DIRTY_REGION_MERGE_DISTANCE;
    
    return list;
}

/**
 * Free a dirty region list
 */
void dirty_region_list_free(DirtyRegionList* list)
{
    if (!list) {
        return;
    }
    
    if (list->rects) {
        g_free(list->rects);
    }
    
    g_free(list);
}

/**
 * Clear all rectangles from the list
 */
void dirty_region_list_clear(DirtyRegionList* list)
{
    if (!list) {
        return;
    }
    
    list->count = 0;
}

/**
 * Ensure the list has capacity for at least one more rectangle
 */
static gboolean dirty_region_list_ensure_capacity(DirtyRegionList* list)
{
    gint new_capacity;
    DirtyRect* new_rects;
    
    if (!list) {
        return FALSE;
    }
    
    if (list->count < list->capacity) {
        return TRUE; /* Already have capacity */
    }
    
    /* Grow by 1.5x */
    new_capacity = list->capacity + (list->capacity / 2);
    if (new_capacity < list->capacity + 4) {
        new_capacity = list->capacity + 4;
    }
    
    new_rects = g_realloc(list->rects, sizeof(DirtyRect) * new_capacity);
    if (!new_rects) {
        return FALSE;
    }
    
    list->rects = new_rects;
    list->capacity = new_capacity;
    return TRUE;
}

/**
 * Check if merging two rectangles would create acceptable over-invalidation
 * Returns TRUE if the union area is reasonable compared to individual areas
 */
static gboolean should_merge_rects(const DirtyRect* a, const DirtyRect* b)
{
    DirtyRect union_rect;
    gint64 area_a, area_b, area_union, area_combined, waste;
    gdouble waste_ratio;
    
    if (!a || !b || !a->valid || !b->valid) {
        return FALSE;
    }
    
    /* Always merge overlapping rectangles */
    if (dirty_rect_overlaps(a, b)) {
        return TRUE;
    }
    
    /* Calculate areas */
    area_a = dirty_rect_area(a);
    area_b = dirty_rect_area(b);
    area_combined = area_a + area_b;
    
    dirty_rect_union(a, b, &union_rect);
    area_union = dirty_rect_area(&union_rect);
    
    /* Calculate wasted area (over-invalidation) */
    waste = area_union - area_combined;
    
    /* Don't merge if no benefit */
    if (waste <= 0) {
        return TRUE; /* Union is same or smaller (shouldn't happen, but handle it) */
    }
    
    /* Calculate waste ratio */
    waste_ratio = (gdouble)waste / (gdouble)area_combined;
    
    /* Merge if waste is less than 50% of original combined area */
    /* This prevents merging distant rectangles that would cause massive over-invalidation */
    return (waste_ratio < DIRTY_REGION_OVERLAP_THRESHOLD);
}

/**
 * Add a dirty rectangle to the list
 */
gboolean dirty_region_list_add(DirtyRegionList* list, const DirtyRect* rect)
{
    if (!list || !rect || !rect->valid) {
        return FALSE;
    }
    
    if (rect->width <= 0 || rect->height <= 0) {
        return FALSE;
    }
    
    /* Try to merge with an existing rectangle first */
    for (gint i = 0; i < list->count; i++) {
        DirtyRect* existing = &list->rects[i];
        
        /* Check if we should merge */
        if (dirty_rect_is_nearby(existing, rect, list->merge_distance) &&
            should_merge_rects(existing, rect)) {
            /* Merge into existing rectangle */
            DirtyRect merged;
            dirty_rect_union(existing, rect, &merged);
            *existing = merged;
            return TRUE;
        }
    }
    
    /* No merge candidate found, add as new rectangle */
    if (!dirty_region_list_ensure_capacity(list)) {
        return FALSE;
    }
    
    list->rects[list->count] = *rect;
    list->count++;
    
    /* Auto-coalesce if we exceed max_rects */
    if (list->count >= list->max_rects) {
        dirty_region_list_coalesce(list);
    }
    
    return TRUE;
}

/**
 * Add a dirty rectangle by coordinates
 */
gboolean dirty_region_list_add_rect(DirtyRegionList* list, gint x, gint y, gint width, gint height)
{
    DirtyRect rect;
    dirty_rect_set(&rect, x, y, width, height);
    return dirty_region_list_add(list, &rect);
}

/**
 * Remove a rectangle from the list by swapping with last element
 */
static void dirty_region_list_remove_at(DirtyRegionList* list, gint index)
{
    if (!list || index < 0 || index >= list->count) {
        return;
    }
    
    /* Swap with last element and decrement count */
    if (index < list->count - 1) {
        list->rects[index] = list->rects[list->count - 1];
    }
    list->count--;
}

/**
 * Coalesce all rectangles in the list
 * This is a greedy algorithm that repeatedly merges the best pair of rectangles
 * until no more beneficial merges can be made.
 */
void dirty_region_list_coalesce(DirtyRegionList* list)
{
    gboolean merged_any;
    gint pass_count = 0;
    const gint max_passes = 10; /* Prevent infinite loops */
    
    if (!list || list->count <= 1) {
        return;
    }
    
    /* Repeat until no more merges can be made */
    do {
        merged_any = FALSE;
        pass_count++;
        
        /* Try to merge each pair of rectangles */
        for (gint i = 0; i < list->count && !merged_any; i++) {
            for (gint j = i + 1; j < list->count; j++) {
                DirtyRect* rect_i = &list->rects[i];
                DirtyRect* rect_j = &list->rects[j];
                
                /* Check if these rectangles should be merged */
                if (dirty_rect_overlaps(rect_i, rect_j) ||
                    (dirty_rect_is_nearby(rect_i, rect_j, list->merge_distance) &&
                     should_merge_rects(rect_i, rect_j))) {
                    
                    /* Merge j into i */
                    DirtyRect merged;
                    dirty_rect_union(rect_i, rect_j, &merged);
                    *rect_i = merged;
                    
                    /* Remove j */
                    dirty_region_list_remove_at(list, j);
                    
                    merged_any = TRUE;
                    break; /* Restart outer loop */
                }
            }
        }
    } while (merged_any && pass_count < max_passes);
    
    /* If still too many rectangles, force merge by increasing merge distance */
    while (list->count > list->max_rects / 2 && list->count > 1) {
        /* Find the closest pair and merge them */
        gint best_i = -1, best_j = -1;
        gint64 best_waste = G_MAXINT64;
        
        for (gint i = 0; i < list->count; i++) {
            for (gint j = i + 1; j < list->count; j++) {
                DirtyRect union_rect;
                dirty_rect_union(&list->rects[i], &list->rects[j], &union_rect);
                
                gint64 waste = dirty_rect_area(&union_rect) - 
                               dirty_rect_area(&list->rects[i]) - 
                               dirty_rect_area(&list->rects[j]);
                
                if (waste < best_waste) {
                    best_waste = waste;
                    best_i = i;
                    best_j = j;
                }
            }
        }
        
        if (best_i >= 0 && best_j >= 0) {
            DirtyRect merged;
            dirty_rect_union(&list->rects[best_i], &list->rects[best_j], &merged);
            list->rects[best_i] = merged;
            dirty_region_list_remove_at(list, best_j);
        } else {
            break; /* No pairs found */
        }
    }
}

/**
 * Get the number of rectangles in the list
 */
gint dirty_region_list_get_count(const DirtyRegionList* list)
{
    if (!list) {
        return 0;
    }
    return list->count;
}

/**
 * Get a rectangle from the list by index
 */
const DirtyRect* dirty_region_list_get_rect(const DirtyRegionList* list, gint index)
{
    if (!list || index < 0 || index >= list->count) {
        return NULL;
    }
    return &list->rects[index];
}

/**
 * Check if the list is empty
 */
gboolean dirty_region_list_is_empty(const DirtyRegionList* list)
{
    if (!list) {
        return TRUE;
    }
    return list->count == 0;
}

/**
 * Get the bounding box of all rectangles in the list
 */
gboolean dirty_region_list_get_bounds(const DirtyRegionList* list, DirtyRect* result)
{
    if (!list || !result || list->count == 0) {
        if (result) {
            dirty_rect_init(result);
        }
        return FALSE;
    }
    
    /* Start with first rectangle */
    *result = list->rects[0];
    
    /* Union with remaining rectangles */
    for (gint i = 1; i < list->count; i++) {
        DirtyRect temp;
        dirty_rect_union(result, &list->rects[i], &temp);
        *result = temp;
    }
    
    return TRUE;
}

/**
 * Iterate over all rectangles in the list
 */
void dirty_region_list_foreach(const DirtyRegionList* list, DirtyRegionCallback callback, gpointer user_data)
{
    if (!list || !callback) {
        return;
    }
    
    for (gint i = 0; i < list->count; i++) {
        callback(&list->rects[i], user_data);
    }
}

