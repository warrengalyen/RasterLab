#ifndef RGB_SCALE_H
#define RGB_SCALE_H

#include <gtk/gtk.h>

typedef enum {
    RGB_SCALE_RED,
    RGB_SCALE_GREEN,
    RGB_SCALE_BLUE
} RgbScaleType;

typedef struct _RgbScale RgbScale;
typedef struct _RgbScaleClass RgbScaleClass;

#define RGB_SCALE_TYPE (rgb_scale_get_type())
#define RGB_SCALE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), RGB_SCALE_TYPE, RgbScale))
#define RGB_IS_SCALE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), RGB_SCALE_TYPE))

GType rgb_scale_get_type(void);

// Create a new RGB scale widget
GtkWidget* rgb_scale_new(RgbScaleType type);

// Set the reference RGB values (for gradient rendering)
void rgb_scale_set_rgb(RgbScale* scale, double r, double g, double b);

// Get current RGB values
void rgb_scale_get_rgb(RgbScale* scale, double* r, double* g, double* b);

// Set the current value
void rgb_scale_set_value(RgbScale* scale, double value);

// Get the current value
double rgb_scale_get_value(RgbScale* scale);

#endif /* RGB_SCALE_H */
