#include "ui/widgets/rgb_scale.h"
#include <gtk/gtk.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Define the struct
struct _RgbScale {
    GtkScale parent_instance;
    RgbScaleType type;
    double red;        // Current red (0-1), used when type is GREEN or BLUE
    double green;      // Current green (0-1), used when type is RED or BLUE
    double blue;       // Current blue (0-1), used when type is RED or GREEN
    gboolean dragging; // Whether the user is dragging the thumb
    gboolean hovering; // Whether the mouse is hovering over the thumb
};

// Define the class structure (final type, so just parent class)
struct _RgbScaleClass {
    GtkScaleClass parent_class;
};

// GType implementation
G_DEFINE_TYPE(RgbScale, rgb_scale, GTK_TYPE_SCALE)

static gboolean rgb_scale_draw(GtkWidget* widget, cairo_t* cr) {
    RgbScale* scale = RGB_SCALE(widget);
    (void)gtk_widget_get_style_context(widget);

    // Get scale dimensions
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    // Calculate trough (track) rectangle
    double thumb_size = 16.0;
    double trough_height = 10.0;
    double trough_y = (height - trough_height) / 2.0;
    double trough_width = width - thumb_size - 4.0;
    double trough_x = thumb_size / 2.0 + 2.0;

    // Get value range
    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(widget));
    double min_val = gtk_adjustment_get_lower(adjustment);
    double max_val = gtk_adjustment_get_upper(adjustment);
    double current_val = gtk_adjustment_get_value(adjustment);

    // Draw gradient background
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    // Create horizontal gradient
    cairo_pattern_t* pattern = cairo_pattern_create_linear(trough_x, 0, trough_x + trough_width, 0);

    // Generate gradient stops based on type
    int num_stops = 256;
    for (int i = 0; i <= num_stops; i++) {
        double t = (double)i / num_stops;
        double r, g, b;

        if (scale->type == RGB_SCALE_RED) {
            r = min_val + t * (max_val - min_val);
            g = scale->green;
            b = scale->blue;
        } else if (scale->type == RGB_SCALE_GREEN) {
            r = scale->red;
            g = min_val + t * (max_val - min_val);
            b = scale->blue;
        } else { // RGB_SCALE_BLUE
            r = scale->red;
            g = scale->green;
            b = min_val + t * (max_val - min_val);
        }

        cairo_pattern_add_color_stop_rgb(pattern, t, r, g, b);
    }

    // Draw the gradient track
    cairo_set_source(cr, pattern);
    cairo_rectangle(cr, trough_x, trough_y, trough_width, trough_height);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);

    // Draw track border
    cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, trough_x + 0.5, trough_y + 0.5, trough_width - 1, trough_height - 1);
    cairo_stroke(cr);

    // Calculate thumb position
    double value_pos;
    double range = max_val - min_val;
    if (range < 0.0001) {
        value_pos = trough_x + trough_width / 2.0;
    } else {
        double t = (current_val - min_val) / range;
        t = fmax(0.0, fmin(1.0, t));
        value_pos = trough_x + t * trough_width;
    }

    // Draw custom thumb indicator
    double thumb_w = thumb_size - 3.0;
    double thumb_half_w = thumb_size / 2.0;
    double thumb_x_unclamped = value_pos - thumb_half_w;
    double thumb_x_min = trough_x + 1.5;
    double thumb_x_max = trough_x + trough_width - thumb_w - 1.5;
    double thumb_x = fmax(thumb_x_min, fmin(thumb_x_unclamped, thumb_x_max));
    double thumb_y = trough_y - 3.0;
    double thumb_h = trough_height + 6.0;

    // Draw black outer rectangle
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 3);
    cairo_rectangle(cr, thumb_x, thumb_y, thumb_w, thumb_h);
    cairo_stroke(cr);

    // Inner rectangle (white normally, blue when hovering)
    if (scale->hovering) {
        cairo_set_source_rgb(cr, 35.0 / 255.0, 135.0 / 255.0, 215.0 / 255.0);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
    }
    cairo_set_line_width(cr, 1.5);
    cairo_rectangle(cr, thumb_x, thumb_y, thumb_w, thumb_h);
    cairo_stroke(cr);

    return FALSE;
}

// Helper to convert x position to value
static double x_to_value(RgbScale* scale, double x, int width) {
    GtkAdjustment* adj = gtk_range_get_adjustment(GTK_RANGE(scale));
    double min_val = gtk_adjustment_get_lower(adj);
    double max_val = gtk_adjustment_get_upper(adj);
    double thumb_size = 16.0;
    double trough_x = thumb_size / 2.0 + 2.0;
    double trough_width = width - thumb_size - 4.0;

    double t = (x - trough_x) / trough_width;
    t = fmax(0.0, fmin(1.0, t));
    return min_val + t * (max_val - min_val);
}

// Helper to check if a point is over the thumb button
static gboolean is_point_over_thumb(RgbScale* scale, double x, double y, int width, int height) {
    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(scale));
    double min_val = gtk_adjustment_get_lower(adjustment);
    double max_val = gtk_adjustment_get_upper(adjustment);
    double current_val = gtk_adjustment_get_value(adjustment);

    double thumb_size = 16.0;
    double trough_height = 10.0;
    double trough_y = (height - trough_height) / 2.0;
    double trough_width = width - thumb_size - 4.0;
    double trough_x = thumb_size / 2.0 + 2.0;

    // Calculate thumb position
    double value_pos;
    double range = max_val - min_val;
    if (range < 0.0001) {
        value_pos = trough_x + trough_width / 2.0;
    } else {
        double t = (current_val - min_val) / range;
        t = fmax(0.0, fmin(1.0, t));
        value_pos = trough_x + t * trough_width;
    }

    // Apply same clamping as draw function
    double thumb_half_w = thumb_size / 2.0;
    double thumb_x_unclamped = value_pos - thumb_half_w;
    double thumb_w = thumb_size;
    double thumb_x_min = trough_x + 1.5;
    double thumb_x_max = trough_x + trough_width - thumb_w - 1.5;
    double thumb_x = fmax(thumb_x_min, fmin(thumb_x_unclamped, thumb_x_max));
    double thumb_y = trough_y - 3.0;
    double thumb_h = trough_height + 6.0;

    return (x >= thumb_x && x <= thumb_x + thumb_w &&
            y >= thumb_y && y <= thumb_y + thumb_h);
}

static gboolean rgb_scale_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)data;
    RgbScale* scale = RGB_SCALE(widget);

    if (event->button != 1)
        return FALSE;

    scale->dragging = TRUE;

    int width = gtk_widget_get_allocated_width(widget);
    double new_value = x_to_value(scale, event->x, width);

    gtk_range_set_value(GTK_RANGE(scale), new_value);
    gtk_widget_queue_draw(widget);

    return TRUE;
}

static gboolean rgb_scale_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)data;
    (void)event;
    RgbScale* scale = RGB_SCALE(widget);

    scale->dragging = FALSE;
    gtk_widget_queue_draw(widget);

    return TRUE;
}

static gboolean rgb_scale_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    (void)data;
    RgbScale* scale = RGB_SCALE(widget);

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    gboolean was_hovering = scale->hovering;
    scale->hovering = is_point_over_thumb(scale, event->x, event->y, width, height);

    // Update cursor based on hover state
    GdkCursor* cursor = NULL;
    if (scale->hovering) {
        GdkDisplay* display = gdk_display_get_default();
        cursor = gdk_cursor_new_from_name(display, "pointer");
    }
    gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
    if (cursor)
        g_object_unref(cursor);

    if (scale->hovering != was_hovering) {
        gtk_widget_queue_draw(widget);
    }

    if (!scale->dragging)
        return FALSE;

    double new_value = x_to_value(scale, event->x, width);
    gtk_range_set_value(GTK_RANGE(scale), new_value);
    gtk_widget_queue_draw(widget);

    return TRUE;
}

static gboolean rgb_scale_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer data) {
    (void)event;
    (void)data;
    RgbScale* scale = RGB_SCALE(widget);

    if (scale->hovering) {
        scale->hovering = FALSE;
        gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static void rgb_scale_class_init(RgbScaleClass* klass) {
    (void)klass;
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->draw = rgb_scale_draw;

    // Hide default thumb using CSS
    gtk_widget_class_set_css_name(widget_class, "rgbscale");
}

static void rgb_scale_init(RgbScale* scale) {
    scale->type = RGB_SCALE_RED;
    scale->red = 1.0;
    scale->green = 0.0;
    scale->blue = 0.0;
    scale->dragging = FALSE;
    scale->hovering = FALSE;

    // Load CSS to hide default slider/trough
    GtkCssProvider* provider = gtk_css_provider_new();
    const gchar* css =
        "scale.rgbscale {"
        "  background: transparent;"
        "  padding: 0;"
        "}"
        "scale.rgbscale trough {"
        "  background: transparent;"
        "  border: none;"
        "  min-height: 0;"
        "  min-width: 0;"
        "}"
        "scale.rgbscale slider {"
        "  background: transparent;"
        "  border: none;"
        "  box-shadow: none;"
        "  min-width: 0;"
        "  min-height: 0;"
        "  margin: 0;"
        "}";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);

    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(scale));
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Add CSS class
    gtk_style_context_add_class(context, "rgbscale");

    // Enable events for manual interaction
    gtk_widget_add_events(GTK_WIDGET(scale),
                          GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_ENTER_NOTIFY_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    // Connect event handlers for manual thumb interaction
    g_signal_connect(scale, "button-press-event", G_CALLBACK(rgb_scale_button_press), NULL);
    g_signal_connect(scale, "button-release-event", G_CALLBACK(rgb_scale_button_release), NULL);
    g_signal_connect(scale, "motion-notify-event", G_CALLBACK(rgb_scale_motion_notify), NULL);
    g_signal_connect(scale, "leave-notify-event", G_CALLBACK(rgb_scale_leave_notify), NULL);
}

// Public API

GtkWidget* rgb_scale_new(RgbScaleType type) {
    RgbScale* scale = g_object_new(RGB_SCALE_TYPE, NULL);
    scale->type = type;

    GtkAdjustment* adjustment = gtk_adjustment_new(
        0.5,  // initial value
        0.0,  // minimum
        1.0,  // maximum
        0.01, // step increment
        0.1,  // page increment
        0.0   // page size (unused)
    );

    gtk_range_set_adjustment(GTK_RANGE(scale), adjustment);
    gtk_widget_set_size_request(GTK_WIDGET(scale), 200, 20);

    return GTK_WIDGET(scale);
}

void rgb_scale_set_rgb(RgbScale* scale, double r, double g, double b) {
    g_return_if_fail(RGB_IS_SCALE(scale));

    scale->red = r;
    scale->green = g;
    scale->blue = b;

    gtk_widget_queue_draw(GTK_WIDGET(scale));
}

void rgb_scale_get_rgb(RgbScale* scale, double* r, double* g, double* b) {
    g_return_if_fail(RGB_IS_SCALE(scale));

    if (r)
        *r = scale->red;
    if (g)
        *g = scale->green;
    if (b)
        *b = scale->blue;
}

void rgb_scale_set_value(RgbScale* scale, double value) {
    g_return_if_fail(RGB_IS_SCALE(scale));

    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(scale));
    gtk_adjustment_set_value(adjustment, value);
}

double rgb_scale_get_value(RgbScale* scale) {
    g_return_val_if_fail(RGB_IS_SCALE(scale), 0.0);

    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(scale));
    return gtk_adjustment_get_value(adjustment);
}
