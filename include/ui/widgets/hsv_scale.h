/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef HSV_SCALE_H
#define HSV_SCALE_H

#include <gtk/gtk.h>

typedef enum {
    HSV_SCALE_HUE,
    HSV_SCALE_SATURATION,
    HSV_SCALE_VALUE
} HsvScaleType;

typedef struct _HsvScale HsvScale;
typedef struct _HsvScaleClass HsvScaleClass;

#define HSV_SCALE_TYPE (hsv_scale_get_type())
#define HSV_SCALE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), HSV_SCALE_TYPE, HsvScale))
#define HSV_IS_SCALE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), HSV_SCALE_TYPE))

GType hsv_scale_get_type(void);

// Create a new HSV scale widget
GtkWidget* hsv_scale_new(HsvScaleType type, double min, double max);

// Set the reference HSV values (for gradient rendering)
void hsv_scale_set_hsv(HsvScale* scale, double h, double s, double v);

// Get current HSV values
void hsv_scale_get_hsv(HsvScale* scale, double* h, double* s, double* v);

// Set the current value
void hsv_scale_set_value(HsvScale* scale, double value);

// Get the current value
double hsv_scale_get_value(HsvScale* scale);

#endif /* HSV_SCALE_H */
