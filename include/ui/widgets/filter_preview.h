#ifndef FILTER_PREVIEW_H
#define FILTER_PREVIEW_H

#include <gtk/gtk.h>
#include <cairo.h>

/**
 * Filter preview widget
 * Displays before/after image preview with fit/1:1 modes and panning support
 */

#define FILTER_PREVIEW_TYPE (filter_preview_get_type())
G_DECLARE_FINAL_TYPE(FilterPreview, filter_preview, FILTER, PREVIEW, GtkBox)

/**
 * Display mode for the preview
 */
typedef enum {
    FILTER_PREVIEW_MODE_FIT,    /* Fit image to container, keep aspect ratio */
    FILTER_PREVIEW_MODE_1TO1    /* Display at 1:1 scale, allow panning */
} FilterPreviewMode;

/**
 * View mode for the preview
 */
typedef enum {
    FILTER_PREVIEW_VIEW_BEFORE, /* Show before image */
    FILTER_PREVIEW_VIEW_AFTER   /* Show after image */
} FilterPreviewView;

/**
 * Create a new filter preview widget
 * @return The new FilterPreview widget
 */
GtkWidget* filter_preview_new(void);

/**
 * Set the before image (Cairo surface)
 * @param preview The filter preview widget
 * @param surface The Cairo surface for the before image (can be NULL)
 */
void filter_preview_set_before_surface(FilterPreview *preview, cairo_surface_t *surface);

/**
 * Set the after image (Cairo surface)
 * @param preview The filter preview widget
 * @param surface The Cairo surface for the after image (can be NULL)
 */
void filter_preview_set_after_surface(FilterPreview *preview, cairo_surface_t *surface);

/**
 * Set the display mode (fit or 1:1)
 * @param preview The filter preview widget
 * @param mode The display mode
 */
void filter_preview_set_mode(FilterPreview *preview, FilterPreviewMode mode);

/**
 * Get the current display mode
 * @param preview The filter preview widget
 * @return The current display mode
 */
FilterPreviewMode filter_preview_get_mode(FilterPreview *preview);

/**
 * Set the view mode (before or after)
 * @param preview The filter preview widget
 * @param view The view mode
 */
void filter_preview_set_view(FilterPreview *preview, FilterPreviewView view);

/**
 * Get the current view mode
 * @param preview The filter preview widget
 * @return The current view mode
 */
FilterPreviewView filter_preview_get_view(FilterPreview *preview);

#endif /* FILTER_PREVIEW_H */

