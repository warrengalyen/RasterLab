/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/widgets/curves_widget.h"
#include <cairo.h>
#include <math.h>
#include <string.h>
#include "i18n.h"

#define NODE_RADIUS 8.0
#define GRID_DIVISIONS 4
#define PADDING 20.0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Signal enum
enum {
    CURVE_CHANGED_SIGNAL,
    LAST_SIGNAL
};

static guint curves_widget_signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(CurvesWidget, curves_widget, GTK_TYPE_DRAWING_AREA)

// Forward declarations
static gboolean curves_widget_draw(GtkWidget* widget, cairo_t* cr);
static gboolean curves_widget_button_press(GtkWidget* widget, GdkEventButton* event);
static gboolean curves_widget_button_release(GtkWidget* widget, GdkEventButton* event);
static gboolean curves_widget_motion_notify(GtkWidget* widget, GdkEventMotion* event);
static int find_node_at_position(CurvesWidget* self, double x, double y);

// Utility functions: monotonic cubic Hermite spline basis functions
static double hermite_basis_h00(double t) {
    double t2 = t * t;
    double t3 = t2 * t;
    return 2.0 * t3 - 3.0 * t2 + 1.0;
}

static double hermite_basis_h10(double t) {
    double t2 = t * t;
    double t3 = t2 * t;
    return t3 - 2.0 * t2 + t;
}

static double hermite_basis_h01(double t) {
    double t2 = t * t;
    double t3 = t2 * t;
    return -2.0 * t3 + 3.0 * t2;
}

static double hermite_basis_h11(double t) {
    double t2 = t * t;
    double t3 = t2 * t;
    return t3 - t2;
}

// Evaluate monotonic cubic Hermite spline
static double eval_hermite_segment(double t, double p1, double p2, double m1, double m2) {
    return hermite_basis_h00(t) * p1 +
           hermite_basis_h10(t) * m1 +
           hermite_basis_h01(t) * p2 +
           hermite_basis_h11(t) * m2;
}

// Compute monotonic tangent using Fritsch-Carlson algorithm
static void compute_monotonic_tangents(CurveNode* nodes, int node_count, double* tangents) {
    if (node_count < 2) {
        tangents[0] = 0.0;
        return;
    }

    // Compute slopes between consecutive points
    double* slopes = g_malloc(sizeof(double) * (node_count - 1));
    for (int i = 0; i < node_count - 1; i++) {
        double dx = nodes[i + 1].x - nodes[i].x;
        if (fabs(dx) > 1e-10) {
            slopes[i] = (nodes[i + 1].y - nodes[i].y) / dx;
        } else {
            slopes[i] = 0.0;
        }
    }

    // First point tangent
    if (node_count > 2) {
        double s1 = slopes[0];
        double s2 = slopes[1];
        tangents[0] = (s1 + s2) / 2.0;

        // Ensure monotonicity
        if (s1 * s2 <= 0) {
            tangents[0] = 0.0;
        } else {
            double alpha = fabs(tangents[0]) / fmin(fabs(s1), fabs(s2));
            if (alpha > 3.0) {
                tangents[0] = 3.0 * fmin(fabs(s1), fabs(s2)) * (tangents[0] >= 0 ? 1.0 : -1.0);
            }
        }
    } else {
        tangents[0] = slopes[0];
    }

    // Interior point tangents
    for (int i = 1; i < node_count - 1; i++) {
        double s1 = slopes[i - 1];
        double s2 = slopes[i];

        tangents[i] = (s1 + s2) / 2.0;

        // Ensure monotonicity
        if (s1 * s2 <= 0.0) {
            tangents[i] = 0.0;
        } else {
            double alpha = fabs(tangents[i]) / fmin(fabs(s1), fabs(s2));
            if (alpha > 3.0) {
                tangents[i] = 3.0 * fmin(fabs(s1), fabs(s2)) * (tangents[i] >= 0 ? 1.0 : -1.0);
            }
        }
    }

    // Last point tangent
    if (node_count > 2) {
        double s1 = slopes[node_count - 2];
        tangents[node_count - 1] = s1;

        if (fabs(s1) < 1e-10) {
            tangents[node_count - 1] = 0.0;
        }
    } else if (node_count == 2) {
        tangents[1] = slopes[0];
    }

    g_free(slopes);
}

// Utility function: evaluate curve at x using monotonic cubic Hermite spline
static double evaluate_curve_from_nodes(CurveNode* nodes, int node_count, double x) {
    if (node_count == 0)
        return x;
    if (node_count == 1)
        return nodes[0].y;

    const double epsilon = 0.001; // Tolerance for min/max detection

    // Compute tangents for all nodes
    double* tangents = g_malloc(sizeof(double) * node_count);
    compute_monotonic_tangents(nodes, node_count, tangents);

    // Find segment
    double result = 0.0;
    for (int i = 0; i < node_count - 1; i++) {
        if (x >= nodes[i].x && x <= nodes[i + 1].x) {
            double dx = nodes[i + 1].x - nodes[i].x;
            double t = (dx > 1e-10) ? (x - nodes[i].x) / dx : 0.0;

            double p1_y = nodes[i].y;
            double p2_y = nodes[i + 1].y;

            // Check if both endpoints are at min (0) or both at max (1)
            gboolean both_at_min = (p1_y < epsilon && p2_y < epsilon);
            gboolean both_at_max = (p1_y > 1.0 - epsilon && p2_y > 1.0 - epsilon);

            // If both points are at the same extreme, the line is flat
            if (both_at_min || both_at_max) {
                result = p1_y;
            } else {
                // Use monotonic cubic Hermite spline
                double m1 = tangents[i] * dx;
                double m2 = tangents[i + 1] * dx;
                result = eval_hermite_segment(t, p1_y, p2_y, m1, m2);
            }
            g_free(tangents);
            return result;
        }
    }

    g_free(tangents);
    if (x < nodes[0].x)
        return nodes[0].y;
    return nodes[node_count - 1].y;
}

// Initialize widget
static void curves_widget_init(CurvesWidget* self) {
    // Initialize with diagonal curve (identity) for all channels, plus midpoint
    self->node_count_rgb = 3;
    self->nodes_rgb[0].x = 0.0;
    self->nodes_rgb[0].y = 0.0;
    self->nodes_rgb[1].x = 0.5;
    self->nodes_rgb[1].y = 0.5;
    self->nodes_rgb[2].x = 1.0;
    self->nodes_rgb[2].y = 1.0;

    self->node_count_r = 3;
    self->nodes_r[0].x = 0.0;
    self->nodes_r[0].y = 0.0;
    self->nodes_r[1].x = 0.5;
    self->nodes_r[1].y = 0.5;
    self->nodes_r[2].x = 1.0;
    self->nodes_r[2].y = 1.0;

    self->node_count_g = 3;
    self->nodes_g[0].x = 0.0;
    self->nodes_g[0].y = 0.0;
    self->nodes_g[1].x = 0.5;
    self->nodes_g[1].y = 0.5;
    self->nodes_g[2].x = 1.0;
    self->nodes_g[2].y = 1.0;

    self->node_count_b = 3;
    self->nodes_b[0].x = 0.0;
    self->nodes_b[0].y = 0.0;
    self->nodes_b[1].x = 0.5;
    self->nodes_b[1].y = 0.5;
    self->nodes_b[2].x = 1.0;
    self->nodes_b[2].y = 1.0;

    self->selected_node = -1;
    self->hovered_node = -1;
    self->dragging = FALSE;
    self->show_tooltip = FALSE;

    // Default display options
    self->show_histogram = TRUE;
    self->show_grid = TRUE;
    self->show_diagonal = TRUE;
    self->active_channel = CHANNEL_RGB;

    // Initialize histogram with sample data
    for (int i = 0; i < 256; i++) {
        // Create sample histogram data
        double x = (i - 128.0) / 64.0;
        self->histogram_rgb[i] = exp(-x * x) * 100.0;
        self->histogram_r[i] = exp(-(i - 100.0) * (i - 100.0) / 1000.0) * 80.0;
        self->histogram_g[i] = exp(-(i - 128.0) * (i - 128.0) / 1000.0) * 90.0;
        self->histogram_b[i] = exp(-(i - 156.0) * (i - 156.0) / 1000.0) * 70.0;
    }

    // Add events
    gtk_widget_add_events(GTK_WIDGET(self),
                          GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);
}

static void curves_widget_class_init(CurvesWidgetClass* klass) {
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->draw = curves_widget_draw;
    widget_class->button_press_event = curves_widget_button_press;
    widget_class->button_release_event = curves_widget_button_release;
    widget_class->motion_notify_event = curves_widget_motion_notify;

    // Register "curve-changed" signal
    curves_widget_signals[CURVE_CHANGED_SIGNAL] = g_signal_new(
        "curve-changed",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0,           // no handler offset
        NULL,        // accumulator
        NULL,        // accumulator data
        NULL,        // C marshaller (NULL = use default)
        G_TYPE_NONE, // return type
        0            // no parameters
    );
}

// Helper function to draw a single histogram
static void draw_histogram_channel(cairo_t* cr, double* histogram, int width, int height,
                                   double r, double g, double b, double max_value) {
    // Enable antialiasing for smooth lines
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    // Draw histogram peaks as continuous line at full opacity first
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, r, g, b, 1.0);

    // Start at first point
    double x = (0 / 255.0) * width;
    double bar_height = (histogram[0] / max_value) * height;
    cairo_move_to(cr, x, height - bar_height);

    // Draw continuous line through all peaks
    for (int i = 1; i < 256; i++) {
        x = (i / 255.0) * width;
        bar_height = (histogram[i] / max_value) * height;
        cairo_line_to(cr, x, height - bar_height);
    }
    cairo_stroke(cr);

    // Draw histogram fill at lower opacity
    cairo_set_source_rgba(cr, r, g, b, 0.25);
    cairo_move_to(cr, 0, height);

    for (int i = 0; i < 256; i++) {
        x = (i / 255.0) * width;
        bar_height = (histogram[i] / max_value) * height;
        cairo_line_to(cr, x, height - bar_height);
    }

    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
    cairo_fill(cr);
}

// Draw histogram
static void draw_histogram(CurvesWidget* self, cairo_t* cr, int width, int height) {
    if (!self->show_histogram)
        return;

    // Always calculate max value across all three channels for consistent scaling
    double max_value = 0.0;
    for (int i = 0; i < 256; i++) {
        if (self->histogram_r[i] > max_value)
            max_value = self->histogram_r[i];
        if (self->histogram_g[i] > max_value)
            max_value = self->histogram_g[i];
        if (self->histogram_b[i] > max_value)
            max_value = self->histogram_b[i];
    }
    if (max_value == 0.0)
        max_value = 1.0;

    if (self->active_channel == CHANNEL_RGB) {
        // For RGB mode, overlay all three channels with their respective colors
        draw_histogram_channel(cr, self->histogram_r, width, height, 1.0, 0.0, 0.0, max_value);
        draw_histogram_channel(cr, self->histogram_g, width, height, 0.0, 1.0, 0.0, max_value);
        draw_histogram_channel(cr, self->histogram_b, width, height, 0.0, 0.0, 1.0, max_value);
    } else {
        // Single channel mode - display selected channel with global scaling
        double* histogram;
        double r, g, b;

        switch (self->active_channel) {
            case CHANNEL_RED:
                histogram = self->histogram_r;
                r = 1.0;
                g = 0.0;
                b = 0.0;
                break;
            case CHANNEL_GREEN:
                histogram = self->histogram_g;
                r = 0.0;
                g = 1.0;
                b = 0.0;
                break;
            case CHANNEL_BLUE:
                histogram = self->histogram_b;
                r = 0.0;
                g = 0.0;
                b = 1.0;
                break;
            default:
                histogram = self->histogram_rgb;
                r = 0.5;
                g = 0.5;
                b = 0.5;
        }

        draw_histogram_channel(cr, histogram, width, height, r, g, b, max_value);
    }
}

// Draw grid
static void draw_grid(CurvesWidget* self, cairo_t* cr, int width, int height) {
    if (!self->show_grid)
        return;

    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.3);
    cairo_set_line_width(cr, 1.0);

    for (int i = 1; i < GRID_DIVISIONS; i++) {
        double pos = (i / (double)GRID_DIVISIONS) * width;
        cairo_move_to(cr, pos, 0);
        cairo_line_to(cr, pos, height);
        cairo_stroke(cr);

        pos = (i / (double)GRID_DIVISIONS) * height;
        cairo_move_to(cr, 0, pos);
        cairo_line_to(cr, width, pos);
        cairo_stroke(cr);
    }
}

// Draw diagonal reference line
static void draw_diagonal(CurvesWidget* self, cairo_t* cr, int width, int height) {
    if (!self->show_diagonal)
        return;

    cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.5);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0, height);
    cairo_line_to(cr, width, 0);
    cairo_stroke(cr);
}

// Helper function to draw a single curve using monotonic cubic Hermite spline
static void draw_curve_from_nodes(cairo_t* cr, CurveNode* nodes, int node_count,
                                  int width, int height) {
    if (node_count < 2)
        return;

    // Compute tangents for all nodes
    double* tangents = g_malloc(sizeof(double) * node_count);
    compute_monotonic_tangents(nodes, node_count, tangents);

    // Sample the curve at regular intervals for smooth drawing
    const int samples_per_segment = 32;
    const double epsilon = 0.001; // Tolerance for min/max detection

    // Start by drawing a flat line before the first node
    double x_before = 0.0;
    double y_before = height - (nodes[0].y * height);
    double x_first = nodes[0].x * width;
    double y_first = height - (nodes[0].y * height);
    cairo_move_to(cr, x_before, y_before);
    cairo_line_to(cr, x_first, y_first);

    for (int i = 0; i < node_count - 1; i++) {
        double p1_x = nodes[i].x;
        double p2_x = nodes[i + 1].x;
        double p1_y = nodes[i].y;
        double p2_y = nodes[i + 1].y;

        // Check if both endpoints are at min (0) or both at max (1)
        gboolean both_at_min = (p1_y < epsilon && p2_y < epsilon);
        gboolean both_at_max = (p1_y > 1.0 - epsilon && p2_y > 1.0 - epsilon);

        // If both points are at the same extreme, draw a flat line
        if (both_at_min || both_at_max) {
            double x1 = p1_x * width;
            double y1 = height - (p1_y * height);
            double x2 = p2_x * width;
            double y2 = height - (p2_y * height);

            cairo_line_to(cr, x2, y2);
        } else {
            // Use monotonic cubic Hermite spline
            double dx = p2_x - p1_x;
            double m1 = tangents[i] * dx;
            double m2 = tangents[i + 1] * dx;

            // Draw segment with samples
            for (int s = 0; s <= samples_per_segment; s++) {
                double t = (double)s / samples_per_segment;

                // Interpolate x position
                double x_norm = p1_x + t * (p2_x - p1_x);
                double y_norm = eval_hermite_segment(t, p1_y, p2_y, m1, m2);

                double x = x_norm * width;
                double y = height - (y_norm * height);

                cairo_line_to(cr, x, y);
            }
        }
    }

    // Draw a flat line after the last node
    double x_last = nodes[node_count - 1].x * width;
    double y_last = height - (nodes[node_count - 1].y * height);
    double x_after = width;
    double y_after = y_last;
    cairo_line_to(cr, x_after, y_after);

    g_free(tangents);
}

// Draw curve using Bezier segments with white fill and black borders
static void draw_curve(CurvesWidget* self, cairo_t* cr, int width, int height) {
    CurveNode* nodes;
    int node_count;

    // Select nodes based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count = self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count = self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count = self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count = self->node_count_rgb;
    }

    if (node_count < 2)
        return;

    // Enable antialiasing for smooth curves
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    // First draw black border (thicker)
    cairo_set_line_width(cr, 4.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    draw_curve_from_nodes(cr, nodes, node_count, width, height);
    cairo_stroke(cr);

    // Then draw white fill on top (thinner)
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    draw_curve_from_nodes(cr, nodes, node_count, width, height);
    cairo_stroke(cr);
}

// Draw nodes
static void draw_nodes(CurvesWidget* self, cairo_t* cr, int full_width, int full_height) {
    CurveNode* nodes;
    int node_count;

    // Select nodes based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count = self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count = self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count = self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count = self->node_count_rgb;
    }

    int draw_width = full_width - 2 * PADDING;
    int draw_height = full_height - 2 * PADDING;

    for (int i = 0; i < node_count; i++) {
        double x = PADDING + (nodes[i].x * draw_width);
        double y = PADDING + (draw_height - (nodes[i].y * draw_height));

        // Outer circle
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_arc(cr, x, y, NODE_RADIUS + 1, 0, 2 * M_PI);
        cairo_fill(cr);

        // Inner circle - fill if hovered
        if (i == self->hovered_node) {
            cairo_set_source_rgb(cr, 0.2078, 0.5176, 0.8941);
        } else {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        }
        cairo_arc(cr, x, y, NODE_RADIUS, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

// Draw crosshair
static void draw_crosshair(CurvesWidget* self, cairo_t* cr, int width, int height) {
    if (!self->show_tooltip)
        return;

    // Don't render crosshair if cursor is outside the widget bounds
    // mouse_x and mouse_y are already adjusted for padding, so valid range is [0, width] and [0, height]
    if (self->mouse_x < 0 || self->mouse_x > width ||
        self->mouse_y < 0 || self->mouse_y > height) {
        return;
    }

    CurveNode* nodes;
    int node_count;

    // Select nodes based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count = self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count = self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count = self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count = self->node_count_rgb;
    }

    // Check if mouse is over a node (pass full widget coordinates)
    int node_index = find_node_at_position(self, self->mouse_x + PADDING, self->mouse_y + PADDING);
    double x, y_curve;

    if (node_index >= 0) {
        // Lock crosshair to node center
        x = nodes[node_index].x * width;
        y_curve = height - (nodes[node_index].y * height);
    } else {
        // Use interpolated curve value
        x = self->mouse_x;
        y_curve = height - (evaluate_curve_from_nodes(nodes, node_count, self->mouse_x / width) * height);
    }

    // Draw crosshair
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
    cairo_set_line_width(cr, 1.0);

    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, height);
    cairo_stroke(cr);

    cairo_move_to(cr, 0, y_curve);
    cairo_line_to(cr, width, y_curve);
    cairo_stroke(cr);
}

// Main draw function
static gboolean curves_widget_draw(GtkWidget* widget, cairo_t* cr) {
    CurvesWidget* self = CURVES_WIDGET(widget);
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    int width = allocation.width;
    int height = allocation.height;

    // Background
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_paint(cr);

    // Create a clipping region for the drawing area (excluding padding)
    int draw_width = width - 2 * PADDING;
    int draw_height = height - 2 * PADDING;

    // Save current transform
    cairo_save(cr);

    // Set clipping rectangle to the padded area
    cairo_rectangle(cr, PADDING, PADDING, draw_width, draw_height);
    cairo_clip(cr);

    // Translate to account for padding
    cairo_translate(cr, PADDING, PADDING);

    // Draw components within the padded area
    draw_histogram(self, cr, draw_width, draw_height);
    draw_grid(self, cr, draw_width, draw_height);
    draw_diagonal(self, cr, draw_width, draw_height);
    draw_curve(self, cr, draw_width, draw_height);
    draw_crosshair(self, cr, draw_width, draw_height);

    // Restore transform before drawing nodes (they use full coordinates)
    cairo_restore(cr);

    // Draw border around padded area
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, PADDING, PADDING, draw_width, draw_height);
    cairo_stroke(cr);

    // Draw nodes on top (outside the padded coordinate system, uses full widget coords)
    draw_nodes(self, cr, width, height);

    // Draw tooltip on top of everything (including nodes)
    if (self->show_tooltip && self->mouse_x >= 0 && self->mouse_x <= draw_width &&
        self->mouse_y >= 0 && self->mouse_y <= draw_height) {
        CurveNode* nodes;
        int node_count;

        switch (self->active_channel) {
            case CHANNEL_RED:
                nodes = self->nodes_r;
                node_count = self->node_count_r;
                break;
            case CHANNEL_GREEN:
                nodes = self->nodes_g;
                node_count = self->node_count_g;
                break;
            case CHANNEL_BLUE:
                nodes = self->nodes_b;
                node_count = self->node_count_b;
                break;
            default:
                nodes = self->nodes_rgb;
                node_count = self->node_count_rgb;
        }

        // Check if mouse is over a node (pass full widget coordinates)
        int node_index = find_node_at_position(self, self->mouse_x + PADDING, self->mouse_y + PADDING);
        double x_pixel, y_pixel;

        if (node_index >= 0) {
            // Lock tooltip to node center
            x_pixel = PADDING + (nodes[node_index].x * draw_width);
            y_pixel = PADDING + (draw_height - (nodes[node_index].y * draw_height));
        } else {
            // Use interpolated curve value
            x_pixel = PADDING + self->mouse_x;
            y_pixel = PADDING + (draw_height - (evaluate_curve_from_nodes(nodes, node_count, self->mouse_x / draw_width) * draw_height));
        }

        // Draw tooltip
        int input_val, output_val;

        if (node_index >= 0) {
            // When hovering over a node, show the node's position
            input_val = (int)(nodes[node_index].x * 255);
            output_val = (int)(nodes[node_index].y * 255);
        } else {
            // When hovering over the curve, show the interpolated position
            input_val = (int)(self->mouse_x / draw_width * 255);
            output_val = (int)((draw_height - self->mouse_y) / draw_height * 255);
        }

        char line1[32];
        char line2[32];
        snprintf(line1, sizeof(line1), "input: %d", input_val);
        snprintf(line2, sizeof(line2), "output: %d", output_val);

        // Calculate tooltip dimensions
        int tooltip_width = 100;
        int tooltip_height = 50;

        // Initial position: below and to the right of crosshair
        int tooltip_x = (int)(x_pixel + 10);
        int tooltip_y = (int)(y_pixel - 40);

        // Reposition if tooltip goes outside widget bounds
        if (tooltip_x + tooltip_width > width) {
            tooltip_x = (int)(x_pixel - tooltip_width - 10);
        }
        if (tooltip_y < PADDING) {
            tooltip_y = (int)(y_pixel + 10);
        }
        if (tooltip_y + tooltip_height > height) {
            tooltip_y = (int)(y_pixel - tooltip_height - 10);
        }

        // Clamp to valid bounds
        if (tooltip_x < PADDING)
            tooltip_x = PADDING;
        if (tooltip_x + tooltip_width > width)
            tooltip_x = width - tooltip_width;
        if (tooltip_y < PADDING)
            tooltip_y = PADDING;
        if (tooltip_y + tooltip_height > height)
            tooltip_y = height - tooltip_height;

        // Draw tooltip background
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_rectangle(cr, tooltip_x, tooltip_y, tooltip_width, tooltip_height);
        cairo_fill(cr);

        // Draw tooltip border
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_rectangle(cr, tooltip_x, tooltip_y, tooltip_width, tooltip_height);
        cairo_stroke(cr);

        // Draw text
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

        cairo_move_to(cr, tooltip_x + 5, tooltip_y + 15);
        cairo_show_text(cr, line1);

        cairo_move_to(cr, tooltip_x + 5, tooltip_y + 30);
        cairo_show_text(cr, line2);
    }

    return FALSE;
}

// Find node at position
static int find_node_at_position(CurvesWidget* self, double x, double y) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(self), &allocation);

    CurveNode* nodes;
    int node_count;

    // Select nodes based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count = self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count = self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count = self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count = self->node_count_rgb;
    }

    int draw_width = allocation.width - 2 * PADDING;
    int draw_height = allocation.height - 2 * PADDING;

    for (int i = 0; i < node_count; i++) {
        double node_x = PADDING + (nodes[i].x * draw_width);
        double node_y = PADDING + (draw_height - (nodes[i].y * draw_height));

        double dx = x - node_x;
        double dy = y - node_y;
        double dist = sqrt(dx * dx + dy * dy);

        if (dist <= NODE_RADIUS + 3) {
            return i;
        }
    }
    return -1;
}

// Mouse button press
static gboolean curves_widget_button_press(GtkWidget* widget, GdkEventButton* event) {
    CurvesWidget* self = CURVES_WIDGET(widget);
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    CurveNode* nodes;
    int* node_count_ptr;

    // Select nodes pointer based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count_ptr = &self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count_ptr = &self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count_ptr = &self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count_ptr = &self->node_count_rgb;
    }

    if (event->button == 1) { // Left click
        int node = find_node_at_position(self, event->x, event->y);

        if (node >= 0) {
            // Start dragging existing node
            self->selected_node = node;
            self->dragging = TRUE;
        } else if (*node_count_ptr < MAX_NODES) {
            // Check if click is within drawing area
            int draw_width = allocation.width - 2 * PADDING;
            int draw_height = allocation.height - 2 * PADDING;

            if (event->x >= PADDING && event->x <= PADDING + draw_width &&
                event->y >= PADDING && event->y <= PADDING + draw_height) {
                // Add new node
                double norm_x = (event->x - PADDING) / draw_width;
                double norm_y = 1.0 - ((event->y - PADDING) / draw_height);

                // Clamp to [0, 1]
                norm_x = CLAMP(norm_x, 0.0, 1.0);
                norm_y = CLAMP(norm_y, 0.0, 1.0);

                // Find insertion position
                int insert_pos = *node_count_ptr;
                for (int i = 0; i < *node_count_ptr; i++) {
                    if (norm_x < nodes[i].x) {
                        insert_pos = i;
                        break;
                    }
                }

                // Shift nodes
                for (int i = *node_count_ptr; i > insert_pos; i--) {
                    nodes[i] = nodes[i - 1];
                }

                nodes[insert_pos].x = norm_x;
                nodes[insert_pos].y = norm_y;
                (*node_count_ptr)++;
                self->selected_node = insert_pos;
                self->dragging = TRUE;

                gtk_widget_queue_draw(widget);
            }
        }
    } else if (event->button == 3) { // Right click
        int node = find_node_at_position(self, event->x, event->y);

        if (node >= 0 && *node_count_ptr > 2) {
            // Delete node (keep at least 2)
            for (int i = node; i < *node_count_ptr - 1; i++) {
                nodes[i] = nodes[i + 1];
            }
            (*node_count_ptr)--;
            gtk_widget_queue_draw(widget);

            // Emit curve-changed signal when node is deleted
            g_signal_emit(widget, curves_widget_signals[CURVE_CHANGED_SIGNAL], 0);
        }
    }

    return TRUE;
}

// Mouse button release
static gboolean curves_widget_button_release(GtkWidget* widget, GdkEventButton* event) {
    CurvesWidget* self = CURVES_WIDGET(widget);

    if (event->button == 1) {
        self->dragging = FALSE;
        self->selected_node = -1;

        // Emit curve-changed signal after finishing node manipulation
        g_signal_emit(widget, curves_widget_signals[CURVE_CHANGED_SIGNAL], 0);
    }

    return TRUE;
}

// Mouse motion
static gboolean curves_widget_motion_notify(GtkWidget* widget, GdkEventMotion* event) {
    CurvesWidget* self = CURVES_WIDGET(widget);
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    CurveNode* nodes;
    int* node_count_ptr;

    // Select nodes pointer based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count_ptr = &self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count_ptr = &self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count_ptr = &self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count_ptr = &self->node_count_rgb;
    }

    int draw_width = allocation.width - 2 * PADDING;
    int draw_height = allocation.height - 2 * PADDING;

    // Store mouse position adjusted for drawing area
    self->mouse_x = event->x - PADDING;
    self->mouse_y = event->y - PADDING;

    if (self->dragging && self->selected_node >= 0) {
        // Update node position
        double norm_x = CLAMP((event->x - PADDING) / draw_width, 0.0, 1.0);
        double norm_y = CLAMP(1.0 - ((event->y - PADDING) / draw_height), 0.0, 1.0);

        // Keep order for non-endpoint nodes
        if (self->selected_node > 0 && self->selected_node < *node_count_ptr - 1) {
            norm_x = fmax(norm_x, nodes[self->selected_node - 1].x + 0.01);
            norm_x = fmin(norm_x, nodes[self->selected_node + 1].x - 0.01);
        }

        nodes[self->selected_node].x = norm_x;
        nodes[self->selected_node].y = norm_y;

        gtk_widget_queue_draw(widget);
    } else {
        // Update hovered node
        int old_hovered = self->hovered_node;
        self->hovered_node = find_node_at_position(self, event->x, event->y);

        // Show tooltip when hovering over drawing area
        self->show_tooltip = (event->x >= PADDING && event->x <= PADDING + draw_width &&
                              event->y >= PADDING && event->y <= PADDING + draw_height);

        // Update cursor based on hover state
        GdkCursor* cursor = NULL;
        if (self->hovered_node >= 0) {
            // Use hand cursor when over a node
            cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "hand2");
        } else {
            // Use default cursor
            cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "default");
        }
        gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
        if (cursor)
            g_object_unref(cursor);

        if (old_hovered != self->hovered_node || self->show_tooltip) {
            gtk_widget_queue_draw(widget);
        }
    }

    return TRUE;
}

// Public API functions
GtkWidget* curves_widget_new(void) {
    return g_object_new(CURVES_TYPE_WIDGET, NULL);
}

void curves_widget_set_histogram_visible(CurvesWidget* self, gboolean visible) {
    g_return_if_fail(CURVES_IS_WIDGET(self));
    self->show_histogram = visible;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void curves_widget_set_grid_visible(CurvesWidget* self, gboolean visible) {
    g_return_if_fail(CURVES_IS_WIDGET(self));
    self->show_grid = visible;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void curves_widget_set_diagonal_visible(CurvesWidget* self, gboolean visible) {
    g_return_if_fail(CURVES_IS_WIDGET(self));
    self->show_diagonal = visible;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void curves_widget_set_channel(CurvesWidget* self, Channel channel) {
    g_return_if_fail(CURVES_IS_WIDGET(self));
    self->active_channel = channel;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void curves_widget_set_histogram_data(CurvesWidget* self, Channel channel,
                                      const double* data, int size) {
    g_return_if_fail(CURVES_IS_WIDGET(self));
    if (size != 256)
        return;

    double* target;
    switch (channel) {
        case CHANNEL_RED:
            target = self->histogram_r;
            break;
        case CHANNEL_GREEN:
            target = self->histogram_g;
            break;
        case CHANNEL_BLUE:
            target = self->histogram_b;
            break;
        default:
            target = self->histogram_rgb;
    }

    memcpy(target, data, 256 * sizeof(double));
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

int curves_widget_get_node_count(CurvesWidget* self) {
    g_return_val_if_fail(CURVES_IS_WIDGET(self), 0);
    switch (self->active_channel) {
        case CHANNEL_RED:
            return self->node_count_r;
        case CHANNEL_GREEN:
            return self->node_count_g;
        case CHANNEL_BLUE:
            return self->node_count_b;
        default:
            return self->node_count_rgb;
    }
}

void curves_widget_get_node(CurvesWidget* self, int index, double* x, double* y) {
    g_return_if_fail(CURVES_IS_WIDGET(self));

    CurveNode* nodes;
    int node_count;

    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count = self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count = self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count = self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count = self->node_count_rgb;
    }

    g_return_if_fail(index >= 0 && index < node_count);

    if (x)
        *x = nodes[index].x;
    if (y)
        *y = nodes[index].y;
}

void curves_widget_reset_curve(CurvesWidget* self) {
    g_return_if_fail(CURVES_IS_WIDGET(self));

    switch (self->active_channel) {
        case CHANNEL_RED:
            self->node_count_r = 2;
            self->nodes_r[0].x = 0.0;
            self->nodes_r[0].y = 0.0;
            self->nodes_r[1].x = 1.0;
            self->nodes_r[1].y = 1.0;
            break;
        case CHANNEL_GREEN:
            self->node_count_g = 2;
            self->nodes_g[0].x = 0.0;
            self->nodes_g[0].y = 0.0;
            self->nodes_g[1].x = 1.0;
            self->nodes_g[1].y = 1.0;
            break;
        case CHANNEL_BLUE:
            self->node_count_b = 2;
            self->nodes_b[0].x = 0.0;
            self->nodes_b[0].y = 0.0;
            self->nodes_b[1].x = 1.0;
            self->nodes_b[1].y = 1.0;
            break;
        default:
            self->node_count_rgb = 2;
            self->nodes_rgb[0].x = 0.0;
            self->nodes_rgb[0].y = 0.0;
            self->nodes_rgb[1].x = 1.0;
            self->nodes_rgb[1].y = 1.0;
    }

    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void curves_widget_get_lut(CurvesWidget* self, uint8_t* lut) {
    g_return_if_fail(CURVES_IS_WIDGET(self));
    g_return_if_fail(lut != NULL);

    CurveNode* nodes;
    int node_count;

    // Select nodes based on active channel
    switch (self->active_channel) {
        case CHANNEL_RED:
            nodes = self->nodes_r;
            node_count = self->node_count_r;
            break;
        case CHANNEL_GREEN:
            nodes = self->nodes_g;
            node_count = self->node_count_g;
            break;
        case CHANNEL_BLUE:
            nodes = self->nodes_b;
            node_count = self->node_count_b;
            break;
        default:
            nodes = self->nodes_rgb;
            node_count = self->node_count_rgb;
    }

    // Generate 256-point lookup table
    for (int i = 0; i < 256; i++) {
        double x_normalized = (double)i / 255.0;
        double y_normalized = evaluate_curve_from_nodes(nodes, node_count, x_normalized);

        // Clamp to [0, 1] and convert to [0, 255]
        y_normalized = fmax(0.0, fmin(1.0, y_normalized));
        lut[i] = (uint8_t)(y_normalized * 255.0 + 0.5);
    }
}