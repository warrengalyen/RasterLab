#include "ui/widgets/filter_preview.h"
#include "render/render_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Private structure for FilterPreview widget
 */
struct _FilterPreview {
    GtkBox parent;

    /* Full resolution surfaces (original data) */
    cairo_surface_t* before_surface_full;
    cairo_surface_t* after_surface_full;

    /* Viewport cache - only the visible portion with filter applied */
    cairo_surface_t* viewport_cache;
    gint cache_x, cache_y; /* Position of cached viewport in image coords */
    gint cache_width, cache_height;

    gint original_width;
    gint original_height;

    /* Display state */
    FilterPreviewMode mode;
    FilterPreviewView view;

    /* Panning state (for 1:1 mode) */
    gdouble pan_x;
    gdouble pan_y;
    gboolean is_dragging;
    gdouble drag_start_x;
    gdouble drag_start_y;
    gdouble drag_start_pan_x;
    gdouble drag_start_pan_y;

    /* Filter state */
    gpointer filter_params;
    FilterApplyFunc filter_apply_func;

    /* Async update handling */
    guint update_timeout_id;
    gboolean needs_update;
    GThread* filter_thread;
    GMutex cache_mutex;
    guint update_sequence; /* Sequence number to track latest update */
    gboolean force_update; /* Force update even if viewport hasn't changed */

    /* Control buttons */
    GtkWidget* before_button;
    GtkWidget* after_button;
    GtkWidget* fit_button;
    GtkWidget* one_to_one_button;

    /* Container widgets */
    GtkWidget* main_box;
    GtkWidget* preview_area;
    GtkWidget* controls_box;

    /* Flag to prevent recursive signal handling */
    gboolean updating_buttons;
};

G_DEFINE_TYPE(FilterPreview, filter_preview, GTK_TYPE_BOX)

/* Forward declarations for button click handlers */
static void on_before_clicked(GtkWidget* widget, gpointer user_data);
static void on_after_clicked(GtkWidget* widget, gpointer user_data);
static void on_fit_clicked(GtkWidget* widget, gpointer user_data);
static void on_one_to_one_clicked(GtkWidget* widget, gpointer user_data);

/* Forward declarations for viewport update functions */
static void request_viewport_update(FilterPreview* preview);

/**
 * Calculate the viewport rectangle in image coordinates
 */
typedef struct {
    gint x, y;
    gint width, height;
} ViewportRect;

/**
 * Data for filter thread
 */
typedef struct {
    FilterPreview* preview;
    cairo_surface_t* input_viewport;
    ViewportRect viewport;
    gpointer filter_params;
    guint sequence; /* Sequence number for this update */
} FilterThreadData;

static ViewportRect calculate_viewport(FilterPreview* preview) {
    ViewportRect viewport = {0, 0, 0, 0};

    if (!preview || !preview->preview_area) {
        return viewport;
    }

    gint widget_width = gtk_widget_get_allocated_width(preview->preview_area);
    gint widget_height = gtk_widget_get_allocated_height(preview->preview_area);

    if (preview->mode == FILTER_PREVIEW_MODE_FIT) {
        /* In fit mode, entire image is visible */
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = preview->original_width;
        viewport.height = preview->original_height;
    } else {
        /* In 1:1 mode, calculate visible rectangle with some padding */
        gint padding = 100; /* Render extra pixels around the viewport */

        viewport.x = (gint)preview->pan_x - padding;
        viewport.y = (gint)preview->pan_y - padding;
        viewport.width = widget_width + (padding * 2);
        viewport.height = widget_height + (padding * 2);

        /* Clamp to image bounds */
        if (viewport.x < 0) {
            viewport.width += viewport.x;
            viewport.x = 0;
        }
        if (viewport.y < 0) {
            viewport.height += viewport.y;
            viewport.y = 0;
        }

        if (viewport.x + viewport.width > preview->original_width) {
            viewport.width = preview->original_width - viewport.x;
        }
        if (viewport.y + viewport.height > preview->original_height) {
            viewport.height = preview->original_height - viewport.y;
        }

        /* Ensure positive dimensions */
        if (viewport.width < 0)
            viewport.width = 0;
        if (viewport.height < 0)
            viewport.height = 0;
    }

    return viewport;
}

/**
 * Check if viewport has changed significantly
 */
static gboolean viewport_changed(FilterPreview* preview,
                                 ViewportRect new_viewport) {
    /* Force update if flag is set (e.g., filter parameters changed) */
    if (preview->force_update) {
        preview->force_update = FALSE;
        return TRUE;
    }

    if (!preview->viewport_cache) {
        return TRUE;
    }

    /* Check if new viewport is outside cached area */
    if (new_viewport.x < preview->cache_x || new_viewport.y < preview->cache_y ||
        new_viewport.x + new_viewport.width >
            preview->cache_x + preview->cache_width ||
        new_viewport.y + new_viewport.height >
            preview->cache_y + preview->cache_height) {
        return TRUE;
    }

    return FALSE;
}

static cairo_surface_t* extract_viewport(cairo_surface_t* source,
                                         ViewportRect viewport) {
    if (!source || viewport.width <= 0 || viewport.height <= 0) {
        return NULL;
    }

    /* Create new surface for viewport */
    cairo_surface_t* cropped = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, viewport.width, viewport.height);

    cairo_t* cr = cairo_create(cropped);

    /* Copy the viewport region */
    cairo_set_source_surface(cr, source, -viewport.x, -viewport.y);
    cairo_paint(cr);

    cairo_destroy(cr);

    return cropped;
}

/**
 * Apply filter in background thread
 */
static gpointer filter_thread_func(gpointer user_data) {
    FilterThreadData* data = (FilterThreadData*)user_data;
    FilterPreview* preview = data->preview;
    guint my_sequence = data->sequence;

    /* Apply filter to viewport (your actual filter function here) */
    cairo_surface_t* filtered = NULL;

    /* Apply filter using the callback if provided */
    if (preview && preview->filter_apply_func) {
        filtered = preview->filter_apply_func(
            data->input_viewport,
            data->filter_params);
    } else {
        /* No filter set, just copy the input */
        filtered = cairo_surface_reference(data->input_viewport);
    }

    /* Lock mutex and update cache - check if preview still exists and this is still the latest update */
    if (preview) {
        g_mutex_lock(&preview->cache_mutex);

        /* Only update if this is still the latest sequence (newer updates may have started) */
        if (my_sequence == preview->update_sequence) {
            if (preview->viewport_cache) {
                cairo_surface_destroy(preview->viewport_cache);
            }

            preview->viewport_cache = filtered;
            preview->cache_x = data->viewport.x;
            preview->cache_y = data->viewport.y;
            preview->cache_width = data->viewport.width;
            preview->cache_height = data->viewport.height;

            /* Trigger redraw on main thread - only if preview still exists */
            if (preview->preview_area) {
                g_idle_add((GSourceFunc)gtk_widget_queue_draw, preview->preview_area);
            }
        } else {
            /* This update is stale, discard the result */
            if (filtered) {
                cairo_surface_destroy(filtered);
            }
        }

        g_mutex_unlock(&preview->cache_mutex);
    } else {
        /* Preview was disposed, just free the filtered surface */
        if (filtered) {
            cairo_surface_destroy(filtered);
        }
    }

    cairo_surface_destroy(data->input_viewport);
    g_free(data);

    return NULL;
}

/**
 * Timeout callback for delayed update (after panning stops)
 */
static gboolean update_timeout_callback(gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    preview->update_timeout_id = 0;
    request_viewport_update(preview);

    return G_SOURCE_REMOVE;
}

/**
 * Schedule a viewport update with debouncing
 */
static void schedule_viewport_update(FilterPreview* preview, guint delay_ms) {
    if (!preview) {
        return;
    }

    /* Cancel existing timeout */
    if (preview->update_timeout_id > 0) {
        g_source_remove(preview->update_timeout_id);
    }

    /* Schedule new update */
    preview->update_timeout_id =
        g_timeout_add(delay_ms, update_timeout_callback, preview);
}

/**
 * Request viewport update (called after panning or mode change)
 */
static void request_viewport_update(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    /* Only update "after" view with filters */
    if (preview->view != FILTER_PREVIEW_VIEW_AFTER) {
        return;
    }

    ViewportRect viewport = calculate_viewport(preview);

    /* Check if update is needed */
    if (!viewport_changed(preview, viewport)) {
        return;
    }

    /* Extract viewport from full resolution image */
    cairo_surface_t* viewport_input =
        extract_viewport(preview->before_surface_full, viewport);

    if (!viewport_input) {
        return;
    }

    /* Increment sequence number for this update */
    preview->update_sequence++;

    /* Prepare thread data */
    FilterThreadData* data = g_malloc(sizeof(FilterThreadData));
    data->preview = preview;
    data->input_viewport = viewport_input;
    data->viewport = viewport;
    data->filter_params = preview->filter_params;
    data->sequence = preview->update_sequence;

    /* If there's an existing thread, we'll let it finish but ignore its result */
    /* Store the old thread reference if it exists */
    GThread* old_thread = preview->filter_thread;

    /* Launch filter in background thread */
    preview->filter_thread =
        g_thread_new("filter_thread", filter_thread_func, data);

    /* Unref the old thread if it exists (we don't need to wait for it) */
    if (old_thread) {
        g_thread_unref(old_thread);
    }
}

/**
 * Get current surface based on view mode
 */
static cairo_surface_t* get_current_surface(FilterPreview* preview) {
    if (!preview) {
        return NULL;
    }

    if (preview->view == FILTER_PREVIEW_VIEW_BEFORE) {
        return preview->before_surface_full;
    } else {
        return preview->after_surface_full;
    }
}

/**
 * Get image dimensions from surface
 */
static void get_surface_size(cairo_surface_t* surface, gint* width,
                             gint* height) {
    if (!surface || !width || !height) {
        *width = 0;
        *height = 0;
        return;
    }

    *width = cairo_image_surface_get_width(surface);
    *height = cairo_image_surface_get_height(surface);
}

/**
 * Calculate fit scale to fit image in container while maintaining aspect ratio
 */
static gdouble calculate_fit_scale(gint img_width, gint img_height,
                                   gint container_width,
                                   gint container_height) {
    if (img_width <= 0 || img_height <= 0 || container_width <= 0 ||
        container_height <= 0) {
        return 1.0;
    }

    gdouble scale_x = (gdouble)container_width / (gdouble)img_width;
    gdouble scale_y = (gdouble)container_height / (gdouble)img_height;

    return (scale_x < scale_y) ? scale_x : scale_y;
}

/**
 * Clamp pan position to keep image within bounds
 */
static void clamp_pan(FilterPreview* preview, gint container_width, gint container_height) {
    if (!preview) {
        return;
    }

    /* In 1:1 mode, use original dimensions for clamping */
    gint virtual_width = preview->original_width;
    gint virtual_height = preview->original_height;

    /* Clamp pan so image doesn't go outside container */
    gint max_pan_x = (virtual_width > container_width) ? (virtual_width - container_width) : 0;
    gint max_pan_y = (virtual_height > container_height) ? (virtual_height - container_height) : 0;

    if (preview->pan_x < 0) {
        preview->pan_x = 0;
    } else if (preview->pan_x > max_pan_x) {
        preview->pan_x = max_pan_x;
    }

    if (preview->pan_y < 0) {
        preview->pan_y = 0;
    } else if (preview->pan_y > max_pan_y) {
        preview->pan_y = max_pan_y;
    }
}

/**
 * Draw callback for preview area
 */
static gboolean on_preview_draw(GtkWidget* widget, cairo_t* cr,
                                gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);
    cairo_surface_t* surface;
    gint widget_width, widget_height;
    gdouble scale;
    gdouble draw_x, draw_y;

    if (!preview) {
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    /* Draw checkered background */
    draw_checkered_background(cr, widget_width, widget_height);

    /* Determine which surface to use */
    if (preview->view == FILTER_PREVIEW_VIEW_BEFORE) {
        surface = preview->before_surface_full;
    } else {
        /* Use cached viewport for after view if filter function is set */
        if (preview->filter_apply_func) {
            /* Async filter mode - use viewport cache */
            g_mutex_lock(&preview->cache_mutex);
            surface = preview->viewport_cache;

            if (!surface) {
                /* No cache yet, show before image with "Processing..." overlay */
                g_mutex_unlock(&preview->cache_mutex);

                /* Draw before image first */
                if (preview->before_surface_full) {
                    gint img_width = preview->original_width;
                    gint img_height = preview->original_height;
                    gdouble scale = calculate_fit_scale(img_width, img_height, widget_width, widget_height);
                    gdouble scaled_width = img_width * scale;
                    gdouble scaled_height = img_height * scale;
                    gdouble draw_x = (widget_width - scaled_width) / 2.0;
                    gdouble draw_y = (widget_height - scaled_height) / 2.0;

                    cairo_save(cr);
                    cairo_translate(cr, draw_x, draw_y);
                    cairo_scale(cr, scale, scale);
                    cairo_set_source_surface(cr, preview->before_surface_full, 0, 0);
                    cairo_paint(cr);
                    cairo_restore(cr);
                }

                /* Draw loading indicator overlay */
                cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
                cairo_paint(cr);

                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, 20);
                cairo_move_to(cr, widget_width / 2 - 50, widget_height / 2);
                cairo_show_text(cr, "Processing...");

                return FALSE;
            }
        } else {
            /* Direct surface mode - use after_surface_full directly */
            surface = preview->after_surface_full;
            if (!surface) {
                /* Fall back to before surface if after is not set */
                surface = preview->before_surface_full;
            }
        }
    }

    if (preview->mode == FILTER_PREVIEW_MODE_FIT) {
        /* Fit mode: scale to fit container */
        gint img_width, img_height;
        if (preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->filter_apply_func) {
            img_width = preview->cache_width;
            img_height = preview->cache_height;
        } else if (preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->after_surface_full) {
            get_surface_size(preview->after_surface_full, &img_width, &img_height);
        } else {
            img_width = preview->original_width;
            img_height = preview->original_height;
        }

        scale =
            calculate_fit_scale(img_width, img_height, widget_width, widget_height);

        gdouble scaled_width = img_width * scale;
        gdouble scaled_height = img_height * scale;

        draw_x = (widget_width - scaled_width) / 2.0;
        draw_y = (widget_height - scaled_height) / 2.0;

        cairo_save(cr);
        cairo_translate(cr, draw_x, draw_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);

        if (preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->filter_apply_func) {
            g_mutex_unlock(&preview->cache_mutex);
        }
    } else {
        /* 1:1 mode: display at actual size */
        if (preview->view == FILTER_PREVIEW_VIEW_AFTER) {
            if (preview->filter_apply_func) {
                /* Draw cached viewport at its correct position */
                draw_x = preview->cache_x - preview->pan_x;
                draw_y = preview->cache_y - preview->pan_y;

                cairo_set_source_surface(cr, surface, draw_x, draw_y);
                cairo_paint(cr);

                g_mutex_unlock(&preview->cache_mutex);
            } else {
                /* Draw full after image */
                draw_x = -preview->pan_x;
                draw_y = -preview->pan_y;

                cairo_set_source_surface(cr, surface, draw_x, draw_y);
                cairo_paint(cr);
            }
        } else {
            /* Draw full before image */
            draw_x = -preview->pan_x;
            draw_y = -preview->pan_y;

            cairo_set_source_surface(cr, surface, draw_x, draw_y);
            cairo_paint(cr);
        }
    }

    return FALSE;
}

/**
 * Button press handler for panning
 */
static gboolean on_preview_button_press(GtkWidget* widget,
                                        GdkEventButton* event,
                                        gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || event->button != 1) {
        return FALSE;
    }

    /* Only allow panning in 1:1 mode */
    if (preview->mode != FILTER_PREVIEW_MODE_1TO1) {
        return FALSE;
    }

    preview->is_dragging = TRUE;
    preview->drag_start_x = event->x;
    preview->drag_start_y = event->y;
    preview->drag_start_pan_x = preview->pan_x;
    preview->drag_start_pan_y = preview->pan_y;

    /* Change cursor to move */
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }

    return TRUE;
}

/**
 * Button release handler
 */
static gboolean on_preview_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || event->button != 1) {
        return FALSE;
    }

    if (preview->is_dragging) {
        preview->is_dragging = FALSE;

        /* Trigger immediate viewport update after drag ends */
        schedule_viewport_update(preview, 50); /* Short delay */

        /* Restore hand cursor if in 1:1 mode */
        if (preview->mode == FILTER_PREVIEW_MODE_1TO1) {
            GdkWindow* window = gtk_widget_get_window(widget);
            if (window) {
                GdkDisplay* display = gdk_window_get_display(window);
                GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
        }
    }

    return FALSE;
}

/**
 * Motion notify handler for panning
 */
static gboolean on_preview_motion_notify(GtkWidget* widget,
                                         GdkEventMotion* event,
                                         gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);
    gint widget_width, widget_height;

    if (!preview) {
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    if (preview->is_dragging && preview->mode == FILTER_PREVIEW_MODE_1TO1) {
        /* Update pan position based on drag */
        gdouble delta_x = preview->drag_start_x - event->x;
        gdouble delta_y = preview->drag_start_y - event->y;

        preview->pan_x = preview->drag_start_pan_x + delta_x;
        preview->pan_y = preview->drag_start_pan_y + delta_y;

        /* Clamp pan to keep image in bounds */
        clamp_pan(preview, widget_width, widget_height);

        /* Queue redraw immediately (shows cached content during drag) */
        gtk_widget_queue_draw(widget);

        /* Schedule viewport update after panning stops */
        schedule_viewport_update(preview, 150); /* 150ms delay */
    } else if (preview->mode == FILTER_PREVIEW_MODE_1TO1) {
        /* Show hand cursor when hovering in 1:1 mode */
        GdkWindow* window = gtk_widget_get_window(widget);
        if (window) {
            GdkDisplay* display = gdk_window_get_display(window);
            GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }

    return FALSE;
}

/**
 * Leave notify handler - restore default cursor
 */
static gboolean on_preview_leave_notify(GtkWidget* widget,
                                        GdkEventCrossing* event,
                                        gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    (void)event;

    if (!preview || preview->is_dragging) {
        return FALSE;
    }

    /* Restore default cursor */
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        gdk_window_set_cursor(window, NULL);
    }

    return FALSE;
}

/**
 * Before button clicked
 */
static void on_before_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only before is active in view group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->after_button) {
            g_signal_handlers_block_by_func(preview->after_button,
                                            (gpointer)on_after_clicked, preview);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->after_button),
                                         FALSE);
            g_signal_handlers_unblock_by_func(preview->after_button,
                                              (gpointer)on_after_clicked, preview);
        }
        filter_preview_set_view(preview, FILTER_PREVIEW_VIEW_BEFORE);
    } else {
        /* If before is being deactivated, activate after instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * After button clicked
 */
static void on_after_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only after is active in view group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->before_button) {
            g_signal_handlers_block_by_func(preview->before_button,
                                            (gpointer)on_before_clicked, preview);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->before_button),
                                         FALSE);
            g_signal_handlers_unblock_by_func(preview->before_button,
                                              (gpointer)on_before_clicked, preview);
        }
        filter_preview_set_view(preview, FILTER_PREVIEW_VIEW_AFTER);
    } else {
        /* If after is being deactivated, activate before instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * Fit button clicked
 */
static void on_fit_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only fit is active in mode group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->one_to_one_button) {
            g_signal_handlers_block_by_func(preview->one_to_one_button,
                                            (gpointer)on_one_to_one_clicked, preview);
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(preview->one_to_one_button), FALSE);
            g_signal_handlers_unblock_by_func(
                preview->one_to_one_button, (gpointer)on_one_to_one_clicked, preview);
        }
        filter_preview_set_mode(preview, FILTER_PREVIEW_MODE_FIT);
    } else {
        /* If fit is being deactivated, activate 1:1 instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * 1:1 button clicked
 */
static void on_one_to_one_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only 1:1 is active in mode group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->fit_button) {
            g_signal_handlers_block_by_func(preview->fit_button,
                                            (gpointer)on_fit_clicked, preview);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->fit_button),
                                         FALSE);
            g_signal_handlers_unblock_by_func(preview->fit_button,
                                              (gpointer)on_fit_clicked, preview);
        }
        filter_preview_set_mode(preview, FILTER_PREVIEW_MODE_1TO1);
    } else {
        /* If 1:1 is being deactivated, activate fit instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * Update button states
 */
static void update_button_states(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    /* Update before/after radio buttons */
    if (preview->before_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->before_button),
                                     preview->view == FILTER_PREVIEW_VIEW_BEFORE);
    }

    if (preview->after_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->after_button),
                                     preview->view == FILTER_PREVIEW_VIEW_AFTER);
    }

    /* Update fit/1:1 radio buttons */
    if (preview->fit_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->fit_button),
                                     preview->mode == FILTER_PREVIEW_MODE_FIT);
    }

    if (preview->one_to_one_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->one_to_one_button),
                                     preview->mode == FILTER_PREVIEW_MODE_1TO1);
    }
}

/**
 * Initialize FilterPreview instance
 */
static void filter_preview_init(FilterPreview* preview) {
    GtkWidget* button_box;

    /* Initialize parent class first */
    /* Note: G_DEFINE_TYPE automatically chains to parent init */

    preview->before_surface_full = NULL;
    preview->after_surface_full = NULL;
    preview->viewport_cache = NULL;
    preview->cache_x = 0;
    preview->cache_y = 0;
    preview->cache_width = 0;
    preview->cache_height = 0;
    preview->original_width = 0;
    preview->original_height = 0;

    preview->filter_params = NULL;
    preview->filter_apply_func = NULL;
    preview->update_timeout_id = 0;
    preview->needs_update = FALSE;
    preview->filter_thread = NULL;
    preview->update_sequence = 0;
    preview->force_update = FALSE;

    g_mutex_init(&preview->cache_mutex);

    preview->mode = FILTER_PREVIEW_MODE_FIT;
    preview->view = FILTER_PREVIEW_VIEW_AFTER;
    preview->pan_x = 0.0;
    preview->pan_y = 0.0;
    preview->is_dragging = FALSE;
    preview->updating_buttons = FALSE;

    /* Initialize as a vertical box - set orientation during construction */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(preview),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(preview), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(preview), FALSE);

    /* Store reference to self as main_box for compatibility */
    preview->main_box = GTK_WIDGET(preview);

    /* Create preview drawing area directly (no scrolled window for manual panning
     * control) */
    preview->preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(preview->preview_area, 375, 338);
    gtk_widget_set_hexpand(preview->preview_area, FALSE);
    gtk_widget_set_vexpand(preview->preview_area, FALSE);
    gtk_box_pack_start(GTK_BOX(preview), preview->preview_area, FALSE, FALSE, 0);

    /* Enable mouse events */
    gtk_widget_set_events(preview->preview_area,
                          gtk_widget_get_events(preview->preview_area) |
                              GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);

    /* Connect signals */
    g_signal_connect(preview->preview_area, "draw", G_CALLBACK(on_preview_draw),
                     preview);
    g_signal_connect(preview->preview_area, "button-press-event",
                     G_CALLBACK(on_preview_button_press), preview);
    g_signal_connect(preview->preview_area, "button-release-event",
                     G_CALLBACK(on_preview_button_release), preview);
    g_signal_connect(preview->preview_area, "motion-notify-event",
                     G_CALLBACK(on_preview_motion_notify), preview);
    g_signal_connect(preview->preview_area, "leave-notify-event",
                     G_CALLBACK(on_preview_leave_notify), preview);

    /* Create controls box */
    preview->controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(preview->controls_box, 5);
    gtk_widget_set_margin_bottom(preview->controls_box, 5);
    gtk_widget_set_margin_start(preview->controls_box, 5);
    gtk_widget_set_margin_end(preview->controls_box, 5);
    gtk_box_pack_start(GTK_BOX(preview), preview->controls_box, FALSE, FALSE, 0);

    /* Create button box */
    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(preview->controls_box), button_box, FALSE, FALSE,
                       0);

    /* Create before button (toggle button in view group) */
    preview->before_button = gtk_toggle_button_new_with_label("before");
    gtk_widget_set_margin_start(preview->before_button, 5);
    gtk_widget_set_margin_end(preview->before_button, 5);
    g_signal_connect(preview->before_button, "clicked",
                     G_CALLBACK(on_before_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->before_button, FALSE, FALSE,
                       0);

    /* Create after button (toggle button in view group) */
    preview->after_button = gtk_toggle_button_new_with_label("after");
    gtk_widget_set_margin_start(preview->after_button, 5);
    gtk_widget_set_margin_end(preview->after_button, 5);
    g_signal_connect(preview->after_button, "clicked",
                     G_CALLBACK(on_after_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->after_button, FALSE, FALSE,
                       0);

    /* Create 1:1 button (toggle button in mode group) */
    preview->one_to_one_button = gtk_toggle_button_new_with_label("1:1");
    gtk_widget_set_margin_start(preview->one_to_one_button, 5);
    gtk_widget_set_margin_end(preview->one_to_one_button, 5);
    g_signal_connect(preview->one_to_one_button, "clicked",
                     G_CALLBACK(on_one_to_one_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->one_to_one_button, FALSE,
                       FALSE, 0);

    /* Create fit button (toggle button in mode group) */
    preview->fit_button = gtk_toggle_button_new_with_label("fit");
    gtk_widget_set_margin_start(preview->fit_button, 5);
    gtk_widget_set_margin_end(preview->fit_button, 5);
    g_signal_connect(preview->fit_button, "clicked", G_CALLBACK(on_fit_clicked),
                     preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->fit_button, FALSE, FALSE, 0);

    /* Update initial button states */
    update_button_states(preview);

    /* Show all widgets */
    gtk_widget_show_all(GTK_WIDGET(preview));
}

/**
 * Dispose handler
 */
static void filter_preview_dispose(GObject* object) {
    FilterPreview* preview = FILTER_PREVIEW(object);

    /* Cancel pending updates */
    if (preview->update_timeout_id > 0) {
        g_source_remove(preview->update_timeout_id);
        preview->update_timeout_id = 0;
    }

    /* Wait for filter thread to complete */
    if (preview->filter_thread) {
        /* Mark that we're disposing so thread knows not to access preview */
        /* Join the thread - this will wait for it to complete */
        GThread* thread = preview->filter_thread;
        preview->filter_thread = NULL; /* Clear reference before joining to avoid race */
        g_thread_join(thread);
    }

    /* Clean up surfaces */
    if (preview->before_surface_full) {
        cairo_surface_destroy(preview->before_surface_full);
        preview->before_surface_full = NULL;
    }

    if (preview->after_surface_full) {
        cairo_surface_destroy(preview->after_surface_full);
        preview->after_surface_full = NULL;
    }

    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    g_mutex_clear(&preview->cache_mutex);

    G_OBJECT_CLASS(filter_preview_parent_class)->dispose(object);
}

/**
 * Finalize handler
 */
static void filter_preview_finalize(GObject* object) {
    (void)object;
    G_OBJECT_CLASS(filter_preview_parent_class)->finalize(object);
}

/**
 * Class initialization
 */
static void filter_preview_class_init(FilterPreviewClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = filter_preview_dispose;
    object_class->finalize = filter_preview_finalize;
}

/**
 * Create a new filter preview widget
 */
GtkWidget* filter_preview_new(void) {
    return GTK_WIDGET(g_object_new(FILTER_PREVIEW_TYPE, "orientation",
                                   GTK_ORIENTATION_VERTICAL, "spacing", 0, NULL));
}

/**
 * Set the before image surface
 */
void filter_preview_set_before_surface(FilterPreview* preview, cairo_surface_t* surface) {
    if (!preview) {
        return;
    }

    if (preview->before_surface_full) {
        cairo_surface_destroy(preview->before_surface_full);
    }

    if (surface) {
        preview->before_surface_full = cairo_surface_reference(surface);
        preview->original_width = cairo_image_surface_get_width(surface);
        preview->original_height = cairo_image_surface_get_height(surface);
    } else {
        preview->before_surface_full = NULL;
    }

    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Set the after image surface
 */
void filter_preview_set_after_surface(FilterPreview* preview,
                                      cairo_surface_t* surface) {
    if (!preview) {
        return;
    }

    if (preview->after_surface_full) {
        cairo_surface_destroy(preview->after_surface_full);
    }

    if (surface) {
        preview->after_surface_full = cairo_surface_reference(surface);
    } else {
        preview->after_surface_full = NULL;
    }

    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Set the display mode
 */
void filter_preview_set_mode(FilterPreview* preview, FilterPreviewMode mode) {
    if (!preview) {
        return;
    }

    preview->mode = mode;

    /* Reset pan position when switching modes */
    if (mode == FILTER_PREVIEW_MODE_FIT) {
        preview->pan_x = 0.0;
        preview->pan_y = 0.0;
    }

    update_button_states(preview);
    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Get the display mode
 */
FilterPreviewMode filter_preview_get_mode(FilterPreview* preview) {
    if (!preview) {
        return FILTER_PREVIEW_MODE_FIT;
    }

    return preview->mode;
}

/**
 * Set the view mode
 */
void filter_preview_set_view(FilterPreview* preview, FilterPreviewView view) {
    if (!preview) {
        return;
    }

    preview->view = view;
    update_button_states(preview);
    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Get the view mode
 */
FilterPreviewView filter_preview_get_view(FilterPreview* preview) {
    if (!preview) {
        return FILTER_PREVIEW_VIEW_AFTER;
    }

    return preview->view;
}

/**
 * Set filter parameters and trigger update
 */
void filter_preview_set_filter_params(FilterPreview* preview, gpointer params) {
    if (!preview) {
        return;
    }

    preview->filter_params = params;

    /* Invalidate cache */
    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    /* Force update even if viewport rectangle hasn't changed */
    preview->force_update = TRUE;

    /* Request immediate update */
    request_viewport_update(preview);
}

/**
 * Force immediate viewport update
 */
void filter_preview_refresh(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    /* Invalidate cache */
    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    request_viewport_update(preview);
}

/**
 * Set the filter function to apply
 */
void filter_preview_set_filter_function(FilterPreview* preview,
                                        FilterApplyFunc filter_func,
                                        gpointer params) {
    if (!preview) {
        return;
    }

    preview->filter_apply_func = filter_func;
    preview->filter_params = params;

    /* Invalidate cache */
    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    /* Force update even if viewport rectangle hasn't changed */
    preview->force_update = TRUE;

    /* Request immediate update */
    request_viewport_update(preview);
}