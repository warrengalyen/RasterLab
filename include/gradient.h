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

/* -------------------------------------------------------------------------
 * UI Preview texture
 * ---------------------------------------------------------------------- */

/**
 * Dimensions of the precomputed UI preview texture.
 * Adjust these constants to resize all gradient swatches application-wide.
 */
#define GRADIENT_PREVIEW_WIDTH  198
#define GRADIENT_PREVIEW_HEIGHT  44

/**
 * Precomputed RGBA8 preview texture for a gradient.
 *
 * Pixels are stored row-major, top-to-bottom, with 4 bytes per pixel in
 * RGBA order (straight, non-premultiplied alpha).  The stride is always
 * width * 4 bytes (tightly packed, no padding).
 *
 * gradient_preview_get() generates this lazily on the first call and after
 * every gradient_preview_invalidate() call.
 */
typedef struct {
    int      width;           /* GRADIENT_PREVIEW_WIDTH  at build time */
    int      height;          /* GRADIENT_PREVIEW_HEIGHT at build time */
    uint8_t* pixels;          /* width * height * 4 bytes, RGBA8 */
} GradientPreview;

/* -------------------------------------------------------------------------
 * LUT (Look-Up Table) cache
 * ---------------------------------------------------------------------- */

/**
 * Default number of entries used when gradient_lut_build() is called with
 * resolution == 0.  512 entries gives ~0.2% positional error while keeping
 * the table to 8 KiB (512 x 4 channels x 4 bytes).
 */
#define GRADIENT_LUT_DEFAULT_RESOLUTION 512

/**
 * Precomputed RGBA look-up table for a gradient.
 *
 * Entries are packed as interleaved floats: [R0,G0,B0,A0, R1,G1,B1,A1, ...]
 * covering evenly-spaced positions from 0.0 to 1.0 inclusive.  All component
 * values are in [0.0f, 1.0f].
 *
 * gradient_lut_evaluate() performs linear interpolation between adjacent
 * entries, so sub-resolution accuracy is preserved.
 *
 * The LUT is not thread-safe: do not call gradient_lut_build() or
 * gradient_lut_invalidate() concurrently with gradient_lut_evaluate().
 */
typedef struct {
    int    resolution; /* number of table entries; always >= 2 */
    float* entries;    /* resolution x 4 floats: R, G, B, A per entry */
} GradientLUT;

/**
 * A single named gradient.
 *
 * segments is the primary representation used for evaluation.
 * transparency_stops (if non-NULL) is an optional Photoshop-specific overlay
 * whose alpha is multiplied into the evaluated color during rendering.
 *
 * Both cache subsystems (LUT and preview) are managed by their respective
 * gradient_lut_*() / gradient_preview_*() functions.  Call gradient_invalidate()
 * to flush both when gradient data has been edited.  All cache fields should
 * be treated as opaque by callers.
 */
typedef struct {
    char* name;

    int num_segments;
    GradientSegment* segments;

    int num_transparency_stops; /* 0 for GGR */
    GradientTransparencyStop* transparency_stops;

    int smoothness; /* [0, 4096], preserved from GRD for round-tripping */

    /* --- LUT cache (managed by gradient_lut_*()) --- */
    GradientLUT* lut;            /* precomputed float table; NULL = not yet built */
    bool         lut_dirty;      /* true = rebuild required on next evaluate */
    int          lut_resolution; /* 0 = use GRADIENT_LUT_DEFAULT_RESOLUTION */

    /* --- UI preview cache (managed by gradient_preview_*()) --- */
    GradientPreview* preview;       /* RGBA8 swatch texture; NULL = not yet built */
    bool             preview_dirty; /* true = rebuild required on next get */
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

/* -------------------------------------------------------------------------
 * LUT cache API
 * ---------------------------------------------------------------------- */

/**
 * Build (or rebuild) the precomputed LUT for a gradient.
 *
 * Calls gradient_evaluate() for each of the @p resolution evenly-spaced
 * positions and stores the results as packed floats.  Any previously cached
 * LUT is freed before the new one is allocated.
 *
 * @param def        Gradient to build the LUT for (must not be NULL)
 * @param resolution Number of table entries.  Pass 0 to use
 *                   GRADIENT_LUT_DEFAULT_RESOLUTION.  Values < 2 are clamped
 *                   to 2; values > 65536 are clamped to 65536.
 * @return true on success, false on out-of-memory.
 */
bool gradient_lut_build(GradientDef* def, int resolution);

/**
 * Mark the LUT cache as dirty and free the currently cached table.
 *
 * The next call to gradient_lut_evaluate() will rebuild the LUT lazily using
 * the resolution stored in def->lut_resolution (or GRADIENT_LUT_DEFAULT_RESOLUTION
 * if that is 0).  Call this whenever gradient data (segments, transparency
 * stops) has been modified after the LUT was last built.
 *
 * Safe to call when no LUT has been built yet.
 *
 * @param def  Gradient to invalidate (must not be NULL)
 */
void gradient_lut_invalidate(GradientDef* def);

/**
 * Set the desired LUT resolution and mark the LUT dirty for a lazy rebuild.
 *
 * The new resolution takes effect on the next gradient_lut_evaluate() call.
 * Pass 0 to reset to GRADIENT_LUT_DEFAULT_RESOLUTION.
 *
 * @param def        Gradient to configure (must not be NULL)
 * @param resolution Desired number of table entries [0, 65536]
 */
void gradient_lut_set_resolution(GradientDef* def, int resolution);

/**
 * Evaluate a gradient using its precomputed LUT with linear interpolation
 * between adjacent entries.
 *
 * The LUT is built lazily on the first call and after every
 * gradient_lut_invalidate() call.  If the lazy build fails (out-of-memory)
 * the function falls back to gradient_evaluate() transparently.
 *
 * @param def       Gradient to evaluate (must not be NULL)
 * @param position  Normalized position in [0.0, 1.0]
 * @param out_r     Output red   [0.0, 1.0]
 * @param out_g     Output green [0.0, 1.0]
 * @param out_b     Output blue  [0.0, 1.0]
 * @param out_a     Output alpha [0.0, 1.0]
 */
void gradient_lut_evaluate(const GradientDef* def, double position,
                             double* out_r, double* out_g, double* out_b, double* out_a);

/* -------------------------------------------------------------------------
 * UI Preview cache API
 * ---------------------------------------------------------------------- */

/**
 * Build (or rebuild) the UI preview texture for a gradient.
 *
 * Rasterizes the gradient into a GRADIENT_PREVIEW_WIDTH x GRADIENT_PREVIEW_HEIGHT
 * RGBA8 image by evaluating the gradient (via the LUT) at each column and
 * replicating that color across all rows.  Any existing preview is freed first.
 *
 * Prefer gradient_preview_get() for normal use; call this directly only when
 * an eager up-front build is needed.
 *
 * @param def  Gradient to build the preview for (must not be NULL)
 * @return true on success, false on out-of-memory.
 */
bool gradient_preview_build(GradientDef* def);

/**
 * Mark the preview cache as dirty and free the current pixel buffer.
 *
 * The next call to gradient_preview_get() will regenerate the texture.
 * Safe to call when no preview has been built yet.
 *
 * @param def  Gradient to invalidate (must not be NULL)
 */
void gradient_preview_invalidate(GradientDef* def);

/**
 * Return the cached preview texture, building it lazily if necessary.
 *
 * On the first call and after every gradient_preview_invalidate() call the
 * texture is generated automatically.  Returns NULL only if the build fails
 * due to out-of-memory.
 *
 * @param def  Gradient whose preview is requested (must not be NULL)
 * @return Pointer to the owned GradientPreview, or NULL on build failure.
 *         The pointer remains valid until the next invalidate or free.
 */
const GradientPreview* gradient_preview_get(GradientDef* def);

/* -------------------------------------------------------------------------
 * Compound invalidation
 * ---------------------------------------------------------------------- */

/**
 * Invalidate both the LUT cache and the UI preview cache in one call.
 *
 * Call this whenever gradient data (segments, transparency stops) has been
 * edited so that both caches are scheduled for a lazy rebuild on next use.
 *
 * @param def  Gradient to invalidate (must not be NULL)
 */
void gradient_invalidate(GradientDef* def);

#ifdef __cplusplus
}
#endif

#endif /* GRADIENT_H */
