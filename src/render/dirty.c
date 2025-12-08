#include "render/dirty.h"
#include <string.h>

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

