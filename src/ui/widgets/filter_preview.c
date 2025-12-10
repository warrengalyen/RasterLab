#include "ui/widgets/filter_preview.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * Private structure for FilterPreview widget
 */
struct _FilterPreview {
    GtkBox parent;

    /* Image surfaces */
    cairo_surface_t *before_surface;
    cairo_surface_t *after_surface;

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

    /* Control buttons */
    GtkWidget *before_button;
    GtkWidget *after_button;
    GtkWidget *fit_button;
    GtkWidget *one_to_one_button;

    /* Container widgets */
    GtkWidget *main_box;
    GtkWidget *preview_area;
    GtkWidget *controls_box;

    /* Flag to prevent recursive signal handling */
    gboolean updating_buttons;
};

G_DEFINE_TYPE(FilterPreview, filter_preview, GTK_TYPE_BOX)

/* Forward declarations for button click handlers */
static void on_before_clicked(GtkWidget *widget, gpointer user_data);
static void on_after_clicked(GtkWidget *widget, gpointer user_data);
static void on_fit_clicked(GtkWidget *widget, gpointer user_data);
static void on_one_to_one_clicked(GtkWidget *widget, gpointer user_data);

/**
 * Get current surface based on view mode
 */
static cairo_surface_t* get_current_surface(FilterPreview *preview)
{
    if (!preview) {
        return NULL;
    }

    if (preview->view == FILTER_PREVIEW_VIEW_BEFORE) {
        return preview->before_surface;
    } else {
        return preview->after_surface;
    }
}

/**
 * Get image dimensions from surface
 */
static void get_surface_size(cairo_surface_t *surface, gint *width, gint *height)
{
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
                                   gint container_width, gint container_height)
{
    if (img_width <= 0 || img_height <= 0 || container_width <= 0 || container_height <= 0) {
        return 1.0;
    }

    gdouble scale_x = (gdouble)container_width / (gdouble)img_width;
    gdouble scale_y = (gdouble)container_height / (gdouble)img_height;
    
    return (scale_x < scale_y) ? scale_x : scale_y;
}

/**
 * Clamp pan position to keep image within bounds
 */
static void clamp_pan(FilterPreview *preview, gint img_width, gint img_height,
                      gint container_width, gint container_height)
{
    if (!preview) {
        return;
    }

    /* In 1:1 mode, clamp pan so image doesn't go outside container */
    gint max_pan_x = (img_width > container_width) ? (img_width - container_width) : 0;
    gint max_pan_y = (img_height > container_height) ? (img_height - container_height) : 0;

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
static gboolean on_preview_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);
    cairo_surface_t *surface;
    gint img_width, img_height;
    gint widget_width, widget_height;
    gdouble scale;
    gdouble draw_x, draw_y;

    if (!preview) {
        return FALSE;
    }

    /* Get widget size */
    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    /* Get current surface */
    surface = get_current_surface(preview);
    if (!surface) {
        /* Draw checkered background if no image */
        /* Simple checkered pattern */
        gint checker_size = 20;
        gboolean dark = FALSE;
        
        for (gint y = 0; y < widget_height; y += checker_size) {
            for (gint x = 0; x < widget_width; x += checker_size) {
                if (dark) {
                    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
                } else {
                    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                }
                cairo_rectangle(cr, x, y, checker_size, checker_size);
                cairo_fill(cr);
                dark = !dark;
            }
            dark = !dark;
        }
        return FALSE;
    }

    get_surface_size(surface, &img_width, &img_height);

    if (preview->mode == FILTER_PREVIEW_MODE_FIT) {
        /* Fit mode: scale to fit container, center image */
        scale = calculate_fit_scale(img_width, img_height, widget_width, widget_height);
        
        gdouble scaled_width = img_width * scale;
        gdouble scaled_height = img_height * scale;
        
        draw_x = (widget_width - scaled_width) / 2.0;
        draw_y = (widget_height - scaled_height) / 2.0;

        /* Draw checkered background */
        gint checker_size = 20;
        gboolean dark = FALSE;
        for (gint y = 0; y < widget_height; y += checker_size) {
            for (gint x = 0; x < widget_width; x += checker_size) {
                if (dark) {
                    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
                } else {
                    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                }
                cairo_rectangle(cr, x, y, checker_size, checker_size);
                cairo_fill(cr);
                dark = !dark;
            }
            dark = !dark;
        }

        /* Draw image */
        cairo_save(cr);
        cairo_translate(cr, draw_x, draw_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        /* 1:1 mode: display at actual size, allow panning */
        draw_x = -preview->pan_x;
        draw_y = -preview->pan_y;

        /* Draw checkered background */
        gint checker_size = 20;
        gboolean dark = FALSE;
        for (gint y = 0; y < widget_height; y += checker_size) {
            for (gint x = 0; x < widget_width; x += checker_size) {
                if (dark) {
                    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
                } else {
                    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                }
                cairo_rectangle(cr, x, y, checker_size, checker_size);
                cairo_fill(cr);
                dark = !dark;
            }
            dark = !dark;
        }

        /* Draw image at 1:1 scale */
        cairo_set_source_surface(cr, surface, draw_x, draw_y);
        cairo_paint(cr);
    }

    return FALSE;
}

/**
 * Button press handler for panning
 */
static gboolean on_preview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

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
    GdkWindow *window = gtk_widget_get_window(widget);
    if (window) {
        GdkDisplay *display = gdk_window_get_display(window);
        GdkCursor *cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }

    return TRUE;
}

/**
 * Button release handler
 */
static gboolean on_preview_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

    if (!preview || event->button != 1) {
        return FALSE;
    }

    if (preview->is_dragging) {
        preview->is_dragging = FALSE;

        /* Restore hand cursor if in 1:1 mode */
        if (preview->mode == FILTER_PREVIEW_MODE_1TO1) {
            GdkWindow *window = gtk_widget_get_window(widget);
            if (window) {
                GdkDisplay *display = gdk_window_get_display(window);
                GdkCursor *cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
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
static gboolean on_preview_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);
    cairo_surface_t *surface;
    gint img_width, img_height;
    gint widget_width, widget_height;

    if (!preview) {
        return FALSE;
    }

    surface = get_current_surface(preview);
    if (!surface) {
        return FALSE;
    }

    get_surface_size(surface, &img_width, &img_height);
    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    if (preview->is_dragging && preview->mode == FILTER_PREVIEW_MODE_1TO1) {
        /* Update pan position based on drag */
        gdouble delta_x = preview->drag_start_x - event->x;
        gdouble delta_y = preview->drag_start_y - event->y;

        preview->pan_x = preview->drag_start_pan_x + delta_x;
        preview->pan_y = preview->drag_start_pan_y + delta_y;

        /* Clamp pan to keep image in bounds */
        clamp_pan(preview, img_width, img_height, widget_width, widget_height);

        /* Queue redraw */
        gtk_widget_queue_draw(widget);
    } else if (preview->mode == FILTER_PREVIEW_MODE_1TO1) {
        /* Show hand cursor when hovering in 1:1 mode */
        GdkWindow *window = gtk_widget_get_window(widget);
        if (window) {
            GdkDisplay *display = gdk_window_get_display(window);
            GdkCursor *cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }

    return FALSE;
}

/**
 * Leave notify handler - restore default cursor
 */
static gboolean on_preview_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

    (void)event;

    if (!preview || preview->is_dragging) {
        return FALSE;
    }

    /* Restore default cursor */
    GdkWindow *window = gtk_widget_get_window(widget);
    if (window) {
        gdk_window_set_cursor(window, NULL);
    }

    return FALSE;
}

/**
 * Before button clicked
 */
static void on_before_clicked(GtkWidget *widget, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

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
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->after_button), FALSE);
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
static void on_after_clicked(GtkWidget *widget, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

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
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->before_button), FALSE);
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
static void on_fit_clicked(GtkWidget *widget, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

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
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->one_to_one_button), FALSE);
            g_signal_handlers_unblock_by_func(preview->one_to_one_button,
                                             (gpointer)on_one_to_one_clicked, preview);
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
static void on_one_to_one_clicked(GtkWidget *widget, gpointer user_data)
{
    FilterPreview *preview = FILTER_PREVIEW(user_data);

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
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->fit_button), FALSE);
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
static void update_button_states(FilterPreview *preview)
{
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
static void filter_preview_init(FilterPreview *preview)
{
    GtkWidget *button_box;

    /* Initialize parent class first */
    /* Note: G_DEFINE_TYPE automatically chains to parent init */

    preview->before_surface = NULL;
    preview->after_surface = NULL;
    preview->mode = FILTER_PREVIEW_MODE_FIT;
    preview->view = FILTER_PREVIEW_VIEW_AFTER;
    preview->pan_x = 0.0;
    preview->pan_y = 0.0;
    preview->is_dragging = FALSE;
    preview->updating_buttons = FALSE;

    /* Initialize as a vertical box - set orientation during construction */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(preview), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(preview), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(preview), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(preview), TRUE);

    /* Store reference to self as main_box for compatibility */
    preview->main_box = GTK_WIDGET(preview);

    /* Create preview drawing area directly (no scrolled window for manual panning control) */
    preview->preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(preview->preview_area, 400, 300);
    gtk_widget_set_hexpand(preview->preview_area, TRUE);
    gtk_widget_set_vexpand(preview->preview_area, TRUE);
    gtk_box_pack_start(GTK_BOX(preview), preview->preview_area, TRUE, TRUE, 0);

    /* Enable mouse events */
    gtk_widget_set_events(preview->preview_area,
                         gtk_widget_get_events(preview->preview_area) |
                         GDK_BUTTON_PRESS_MASK |
                         GDK_BUTTON_RELEASE_MASK |
                         GDK_POINTER_MOTION_MASK |
                         GDK_LEAVE_NOTIFY_MASK);

    /* Connect signals */
    g_signal_connect(preview->preview_area, "draw",
                    G_CALLBACK(on_preview_draw), preview);
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
    gtk_box_pack_start(GTK_BOX(preview->controls_box), button_box, FALSE, FALSE, 0);

    /* Create before button (toggle button in view group) */
    preview->before_button = gtk_toggle_button_new_with_label("before");
    gtk_widget_set_margin_start(preview->before_button, 5);
    gtk_widget_set_margin_end(preview->before_button, 5);
    g_signal_connect(preview->before_button, "clicked",
                    G_CALLBACK(on_before_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->before_button, FALSE, FALSE, 0);

    /* Create after button (toggle button in view group) */
    preview->after_button = gtk_toggle_button_new_with_label("after");
    gtk_widget_set_margin_start(preview->after_button, 5);
    gtk_widget_set_margin_end(preview->after_button, 5);
    g_signal_connect(preview->after_button, "clicked",
                    G_CALLBACK(on_after_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->after_button, FALSE, FALSE, 0);

    /* Create 1:1 button (toggle button in mode group) */
    preview->one_to_one_button = gtk_toggle_button_new_with_label("1:1");
    gtk_widget_set_margin_start(preview->one_to_one_button, 5);
    gtk_widget_set_margin_end(preview->one_to_one_button, 5);
    g_signal_connect(preview->one_to_one_button, "clicked",
                    G_CALLBACK(on_one_to_one_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->one_to_one_button, FALSE, FALSE, 0);

    /* Create fit button (toggle button in mode group) */
    preview->fit_button = gtk_toggle_button_new_with_label("fit");
    gtk_widget_set_margin_start(preview->fit_button, 5);
    gtk_widget_set_margin_end(preview->fit_button, 5);
    g_signal_connect(preview->fit_button, "clicked",
                    G_CALLBACK(on_fit_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->fit_button, FALSE, FALSE, 0);

    /* Update initial button states */
    update_button_states(preview);

    /* Show all widgets */
    gtk_widget_show_all(GTK_WIDGET(preview));
}

/**
 * Dispose handler
 */
static void filter_preview_dispose(GObject *object)
{
    FilterPreview *preview = FILTER_PREVIEW(object);

    if (preview->before_surface) {
        cairo_surface_destroy(preview->before_surface);
        preview->before_surface = NULL;
    }

    if (preview->after_surface) {
        cairo_surface_destroy(preview->after_surface);
        preview->after_surface = NULL;
    }

    G_OBJECT_CLASS(filter_preview_parent_class)->dispose(object);
}

/**
 * Finalize handler
 */
static void filter_preview_finalize(GObject *object)
{
    (void)object;
    G_OBJECT_CLASS(filter_preview_parent_class)->finalize(object);
}

/**
 * Class initialization
 */
static void filter_preview_class_init(FilterPreviewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = filter_preview_dispose;
    object_class->finalize = filter_preview_finalize;
}

/**
 * Create a new filter preview widget
 */
GtkWidget* filter_preview_new(void)
{
    return GTK_WIDGET(g_object_new(FILTER_PREVIEW_TYPE,
                                   "orientation", GTK_ORIENTATION_VERTICAL,
                                   "spacing", 0,
                                   NULL));
}

/**
 * Set the before image surface
 */
void filter_preview_set_before_surface(FilterPreview *preview, cairo_surface_t *surface)
{
    if (!preview) {
        return;
    }

    if (preview->before_surface) {
        cairo_surface_destroy(preview->before_surface);
    }

    if (surface) {
        preview->before_surface = cairo_surface_reference(surface);
    } else {
        preview->before_surface = NULL;
    }

    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Set the after image surface
 */
void filter_preview_set_after_surface(FilterPreview *preview, cairo_surface_t *surface)
{
    if (!preview) {
        return;
    }

    if (preview->after_surface) {
        cairo_surface_destroy(preview->after_surface);
    }

    if (surface) {
        preview->after_surface = cairo_surface_reference(surface);
    } else {
        preview->after_surface = NULL;
    }

    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Set the display mode
 */
void filter_preview_set_mode(FilterPreview *preview, FilterPreviewMode mode)
{
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
FilterPreviewMode filter_preview_get_mode(FilterPreview *preview)
{
    if (!preview) {
        return FILTER_PREVIEW_MODE_FIT;
    }

    return preview->mode;
}

/**
 * Set the view mode
 */
void filter_preview_set_view(FilterPreview *preview, FilterPreviewView view)
{
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
FilterPreviewView filter_preview_get_view(FilterPreview *preview)
{
    if (!preview) {
        return FILTER_PREVIEW_VIEW_AFTER;
    }

    return preview->view;
}

