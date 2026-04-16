/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_H
#define GRADIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Blend function applied between two adjacent gradient endpoints within a segment.
 * Maps to GIMP's segment blend types; GRD files always use LINEAR.
 */
typedef enum {
    GRADIENT_BLEND_LINEAR = 0,
    GRADIENT_BLEND_EASE = 1, /* curved ease-in/ease-out */
    GRADIENT_BLEND_SINUSOIDAL = 2,
    GRADIENT_BLEND_SPHERICAL_INC = 3, /* spherical arc, concave up */
    GRADIENT_BLEND_SPHERICAL_DEC = 4, /* spherical arc, concave down */
    GRADIENT_BLEND_STEP = 5           /* hard cut at midpoint */
} GradientBlendMode;

/**
 * Color space used for interpolating colors within a segment.
 * Maps to GIMP's coloring types; GRD files interpolate in RGB.
 */
typedef enum {
    GRADIENT_COLOR_RGB = 0,
    GRADIENT_COLOR_HSV_CCW = 1, /* hue counter-clockwise */
    GRADIENT_COLOR_HSV_CW = 2   /* hue clockwise */
} GradientColorSpace;

/**
 * Special endpoint type for dynamic foreground/background colors.
 * FIXED means the literal RGBA stored in the segment is used.
 */
typedef enum {
    GRADIENT_ENDPOINT_FIXED = 0,
    GRADIENT_ENDPOINT_FOREGROUND = 1,
    GRADIENT_ENDPOINT_FOREGROUND_TRANSPARENT = 2,
    GRADIENT_ENDPOINT_BACKGROUND = 3,
    GRADIENT_ENDPOINT_BACKGROUND_TRANSPARENT = 4
} GradientEndpointType;

/**
 * A single contiguous segment of a gradient.
 *
 * Segments tile over [0.0, 1.0] without gaps: segment[i].right_pos must equal
 * segment[i+1].left_pos.  The midpoint biases where the 50% blend point falls
 * within the segment (range: [left_pos, right_pos]).
 *
 * GGR maps 1:1 to this struct.
 * GRD color + transparency stops are converted to segments on load.
 */
typedef struct {
    double left_pos;  /* [0.0, 1.0] start of segment */
    double midpoint;  /* [0.0, 1.0] blend midpoint position */
    double right_pos; /* [0.0, 1.0] end of segment */

    /* Left endpoint color, RGBA in [0.0, 1.0] */
    double left_r;
    double left_g;
    double left_b;
    double left_a;

    /* Right endpoint color, RGBA in [0.0, 1.0] */
    double right_r;
    double right_g;
    double right_b;
    double right_a;

    GradientBlendMode blend_mode;
    GradientColorSpace color_space;
    GradientEndpointType left_type;
    GradientEndpointType right_type;
} GradientSegment;

/**
 * An opacity stop from a Photoshop .grd file.
 *
 * GRD files store color and transparency as independent stop lists.
 * These are preserved here for lossless round-tripping; gradient_evaluate()
 * composites them at evaluation time.  For GGR files this array is NULL.
 */
typedef struct {
    double position; /* [0.0, 1.0] */
    double midpoint; /* [0.0, 1.0] midpoint between this and the next stop */
    double opacity;  /* [0.0, 1.0] */
} GradientTransparencyStop;

/**
 * A single named gradient.
 *
 * segments is the primary representation used for evaluation.
 * transparency_stops (if non-NULL) is an optional Photoshop-specific overlay
 * whose alpha is multiplied into the evaluated color during rendering.
 */
typedef struct {
    char* name;

    int num_segments;
    GradientSegment* segments;

    int num_transparency_stops; /* 0 for GGR */
    GradientTransparencyStop* transparency_stops;

    int smoothness; /* [0, 4096], preserved from GRD for round-tripping */
} GradientDef;

/**
 * A collection of gradients loaded from a single file.
 * GGR files always contain exactly one gradient; GRD files may contain many.
 */
typedef struct {
    int num_gradients;
    GradientDef* gradients;
} GradientSet;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * Allocate an empty GradientSet with the given number of gradient slots.
 * All GradientDef members are zeroed; caller must populate them.
 * @return Newly allocated GradientSet, or NULL on out-of-memory.
 */
GradientSet* gradient_set_new(int num_gradients);

/**
 * Free all memory owned by a GradientSet, including all GradientDefs inside.
 * Safe to call with NULL.
 */
void gradient_set_free(GradientSet* set);

/**
 * Free all memory owned by a GradientDef (name, segments, transparency_stops).
 * Does NOT free the GradientDef pointer itself.
 */
void gradient_def_free(GradientDef* def);

/* -------------------------------------------------------------------------
 * Evaluation
 * ---------------------------------------------------------------------- */

/**
 * Evaluate a gradient at a given position and output RGBA components.
 *
 * @param def       Gradient to evaluate (must not be NULL)
 * @param position  Normalized position in [0.0, 1.0]
 * @param out_r     Output red   [0.0, 1.0]
 * @param out_g     Output green [0.0, 1.0]
 * @param out_b     Output blue  [0.0, 1.0]
 * @param out_a     Output alpha [0.0, 1.0]
 *
 * If def has no segments the outputs are set to 0.0.
 * Positions outside [0.0, 1.0] are clamped to the nearest endpoint.
 */
void gradient_evaluate(const GradientDef* def, double position,
                       double* out_r, double* out_g, double* out_b, double* out_a);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_H */
