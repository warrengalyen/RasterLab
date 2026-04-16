/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "gradient.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static double clamp_d(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Apply blend function to a normalized t in [0,1] using the midpoint. */
static double apply_blend(double t, double midpoint, GradientBlendMode mode) {
    /* Remap t so the 50% point falls at midpoint fraction within segment.
     * The midpoint value is given as an absolute position in [left_pos, right_pos];
     * callers must normalize it to [0,1] before passing here. */
    double mid = clamp_d(midpoint, 0.0, 1.0);

    double u;
    if (t <= mid) {
        u = (mid > 0.0) ? (t / mid) * 0.5 : 0.0;
    } else {
        u = (mid < 1.0) ? 0.5 + ((t - mid) / (1.0 - mid)) * 0.5 : 1.0;
    }

    switch (mode) {
        case GRADIENT_BLEND_LINEAR:
            return u;
        case GRADIENT_BLEND_EASE: {
            /* Smooth S-curve: 3u^2 - 2u^3 */
            return u * u * (3.0 - 2.0 * u);
        }
        case GRADIENT_BLEND_SINUSOIDAL:
            return (sin((-M_PI / 2.0) + M_PI * u) + 1.0) / 2.0;
        case GRADIENT_BLEND_SPHERICAL_INC:
            return sqrt(1.0 - (u - 1.0) * (u - 1.0));
        case GRADIENT_BLEND_SPHERICAL_DEC:
            return 1.0 - sqrt(1.0 - u * u);
        case GRADIENT_BLEND_STEP:
            return u < 0.5 ? 0.0 : 1.0;
        default:
            return u;
    }
}

/* Convert HSV to RGB.  All values in [0.0, 1.0]. */
static void hsv_to_rgb(double h, double s, double v,
                        double* out_r, double* out_g, double* out_b) {
    if (s <= 0.0) {
        *out_r = *out_g = *out_b = v;
        return;
    }
    double hh = h * 6.0;
    if (hh >= 6.0) hh = 0.0;
    int i = (int)hh;
    double f  = hh - (double)i;
    double p  = v * (1.0 - s);
    double q  = v * (1.0 - s * f);
    double t2 = v * (1.0 - s * (1.0 - f));
    switch (i) {
        case 0: *out_r = v; *out_g = t2; *out_b = p; break;
        case 1: *out_r = q; *out_g = v;  *out_b = p; break;
        case 2: *out_r = p; *out_g = v;  *out_b = t2; break;
        case 3: *out_r = p; *out_g = q;  *out_b = v; break;
        case 4: *out_r = t2; *out_g = p; *out_b = v; break;
        default: *out_r = v; *out_g = p; *out_b = q; break;
    }
}

/* Convert RGB to HSV.  All values in [0.0, 1.0]. */
static void rgb_to_hsv(double r, double g, double b,
                        double* out_h, double* out_s, double* out_v) {
    double max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double delta = max - min;
    *out_v = max;
    *out_s = (max > 0.0) ? delta / max : 0.0;
    if (delta == 0.0) {
        *out_h = 0.0;
        return;
    }
    if (max == r)      *out_h = (g - b) / delta + (g < b ? 6.0 : 0.0);
    else if (max == g) *out_h = (b - r) / delta + 2.0;
    else               *out_h = (r - g) / delta + 4.0;
    *out_h /= 6.0;
}

/* Interpolate two RGBA endpoints according to the segment's color space. */
static void interpolate_color(const GradientSegment* seg, double blend,
                               double* out_r, double* out_g, double* out_b, double* out_a) {
    double lr = seg->left_r,  lg = seg->left_g,  lb = seg->left_b,  la = seg->left_a;
    double rr = seg->right_r, rg = seg->right_g, rb = seg->right_b, ra = seg->right_a;

    *out_a = la + blend * (ra - la);

    switch (seg->color_space) {
        case GRADIENT_COLOR_RGB:
            *out_r = lr + blend * (rr - lr);
            *out_g = lg + blend * (rg - lg);
            *out_b = lb + blend * (rb - lb);
            break;
        case GRADIENT_COLOR_HSV_CCW:
        case GRADIENT_COLOR_HSV_CW: {
            double lh, ls, lv, rh, rs, rv;
            rgb_to_hsv(lr, lg, lb, &lh, &ls, &lv);
            rgb_to_hsv(rr, rg, rb, &rh, &rs, &rv);

            /* Choose hue interpolation direction */
            double dh = rh - lh;
            if (seg->color_space == GRADIENT_COLOR_HSV_CCW) {
                if (dh < 0.0) dh += 1.0;
            } else {
                if (dh > 0.0) dh -= 1.0;
            }
            double h = lh + blend * dh;
            if (h < 0.0) h += 1.0;
            if (h > 1.0) h -= 1.0;
            double s = ls + blend * (rs - ls);
            double v = lv + blend * (rv - lv);
            hsv_to_rgb(h, s, v, out_r, out_g, out_b);
            break;
        }
        default:
            *out_r = lr + blend * (rr - lr);
            *out_g = lg + blend * (rg - lg);
            *out_b = lb + blend * (rb - lb);
            break;
    }
}

/* Evaluate transparency stops and return opacity at position. */
static double evaluate_transparency(const GradientDef* def, double position) {
    if (!def->transparency_stops || def->num_transparency_stops == 0)
        return 1.0;
    if (def->num_transparency_stops == 1)
        return def->transparency_stops[0].opacity;

    /* Find enclosing pair */
    int n = def->num_transparency_stops;
    const GradientTransparencyStop* stops = def->transparency_stops;

    if (position <= stops[0].position)    return stops[0].opacity;
    if (position >= stops[n - 1].position) return stops[n - 1].opacity;

    for (int i = 0; i < n - 1; i++) {
        if (position >= stops[i].position && position <= stops[i + 1].position) {
            double span = stops[i + 1].position - stops[i].position;
            double t = (span > 0.0) ? (position - stops[i].position) / span : 0.0;
            /* Normalize midpoint to [0,1] within this span */
            double mid_norm = (span > 0.0)
                ? (stops[i].midpoint - stops[i].position) / span
                : 0.5;
            mid_norm = clamp_d(mid_norm, 0.0, 1.0);
            double blend = apply_blend(t, mid_norm, GRADIENT_BLEND_LINEAR);
            return stops[i].opacity + blend * (stops[i + 1].opacity - stops[i].opacity);
        }
    }
    return 1.0;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

GradientSet* gradient_set_new(int num_gradients) {
    if (num_gradients < 0) return NULL;
    GradientSet* set = calloc(1, sizeof(GradientSet));
    if (!set) return NULL;
    if (num_gradients > 0) {
        set->gradients = calloc((size_t)num_gradients, sizeof(GradientDef));
        if (!set->gradients) {
            free(set);
            return NULL;
        }
    }
    set->num_gradients = num_gradients;
    return set;
}

void gradient_def_free(GradientDef* def) {
    if (!def) return;
    free(def->name);
    free(def->segments);
    free(def->transparency_stops);
    def->name                  = NULL;
    def->segments              = NULL;
    def->transparency_stops    = NULL;
    def->num_segments          = 0;
    def->num_transparency_stops = 0;
}

void gradient_set_free(GradientSet* set) {
    if (!set) return;
    for (int i = 0; i < set->num_gradients; i++) {
        gradient_def_free(&set->gradients[i]);
    }
    free(set->gradients);
    free(set);
}

/* -------------------------------------------------------------------------
 * Evaluation
 * ---------------------------------------------------------------------- */

void gradient_evaluate(const GradientDef* def, double position,
                        double* out_r, double* out_g, double* out_b, double* out_a) {
    if (!def || !out_r || !out_g || !out_b || !out_a) return;

    *out_r = *out_g = *out_b = *out_a = 0.0;

    if (!def->segments || def->num_segments == 0) return;

    position = clamp_d(position, 0.0, 1.0);

    /* Find the segment that contains this position */
    const GradientSegment* seg = NULL;
    for (int i = 0; i < def->num_segments; i++) {
        if (position <= def->segments[i].right_pos) {
            seg = &def->segments[i];
            break;
        }
    }
    /* Fallback to last segment if position == 1.0 exactly */
    if (!seg) {
        seg = &def->segments[def->num_segments - 1];
    }

    double span = seg->right_pos - seg->left_pos;
    double t    = (span > 0.0) ? (position - seg->left_pos) / span : 0.0;

    /* Normalize midpoint to [0,1] within this segment's span */
    double mid_norm = (span > 0.0)
        ? (seg->midpoint - seg->left_pos) / span
        : 0.5;
    mid_norm = clamp_d(mid_norm, 0.0, 1.0);

    double blend = apply_blend(t, mid_norm, seg->blend_mode);
    interpolate_color(seg, blend, out_r, out_g, out_b, out_a);

    /* Multiply by transparency stops if present (GRD overlay) */
    if (def->transparency_stops && def->num_transparency_stops > 0) {
        double ts_alpha = evaluate_transparency(def, position);
        *out_a *= ts_alpha;
    }

    *out_r = clamp_d(*out_r, 0.0, 1.0);
    *out_g = clamp_d(*out_g, 0.0, 1.0);
    *out_b = clamp_d(*out_b, 0.0, 1.0);
    *out_a = clamp_d(*out_a, 0.0, 1.0);
}
