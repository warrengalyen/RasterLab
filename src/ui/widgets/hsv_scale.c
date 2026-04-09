/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/widgets/hsv_scale.h"
#include <gtk/gtk.h>
#include <math.h>
#include "i18n.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Convert HSV to RGB
static void hsv_to_rgb(double h, double s, double v, double* r, double* g, double* b) {
    double c = v * s;
    double hp = h / 60.0;
    double x = c * (1.0 - fabs(fmod(hp, 2.0) - 1.0));
    double r1, g1, b1;

    if (hp >= 0 && hp < 1) {
        r1 = c;
        g1 = x;
        b1 = 0;
    } else if (hp >= 1 && hp < 2) {
        r1 = x;
        g1 = c;
        b1 = 0;
    } else if (hp >= 2 && hp < 3) {
        r1 = 0;
        g1 = c;
        b1 = x;
    } else if (hp >= 3 && hp < 4) {
        r1 = 0;
        g1 = x;
        b1 = c;
    } else if (hp >= 4 && hp < 5) {
        r1 = x;
        g1 = 0;
        b1 = c;
    } else {
        r1 = c;
        g1 = 0;
        b1 = x;
    }

    double m = v - c;
    *r = r1 + m;
    *g = g1 + m;
    *b = b1 + m;
}

// Define the struct
struct _HsvScale {
    GtkScale parent_instance;
    HsvScaleType type;
    double hue;        // Current hue (0-360), used when type is SATURATION or LIGHTNESS
    double saturation; // Current saturation (0-1), used when type is HUE or LIGHTNESS
    double value;      // Current value (0-1), used when type is HUE or SATURATION
    gboolean dragging; // Whether the user is dragging the thumb
    gboolean hovering; // Whether the mouse is hovering over the thumb
};

// Define the class structure (final type, so just parent class)
struct _HsvScaleClass {
    GtkScaleClass parent_class;
};

// GType implementation
G_DEFINE_TYPE(HsvScale, hsv_scale, GTK_TYPE_SCALE)

static gboolean hsv_scale_draw(GtkWidget* widget, cairo_t* cr) {
    HsvScale* scale = HSV_SCALE(widget);
    (void)gtk_widget_get_style_context(widget); // Unused but kept for potential future use

    // Get scale dimensions
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    // Calculate trough (track) rectangle - horizontal only
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
        double h, s, v;
        double r, g, b;

        if (scale->type == HSV_SCALE_HUE) {
            h = min_val + t * (max_val - min_val);
            s = scale->saturation;
            v = scale->value;
            // For hue scale, use full saturation and value for gradient
            s = 1.0;
            v = 1.0;
        } else if (scale->type == HSV_SCALE_SATURATION) {
            h = scale->hue;
            s = min_val + t * (max_val - min_val);
            v = scale->value;
        } else { // HSV_SCALE_VALUE
            h = scale->hue;
            s = scale->saturation;
            v = min_val + t * (max_val - min_val);
        }

        hsv_to_rgb(h, s, v, &r, &g, &b);
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

    // Draw custom thumb indicator (rectangle style like color wheel)
    // Constrain thumb horizontally so it doesn't extend past trackbar
    // Account for 3px outer stroke (extends 1.5px beyond path)
    double thumb_w = thumb_size - 3.0;
    double thumb_half_w = thumb_size / 2.0;
    double thumb_x_unclamped = value_pos - thumb_half_w;
    // MIN: left edge (accounting for 3px stroke) should be >= trough_x
    // LEFT edge at thumb_x - 1.5, so thumb_x >= trough_x + 1.5
    double thumb_x_min = trough_x + 1.5;
    // MAX: right edge (accounting for 3px stroke) should be <= trough_x + trough_width
    // RIGHT edge at thumb_x + thumb_w + 1.5, so thumb_x <= trough_x + trough_width - thumb_w - 1.5
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
    // Draw at same position as outer with thinner line width to create stroke effect
    if (scale->hovering) {
        cairo_set_source_rgb(cr, 35.0 / 255.0, 135.0 / 255.0, 215.0 / 255.0);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
    }
    cairo_set_line_width(cr, 1.5);
    // Draw inner rectangle at same path as outer - thinner line creates stroke effect
    cairo_rectangle(cr, thumb_x, thumb_y, thumb_w, thumb_h);
    cairo_stroke(cr);

    return FALSE;
}

// Helper to convert x position to value (horizontal only)
static double x_to_value(HsvScale* scale, double x, int width) {
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
static gboolean is_point_over_thumb(HsvScale* scale, double x, double y, int width, int height) {
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

static gboolean hsv_scale_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)data;
    HsvScale* scale = HSV_SCALE(widget);

    if (event->button != 1)
        return FALSE;

    scale->dragging = TRUE;

    int width = gtk_widget_get_allocated_width(widget);
    double new_value = x_to_value(scale, event->x, width);

    gtk_range_set_value(GTK_RANGE(scale), new_value);
    gtk_widget_queue_draw(widget);

    return TRUE;
}

static gboolean hsv_scale_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)data;
    (void)event;
    HsvScale* scale = HSV_SCALE(widget);

    scale->dragging = FALSE;
    gtk_widget_queue_draw(widget);

    return TRUE;
}

static gboolean hsv_scale_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    (void)data;
    HsvScale* scale = HSV_SCALE(widget);

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

static gboolean hsv_scale_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer data) {
    (void)event;
    (void)data;
    HsvScale* scale = HSV_SCALE(widget);

    if (scale->hovering) {
        scale->hovering = FALSE;
        gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static void hsv_scale_class_init(HsvScaleClass* klass) {
    (void)klass; // Final type, no class structure needed
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->draw = hsv_scale_draw;

    // Hide default thumb using CSS
    // The thumb will be invisible, we draw our own custom one
    gtk_widget_class_set_css_name(widget_class, "hslscale");
}

static void hsv_scale_init(HsvScale* scale) {
    scale->type = HSV_SCALE_HUE;
    scale->hue = 240.0;
    scale->saturation = 1.0;
    scale->value = 0.5;
    scale->dragging = FALSE;
    scale->hovering = FALSE;

    // Load CSS to hide default slider/trough - we draw everything custom
    GtkCssProvider* provider = gtk_css_provider_new();
    const gchar* css =
        "scale.hslscale {"
        "  background: transparent;"
        "  padding: 0;"
        "}"
        "scale.hslscale trough {"
        "  background: transparent;"
        "  border: none;"
        "  min-height: 0;"
        "  min-width: 0;"
        "}"
        "scale.hslscale slider {"
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
    gtk_style_context_add_class(context, "hslscale");

    // Enable events for manual interaction
    gtk_widget_add_events(GTK_WIDGET(scale),
                          GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_ENTER_NOTIFY_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    // Connect event handlers for manual thumb interaction
    g_signal_connect(scale, "button-press-event", G_CALLBACK(hsv_scale_button_press), NULL);
    g_signal_connect(scale, "button-release-event", G_CALLBACK(hsv_scale_button_release), NULL);
    g_signal_connect(scale, "motion-notify-event", G_CALLBACK(hsv_scale_motion_notify), NULL);
    g_signal_connect(scale, "leave-notify-event", G_CALLBACK(hsv_scale_leave_notify), NULL);
}

// Public API

GtkWidget* hsv_scale_new(HsvScaleType type, double min, double max) {
    HsvScale* scale = g_object_new(HSV_SCALE_TYPE, NULL);
    scale->type = type;

    GtkAdjustment* adjustment = gtk_adjustment_new(
        (type == HSV_SCALE_HUE) ? 240.0 : 0.5, // initial value
        min,                                   // minimum
        max,                                   // maximum
        (type == HSV_SCALE_HUE) ? 1.0 : 0.01,  // step increment
        (type == HSV_SCALE_HUE) ? 10.0 : 0.1,  // page increment
        0.0                                    // page size (unused)
    );

    gtk_range_set_adjustment(GTK_RANGE(scale), adjustment);

    // Set size request for better default size
    if (type == HSV_SCALE_HUE) {
        gtk_widget_set_size_request(GTK_WIDGET(scale), 200, 20);
    } else {
        gtk_widget_set_size_request(GTK_WIDGET(scale), 200, 20);
    }

    return GTK_WIDGET(scale);
}

void hsv_scale_set_hsv(HsvScale* scale, double h, double s, double v) {
    g_return_if_fail(HSV_IS_SCALE(scale));

    scale->hue = h;
    scale->saturation = s;
    scale->value = v;

    gtk_widget_queue_draw(GTK_WIDGET(scale));
}

void hsv_scale_get_hsv(HsvScale* scale, double* h, double* s, double* v) {
    g_return_if_fail(HSV_IS_SCALE(scale));

    if (h)
        *h = scale->hue;
    if (s)
        *s = scale->saturation;
    if (v)
        *v = scale->value;
}

void hsv_scale_set_value(HsvScale* scale, double value) {
    g_return_if_fail(HSV_IS_SCALE(scale));

    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(scale));
    gtk_adjustment_set_value(adjustment, value);
}

double hsv_scale_get_value(HsvScale* scale) {
    g_return_val_if_fail(HSV_IS_SCALE(scale), 0.0);

    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(scale));
    return gtk_adjustment_get_value(adjustment);
}
