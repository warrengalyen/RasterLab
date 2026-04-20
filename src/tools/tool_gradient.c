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

/* Free pixel backup in state (does NOT free state itself) */
static void gradient_state_free_backup(GradientToolState* state) {
    if (state->pixel_backup) {
        g_free(state->pixel_backup);
        state->pixel_backup = NULL;
    }
}

/* Free the cached gradient surface */
static void gradient_state_free_surface(GradientToolState* state) {
    if (state->grad_surface) {
        cairo_surface_destroy(state->grad_surface);
        state->grad_surface = NULL;
    }
    state->grad_surf_w = 0;
    state->grad_surf_h = 0;
}

/* Cancel an in-progress drag without finalizing */
static void gradient_cancel_drag(GradientToolState* state) {
    if (state->draw_cmd) {
        command_free((Command*)state->draw_cmd);
        state->draw_cmd = NULL;
    }
    gradient_state_free_backup(state);
    gradient_state_free_surface(state);
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
 * Apply the gradient to a layer.
 *
 * If restore_from is non-NULL the layer pixels are first restored from that
 * buffer before the gradient is applied (used for real-time drag updates).
 *
 * start/end are in IMAGE (document) coordinates.
 * grad may be NULL; a built-in black-to-white linear fallback is used.
 *
 * If cached_surf is non-NULL it points to a reusable temp surface pointer.
 * On first call *cached_surf is NULL; the function allocates it and writes it
 * back.  On subsequent calls the same surface is reused (zero allocation).
 * cache_w/cache_h track the cached dimensions so we reallocate if needed.
 *
 * doc is used to access the selection mask; may be NULL (no selection clipping).
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

static void gradient_apply_to_layer(struct ImageLayer* layer,
                                    GradientDef* grad,
                                    gdouble start_x, gdouble start_y,
                                    gdouble end_x, gdouble end_y,
                                    gint shape, gint repeat,
                                    gfloat opacity, gint blend_mode_idx,
                                    gdouble center_offset,
                                    const guchar* restore_from,
                                    gint restore_stride,
                                    cairo_surface_t** cached_surf,
                                    gint* cache_w, gint* cache_h,
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

    /* Release legacy cached surface — no longer needed with row pipeline */
    if (cached_surf && *cached_surf) {
        cairo_surface_destroy(*cached_surf);
        *cached_surf = NULL;
        if (cache_w) *cache_w = 0;
        if (cache_h) *cache_h = 0;
    }

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

        if (restore_from) {
            const guint32* src = (const guint32*)(
                restore_from + (gsize)y * restore_stride) + fill_x0;
            memcpy(dst, src, (gsize)row_width * 4);
        }

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

/* Draw a plus-sign at (x, y) in viewport coords */
static void gradient_draw_plus(cairo_t* cr, gdouble x, gdouble y,
                               gdouble arm, gdouble lw) {
    cairo_move_to(cr, x - arm, y);
    cairo_line_to(cr, x + arm, y);
    cairo_move_to(cr, x, y - arm);
    cairo_line_to(cr, x, y + arm);
    gradient_stroke_path(cr, lw);
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

    gdouble sx = state->start_x * zoom;
    gdouble sy = state->start_y * zoom;
    gdouble ex = state->end_x * zoom;
    gdouble ey = state->end_y * zoom;

    gdouble lw = 1.0;
    gdouble arr = 10.0; /* arrowhead size */
    gdouble plus = 8.0; /* plus-sign arm length */

    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    /* Gradient line */
    cairo_move_to(cr, sx, sy);
    cairo_line_to(cr, ex, ey);
    gradient_stroke_path(cr, lw);

    /* Arrowhead at end point (indicates current mouse position) */
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

    /* Flush surface so we get an accurate pixel snapshot */
    cairo_surface_flush(layer->surface);

    /* Capture "before" state for undo */
    Command* cmd = command_create_draw(layer, "Gradient Tool");
    if (!cmd)
        return;

    /* Save a pixel backup for real-time restoration on each mouse_move */
    gint width = cairo_image_surface_get_width(layer->surface);
    gint height = cairo_image_surface_get_height(layer->surface);
    gint stride = cairo_image_surface_get_stride(layer->surface);
    const guchar* pixels = cairo_image_surface_get_data(layer->surface);

    guchar* backup = NULL;
    if (pixels && width > 0 && height > 0) {
        gsize sz = (gsize)(height * stride);
        backup = g_malloc(sz);
        if (backup)
            memcpy(backup, pixels, sz);
    }

    if (!backup) {
        command_free(cmd);
        return;
    }

    state->draw_cmd = (gpointer)cmd;
    state->pixel_backup = backup;
    state->backup_width = width;
    state->backup_height = height;
    state->backup_stride = stride;
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

    if (layer && layer->surface && state->pixel_backup) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_GRADIENT);
        gint shape = opts ? opts->gradient_shape : 0;
        gint repeat = opts ? opts->gradient_repeat : 1;
        gfloat opacity = opts ? opts->gradient_opacity : 100.0f;
        gint blend_mode = opts ? opts->gradient_blend_mode : 0;
        gdouble center_off = opts ? opts->gradient_center_offset : 75.0;

        /* Compute viewport-visible region in layer coordinates so we only
         * fill and composite the pixels currently on screen. */
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
                gdouble sx = gtk_adjustment_get_value(hadj);
                gdouble sy = gtk_adjustment_get_value(vadj);
                gdouble vw = gtk_adjustment_get_page_size(hadj);
                gdouble vh = gtk_adjustment_get_page_size(vadj);
                gdouble inv_zoom = 1.0 / doc->zoom_factor;

                gint pad = 2;
                viewport_clip.x0 = (gint)(sx * inv_zoom) - layer->offset_x - pad;
                viewport_clip.y0 = (gint)(sy * inv_zoom) - layer->offset_y - pad;
                viewport_clip.x1 = (gint)((sx + vw) * inv_zoom) - layer->offset_x + pad;
                viewport_clip.y1 = (gint)((sy + vh) * inv_zoom) - layer->offset_y + pad;

                if (viewport_clip.x0 < 0) viewport_clip.x0 = 0;
                if (viewport_clip.y0 < 0) viewport_clip.y0 = 0;
                if (viewport_clip.x1 > lw) viewport_clip.x1 = lw;
                if (viewport_clip.y1 > lh) viewport_clip.y1 = lh;

                clip_ptr = &viewport_clip;
            }
        }

        gradient_apply_to_layer(layer, grad,
                                state->start_x, state->start_y,
                                state->end_x, state->end_y,
                                shape, repeat, opacity, blend_mode, center_off,
                                state->pixel_backup, state->backup_stride,
                                &state->grad_surface,
                                &state->grad_surf_w, &state->grad_surf_h,
                                doc, clip_ptr);

        cairo_surface_flush(layer->surface);
        layer_invalidate_cache(layer);

        DirtyRect dr;
        dirty_rect_set(&dr, layer->offset_x, layer->offset_y, lw, lh);
        document_invalidate_region(doc, &dr);
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

    /* Apply the final gradient at full resolution (no viewport clip) */
    if (state->pixel_backup) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_GRADIENT);
        gint shape = opts ? opts->gradient_shape : 0;
        gint repeat = opts ? opts->gradient_repeat : 1;
        gfloat opacity = opts ? opts->gradient_opacity : 100.0f;
        gint blend_mode = opts ? opts->gradient_blend_mode : 0;
        gdouble center_off = opts ? opts->gradient_center_offset : 75.0;

        gradient_apply_to_layer(layer, grad,
                                state->start_x, state->start_y,
                                state->end_x, state->end_y,
                                shape, repeat, opacity, blend_mode, center_off,
                                state->pixel_backup, state->backup_stride,
                                &state->grad_surface,
                                &state->grad_surf_w, &state->grad_surf_h,
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

    /* Clean up */
    gradient_state_free_backup(state);
    gradient_state_free_surface(state);
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
