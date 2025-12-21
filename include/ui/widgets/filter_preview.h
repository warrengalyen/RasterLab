#ifndef FILTER_PREVIEW_H
#define FILTER_PREVIEW_H

#include <cairo.h>
#include <gtk/gtk.h>

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
    FILTER_PREVIEW_MODE_FIT, /* Fit image to container, keep aspect ratio */
    FILTER_PREVIEW_MODE_1TO1 /* Display at 1:1 scale, allow panning */
} FilterPreviewMode;

/**
 * View mode for the preview
 */
typedef enum {
    FILTER_PREVIEW_VIEW_BEFORE, /* Show before image */
    FILTER_PREVIEW_VIEW_AFTER   /* Show after image */
} FilterPreviewView;

typedef cairo_surface_t* (*FilterApplyFunc)(cairo_surface_t* input, gpointer params);

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
void filter_preview_set_before_surface(FilterPreview* preview, cairo_surface_t* surface);

/**
 * Set the after image (Cairo surface)
 * @param preview The filter preview widget
 * @param surface The Cairo surface for the after image (can be NULL)
 */
void filter_preview_set_after_surface(FilterPreview* preview, cairo_surface_t* surface);

/**
 * Set the display mode (fit or 1:1)
 * @param preview The filter preview widget
 * @param mode The display mode
 */
void filter_preview_set_mode(FilterPreview* preview, FilterPreviewMode mode);

/**
 * Get the current display mode
 * @param preview The filter preview widget
 * @return The current display mode
 */
FilterPreviewMode filter_preview_get_mode(FilterPreview* preview);

/**
 * Set the view mode (before or after)
 * @param preview The filter preview widget
 * @param view The view mode
 */
void filter_preview_set_view(FilterPreview* preview, FilterPreviewView view);

/**
 * Get the current view mode
 * @param preview The filter preview widget
 * @return The current view mode
 */
FilterPreviewView filter_preview_get_view(FilterPreview* preview);

/**
 * Set filter parameters and trigger update
 * @param preview The filter preview widget
 * @param params The filter parameters
 */
void filter_preview_set_filter_params(FilterPreview* preview, gpointer params);

/**
 * Force immediate viewport update
 * @param preview The filter preview widget
 */
void filter_preview_refresh(FilterPreview* preview);

/**
 * Set the filter function to apply
 * @param preview The filter preview widget
 * @param filter_func The filter function to apply
 * @param params The filter parameters
 */
void filter_preview_set_filter_function(FilterPreview* preview,
                                        FilterApplyFunc filter_func,
                                        gpointer params);

/**
 * Get allow_zoom_pan property
 * @param preview The filter preview widget
 * @return TRUE if zoom/pan (1:1 mode) is allowed, FALSE otherwise
 */
gboolean filter_preview_get_allow_zoom_pan(FilterPreview* preview);

/**
 * Set allow_zoom_pan property
 * When disabled, only FIT mode is available and only Before/After buttons are shown.
 * This is useful for CPU-intensive filters to avoid long update times.
 * @param preview The filter preview widget
 * @param allow TRUE to allow zoom/pan, FALSE to disable it
 */
void filter_preview_set_allow_zoom_pan(FilterPreview* preview, gboolean allow);

#endif /* FILTER_PREVIEW_H */
