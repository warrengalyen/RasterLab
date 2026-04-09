/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/widgets/hsv_color_wheel.h"
#include <math.h>
#include <stdio.h>
#include "i18n.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declarations of static functions
static void hsv_to_rgb(double h, double s, double v, double* r, double* g, double* b);
static void rgb_to_hsv(double r, double g, double b, double* h, double* s, double* v);
static void get_triangle_vertices(ColorWheel* wheel, double* x1, double* y1,
                                  double* x2, double* y2, double* x3, double* y3);
static void sl_to_triangle(ColorWheel* wheel, double s, double v, double* x, double* y);
static void triangle_to_sl(ColorWheel* wheel, double x, double y, double* s, double* l);
static gboolean on_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data);
static gboolean on_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data);
static gboolean on_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer data);
static gboolean on_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer data);
static gboolean on_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer data);
static void invalidate_caches(ColorWheel* wheel);
static void render_wheel_surface(ColorWheel* wheel);
static void render_triangle_surface(ColorWheel* wheel);

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

// Convert RGB to HSV
static void rgb_to_hsv(double r, double g, double b, double* h, double* s, double* v) {
    double max = fmax(r, fmax(g, b));
    double min = fmin(r, fmin(g, b));
    double delta = max - min;

    *v = max;

    if (max < 0.0001) {
        *s = 0;
        *h = 0;
        return;
    }

    *s = delta / max;

    if (delta < 0.0001) {
        *h = 0;
        return;
    }

    if (max == r) {
        *h = 60.0 * fmod((g - b) / delta, 6.0);
    } else if (max == g) {
        *h = 60.0 * ((b - r) / delta + 2.0);
    } else {
        *h = 60.0 * ((r - g) / delta + 4.0);
    }

    if (*h < 0)
        *h += 360.0;
}

// Convert hue to angle on the wheel (blue hue 240 at top, clockwise)
static double hue_to_angle(double hue) {
    // Hue 240 (blue) at top (-90° in standard coords), increasing clockwise
    // When hue=240, angle should be -90° (top)
    return (hue - 330.0) * M_PI / 180.0;
}

// Convert angle (from atan2) back to hue
static double angle_to_hue(double angle_rad) {
    double hue = angle_rad * 180.0 / M_PI + 330.0;
    // Normalize to 0-360
    while (hue < 0)
        hue += 360.0;
    while (hue >= 360.0)
        hue -= 360.0;
    return hue;
}

// Get triangle vertices (rotated by hue)
// vertex1 = pure hue (pointing toward hue on wheel) - H, S=1, V=1
// vertex2 = white (120° clockwise from hue) - H, S=0, V=1
// vertex3 = black (240° clockwise from hue) - H, S=1, V=0
static void get_triangle_vertices(ColorWheel* wheel, double* x1, double* y1,
                                  double* x2, double* y2, double* x3, double* y3) {
    double cx = wheel->width / 2.0;
    double cy = wheel->height / 2.0;
    double outer_radius = fmin(wheel->width, wheel->height) / 2.0 - 10;
    double inner_radius = outer_radius - 40;
    double angle = hue_to_angle(wheel->hue);

    double triangle_radius = inner_radius - 5;

    // vertex1: pure saturated hue (L=0.5, S=1)
    *x1 = cx + triangle_radius * cos(angle);
    *y1 = cy + triangle_radius * sin(angle);

    // vertex2: white (L=1, S=0) - 120° clockwise
    *x2 = cx + triangle_radius * cos(angle + 2.0 * M_PI / 3.0);
    *y2 = cy + triangle_radius * sin(angle + 2.0 * M_PI / 3.0);

    // vertex3: black (L=0, S=0) - 240° clockwise
    *x3 = cx + triangle_radius * cos(angle + 4.0 * M_PI / 3.0);
    *y3 = cy + triangle_radius * sin(angle + 4.0 * M_PI / 3.0);
}

// Convert saturation/lightness to triangle coordinates
static void sl_to_triangle(ColorWheel* wheel, double s, double v, double* x, double* y) {
    double x1, y1, x2, y2, x3, y3;
    get_triangle_vertices(wheel, &x1, &y1, &x2, &y2, &x3, &y3);

    // Barycentric weights derived from:
    // s = w1  (saturation = weight of pure hue vertex)
    // v = 0.5*w1 + w2  (value interpolated from vertices)
    // w1 + w2 + w3 = 1
    //
    // Solving: w1 = s, w2 = v - 0.5*s, w3 = 1 - 0.5*s - v
    double w1 = s;
    double w2 = v - 0.5 * s;
    double w3 = 1.0 - 0.5 * s - v;

    // Clamp to valid range (handles edge cases from floating point)
    w1 = fmax(0, w1);
    w2 = fmax(0, w2);
    w3 = fmax(0, w3);

    // Normalize
    double sum = w1 + w2 + w3;
    if (sum > 0) {
        w1 /= sum;
        w2 /= sum;
        w3 /= sum;
    }

    *x = w1 * x1 + w2 * x2 + w3 * x3;
    *y = w1 * y1 + w2 * y2 + w3 * y3;
}

// Project point onto line segment, returns squared distance
static double project_to_segment(double px, double py,
                                 double ax, double ay, double bx, double by,
                                 double* out_x, double* out_y) {
    double dx = bx - ax;
    double dy = by - ay;
    double len_sq = dx * dx + dy * dy;

    if (len_sq < 0.0001) {
        *out_x = ax;
        *out_y = ay;
    } else {
        double t = ((px - ax) * dx + (py - ay) * dy) / len_sq;
        t = fmax(0, fmin(1, t));
        *out_x = ax + t * dx;
        *out_y = ay + t * dy;
    }

    double dist_x = px - *out_x;
    double dist_y = py - *out_y;
    return dist_x * dist_x + dist_y * dist_y;
}

// Check if a point is inside the triangle
static gboolean is_point_in_triangle(ColorWheel* wheel, double x, double y) {
    double x1, y1, x2, y2, x3, y3;
    get_triangle_vertices(wheel, &x1, &y1, &x2, &y2, &x3, &y3);

    // Barycentric coordinates
    double denom = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if (fabs(denom) < 0.001) {
        return FALSE;
    }

    double w1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / denom;
    double w2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / denom;
    double w3 = 1.0 - w1 - w2;

    // Point is inside triangle if all barycentric coordinates are non-negative
    return (w1 >= 0 && w2 >= 0 && w3 >= 0);
}

// Convert triangle coordinates to saturation/lightness
// Projects any point to the nearest point on/in the triangle
static void triangle_to_sl(ColorWheel* wheel, double x, double y, double* s, double* l) {
    double x1, y1, x2, y2, x3, y3;
    get_triangle_vertices(wheel, &x1, &y1, &x2, &y2, &x3, &y3);

    // Barycentric coordinates
    double denom = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if (fabs(denom) < 0.001) {
        *s = 0;
        *l = 0.5;
        return;
    }

    double w1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / denom;
    double w2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / denom;
    double w3 = 1.0 - w1 - w2;

    // If outside triangle, project to nearest edge (check ALL edges)
    if (w1 < 0 || w2 < 0 || w3 < 0) {
        double best_x = x, best_y = y;
        double best_dist_sq = 1e10;
        double proj_x, proj_y, dist_sq;

        // Always check all three edges and pick the nearest
        // Edge v2-v3 (white-black edge)
        dist_sq = project_to_segment(x, y, x2, y2, x3, y3, &proj_x, &proj_y);
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_x = proj_x;
            best_y = proj_y;
        }

        // Edge v1-v3 (hue-black edge)
        dist_sq = project_to_segment(x, y, x1, y1, x3, y3, &proj_x, &proj_y);
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_x = proj_x;
            best_y = proj_y;
        }

        // Edge v1-v2 (hue-white edge)
        dist_sq = project_to_segment(x, y, x1, y1, x2, y2, &proj_x, &proj_y);
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_x = proj_x;
            best_y = proj_y;
        }

        // Recalculate barycentric for projected point
        w1 = ((y2 - y3) * (best_x - x3) + (x3 - x2) * (best_y - y3)) / denom;
        w2 = ((y3 - y1) * (best_x - x3) + (x1 - x3) * (best_y - y3)) / denom;
        w3 = 1.0 - w1 - w2;

        // Clean up floating point errors
        if (w1 < 0)
            w1 = 0;
        if (w2 < 0)
            w2 = 0;
        if (w3 < 0)
            w3 = 0;
        double sum = w1 + w2 + w3;
        if (sum > 0) {
            w1 /= sum;
            w2 /= sum;
            w3 /= sum;
        }
    }

    // vertex1 = pure hue (L=0.5, S=1)
    // vertex2 = white (L=1, S=0)
    // vertex3 = black (L=0, S=0)
    *l = w1 * 0.5 + w2 * 1.0 + w3 * 0.0;
    *s = w1 * 1.0 + w2 * 0.0 + w3 * 0.0;

    // Final clamp to ensure valid HSV values
    *s = fmax(0, fmin(1, *s));
    *l = fmax(0, fmin(1, *l));
}

// Invalidate cached surfaces
static void invalidate_caches(ColorWheel* wheel) {
    if (wheel->wheel_surface) {
        cairo_surface_destroy(wheel->wheel_surface);
        wheel->wheel_surface = NULL;
    }
    if (wheel->triangle_surface) {
        cairo_surface_destroy(wheel->triangle_surface);
        wheel->triangle_surface = NULL;
    }
}

// Pre-render the hue wheel to a cached surface
static void render_wheel_surface(ColorWheel* wheel) {
    if (wheel->wheel_surface &&
        wheel->cached_width == wheel->width &&
        wheel->cached_height == wheel->height) {
        return; // Already cached and valid
    }

    if (wheel->wheel_surface) {
        cairo_surface_destroy(wheel->wheel_surface);
    }

    wheel->wheel_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                      wheel->width, wheel->height);
    cairo_t* cr = cairo_create(wheel->wheel_surface);

    double cx = wheel->width / 2.0;
    double cy = wheel->height / 2.0;
    double outer_radius = fmin(wheel->width, wheel->height) / 2.0 - 10;
    double inner_radius = outer_radius - 40;

    // Draw color wheel using radial segments
    // Hue 0 (red) at top, increasing clockwise
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    for (double hue = 0; hue < 360; hue += 0.5) {
        double rad = hue_to_angle(hue);
        double rad_next = hue_to_angle(hue + 1.0);
        double r, g, b;
        hsv_to_rgb(hue, 1.0, 1.0, &r, &g, &b);

        cairo_set_source_rgb(cr, r, g, b);
        cairo_move_to(cr, cx + inner_radius * cos(rad), cy + inner_radius * sin(rad));
        cairo_line_to(cr, cx + outer_radius * cos(rad), cy + outer_radius * sin(rad));
        cairo_line_to(cr, cx + outer_radius * cos(rad_next), cy + outer_radius * sin(rad_next));
        cairo_line_to(cr, cx + inner_radius * cos(rad_next), cy + inner_radius * sin(rad_next));
        cairo_close_path(cr);
        cairo_fill(cr);
    }

    cairo_destroy(cr);
    wheel->cached_width = wheel->width;
    wheel->cached_height = wheel->height;
}

// Pre-render the triangle to a cached surface
static void render_triangle_surface(ColorWheel* wheel) {
    if (wheel->triangle_surface &&
        wheel->cached_hue == wheel->hue &&
        wheel->cached_width == wheel->width &&
        wheel->cached_height == wheel->height) {
        return; // Already cached and valid
    }

    if (wheel->triangle_surface) {
        cairo_surface_destroy(wheel->triangle_surface);
    }

    wheel->triangle_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                         wheel->width, wheel->height);

    double x1, y1, x2, y2, x3, y3;
    get_triangle_vertices(wheel, &x1, &y1, &x2, &y2, &x3, &y3);

    // Calculate bounding box
    int min_x = (int)floor(fmin(x1, fmin(x2, x3))) - 1;
    int max_x = (int)ceil(fmax(x1, fmax(x2, x3))) + 1;
    int min_y = (int)floor(fmin(y1, fmin(y2, y3))) - 1;
    int max_y = (int)ceil(fmax(y1, fmax(y2, y3))) + 1;

    // Clamp to surface bounds
    min_x = fmax(0, min_x);
    min_y = fmax(0, min_y);
    max_x = fmin(wheel->width - 1, max_x);
    max_y = fmin(wheel->height - 1, max_y);

    // Get direct access to surface data for fast pixel manipulation
    unsigned char* data = cairo_image_surface_get_data(wheel->triangle_surface);
    int stride = cairo_image_surface_get_stride(wheel->triangle_surface);

    cairo_surface_flush(wheel->triangle_surface);

    // Precompute barycentric coordinate denominators
    double denom = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if (fabs(denom) < 0.001) {
        wheel->cached_hue = wheel->hue;
        return;
    }

    double inv_denom = 1.0 / denom;

    // Precompute barycentric increments for scanline optimization
    double dw1_dx = (y2 - y3) * inv_denom;
    double dw1_dy = (x3 - x2) * inv_denom;
    double dw2_dx = (y3 - y1) * inv_denom;
    double dw2_dy = (x1 - x3) * inv_denom;

    // Draw triangle pixel by pixel using direct memory access
    for (int py = min_y; py <= max_y; py++) {
        // Calculate initial barycentric coordinates for this scanline
        double px_start = min_x + 0.5;
        double py_center = py + 0.5;

        double w1_base = ((y2 - y3) * (px_start - x3) + (x3 - x2) * (py_center - y3)) * inv_denom;
        double w2_base = ((y3 - y1) * (px_start - x3) + (x1 - x3) * (py_center - y3)) * inv_denom;

        unsigned char* row = data + py * stride;

        for (int px = min_x; px <= max_x; px++) {
            double w1 = w1_base + (px - min_x) * dw1_dx;
            double w2 = w2_base + (px - min_x) * dw2_dx;
            double w3 = 1.0 - w1 - w2;

            // Check if inside triangle (with small tolerance for anti-aliasing at edges)
            if (w1 >= -0.01 && w2 >= -0.01 && w3 >= -0.01) {
                // Clamp for edge pixels
                double cw1 = fmax(0, w1);
                double cw2 = fmax(0, w2);
                double cw3 = fmax(0, w3);
                double sum = cw1 + cw2 + cw3;
                if (sum > 0) {
                    cw1 /= sum;
                    cw2 /= sum;
                    cw3 /= sum;
                }

                // Map barycentric to HSV:
                // vertex1 (x1,y1) = pure hue (S=1, V=1)
                // vertex2 (x2,y2) = white (S=0, V=1)
                // vertex3 (x3,y3) = black (S=1, V=0)
                double v = cw1 * 1.0 + cw2 * 1.0 + cw3 * 0.0;
                double s = cw1 * 1.0 + cw2 * 0.0 + cw3 * 1.0;

                double r, g, b;
                hsv_to_rgb(wheel->hue, s, v, &r, &g, &b);

                // Calculate alpha for edge anti-aliasing
                double alpha = 1.0;
                double edge_dist = fmin(w1, fmin(w2, w3));
                if (edge_dist < 0) {
                    alpha = fmax(0, 1.0 + edge_dist * 100); // Smooth edge falloff
                }

                // Write ARGB (cairo uses pre-multiplied alpha)
                unsigned char a = (unsigned char)(alpha * 255);
                unsigned char rb = (unsigned char)(r * alpha * 255);
                unsigned char gb = (unsigned char)(g * alpha * 255);
                unsigned char bb = (unsigned char)(b * alpha * 255);

                // ARGB32 format: bytes are B, G, R, A (little-endian)
                row[px * 4 + 0] = bb;
                row[px * 4 + 1] = gb;
                row[px * 4 + 2] = rb;
                row[px * 4 + 3] = a;
            }
        }
    }

    cairo_surface_mark_dirty(wheel->triangle_surface);
    wheel->cached_hue = wheel->hue;
    wheel->cached_width = wheel->width;
    wheel->cached_height = wheel->height;
}

static gboolean on_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    ColorWheel* wheel = (ColorWheel*)data;

    int new_width = gtk_widget_get_allocated_width(widget);
    int new_height = gtk_widget_get_allocated_height(widget);

    // Invalidate caches if size changed
    if (wheel->width != new_width || wheel->height != new_height) {
        invalidate_caches(wheel);
        wheel->width = new_width;
        wheel->height = new_height;
    }

    // Render and draw cached wheel surface
    render_wheel_surface(wheel);
    if (wheel->wheel_surface) {
        cairo_set_source_surface(cr, wheel->wheel_surface, 0, 0);
        cairo_paint(cr);
    }

    // Render and draw cached triangle surface
    render_triangle_surface(wheel);
    if (wheel->triangle_surface) {
        cairo_set_source_surface(cr, wheel->triangle_surface, 0, 0);
        cairo_paint(cr);
    }

    // Draw triangle outline for clarity
    // double x1, y1, x2, y2, x3, y3;
    // get_triangle_vertices(wheel, &x1, &y1, &x2, &y2, &x3, &y3);

    // cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
    // cairo_set_line_width(cr, 1);
    // cairo_move_to(cr, x1, y1);
    // cairo_line_to(cr, x2, y2);
    // cairo_line_to(cr, x3, y3);
    // cairo_close_path(cr);
    // cairo_stroke(cr);

    // Draw wheel indicator (hue selector)
    double cx = wheel->width / 2.0;
    double cy = wheel->height / 2.0;
    double outer_radius = fmin(wheel->width, wheel->height) / 2.0 - 10;
    double inner_radius = outer_radius - 40;

    double hue_rad = hue_to_angle(wheel->hue);
    double ind_radius = (outer_radius + inner_radius) / 2.0;
    double hue_x = cx + ind_radius * cos(hue_rad);
    double hue_y = cy + ind_radius * sin(hue_rad);

    // Outer ring (black)
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 3);
    cairo_arc(cr, hue_x, hue_y, 8, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Inner ring (white normally, blue when dragging)
    if (wheel->dragging_wheel) {
        cairo_set_source_rgb(cr, 35.0 / 255.0, 135.0 / 255.0, 215.0 / 255.0);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
    }
    cairo_set_line_width(cr, 1.5);
    cairo_arc(cr, hue_x, hue_y, 8, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Draw triangle indicator (saturation/lightness selector)
    double tri_x, tri_y;
    sl_to_triangle(wheel, wheel->saturation, wheel->value, &tri_x, &tri_y);

    // Outer ring (black)
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 3);
    cairo_arc(cr, tri_x, tri_y, 7, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Inner ring (white normally, blue when dragging)
    if (wheel->dragging_triangle) {
        cairo_set_source_rgb(cr, 35.0 / 255.0, 135.0 / 255.0, 215.0 / 255.0);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
    }
    cairo_set_line_width(cr, 1.5);
    cairo_arc(cr, tri_x, tri_y, 7, 0, 2 * M_PI);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ColorWheel* wheel = (ColorWheel*)data;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double cx = width / 2.0;
    double cy = height / 2.0;
    double dx = event->x - cx;
    double dy = event->y - cy;
    double dist = sqrt(dx * dx + dy * dy);
    double outer_radius = fmin(width, height) / 2.0 - 10;
    double inner_radius = outer_radius - 40;

    if (dist >= inner_radius && dist <= outer_radius) {
        wheel->dragging_wheel = TRUE;
        wheel->hue = angle_to_hue(atan2(dy, dx));
        gtk_widget_queue_draw(widget);
    } else if (dist < inner_radius) {
        wheel->dragging_triangle = TRUE;
        triangle_to_sl(wheel, event->x, event->y, &wheel->saturation, &wheel->value);
        gtk_widget_queue_draw(widget);
    }

    return TRUE;
}

static gboolean on_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)event;
    ColorWheel* wheel = (ColorWheel*)data;

    // Only redraw if we were actually dragging something
    if (wheel->dragging_wheel || wheel->dragging_triangle) {
        wheel->dragging_wheel = FALSE;
        wheel->dragging_triangle = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return TRUE;
}

static gboolean on_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    ColorWheel* wheel = (ColorWheel*)data;

    if (wheel->dragging_wheel) {
        int width = gtk_widget_get_allocated_width(widget);
        int height = gtk_widget_get_allocated_height(widget);
        double cx = width / 2.0;
        double cy = height / 2.0;
        double dx = event->x - cx;
        double dy = event->y - cy;
        wheel->hue = angle_to_hue(atan2(dy, dx));
        gtk_widget_queue_draw(widget);
    } else if (wheel->dragging_triangle) {
        triangle_to_sl(wheel, event->x, event->y, &wheel->saturation, &wheel->value);
        gtk_widget_queue_draw(widget);
    }

    // Calculate distances for cursor detection and tooltip
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    // Update wheel dimensions for triangle check (need current width/height)
    wheel->width = width;
    wheel->height = height;

    double cx = width / 2.0;
    double cy = height / 2.0;
    double dx = event->x - cx;
    double dy = event->y - cy;
    double dist = sqrt(dx * dx + dy * dy);
    double outer_radius = fmin(width, height) / 2.0 - 10;
    double inner_radius = outer_radius - 40;

    // Set hand cursor when hovering over interactive areas, default cursor otherwise
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        GdkDisplay* display = gtk_widget_get_display(widget);
        GdkCursor* cursor = NULL;

        if (dist >= inner_radius && dist <= outer_radius) {
            // Over hue wheel
            cursor = gdk_cursor_new_from_name(display, "pointer");
        } else if (is_point_in_triangle(wheel, event->x, event->y)) {
            // Over triangle (check if actually inside triangle, not just inner radius)
            cursor = gdk_cursor_new_from_name(display, "pointer");
        }
        // If cursor is NULL, we set default cursor (NULL = default)

        gdk_window_set_cursor(window, cursor);
        if (cursor) {
            g_object_unref(cursor);
        }
    }

    // Update tooltip with color info
    double r, g, b;
    if (dist >= inner_radius && dist <= outer_radius) {
        double h = angle_to_hue(atan2(dy, dx));
        hsv_to_rgb(h, 1.0, 1.0, &r, &g, &b);
    } else {
        double s, v;
        triangle_to_sl(wheel, event->x, event->y, &s, &v);
        hsv_to_rgb(wheel->hue, s, v, &r, &g, &b);
    }

    char tooltip[100];
    snprintf(tooltip, sizeof(tooltip), "#%02X%02X%02X\nRGB(%d, %d, %d)\nHSL(%.0f°, %.0f%%, %.0f%%)",
             (int)(r * 255), (int)(g * 255), (int)(b * 255),
             (int)(r * 255), (int)(g * 255), (int)(b * 255),
             wheel->hue, wheel->saturation * 100, wheel->value * 100);
    gtk_widget_set_tooltip_text(widget, tooltip);

    return TRUE;
}

static gboolean on_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer data) {
    ColorWheel* wheel = (ColorWheel*)data;

    // Check if entering over an interactive area and set cursor accordingly
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double cx = width / 2.0;
    double cy = height / 2.0;
    double dx = event->x - cx;
    double dy = event->y - cy;
    double dist = sqrt(dx * dx + dy * dy);
    double outer_radius = fmin(width, height) / 2.0 - 10;
    double inner_radius = outer_radius - 40;

    // Update wheel dimensions for triangle check (need current width/height)
    wheel->width = width;
    wheel->height = height;

    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        GdkDisplay* display = gtk_widget_get_display(widget);
        GdkCursor* cursor = NULL;

        if (dist >= inner_radius && dist <= outer_radius) {
            // Over hue wheel
            cursor = gdk_cursor_new_from_name(display, "pointer");
        } else if (is_point_in_triangle(wheel, event->x, event->y)) {
            // Over triangle (check if actually inside triangle, not just inner radius)
            cursor = gdk_cursor_new_from_name(display, "pointer");
        }
        // If cursor is NULL, we set default cursor (NULL = default)

        gdk_window_set_cursor(window, cursor);
        if (cursor) {
            g_object_unref(cursor);
        }
    }

    return FALSE;
}

static gboolean on_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer data) {
    (void)event;
    (void)data;

    // Reset to default cursor on leave
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        gdk_window_set_cursor(window, NULL);
    }

    return FALSE;
}

// Public API implementation

ColorWheel* color_wheel_new(void) {
    ColorWheel* wheel = g_malloc0(sizeof(ColorWheel));
    wheel->hue = 240; // Start with blue (at top of wheel)
    wheel->saturation = 1.0;
    wheel->value = 0.5;
    wheel->dragging_wheel = FALSE;
    wheel->dragging_triangle = FALSE;
    wheel->width = 300;
    wheel->height = 300;
    wheel->wheel_surface = NULL;
    wheel->triangle_surface = NULL;
    wheel->cached_hue = -1; // Force initial render
    wheel->cached_width = 0;
    wheel->cached_height = 0;

    wheel->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(wheel->drawing_area, 300, 300);
    gtk_widget_set_has_tooltip(wheel->drawing_area, TRUE);

    gtk_widget_add_events(wheel->drawing_area,
                          GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_ENTER_NOTIFY_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(wheel->drawing_area, "draw", G_CALLBACK(on_draw), wheel);
    g_signal_connect(wheel->drawing_area, "button-press-event", G_CALLBACK(on_button_press), wheel);
    g_signal_connect(wheel->drawing_area, "button-release-event", G_CALLBACK(on_button_release), wheel);
    g_signal_connect(wheel->drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), wheel);
    g_signal_connect(wheel->drawing_area, "enter-notify-event", G_CALLBACK(on_enter_notify), wheel);
    g_signal_connect(wheel->drawing_area, "leave-notify-event", G_CALLBACK(on_leave_notify), wheel);

    return wheel;
}

void color_wheel_free(ColorWheel* wheel) {
    if (wheel) {
        invalidate_caches(wheel);
        g_free(wheel);
    }
}

GtkWidget* color_wheel_get_widget(ColorWheel* wheel) {
    return wheel->drawing_area;
}

void color_wheel_get_rgb(ColorWheel* wheel, double* r, double* g, double* b) {
    hsv_to_rgb(wheel->hue, wheel->saturation, wheel->value, r, g, b);
}

void color_wheel_set_rgb(ColorWheel* wheel, double r, double g, double b) {
    rgb_to_hsv(r, g, b, &wheel->hue, &wheel->saturation, &wheel->value);
    gtk_widget_queue_draw(wheel->drawing_area);
}

void color_wheel_get_hsv(ColorWheel* wheel, double* h, double* s, double* v) {
    if (!wheel)
        return;
    if (h)
        *h = wheel->hue;
    if (s)
        *s = wheel->saturation;
    if (v)
        *v = wheel->value;
}

void color_wheel_set_hsv(ColorWheel* wheel, double h, double s, double v) {
    if (!wheel)
        return;
    wheel->hue = h;
    wheel->saturation = s;
    wheel->value = v;
    if (wheel->drawing_area)
        gtk_widget_queue_draw(wheel->drawing_area);
}
