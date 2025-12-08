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

#endif /* DIRTY_H */

