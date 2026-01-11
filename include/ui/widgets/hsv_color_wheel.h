#ifndef COLOR_WHEEL_H
#define COLOR_WHEEL_H

#include <gtk/gtk.h>

typedef struct {
    GtkWidget* drawing_area;
    double hue;        // 0-360
    double saturation; // 0-1
    double value;      // 0-1
    gboolean dragging_wheel;
    gboolean dragging_triangle;
    int width;
    int height;
    // Cached surfaces for performance
    cairo_surface_t* wheel_surface;
    cairo_surface_t* triangle_surface;
    double cached_hue; // Hue when triangle was last rendered
    int cached_width;
    int cached_height;
} ColorWheel;

// Create a new color wheel widget
ColorWheel* color_wheel_new(void);

// Free the color wheel
void color_wheel_free(ColorWheel* wheel);

// Get the drawing area widget
GtkWidget* color_wheel_get_widget(ColorWheel* wheel);

// Get current color in RGB (0-1 range)
void color_wheel_get_rgb(ColorWheel* wheel, double* r, double* g, double* b);

// Set color from RGB (0-1 range)
void color_wheel_set_rgb(ColorWheel* wheel, double r, double g, double b);

// Get current HSV values
void color_wheel_get_hsv(ColorWheel* wheel, double* h, double* s, double* v);

// Set HSV values
void color_wheel_set_hsv(ColorWheel* wheel, double h, double s, double v);

#endif /* COLOR_WHEEL_H */