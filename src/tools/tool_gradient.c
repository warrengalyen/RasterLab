/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "tools/tool_gradient.h"
#include "command.h"
#include "document.h"
#include "gradient.h"
#include "render/blend.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "tool_options.h"
#include "ui.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Cursor helpers
 * ========================================================================= */

/*
 * Create a custom plus-sign cursor (crosshair style).
 * The cursor is 32×32 pixels with a white-on-black plus sign, hotspot centred.
 * Caller must g_object_unref() the returned cursor when done.
 */
GdkCursor* tool_gradient_create_plus_cursor(void) {
    const gint size = 32;
    const gint hotspot = size / 2;
    const gint arm = size / 2 - 2; /* half-length of each arm */
    const gint cx = size / 2;
    const gint cy = size / 2;

    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surf);

    /* Transparent background */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

    /* Dark outline */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_set_line_width(cr, 3.0);
    cairo_move_to(cr, cx - arm, cy);
    cairo_line_to(cr, cx + arm, cy);
    cairo_move_to(cr, cx, cy - arm);
    cairo_line_to(cr, cx, cy + arm);
    cairo_stroke(cr);

    /* White centre line */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx - arm, cy);
    cairo_line_to(cr, cx + arm, cy);
    cairo_move_to(cr, cx, cy - arm);
    cairo_line_to(cr, cx, cy + arm);
    cairo_stroke(cr);

    cairo_destroy(cr);

    GdkPixbuf* pixbuf =
        gdk_pixbuf_get_from_surface(surf, 0, 0, size, size);
    cairo_surface_destroy(surf);

    GdkCursor* cursor = NULL;
    if (pixbuf) {
        cursor = gdk_cursor_new_from_pixbuf(gdk_display_get_default(),
                                            pixbuf, hotspot, hotspot);
        g_object_unref(pixbuf);
    }
    return cursor;
}

/* =========================================================================
 * Internal state helpers
 * ========================================================================= */

static GradientToolState* gradient_get_state(Tool* tool) {
    if (!tool->user_data)
        tool->user_data = g_malloc0(sizeof(GradientToolState));
    return (GradientToolState*)tool->user_data;
}

/* Free the gradient preview surface */
static void gradient_state_free_preview(GradientToolState* state) {
    if (state->preview_surface) {
        cairo_surface_destroy(state->preview_surface);
        state->preview_surface = NULL;
    }
}

/* Cancel an in-progress drag without finalizing */
static void gradient_cancel_drag(GradientToolState* state) {
    if (state->draw_cmd) {
        command_free((Command*)state->draw_cmd);
        state->draw_cmd = NULL;
    }
    gradient_state_free_preview(state);
    state->dragging = FALSE;
    state->active_layer = NULL;
}

/* Queue redraw of drawing area and viewport */
static void gradient_queue_redraw(ImageDocument* doc) {
    if (doc->drawing_area)
        gtk_widget_queue_draw(doc->drawing_area);
    if (doc->viewport)
        gtk_widget_queue_draw(doc->viewport);
}

/* =========================================================================
 * Gradient math helpers
 * ========================================================================= */

/* Precomputed invariants for one gradient application pass.
 * Built once before the pixel loop; eliminates per-pixel sqrt/atan2/division. */
typedef struct {
    gdouble lsx, lsy;       /* start in layer coords */
    gdouble dx, dy;         /* direction vector */
    gdouble inv_len;        /* 1 / length */
    gdouble inv_len2;       /* 1 / length^2 */
    gdouble angle_end;      /* atan2(dy, dx), precomputed for conical/spiral */
    gdouble ux, uy;         /* unit direction */
    gdouble sph_cx, sph_cy; /* spherical centre (layer coords) */
    gdouble inv_2pi;        /* 1 / (2π) */
    gint shape;
} GradientParams;

static void gradient_params_init(GradientParams* p,
                                 gdouble lsx, gdouble lsy,
                                 gdouble lex, gdouble ley,
                                 gint shape, gdouble center_offset) {
    p->lsx = lsx;
    p->lsy = lsy;
    p->dx = lex - lsx;
    p->dy = ley - lsy;

    gdouble len2 = p->dx * p->dx + p->dy * p->dy;
    if (len2 < 1e-10)
        len2 = 1e-10;

    gdouble len = sqrt(len2);
    p->inv_len = 1.0 / len;
    p->inv_len2 = 1.0 / len2;
    p->ux = p->dx * p->inv_len;
    p->uy = p->dy * p->inv_len;
    p->angle_end = atan2(p->dy, p->dx);
    p->inv_2pi = 1.0 / (2.0 * M_PI);
    p->shape = shape;

    p->sph_cx = lsx + (center_offset / 100.0) * p->dx;
    p->sph_cy = lsy + (center_offset / 100.0) * p->dy;
}

/* Apply repeat mode to a raw t value */
static inline gdouble gradient_apply_repeat(gdouble t, gint repeat) {
    switch (repeat) {
        case 0:
            return t;
        case 1:
            return (t < 0.0) ? 0.0 : (t > 1.0) ? 1.0
                                               : t;
        case 2:
            t = fmod(t, 1.0);
            if (t < 0.0)
                t += 1.0;
            return t;
        case 3: {
            t = fmod(t, 2.0);
            if (t < 0.0)
                t += 2.0;
            return (t <= 1.0) ? t : 2.0 - t;
        }
        default:
            return (t < 0.0) ? 0.0 : (t > 1.0) ? 1.0
                                               : t;
    }
}

/* Per-pixel t computation using precomputed invariants. */
static inline gdouble gradient_compute_t(const GradientParams* p,
                                         gdouble px, gdouble py) {
    gdouble rx = px - p->lsx;
    gdouble ry = py - p->lsy;

    switch (p->shape) {
        case 0: /* Linear */
            return (rx * p->dx + ry * p->dy) * p->inv_len2;

        case 1: { /* Reflection */
            gdouble raw = (rx * p->dx + ry * p->dy) * p->inv_len2;
            return fabs(raw);
        }

        case 2: /* Radial */
            return sqrt(rx * rx + ry * ry) * p->inv_len;

        case 3: { /* Spherical */
            gdouble ox = px - p->sph_cx;
            gdouble oy = py - p->sph_cy;
            return sqrt(ox * ox + oy * oy) * p->inv_len;
        }

        case 4: { /* Square — Chebyshev */
            gdouble along = fabs(rx * p->ux + ry * p->uy);
            gdouble perp = fabs(rx * (-p->uy) + ry * p->ux);
            return ((along > perp) ? along : perp) * p->inv_len;
        }

        case 5: { /* Diamond — Manhattan */
            return (fabs(rx * p->ux + ry * p->uy) +
                    fabs(rx * (-p->uy) + ry * p->ux)) *
                   p->inv_len;
        }

        case 6: { /* Conical */
            gdouble angle_pix = atan2(ry, rx);
            gdouble diff = fmod((angle_pix - p->angle_end) * p->inv_2pi + 2.0, 1.0);
            return diff;
        }

        case 7: { /* Spiral arm — Archimedean */
            gdouble r = sqrt(rx * rx + ry * ry) * p->inv_len;
            gdouble angle_pix = atan2(ry, rx);
            gdouble a = fmod((angle_pix - p->angle_end) * p->inv_2pi + 2.0, 1.0);
            gdouble arm_dist = fabs(r - a);
            gdouble blend = r < 0.25 ? r * 4.0 : 1.0;
            return arm_dist * blend + r * (1.0 - blend);
        }

        default:
            return (rx * p->dx + ry * p->dy) * p->inv_len2;
    }
}

/* =========================================================================
 * Color LUT — premultiplied ARGB32 table built once per application
 * ========================================================================= */

#define GRAD_COLOR_LUT_SIZE 1024

static void gradient_build_color_lut(GradientDef* grad, gdouble opacity,
                                     guint32* lut) {
    gdouble inv = 1.0 / (gdouble)(GRAD_COLOR_LUT_SIZE - 1);
    for (gint i = 0; i < GRAD_COLOR_LUT_SIZE; i++) {
        gdouble t = (gdouble)i * inv;
        gdouble r, g, b, a;
        if (grad) {
            gradient_lut_evaluate(grad, t, &r, &g, &b, &a);
        } else {
            r = g = b = t;
            a = 1.0;
        }
        a *= opacity;
        guint8 A = (guint8)(a * 255.0 + 0.5);
        guint8 R = (guint8)(r * a * 255.0 + 0.5);
        guint8 G = (guint8)(g * a * 255.0 + 0.5);
        guint8 B = (guint8)(b * a * 255.0 + 0.5);
        lut[i] = ((guint32)A << 24) | ((guint32)R << 16) |
                 ((guint32)G << 8) | (guint32)B;
    }
}

/* =========================================================================
 * Gradient pixel application
 * ========================================================================= */

/*
 * Apply the gradient to a layer (committed write — called once on mouse_up).
 *
 * start/end are in IMAGE (document) coordinates.
 * grad may be NULL; a built-in black-to-white linear fallback is used.
 * doc is used to access the selection mask; may be NULL (no selection clipping).
 * clip may be NULL for a full-layer fill.
 */
/*
 * Viewport clip rectangle for limiting gradient work to the visible region.
 * When non-NULL, only pixels inside [x0, x1) x [y0, y1) (in layer coords)
 * are filled and composited.  Pass NULL for a full-layer fill (mouse_up).
 */
typedef struct {
    gint x0, y0, x1, y1;
} GradientClipRect;

/* =========================================================================
 * Row-based gradient pipeline
 *
 * Eliminates the full-layer intermediate Cairo surface.  A single row
 * buffer is reused for each scanline through a fused
 * restore → fill → mask → blend pipeline, keeping data L1-hot.
 * ========================================================================= */

/* Fill a row buffer with gradient colors — linear/reflection fast path */
static inline void gradient_fill_row_linear(
    guint32* buf, gint fill_x0, gint fill_x1,
    gint y, gint shape, gint repeat,
    gdouble lsx, gdouble lsy, gdouble dt_dx,
    const GradientParams* p, const guint32* lut, gint lut_max) {

    gdouble ry = (gdouble)y - lsy;
    gdouble t_base =
        ((-lsx) * p->dx + ry * p->dy) * p->inv_len2;

    for (gint x = fill_x0; x < fill_x1; x++) {
        gdouble raw_t = t_base + (gdouble)x * dt_dx;
        if (shape == 1)
            raw_t = fabs(raw_t);

        if (repeat == 0) {
            if (raw_t < 0.0 || raw_t > 1.0) {
                buf[x - fill_x0] = 0;
                continue;
            }
        } else {
            raw_t = gradient_apply_repeat(raw_t, repeat);
        }

        gint idx = (gint)(raw_t * lut_max + 0.5);
        if (idx < 0)
            idx = 0;
        else if (idx > lut_max)
            idx = lut_max;
        buf[x - fill_x0] = lut[idx];
    }
}

/* Fill a row buffer with gradient colors — general shapes */
static inline void gradient_fill_row_general(
    guint32* buf, gint fill_x0, gint fill_x1,
    gint y, gint repeat,
    const GradientParams* p, const guint32* lut, gint lut_max) {

    for (gint x = fill_x0; x < fill_x1; x++) {
        gdouble raw_t = gradient_compute_t(p, (gdouble)x, (gdouble)y);

        if (repeat == 0 && (raw_t < 0.0 || raw_t > 1.0)) {
            buf[x - fill_x0] = 0;
            continue;
        }

        gdouble t = gradient_apply_repeat(raw_t, repeat);
        gint idx = (gint)(t * lut_max + 0.5);
        if (idx < 0)
            idx = 0;
        else if (idx > lut_max)
            idx = lut_max;
        buf[x - fill_x0] = lut[idx];
    }
}

/* Multiply premultiplied gradient row by selection mask values */
static inline void gradient_mask_row(
    guint32* buf, gint row_width, gint fill_x0, gint y,
    const guint8* mask_data, gint mask_x, gint mask_y,
    gint mask_width, gint mask_height, gint mask_stride) {

    gint my = y - mask_y;
    if (my < 0 || my >= mask_height) {
        memset(buf, 0, (gsize)row_width * 4);
        return;
    }
    const guint8* mrow = mask_data + (gsize)my * mask_stride;

    for (gint i = 0; i < row_width; i++) {
        gint mx = (fill_x0 + i) - mask_x;
        if (mx < 0 || mx >= mask_width) {
            buf[i] = 0;
            continue;
        }
        guint32 m = mrow[mx];
        if (m == 255)
            continue;
        if (m == 0) {
            buf[i] = 0;
            continue;
        }
        guint32 px = buf[i];
        guint32 a = (((px >> 24)) * m + 127) / 255;
        guint32 r = (((px >> 16) & 0xFF) * m + 127) / 255;
        guint32 g = (((px >> 8) & 0xFF) * m + 127) / 255;
        guint32 b = ((px & 0xFF) * m + 127) / 255;
        buf[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

/* =========================================================================
 * Preview rendering — gradient composited into state->preview_surface only.
 * The actual layer surface is left untouched until mouse_up.
 * ========================================================================= */

/*
 * Render the current gradient into state->preview_surface (viewport-clipped).
 * The preview surface is the same pixel dimensions as the layer (ARGB32,
 * premultiplied), cleared to transparent outside the filled region so the
 * draw-preview callback can composite it over the canvas with CAIRO_OPERATOR_OVER.
 * Selection masking is applied so the preview matches the committed result.
 */
static void gradient_render_preview(GradientToolState* state,
                                    struct ImageLayer* layer,
                                    GradientDef* grad,
                                    gdouble start_x, gdouble start_y,
                                    gdouble end_x, gdouble end_y,
                                    gint shape, gint repeat,
                                    gfloat opacity, gdouble center_offset,
                                    ImageDocument* doc,
                                    const GradientClipRect* clip) {
    if (!state || !layer || !layer->surface)
        return;

    gint lw = cairo_image_surface_get_width(layer->surface);
    gint lh = cairo_image_surface_get_height(layer->surface);
    if (lw <= 0 || lh <= 0)
        return;

    /* Create or reuse the preview surface (full layer dimensions) */
    if (!state->preview_surface ||
        cairo_image_surface_get_width(state->preview_surface) != lw ||
        cairo_image_surface_get_height(state->preview_surface) != lh) {
        if (state->preview_surface)
            cairo_surface_destroy(state->preview_surface);
        state->preview_surface =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lw, lh);
    }

    cairo_surface_flush(state->preview_surface);
    gint prev_stride = cairo_image_surface_get_stride(state->preview_surface);
    guchar* prev_data = cairo_image_surface_get_data(state->preview_surface);
    if (!prev_data)
        return;

    /* Clear to transparent */
    memset(prev_data, 0, (gsize)lh * prev_stride);

    /* Effective fill region */
    gint fill_x0 = 0, fill_y0 = 0, fill_x1 = lw, fill_y1 = lh;
    if (clip) {
        fill_x0 = clip->x0 > 0 ? clip->x0 : 0;
        fill_y0 = clip->y0 > 0 ? clip->y0 : 0;
        fill_x1 = clip->x1 < lw ? clip->x1 : lw;
        fill_y1 = clip->y1 < lh ? clip->y1 : lh;
        if (fill_x0 >= fill_x1 || fill_y0 >= fill_y1) {
            cairo_surface_mark_dirty(state->preview_surface);
            return;
        }
    }

    /* Precompute gradient invariants */
    gdouble lsx = start_x - layer->offset_x;
    gdouble lsy = start_y - layer->offset_y;
    gdouble lex = end_x   - layer->offset_x;
    gdouble ley = end_y   - layer->offset_y;

    GradientParams params;
    gradient_params_init(&params, lsx, lsy, lex, ley, shape, center_offset);

    gdouble opacity_factor = (gdouble)opacity / 100.0;
    if (opacity_factor < 0.0) opacity_factor = 0.0;
    if (opacity_factor > 1.0) opacity_factor = 1.0;

    guint32 color_lut[GRAD_COLOR_LUT_SIZE];
    gradient_build_color_lut(grad, opacity_factor, color_lut);

    /* Selection mask (optional) */
    const guint8* mask_data = NULL;
    gint mask_x = 0, mask_y = 0, mask_w = 0, mask_h = 0, mask_s = 0;
    SelectionMask* region_mask = NULL;

    if (doc && doc->selection_mask &&
        !selection_mask_is_empty(doc->selection_mask)) {
        DirtyRect sel_dirty;
        dirty_rect_set(&sel_dirty,
                       layer->offset_x, layer->offset_y, lw, lh);
        DirtyRect actual_region;
        region_mask = selection_build_combined_mask(
            doc->selection_mask, &sel_dirty,
            FEATHER_QUALITY_NORMAL, &actual_region);
        if (region_mask && region_mask->data) {
            mask_data = region_mask->data;
            mask_x = actual_region.x - layer->offset_x;
            mask_y = actual_region.y - layer->offset_y;
            mask_w = region_mask->width;
            mask_h = region_mask->height;
            mask_s = region_mask->stride;
        }
    }

    /* Fill rows into the preview surface */
    const gint lut_max = GRAD_COLOR_LUT_SIZE - 1;
    gdouble dt_dx = params.dx * params.inv_len2;
    gint row_width = fill_x1 - fill_x0;
    guint32* row_buf = (guint32*)g_malloc((gsize)row_width * 4);

    for (gint y = fill_y0; y < fill_y1; y++) {
        guint32* dst = (guint32*)(prev_data + (gsize)y * prev_stride) + fill_x0;

        if (shape == 0 || shape == 1)
            gradient_fill_row_linear(row_buf, fill_x0, fill_x1,
                                     y, shape, repeat,
                                     lsx, lsy, dt_dx,
                                     &params, color_lut, lut_max);
        else
            gradient_fill_row_general(row_buf, fill_x0, fill_x1,
                                      y, repeat,
                                      &params, color_lut, lut_max);

        if (mask_data)
            gradient_mask_row(row_buf, row_width, fill_x0, y,
                              mask_data, mask_x, mask_y,
                              mask_w, mask_h, mask_s);

        memcpy(dst, row_buf, (gsize)row_width * 4);
    }

    g_free(row_buf);

    if (region_mask)
        selection_mask_free(region_mask);

    cairo_surface_mark_dirty(state->preview_surface);
}

static void gradient_apply_to_layer(struct ImageLayer* layer,
                                    GradientDef* grad,
                                    gdouble start_x, gdouble start_y,
                                    gdouble end_x, gdouble end_y,
                                    gint shape, gint repeat,
                                    gfloat opacity, gint blend_mode_idx,
                                    gdouble center_offset,
                                    ImageDocument* doc,
                                    const GradientClipRect* clip) {
    if (!layer || !layer->surface)
        return;

    cairo_surface_t* surf = layer->surface;
    cairo_surface_flush(surf);

    gint width = cairo_image_surface_get_width(surf);
    gint height = cairo_image_surface_get_height(surf);
    gint stride = cairo_image_surface_get_stride(surf);
    guchar* data = cairo_image_surface_get_data(surf);

    if (!data || width <= 0 || height <= 0)
        return;

    /* --- Effective fill region (viewport clip or full layer) --- */
    gint fill_x0 = 0, fill_y0 = 0, fill_x1 = width, fill_y1 = height;
    if (clip) {
        fill_x0 = clip->x0 > 0 ? clip->x0 : 0;
        fill_y0 = clip->y0 > 0 ? clip->y0 : 0;
        fill_x1 = clip->x1 < width ? clip->x1 : width;
        fill_y1 = clip->y1 < height ? clip->y1 : height;
        if (fill_x0 >= fill_x1 || fill_y0 >= fill_y1)
            return;
    }

    /* --- Precompute all invariants once --- */
    gdouble lsx = start_x - layer->offset_x;
    gdouble lsy = start_y - layer->offset_y;
    gdouble lex = end_x - layer->offset_x;
    gdouble ley = end_y - layer->offset_y;

    GradientParams params;
    gradient_params_init(&params, lsx, lsy, lex, ley, shape, center_offset);

    gdouble opacity_factor = (gdouble)opacity / 100.0;
    if (opacity_factor < 0.0)
        opacity_factor = 0.0;
    if (opacity_factor > 1.0)
        opacity_factor = 1.0;

    guint32 color_lut[GRAD_COLOR_LUT_SIZE];
    gradient_build_color_lut(grad, opacity_factor, color_lut);

    BlendMode bm = (blend_mode_idx >= 0 && blend_mode_idx < BLEND_MODE_COUNT)
                       ? (BlendMode)blend_mode_idx
                       : BLEND_MODE_NORMAL;

    /* --- Build selection mask (if any) --- */
    const guint8* mask_data = NULL;
    gint mask_x = 0, mask_y = 0, mask_w = 0, mask_h = 0, mask_s = 0;
    SelectionMask* region_mask = NULL;

    if (doc && doc->selection_mask &&
        !selection_mask_is_empty(doc->selection_mask)) {
        DirtyRect sel_dirty;
        dirty_rect_set(&sel_dirty,
                       layer->offset_x, layer->offset_y,
                       width, height);
        DirtyRect actual_region;
        region_mask = selection_build_combined_mask(
            doc->selection_mask, &sel_dirty,
            FEATHER_QUALITY_NORMAL, &actual_region);
        if (region_mask && region_mask->data) {
            mask_data = region_mask->data;
            mask_x = actual_region.x - layer->offset_x;
            mask_y = actual_region.y - layer->offset_y;
            mask_w = region_mask->width;
            mask_h = region_mask->height;
            mask_s = region_mask->stride;
        }
    }

    /* --- Fused row pipeline: restore → fill → mask → blend per scanline --- */
    const gint lut_max = GRAD_COLOR_LUT_SIZE - 1;
    gdouble dt_dx = params.dx * params.inv_len2;
    gint row_width = fill_x1 - fill_x0;
    guint32* row_buf = (guint32*)g_malloc((gsize)row_width * 4);

    for (gint y = fill_y0; y < fill_y1; y++) {
        guint32* dst = (guint32*)(data + (gsize)y * stride) + fill_x0;

        if (shape == 0 || shape == 1)
            gradient_fill_row_linear(row_buf, fill_x0, fill_x1,
                                     y, shape, repeat,
                                     lsx, lsy, dt_dx,
                                     &params, color_lut, lut_max);
        else
            gradient_fill_row_general(row_buf, fill_x0, fill_x1,
                                      y, repeat,
                                      &params, color_lut, lut_max);

        if (mask_data)
            gradient_mask_row(row_buf, row_width, fill_x0, y,
                              mask_data, mask_x, mask_y,
                              mask_w, mask_h, mask_s);

        blend_composite_row((const guint32*)row_buf, dst,
                            row_width, fill_x0, y, 255, bm);
    }

    g_free(row_buf);

    if (region_mask)
        selection_mask_free(region_mask);

    cairo_surface_mark_dirty(surf);
}

/* =========================================================================
 * Preview overlay (viewport draw)
 * ========================================================================= */

/* Stroke path with the same double-stroke style as the crop tool */
static void gradient_stroke_path(cairo_t* cr, gdouble line_width) {
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width(cr, line_width * 3.0);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, line_width);
    cairo_stroke(cr);
}

/* Draw a filled arrowhead at (tip_x, tip_y) pointing along (dir_x, dir_y) */
static void gradient_draw_arrow(cairo_t* cr, gdouble tip_x, gdouble tip_y,
                                gdouble dir_x, gdouble dir_y,
                                gdouble size, gdouble lw) {
    gdouble len = sqrt(dir_x * dir_x + dir_y * dir_y);
    if (len < 1e-6)
        return;

    gdouble ux = dir_x / len, uy = dir_y / len;
    gdouble px = -uy, py = ux; /* perpendicular unit */

    gdouble bx = tip_x - ux * size;
    gdouble by = tip_y - uy * size;

    cairo_move_to(cr, tip_x, tip_y);
    cairo_line_to(cr, bx + px * size * 0.5, by + py * size * 0.5);
    cairo_line_to(cr, bx - px * size * 0.5, by - py * size * 0.5);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
}

void tool_gradient_draw_preview(Tool* tool, struct ImageDocument* doc,
                                cairo_t* cr, gdouble zoom) {
    if (!tool || !tool->user_data || !doc || !cr)
        return;

    GradientToolState* state = (GradientToolState*)tool->user_data;
    if (!state->dragging)
        return;

    /* --- Composite the preview surface over the canvas --- */
    if (state->preview_surface && state->active_layer) {
        struct ImageLayer* layer = state->active_layer;
        cairo_save(cr);
        /* Scale to document space then offset to layer origin */
        cairo_scale(cr, zoom, zoom);
        cairo_translate(cr, (gdouble)layer->offset_x, (gdouble)layer->offset_y);
        cairo_set_source_surface(cr, state->preview_surface, 0.0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    /* --- Draw the direction line and arrowhead overlay --- */
    gdouble sx = state->start_x * zoom;
    gdouble sy = state->start_y * zoom;
    gdouble ex = state->end_x * zoom;
    gdouble ey = state->end_y * zoom;

    gdouble lw = 1.0;
    gdouble arr = 10.0; /* arrowhead size */

    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    cairo_move_to(cr, sx, sy);
    cairo_line_to(cr, ex, ey);
    gradient_stroke_path(cr, lw);

    gradient_draw_arrow(cr, ex, ey, ex - sx, ey - sy, arr, lw);

    cairo_restore(cr);
}

/* =========================================================================
 * Mouse event handlers
 * ========================================================================= */

static void gradient_tool_mouse_down(Tool* tool, struct ImageDocument* doc,
                                     MouseEvent* event) {
    if (!tool || !doc || !doc->layers)
        return;

    GradientToolState* state = gradient_get_state(tool);

    /* Cancel any previous unfinished drag */
    if (state->dragging)
        gradient_cancel_drag(state);

    struct ImageLayer* layer = document_get_selected_layer(doc);
    if (!layer || !layer->surface)
        return;

    /* Capture "before" state for undo — the layer is not touched until mouse_up */
    cairo_surface_flush(layer->surface);
    Command* cmd = command_create_draw(layer, "Gradient Tool");
    if (!cmd)
        return;

    state->draw_cmd = (gpointer)cmd;
    state->active_layer = layer;
    state->start_x = event->x;
    state->start_y = event->y;
    state->end_x = event->x;
    state->end_y = event->y;
    state->dragging = TRUE;
    state->last_apply_usec = 0;

    /* Hide the cursor while dragging: the arrowhead shows mouse position */
    {
        GdkDisplay* display = gdk_display_get_default();
        GdkCursor* blank =
            gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
        if (blank) {
            if (doc->drawing_area) {
                GdkWindow* da_win = gtk_widget_get_window(doc->drawing_area);
                if (da_win)
                    gdk_window_set_cursor(da_win, blank);
            }
            if (doc->viewport) {
                GdkWindow* vp_win = gtk_widget_get_window(doc->viewport);
                if (vp_win)
                    gdk_window_set_cursor(vp_win, blank);
            }
            g_object_unref(blank);
        }
    }

    gradient_queue_redraw(doc);
}

/* Minimum interval between gradient re-applications during drag (microseconds).
 * 16667 µs ≈ 60 fps; the overlay line is updated on every move regardless. */
#define GRADIENT_THROTTLE_USEC 16000

static void gradient_tool_mouse_move(Tool* tool, struct ImageDocument* doc,
                                     MouseEvent* event) {
    if (!tool || !doc)
        return;

    GradientToolState* state = (GradientToolState*)tool->user_data;
    if (!state || !state->dragging)
        return;

    state->end_x = event->x;
    state->end_y = event->y;

    /* Always redraw the overlay line immediately */
    if (doc->viewport)
        gtk_widget_queue_draw(doc->viewport);
    if (doc->drawing_area)
        gtk_widget_queue_draw(doc->drawing_area);

    /* Throttle: skip the expensive gradient recomputation if we're
     * within the frame budget of the last apply. */
    gint64 now = g_get_monotonic_time();
    if (now - state->last_apply_usec < GRADIENT_THROTTLE_USEC)
        return;
    state->last_apply_usec = now;

    /* Get the active gradient and options */
    AppContext* ctx = (AppContext*)tool->app_context;
    GradientDef* grad = ctx ? (GradientDef*)ctx->active_gradient : NULL;
    struct ImageLayer* layer = state->active_layer;

    if (layer && layer->surface) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_GRADIENT);
        gint shape = opts ? opts->gradient_shape : 0;
        gint repeat = opts ? opts->gradient_repeat : 1;
        gfloat opacity = opts ? opts->gradient_opacity : 100.0f;
        gdouble center_off = opts ? opts->gradient_center_offset : 75.0;

        /* Clip gradient preview to the viewport-visible region so the
         * render stays cheap even on large layers. */
        GradientClipRect viewport_clip;
        GradientClipRect* clip_ptr = NULL;
        gint lw = cairo_image_surface_get_width(layer->surface);
        gint lh = cairo_image_surface_get_height(layer->surface);

        if (doc->scrolled_window &&
            GTK_IS_SCROLLED_WINDOW(doc->scrolled_window) &&
            doc->zoom_factor > 0.0) {
            GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(
                GTK_SCROLLED_WINDOW(doc->scrolled_window));
            GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(
                GTK_SCROLLED_WINDOW(doc->scrolled_window));
            if (hadj && vadj) {
                gdouble sv_x = gtk_adjustment_get_value(hadj);
                gdouble sv_y = gtk_adjustment_get_value(vadj);
                gdouble vw   = gtk_adjustment_get_page_size(hadj);
                gdouble vh   = gtk_adjustment_get_page_size(vadj);
                gdouble inv_zoom = 1.0 / doc->zoom_factor;
                gint pad = 2;

                viewport_clip.x0 = (gint)(sv_x * inv_zoom) - layer->offset_x - pad;
                viewport_clip.y0 = (gint)(sv_y * inv_zoom) - layer->offset_y - pad;
                viewport_clip.x1 = (gint)((sv_x + vw) * inv_zoom) - layer->offset_x + pad;
                viewport_clip.y1 = (gint)((sv_y + vh) * inv_zoom) - layer->offset_y + pad;

                if (viewport_clip.x0 < 0)  viewport_clip.x0 = 0;
                if (viewport_clip.y0 < 0)  viewport_clip.y0 = 0;
                if (viewport_clip.x1 > lw) viewport_clip.x1 = lw;
                if (viewport_clip.y1 > lh) viewport_clip.y1 = lh;

                clip_ptr = &viewport_clip;
            }
        }

        /* Render gradient into the preview surface — the layer is unchanged. */
        gradient_render_preview(state, layer, grad,
                                state->start_x, state->start_y,
                                state->end_x, state->end_y,
                                shape, repeat, opacity, center_off,
                                doc, clip_ptr);

        /* Trigger a cheap viewport redraw to show the updated preview.
         * No layer cache rebuild or full compositor pass is needed. */
        if (doc->viewport)
            gtk_widget_queue_draw(doc->viewport);
    }
}

/* Restore the plus cursor on both drawing_area and viewport after drag ends */
static void gradient_restore_cursor(struct ImageDocument* doc) {
    GdkCursor* plus_cur = tool_gradient_create_plus_cursor();
    if (plus_cur) {
        if (doc->drawing_area) {
            GdkWindow* da_win = gtk_widget_get_window(doc->drawing_area);
            if (da_win)
                gdk_window_set_cursor(da_win, plus_cur);
        }
        if (doc->viewport) {
            GdkWindow* vp_win = gtk_widget_get_window(doc->viewport);
            if (vp_win)
                gdk_window_set_cursor(vp_win, plus_cur);
        }
        g_object_unref(plus_cur);
    }
}

static void gradient_tool_mouse_up(Tool* tool, struct ImageDocument* doc,
                                   MouseEvent* event) {
    (void)event;

    if (!tool || !doc || !tool->user_data)
        return;

    GradientToolState* state = (GradientToolState*)tool->user_data;
    if (!state->dragging)
        return;

    state->dragging = FALSE;

    AppContext* ctx = (AppContext*)tool->app_context;
    GradientDef* grad = ctx ? (GradientDef*)ctx->active_gradient : NULL;
    struct ImageLayer* layer = state->active_layer;

    if (!layer || !layer->surface || !state->draw_cmd) {
        gradient_cancel_drag(state);
        gradient_restore_cursor(doc);
        gradient_queue_redraw(doc);
        return;
    }

    /* Apply the final gradient at full resolution — layer has not been
     * touched during the drag, so no restore step is needed. */
    {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_GRADIENT);
        gint shape      = opts ? opts->gradient_shape        : 0;
        gint repeat     = opts ? opts->gradient_repeat       : 1;
        gfloat opacity  = opts ? opts->gradient_opacity      : 100.0f;
        gint blend_mode = opts ? opts->gradient_blend_mode   : 0;
        gdouble center_off = opts ? opts->gradient_center_offset : 75.0;

        gradient_apply_to_layer(layer, grad,
                                state->start_x, state->start_y,
                                state->end_x, state->end_y,
                                shape, repeat, opacity, blend_mode, center_off,
                                doc, NULL);
        cairo_surface_flush(layer->surface);
        layer_invalidate_cache(layer);
    }

    /* Finalize undo command ("after" state is now the gradient result) */
    Command* draw_cmd = (Command*)state->draw_cmd;
    state->draw_cmd = NULL;
    if (command_finalize_draw(draw_cmd)) {
        document_push_undo_command(doc, draw_cmd);

        if (ctx) {
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx, NULL);
        }
    } else {
        command_free(draw_cmd);
    }

    /* Free the preview surface and clear active layer */
    gradient_state_free_preview(state);
    state->active_layer = NULL;

    /* Restore plus cursor (drag is over) */
    gradient_restore_cursor(doc);

    /* Invalidate and redraw */
    DirtyRect dr;
    dirty_rect_set(&dr, 0, 0, doc->width, doc->height);
    document_invalidate_region(doc, &dr);
    gradient_queue_redraw(doc);
}

/* =========================================================================
 * Tool creation
 * ========================================================================= */

Tool* tool_gradient_create(void) {
    Tool* tool = tool_new("Gradient", TOOL_GRADIENT, GDK_CROSSHAIR, TOOL_OPT_NONE);
    if (!tool)
        return NULL;

    tool->mouse_down = gradient_tool_mouse_down;
    tool->mouse_move = gradient_tool_mouse_move;
    tool->mouse_up = gradient_tool_mouse_up;

    return tool;
}
