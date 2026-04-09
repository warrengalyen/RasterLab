/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "document.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "tools.h"
#include "ui.h"
#include "ui/tool_options_panel.h"
#include "ui/tools_panel.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include "debug_logger.h"

/* Deferred preview update: motion only stores pending; idle does sample + update */
typedef struct {
    ImageDocument* doc;
    AppContext* ctx;
    gint ix, iy;
    gboolean inside;
} ColorPickerPending;

static ColorPickerPending g_cp_pending = {0};
static guint g_cp_idle_id = 0;

static gboolean sample_color(ImageDocument* doc, gint ix, gint iy,
                             gdouble* r, gdouble* g, gdouble* b, gdouble* a);

static gboolean color_picker_preview_idle(gpointer user_data) {
    (void)user_data;
    g_cp_idle_id = 0;

    ImageDocument* doc = g_cp_pending.doc;
    AppContext* ctx = g_cp_pending.ctx;
    gint ix = g_cp_pending.ix;
    gint iy = g_cp_pending.iy;
    gboolean inside = g_cp_pending.inside;
    g_cp_pending.doc = NULL;
    g_cp_pending.ctx = NULL;

    if (!ctx || !doc || !ctx->documents) {
        return G_SOURCE_REMOVE;
    }
    if (!g_list_find(ctx->documents, doc)) {
        return G_SOURCE_REMOVE; /* Doc closed */
    }
    /* Skip if user switched away from color picker */
    if (!ctx->tool_registry) {
        return G_SOURCE_REMOVE;
    }
    {
        Tool* active = tool_manager_get_active(ctx->tool_registry);
        if (!active || active->type != TOOL_COLOR_PICKER) {
            return G_SOURCE_REMOVE;
        }
    }

    ToolOptionsPanel* panel = ctx->tool_options_panel;
    if (!panel || !panel->color_picker_panel) {
        return G_SOURCE_REMOVE;
    }

    if (!inside) {
        tool_options_panel_set_color_picker_preview(panel, FALSE, 0, 0, 0, 0);
        return G_SOURCE_REMOVE;
    }

    gdouble r, g, b, a;
    if (!sample_color(doc, ix, iy, &r, &g, &b, &a)) {
        tool_options_panel_set_color_picker_preview(panel, FALSE, 0, 0, 0, 0);
        return G_SOURCE_REMOVE;
    }
    tool_options_panel_set_color_picker_preview(panel, TRUE, r, g, b, a);
    return G_SOURCE_REMOVE;
}

static void color_picker_schedule_preview(ImageDocument* doc, AppContext* ctx,
                                          gint ix, gint iy, gboolean inside) {
    g_cp_pending.doc = doc;
    g_cp_pending.ctx = ctx;
    g_cp_pending.ix = ix;
    g_cp_pending.iy = iy;
    g_cp_pending.inside = inside;
    if (g_cp_idle_id == 0) {
        g_cp_idle_id = g_idle_add(color_picker_preview_idle, NULL);
    }
}

static GdkCursor* create_colorpicker_cursor(void) {
    GdkDisplay* display = gdk_display_get_default();
    if (!display) {
        return NULL;
    }

    GError* error = NULL;
    GBytes* bytes = g_resources_lookup_data("/cursors/colorpicker_cursor.cur",
                                            G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!bytes) {
        if (error) {
            debug_log("WRN", "Failed to load colorpicker cursor resource: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    g_bytes_unref(bytes);

    if (!pixbuf) {
        if (error) {
            debug_log("WRN", "Failed to parse colorpicker cursor: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);
    GdkCursor* cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }
    return cursor;
}

/**
 * Sample color from surface at (cx, cy) with given radius.
 * Surface is ARGB32 (BGRA). Returns averaged color in 0–1; alpha 0 means no samples.
 * Loop bounds are clamped to the surface and radius is capped to avoid overflow / runaway loops.
 */
static void sample_surface_at(cairo_surface_t* surface, gint width, gint height, gint stride,
                              gint cx, gint cy, gint radius,
                              gdouble* out_r, gdouble* out_g, gdouble* out_b, gdouble* out_a) {
    guchar* data = cairo_image_surface_get_data(surface);
    if (!data || width <= 0 || height <= 0) {
        *out_r = *out_g = *out_b = 0.0;
        *out_a = 0.0;
        return;
    }

    /* Cap radius to avoid huge loops or integer overflow in extent math */
    if (radius < 0) {
        radius = 0;
    }
    if (radius > 100) {
        radius = 100;
    }

    gint y_lo = (radius > 0) ? (cy - radius) : cy;
    gint y_hi = (radius > 0) ? (cy + radius) : cy;
    gint x_lo = (radius > 0) ? (cx - radius) : cx;
    gint x_hi = (radius > 0) ? (cx + radius) : cx;

    /* Clamp to surface; use gint64 to avoid overflow when adding radius */
    gint64 y_lo64 = (gint64)y_lo;
    gint64 y_hi64 = (gint64)y_hi;
    gint64 x_lo64 = (gint64)x_lo;
    gint64 x_hi64 = (gint64)x_hi;
    if (y_lo64 < 0)
        y_lo64 = 0;
    if (y_hi64 > height - 1)
        y_hi64 = height - 1;
    if (x_lo64 < 0)
        x_lo64 = 0;
    if (x_hi64 > width - 1)
        x_hi64 = width - 1;
    if (y_lo64 > y_hi64 || x_lo64 > x_hi64) {
        *out_r = *out_g = *out_b = 0.0;
        *out_a = 0.0;
        return;
    }

    gint y_start = (gint)y_lo64;
    gint y_end = (gint)y_hi64;
    gint x_start = (gint)x_lo64;
    gint x_end = (gint)x_hi64;

    gint64 rsq = (gint64)radius * (gint64)radius;
    guint64 sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
    guint count = 0;

    for (gint y = y_start; y <= y_end; y++) {
        for (gint x = x_start; x <= x_end; x++) {
            if (radius > 0) {
                gint64 dx = (gint64)x - (gint64)cx;
                gint64 dy = (gint64)y - (gint64)cy;
                if (dx * dx + dy * dy > rsq) {
                    continue;
                }
            }
            guchar* p = data + (gint64)y * (gint64)stride + (gint64)x * 4;
            guchar b = p[0], g = p[1], r = p[2], a = p[3];
            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            }
            sum_r += r;
            sum_g += g;
            sum_b += b;
            sum_a += a;
            count++;
        }
    }

    if (count == 0) {
        *out_r = *out_g = *out_b = 0.0;
        *out_a = 0.0;
        return;
    }
    *out_r = (gdouble)sum_r / (255.0 * (gdouble)count);
    *out_g = (gdouble)sum_g / (255.0 * (gdouble)count);
    *out_b = (gdouble)sum_b / (255.0 * (gdouble)count);
    *out_a = (gdouble)sum_a / (255.0 * (gdouble)count);
}

/**
 * Sample color from doc at image coords (ix, iy) using layer or composite.
 * Returns TRUE if a color was sampled, FALSE otherwise.
 */
static gboolean sample_color(ImageDocument* doc, gint ix, gint iy,
                             gdouble* r, gdouble* g, gdouble* b, gdouble* a) {
    ToolOptions* opts = tool_options_get_for_tool(TOOL_COLOR_PICKER);
    gint radius = opts ? tool_options_get_color_picker_sample_radius(opts) : 0;
    gboolean from_layer = opts ? tool_options_get_color_picker_sample_from_layer(opts) : TRUE;

    if (from_layer) {
        ImageLayer* layer = document_get_selected_layer(doc);
        if (!layer || !layer->surface) {
            return FALSE;
        }
        gint lx = ix - layer->offset_x;
        gint ly = iy - layer->offset_y;
        guint lw = layer->width;
        guint lh = layer->height;
        if (lx < 0 || ly < 0 || (guint)lx >= lw || (guint)ly >= lh) {
            return FALSE;
        }
        cairo_surface_flush(layer->surface);
        gint stride = cairo_image_surface_get_stride(layer->surface);
        sample_surface_at(layer->surface, (gint)lw, (gint)lh, stride, lx, ly, radius, r, g, b, a);
        return (*a > 0.0 || (radius > 0));
    } else {
        if (ix < 0 || iy < 0 || (guint)ix >= doc->width || (guint)iy >= doc->height) {
            return FALSE;
        }
        /* Use cached composite (get) instead of export_composite. Export builds a fresh
         * composite every time (marks all tiles dirty, full re-composite) and freezes
         * the UI on move/click. Get uses cache and only composites if dirty. */
        cairo_surface_t* composite = document_get_composite_surface(doc);
        if (!composite) {
            return FALSE;
        }
        cairo_surface_flush(composite);
        gint cw = cairo_image_surface_get_width(composite);
        gint ch = cairo_image_surface_get_height(composite);
        gint stride = cairo_image_surface_get_stride(composite);
        sample_surface_at(composite, cw, ch, stride, ix, iy, radius, r, g, b, a);
        /* Do NOT destroy composite – we don't own it (document cache). */
        return TRUE;
    }
}

static void color_picker_tool_mouse_down(Tool* tool, ImageDocument* doc, MouseEvent* event) {
    (void)tool;
    gint ix = event->x;
    gint iy = event->y;
    if (ix < 0 || iy < 0 || (guint)ix >= doc->width || (guint)iy >= doc->height) {
        return;
    }

    gdouble r, g, b, a;
    if (!sample_color(doc, ix, iy, &r, &g, &b, &a)) {
        return;
    }

    GdkRGBA color;
    color.red = r;
    color.green = g;
    color.blue = b;
    color.alpha = (a > 0.0) ? 1.0 : 0.0;
    tools_panel_set_foreground_color(&color);

    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx && ctx->tool_options_panel) {
        tool_options_panel_set_color_picker_preview(ctx->tool_options_panel, TRUE, r, g, b, a);
    }
}

static void color_picker_tool_mouse_move(Tool* tool, ImageDocument* doc, MouseEvent* event) {
    (void)tool;
    gint ix = event->x;
    gint iy = event->y;
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (!ctx) {
        return;
    }

    gboolean inside = (ix >= 0 && iy >= 0 && (guint)ix < doc->width && (guint)iy < doc->height);
    color_picker_schedule_preview(doc, ctx, ix, iy, inside);
}

static void color_picker_tool_mouse_up(Tool* tool, ImageDocument* doc, MouseEvent* event) {
    (void)tool;
    (void)doc;
    (void)event;
}

void tool_colorpicker_reset_preview_throttle(void) {
    if (g_cp_idle_id != 0) {
        g_source_remove(g_cp_idle_id);
        g_cp_idle_id = 0;
    }
    g_cp_pending.doc = NULL;
    g_cp_pending.ctx = NULL;
}

Tool* tool_colorpicker_create(void) {
    Tool* tool = tool_new("Color Picker", TOOL_COLOR_PICKER, GDK_CROSSHAIR, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }
    tool->mouse_down = color_picker_tool_mouse_down;
    tool->mouse_move = color_picker_tool_mouse_move;
    tool->mouse_up = color_picker_tool_mouse_up;
    tool->cursor = create_colorpicker_cursor();
    return tool;
}
