/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "tools/tool_magic_wand_select.h"
#include "command.h"
#include "document.h"
#include "selection.h"
#include "selection/selection_mask.h"
#include "selection/selection_undo.h"
#include "selection/selection_undo_helpers.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Radius (in screen pixels) for hit-testing the start node */
#define NODE_HIT_RADIUS 7.0

static gdouble mw_pixel_distance(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                                 guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                                 FillCompareMode mode) {
    switch (mode) {
        case FILL_COMPARE_COLOR: {
            gdouble dr = (gdouble)r1 - r2;
            gdouble dg = (gdouble)g1 - g2;
            gdouble db = (gdouble)b1 - b2;
            return sqrt(dr * dr + dg * dg + db * db);
        }
        case FILL_COMPARE_COLOR_AND_OPACITY: {
            gdouble dr = (gdouble)r1 - r2;
            gdouble dg = (gdouble)g1 - g2;
            gdouble db = (gdouble)b1 - b2;
            gdouble da = (gdouble)a1 - a2;
            return sqrt(dr * dr + dg * dg + db * db + da * da * 0.5);
        }
        case FILL_COMPARE_LUMINANCE: {
            gdouble lum1 = 0.2126 * r1 + 0.7152 * g1 + 0.0722 * b1;
            gdouble lum2 = 0.2126 * r2 + 0.7152 * g2 + 0.0722 * b2;
            return fabs(lum1 - lum2);
        }
        case FILL_COMPARE_RED:
            return fabs((gdouble)r1 - r2);
        case FILL_COMPARE_GREEN:
            return fabs((gdouble)g1 - g2);
        case FILL_COMPARE_BLUE:
            return fabs((gdouble)b1 - b2);
        case FILL_COMPARE_ALPHA:
            return fabs((gdouble)a1 - a2);
        default: {
            gdouble dr = (gdouble)r1 - r2;
            gdouble dg = (gdouble)g1 - g2;
            gdouble db = (gdouble)b1 - b2;
            return sqrt(dr * dr + dg * dg + db * db);
        }
    }
}

static gdouble mw_max_distance(FillCompareMode mode) {
    switch (mode) {
        case FILL_COMPARE_COLOR:
            return 441.67;
        case FILL_COMPARE_COLOR_AND_OPACITY:
            return 477.06;
        default:
            return 255.0;
    }
}

static gboolean mw_pixel_matches(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                                 guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                                 gfloat tolerance, FillCompareMode mode) {
    gdouble threshold = (tolerance / 100.0) * mw_max_distance(mode);
    return mw_pixel_distance(r1, g1, b1, a1, r2, g2, b2, a2, mode) <= threshold;
}

/* ─────────────────────────────────────────────
 * Read a pixel from a Cairo surface (ARGB32, straight alpha out)
 * ───────────────────────────────────────────── */

static void surface_get_pixel(guchar* data, gint stride,
                              gint x, gint y,
                              guint8* r, guint8* g, guint8* b, guint8* a) {
    guchar* p = data + y * stride + x * 4;
    *b = p[0];
    *g = p[1];
    *r = p[2];
    *a = p[3];
    /* Un-premultiply */
    if (*a > 0 && *a < 255) {
        *r = (guint8)((*r * 255 + *a / 2) / *a);
        *g = (guint8)((*g * 255 + *a / 2) / *a);
        *b = (guint8)((*b * 255 + *a / 2) / *a);
        if (*r > 255)
            *r = 255;
        if (*g > 255)
            *g = 255;
        if (*b > 255)
            *b = 255;
    }
}

typedef struct {
    gint x, y;
} FillPt;

static SelectionMask* magic_wand_flood_fill(
    cairo_surface_t* surface,
    gint surface_w, gint surface_h,
    gint seed_x, gint seed_y,
    gint layer_offset_x, gint layer_offset_y,
    gfloat tolerance,
    FillCompareMode mode,
    gboolean contiguous) {
    if (!surface || seed_x < 0 || seed_x >= surface_w ||
        seed_y < 0 || seed_y >= surface_h)
        return NULL;

    cairo_surface_flush(surface);
    guchar* data = cairo_image_surface_get_data(surface);
    gint stride = cairo_image_surface_get_stride(surface);

    guint8 sr, sg, sb, sa;
    surface_get_pixel(data, stride, seed_x, seed_y, &sr, &sg, &sb, &sa);

    /* Build a temporary boolean hit-map in layer coordinates */
    gboolean* hit = (gboolean*)g_malloc0((gsize)surface_w * surface_h * sizeof(gboolean));
    if (!hit)
        return NULL;

    if (contiguous) {
        /* BFS flood fill */
        GQueue* q = g_queue_new();
        FillPt seed_pt = {seed_x, seed_y};
        g_queue_push_tail(q, g_memdup2(&seed_pt, sizeof(FillPt)));
        hit[seed_y * surface_w + seed_x] = TRUE;

        while (!g_queue_is_empty(q)) {
            FillPt* p = (FillPt*)g_queue_pop_head(q);
            gint cx = p->x, cy = p->y;
            g_free(p);

            guint8 pr, pg, pb, pa;
            surface_get_pixel(data, stride, cx, cy, &pr, &pg, &pb, &pa);

            if (!mw_pixel_matches(pr, pg, pb, pa, sr, sg, sb, sa, tolerance, mode)) {
                hit[cy * surface_w + cx] = FALSE;
                continue;
            }

            /* Enqueue 8-connected neighbours */
            for (gint dy = -1; dy <= 1; dy++) {
                for (gint dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0)
                        continue;
                    gint nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && nx < surface_w && ny >= 0 && ny < surface_h &&
                        !hit[ny * surface_w + nx]) {
                        hit[ny * surface_w + nx] = TRUE;
                        FillPt np = {nx, ny};
                        g_queue_push_tail(q, g_memdup2(&np, sizeof(FillPt)));
                    }
                }
            }
        }
        g_queue_free(q);
    } else {
        /* Global: mark every pixel that matches the seed */
        for (gint y = 0; y < surface_h; y++) {
            for (gint x = 0; x < surface_w; x++) {
                guint8 pr, pg, pb, pa;
                surface_get_pixel(data, stride, x, y, &pr, &pg, &pb, &pa);
                hit[y * surface_w + x] = mw_pixel_matches(
                    pr, pg, pb, pa, sr, sg, sb, sa, tolerance, mode);
            }
        }
    }

    /* Count matched pixels to decide if we have anything worth storing */
    gint hit_count = 0;
    for (gint i = 0; i < surface_w * surface_h; i++)
        if (hit[i])
            hit_count++;

    if (hit_count == 0) {
        g_free(hit);
        return NULL;
    }

    /* Create a bounded SelectionMask in document coordinates */
    int pad = 1;
    int doc_x0 = layer_offset_x - pad, doc_y0 = layer_offset_y - pad;
    int doc_x1 = layer_offset_x + surface_w + pad, doc_y1 = layer_offset_y + surface_h + pad;
    if (doc_x0 < 0)
        doc_x0 = 0;
    if (doc_y0 < 0)
        doc_y0 = 0;

    SelectionMask* mask = selection_mask_new_bounded(
        doc_x0, doc_y0, doc_x1 - doc_x0, doc_y1 - doc_y0);
    if (!mask) {
        g_free(hit);
        return NULL;
    }

    /* Write hit pixels into the mask */
    for (gint ly = 0; ly < surface_h; ly++) {
        for (gint lx = 0; lx < surface_w; lx++) {
            if (!hit[ly * surface_w + lx])
                continue;
            gint doc_x = lx + layer_offset_x;
            gint doc_y = ly + layer_offset_y;
            gint mx = doc_x - mask->offset_x;
            gint my = doc_y - mask->offset_y;
            if (mx >= 0 && mx < mask->width && my >= 0 && my < mask->height)
                mask->base_mask[my * mask->stride + mx] = 255;
        }
    }
    g_free(hit);

    mask->data = mask->base_mask;
    mask->dirty = TRUE;
    return mask;
}

static void magic_wand_get_options(MagicWandSelectToolState* state) {
    ToolOptions* opts = tool_options_get_for_tool(TOOL_MAGIC_WAND);
    if (opts) {
        state->combine_mode = tool_options_get_magicwand_combine(opts);
        state->smooth_mode = tool_options_get_magicwand_smooth(opts);
        state->feather_radius = tool_options_get_magicwand_feather(opts);
        state->animate = tool_options_get_magicwand_animate(opts);
        state->tolerance = tool_options_get_magicwand_tolerance(opts);
        state->compare_mode = tool_options_get_magicwand_compare_mode(opts);
        state->contiguous = tool_options_get_magicwand_contiguous(opts);
    } else {
        state->combine_mode = SELECTION_COMBINE_NEW;
        state->smooth_mode = SELECTION_SMOOTH_ANTIALIASED;
        state->feather_radius = 0.0f;
        state->animate = TRUE;
        state->tolerance = 15.0f;
        state->compare_mode = FILL_COMPARE_COLOR;
        state->contiguous = TRUE;
    }
}

static void magic_wand_recompute_preview(Tool* tool, ImageDocument* doc) {
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;
    if (!state || !state->has_start_point)
        return;

    /* Free any previous preview */
    if (state->preview_mask) {
        selection_mask_free(state->preview_mask);
        state->preview_mask = NULL;
    }

    struct ImageLayer* layer = document_get_selected_layer(doc);
    if (!layer || !layer->surface)
        return;

    /* Convert document coordinates to layer-local coordinates */
    gint lx = state->start_x - layer->offset_x;
    gint ly = state->start_y - layer->offset_y;
    gint lw = (gint)cairo_image_surface_get_width(layer->surface);
    gint lh = (gint)cairo_image_surface_get_height(layer->surface);

    if (lx < 0 || lx >= lw || ly < 0 || ly >= lh)
        return;

    state->preview_mask = magic_wand_flood_fill(
        layer->surface, lw, lh, lx, ly,
        layer->offset_x, layer->offset_y,
        state->tolerance, state->compare_mode, state->contiguous);

    /* Attach feather parameters to the bounded preview mask so
     * selection_mask_render_outline displays feathered edges.
     * Only done for FEATHERED mode to avoid an unnecessary EDT pass. */
    if (state->preview_mask &&
        state->smooth_mode == SELECTION_SMOOTH_FEATHERED &&
        state->feather_radius > 0.0f) {

        SelectionMask* pm = state->preview_mask;
        gsize pm_bytes    = (gsize)pm->stride * pm->height;

        Selection* ps = selection_new(
            pm->offset_x, pm->offset_y, pm->width, pm->height,
            SELECTION_COMBINE_NEW, state->smooth_mode, state->feather_radius);
        if (ps) {
            ps->mask = g_malloc(pm_bytes);
            if (ps->mask) {
                memcpy(ps->mask, pm->base_mask, pm_bytes);
                selection_mask_add_selection(pm, ps);
                /* Eagerly run the EDT so it is cached for the first render frame */
                selection_mask_get_surface(pm);
            } else {
                selection_unref(ps);
            }
            selection_unref(ps); /* list now holds the only live reference */
        }
    }
}

typedef struct {
    Tool* tool;
    ImageDocument* doc;
} MWTimerData;

static gboolean on_magic_wand_animation_timer(gpointer user_data) {
    MWTimerData* td = (MWTimerData*)user_data;
    if (!td || !td->tool || !td->doc || !td->tool->user_data) {
        if (td && td->tool && td->tool->user_data)
            ((MagicWandSelectToolState*)td->tool->user_data)->animation_timer_id = 0;
        g_free(td);
        return FALSE;
    }
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)td->tool->user_data;
    if (!td->doc->drawing_area || !state->has_start_point || !state->preview_mask) {
        state->animation_timer_id = 0;
        g_free(td);
        return FALSE;
    }
    state->animation_phase = (state->animation_phase > 0) ? state->animation_phase - 1 : 3;
    gtk_widget_queue_draw(td->doc->drawing_area);
    return TRUE;
}

static void magic_wand_start_animation(Tool* tool, ImageDocument* doc) {
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;
    if (state->animation_timer_id || !state->animate)
        return;
    MWTimerData* td = g_malloc(sizeof(MWTimerData));
    td->tool = tool;
    td->doc = doc;
    state->animation_timer_id = g_timeout_add(ANT_DASH_SPEED_SLOW,
                                              on_magic_wand_animation_timer, td);
}

static void magic_wand_stop_animation(MagicWandSelectToolState* state) {
    if (state->animation_timer_id) {
        g_source_remove(state->animation_timer_id);
        state->animation_timer_id = 0;
    }
}

static void magic_wand_tool_mouse_down(Tool* tool, ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc)
        return;

    if (!tool->user_data)
        tool->user_data = g_malloc0(sizeof(MagicWandSelectToolState));
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;

    magic_wand_get_options(state);

    gdouble zoom = doc->zoom_factor;
    gdouble hit_r = NODE_HIT_RADIUS / zoom;
    if (hit_r < 1.0)
        hit_r = 1.0;

    /* Check if click is on the existing start node */
    if (state->has_start_point) {
        gdouble dx = event->x - state->start_x;
        gdouble dy = event->y - state->start_y;
        if (dx * dx + dy * dy <= hit_r * hit_r) {
            /* Start dragging the node */
            state->is_dragging_node = TRUE;
            state->drag_anchor_x = event->x;
            state->drag_anchor_y = event->y;
            return;
        }

        /* Click elsewhere to discard current preview and start fresh */
        magic_wand_stop_animation(state);
        if (state->preview_mask) {
            selection_mask_free(state->preview_mask);
            state->preview_mask = NULL;
        }
        state->has_been_finalized = FALSE;
    }

    /* Place new start point */
    magic_wand_stop_animation(state);
    state->has_start_point = TRUE;
    state->start_x = event->x;
    state->start_y = event->y;
    state->is_dragging_node = FALSE;

    if (state->preview_mask) {
        selection_mask_free(state->preview_mask);
        state->preview_mask = NULL;
    }

    magic_wand_recompute_preview(tool, doc);

    if (state->preview_mask && state->animate)
        magic_wand_start_animation(tool, doc);

    gtk_widget_queue_draw(doc->drawing_area);
}

static void magic_wand_tool_mouse_move(Tool* tool, ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc || !tool->user_data)
        return;

    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;
    GdkWindow* win = gtk_widget_get_window(doc->drawing_area);

    if (state->is_dragging_node) {
        state->start_x += event->x - state->drag_anchor_x;
        state->start_y += event->y - state->drag_anchor_y;
        state->drag_anchor_x = event->x;
        state->drag_anchor_y = event->y;

        magic_wand_stop_animation(state);
        magic_wand_recompute_preview(tool, doc);

        if (state->preview_mask && state->animate)
            magic_wand_start_animation(tool, doc);

        if (win) {
            GdkCursor* c = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_FLEUR);
            if (c) {
                gdk_window_set_cursor(win, c);
                g_object_unref(c);
            }
        }
        gtk_widget_queue_draw(doc->drawing_area);
        return;
    }

    /* Update cursor when hovering over the node */
    if (state->has_start_point && win) {
        gdouble zoom = doc->zoom_factor;
        gdouble hit_r = NODE_HIT_RADIUS / zoom;
        if (hit_r < 1.0)
            hit_r = 1.0;
        gdouble dx = event->x - state->start_x;
        gdouble dy = event->y - state->start_y;
        if (dx * dx + dy * dy <= hit_r * hit_r) {
            GdkCursor* c = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_FLEUR);
            if (c) {
                gdk_window_set_cursor(win, c);
                g_object_unref(c);
            }
        } else {
            gdk_window_set_cursor(win, tool->cursor);
        }
    }
}

static void magic_wand_tool_mouse_up(Tool* tool, ImageDocument* doc, MouseEvent* event) {
    (void)event;
    if (!tool || !doc || !tool->user_data)
        return;
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;
    state->is_dragging_node = FALSE;
}

void tool_magic_wand_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    if (!doc || !cr)
        return;

    ToolRegistry* reg = (ToolRegistry*)g_object_get_data(
        G_OBJECT(doc->drawing_area), "tool_registry");
    if (!reg)
        return;

    Tool* active = tool_manager_get_active(reg);
    if (!active || active->type != TOOL_MAGIC_WAND || !active->user_data)
        return;

    MagicWandSelectToolState* state = (MagicWandSelectToolState*)active->user_data;
    if (!state->has_start_point)
        return;

    cairo_save(cr);
    if (zoom != 1.0)
        cairo_scale(cr, zoom, zoom);

    /* Draw the selection outline from the preview mask */
    if (state->preview_mask) {
        gint anim_phase = state->animate ? state->animation_phase : 0;
        selection_mask_render_outline(cr, state->preview_mask, anim_phase, zoom, TRUE);
    }

    /* Draw the draggable start node */
    gdouble node_r = NODE_HIT_RADIUS / zoom;
    if (node_r < 2.5)
        node_r = 2.5;

    cairo_arc(cr, state->start_x, state->start_y, node_r, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.7);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
    cairo_set_line_width(cr, 2.5 / zoom);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 1.0 / zoom);
    cairo_stroke(cr);

    cairo_restore(cr);
}

void tool_magic_wand_select_finalize(Tool* tool, ImageDocument* doc) {
    if (!tool || !tool->user_data || !doc || !doc->selection_mask)
        return;
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;

    if (!state->has_start_point || !state->preview_mask || state->has_been_finalized)
        return;

    state->has_been_finalized = TRUE;

    /* Determine the bounding box of the preview mask in document space */
    gint x0 = state->preview_mask->offset_x;
    gint y0 = state->preview_mask->offset_y;
    gint x1 = x0 + state->preview_mask->width;
    gint y1 = y0 + state->preview_mask->height;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (gint)doc->width)
        x1 = (gint)doc->width;
    if (y1 > (gint)doc->height)
        y1 = (gint)doc->height;

    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, "Magic Wand Select");
    if (transaction)
        selection_undo_transaction_register_region(transaction, x0, y0, x1 - x0, y1 - y0);

    /* Build a full-document Selection object so the selection system handles
     * feathering and combine-mode bookkeeping identically to other tools.
     *
     * For COMBINE_NEW, drop all existing selections first so the rebuild
     * produces a clean result (mirrors selection_mask_fill_rect behaviour). */
    {
        SelectionMask* dest = doc->selection_mask;
        SelectionMask* src  = state->preview_mask;

        if (state->combine_mode == SELECTION_COMBINE_NEW && dest->selections) {
            GList* iter;
            for (iter = dest->selections; iter; iter = iter->next) {
                Selection* old = (Selection*)iter->data;
                if (old) selection_unref(old);
            }
            g_list_free(dest->selections);
            dest->selections = NULL;
        }

        Selection* sel = selection_new(
            src->offset_x, src->offset_y, src->width, src->height,
            state->combine_mode, state->smooth_mode, state->feather_radius);

        if (sel) {
            /* sel->mask must be full-document-sized (same layout as dest->base_mask) */
            sel->mask = g_malloc0((gsize)dest->stride * dest->height);
            if (sel->mask) {
                /* Copy bounded source pixels to their document-space positions */
                for (gint sy = 0; sy < src->height; sy++) {
                    gint dy = sy + (src->offset_y - dest->offset_y);
                    if (dy < 0 || dy >= dest->height) continue;
                    for (gint sx = 0; sx < src->width; sx++) {
                        gint dx = sx + (src->offset_x - dest->offset_x);
                        if (dx < 0 || dx >= dest->width) continue;
                        sel->mask[dy * dest->stride + dx] =
                            src->base_mask[sy * src->stride + sx];
                    }
                }
                selection_mask_add_selection(dest, sel);
            } else {
                selection_unref(sel);
                sel = NULL;
            }
            if (sel) selection_unref(sel); /* list now holds the only live reference */
        }
    }

    if (transaction) {
        Command* cmd = selection_undo_transaction_commit(transaction);
        if (cmd) {
            selection_undo_commit_operation(doc, cmd);
            AppContext* ctx = (AppContext*)tool->app_context;
            if (ctx) {
                ui_update_menu_and_button_states(ctx);
                ui_update_window_title(ctx, NULL);
            }
            doc->modified = TRUE;
        }
    }

    /* Hide the tool overlay — the document selection mask now owns the display */
    magic_wand_stop_animation(state);
    state->has_start_point = FALSE;
    if (state->preview_mask) {
        selection_mask_free(state->preview_mask);
        state->preview_mask = NULL;
    }

    gtk_widget_queue_draw(doc->drawing_area);
}

void tool_magic_wand_select_reset(Tool* tool) {
    if (!tool || !tool->user_data)
        return;
    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;

    magic_wand_stop_animation(state);

    state->has_start_point = FALSE;
    state->is_dragging_node = FALSE;
    state->has_been_finalized = FALSE;
    state->animation_phase = 0;

    if (state->preview_mask) {
        selection_mask_free(state->preview_mask);
        state->preview_mask = NULL;
    }
}

void tool_magic_wand_select_update_preview(Tool* tool, ImageDocument* doc) {
    if (!tool || !tool->user_data || !doc)
        return;

    MagicWandSelectToolState* state = (MagicWandSelectToolState*)tool->user_data;

    /* Nothing to recompute if no seed has been placed yet */
    if (!state->has_start_point)
        return;

    /* Re-read options from ToolOptions so the latest UI values are used */
    magic_wand_get_options(state);

    /* Stop and clear the current preview */
    magic_wand_stop_animation(state);
    if (state->preview_mask) {
        selection_mask_free(state->preview_mask);
        state->preview_mask = NULL;
    }

    /* Recompute flood fill with updated options */
    magic_wand_recompute_preview(tool, doc);

    /* Restart animation if the fill produced a result */
    if (state->preview_mask && state->animate)
        magic_wand_start_animation(tool, doc);

    gtk_widget_queue_draw(doc->drawing_area);
}

Tool* tool_magic_wand_select_create(void) {
    Tool* tool = tool_new("Magic Wand",
                          TOOL_MAGIC_WAND,
                          GDK_CROSSHAIR,
                          TOOL_OPT_SELECTION_MODE | TOOL_OPT_SELECTION_SMOOTH);
    if (!tool)
        return NULL;

    tool->mouse_down = magic_wand_tool_mouse_down;
    tool->mouse_move = magic_wand_tool_mouse_move;
    tool->mouse_up = magic_wand_tool_mouse_up;
    return tool;
}
