/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "tools/tool_move.h"
#include "app/settings.h"
#include "command.h"
#include "commands/command_move.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>

extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx, ImageDocument* doc);

/**
 * Create a snapshot of a Cairo surface
 * Returns a new surface with a copy of the source surface
 */
static cairo_surface_t* create_surface_snapshot(cairo_surface_t* source) {
    cairo_surface_t* snapshot;
    cairo_t* cr;
    int width, height;

    if (!source) {
        return NULL;
    }

    width = cairo_image_surface_get_width(source);
    height = cairo_image_surface_get_height(source);

    /* Create new surface with same format */
    snapshot = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    if (!snapshot) {
        return NULL;
    }

    /* Copy source to snapshot */
    cr = cairo_create(snapshot);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    return snapshot;
}

/**
 * Extract selected pixels from a layer to a new layer
 * Returns the new layer with extracted pixels, or NULL on error
 */
static ImageLayer* extract_selection_to_layer(struct ImageDocument* doc, struct ImageLayer* source_layer) {
    if (!doc || !source_layer || !source_layer->surface ||
        !doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        return NULL;
    }

    /* Calculate intersection of layer bounds and selection bounds in document coordinates */
    gint layer_x_min = source_layer->offset_x;
    gint layer_y_min = source_layer->offset_y;
    gint layer_x_max = source_layer->offset_x + source_layer->width;
    gint layer_y_max = source_layer->offset_y + source_layer->height;

    /* Clamp to document bounds */
    layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
    layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
    layer_x_max = (layer_x_max > doc->width) ? doc->width : layer_x_max;
    layer_y_max = (layer_y_max > doc->height) ? doc->height : layer_y_max;

    if (layer_x_max <= layer_x_min || layer_y_max <= layer_y_min) {
        return NULL; /* No intersection */
    }

    /* Create dirty rect for the layer region in document coordinates */
    DirtyRect layer_rect;
    dirty_rect_set(&layer_rect, layer_x_min, layer_y_min,
                   layer_x_max - layer_x_min, layer_y_max - layer_y_min);

    /* Get selection mask for this region */
    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data ||
        dirty_rect_is_empty(&actual_region)) {
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels within the region */
    gint sel_x_min = actual_region.x + actual_region.width;
    gint sel_y_min = actual_region.y + actual_region.height;
    gint sel_x_max = actual_region.x;
    gint sel_y_max = actual_region.y;

    /* Scan mask to find actual bounds */
    for (gint y = 0; y < region_mask->height; y++) {
        for (gint x = 0; x < region_mask->width; x++) {
            uint8_t mask_alpha = region_mask->data[y * region_mask->stride + x];
            if (mask_alpha > 0) {
                gint doc_x = actual_region.x + x;
                gint doc_y = actual_region.y + y;
                if (doc_x < sel_x_min)
                    sel_x_min = doc_x;
                if (doc_y < sel_y_min)
                    sel_y_min = doc_y;
                if (doc_x > sel_x_max)
                    sel_x_max = doc_x;
                if (doc_y > sel_y_max)
                    sel_y_max = doc_y;
            }
        }
    }

    if (sel_x_max < sel_x_min || sel_y_max < sel_y_min) {
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create new layer with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    /* Use original layer name for the new layer */
    const gchar* layer_name = source_layer->name ? source_layer->name : "Layer";
    ImageLayer* new_layer = layer_new(layer_name, new_width, new_height,
                                      TRUE, LAYER_BACKGROUND_TRANSPARENT,
                                      LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!new_layer) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Set new layer offset to the bounding box origin */
    new_layer->offset_x = sel_x_min;
    new_layer->offset_y = sel_y_min;

    /* Copy pixels from source layer to new layer, masked by selection */
    cairo_surface_flush(source_layer->surface);
    cairo_surface_flush(new_layer->surface);

    guchar* src_data = cairo_image_surface_get_data(source_layer->surface);
    gint src_stride = cairo_image_surface_get_stride(source_layer->surface);
    guchar* dst_data = cairo_image_surface_get_data(new_layer->surface);
    gint dst_stride = cairo_image_surface_get_stride(new_layer->surface);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint src_y = doc_y - source_layer->offset_y;
        gint mask_y = doc_y - actual_region.y;

        if (src_y < 0 || src_y >= source_layer->height) {
            continue; /* Outside source layer */
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue; /* Outside mask */
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint src_x = doc_x - source_layer->offset_x;
            gint mask_x = doc_x - actual_region.x;

            if (src_x < 0 || src_x >= source_layer->width) {
                continue; /* Outside source layer */
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue; /* Outside mask */
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from source to destination */
            /* Cairo ARGB32 format: BGRA in memory (little-endian) */
            guchar* src_pixel = src_data + src_y * src_stride + src_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            /* Read BGRA from source */
            guchar src_b = src_pixel[0];
            guchar src_g = src_pixel[1];
            guchar src_r = src_pixel[2];
            guchar src_a = src_pixel[3];

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_b;
                dst_pixel[1] = src_g;
                dst_pixel[2] = src_r;
                dst_pixel[3] = src_a;
            } else {
                /* Partially selected (feathered): apply mask to alpha
                 * Need to handle premultiplied alpha correctly */
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    /* Completely transparent */
                    dst_pixel[0] = 0;
                    dst_pixel[1] = 0;
                    dst_pixel[2] = 0;
                    dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply source color, then re-premultiply with new alpha */
                    /* Un-premultiply: convert from premultiplied to straight alpha */
                    uint16_t r = (src_r * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_g * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_b * 255 + src_a / 2) / src_a;

                    /* Clamp to valid range */
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    /* Re-premultiply with new alpha */
                    dst_pixel[0] = (b * new_alpha + 127) / 255; /* B */
                    dst_pixel[1] = (g * new_alpha + 127) / 255; /* G */
                    dst_pixel[2] = (r * new_alpha + 127) / 255; /* R */
                    dst_pixel[3] = new_alpha;                   /* A */
                } else {
                    /* Source was transparent */
                    dst_pixel[0] = 0;
                    dst_pixel[1] = 0;
                    dst_pixel[2] = 0;
                    dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(new_layer->surface);
    cairo_surface_mark_dirty(source_layer->surface);

    /* Clear pixels from source layer where selection is active
     * For feathered selections, reduce alpha proportionally based on mask alpha */
    for (gint y = 0; y < source_layer->height; y++) {
        gint doc_y = source_layer->offset_y + y;
        gint mask_y = doc_y - actual_region.y;

        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue;
        }

        for (gint x = 0; x < source_layer->width; x++) {
            gint doc_x = source_layer->offset_x + x;
            gint mask_x = doc_x - actual_region.x;

            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue;
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha > 0) {
                /* Cairo ARGB32 format: BGRA in memory (little-endian) */
                guchar* src_pixel = src_data + y * src_stride + x * 4;

                /* Read BGRA from source */
                guchar src_b = src_pixel[0];
                guchar src_g = src_pixel[1];
                guchar src_r = src_pixel[2];
                guchar src_a = src_pixel[3];

                if (mask_alpha == 255) {
                    /* Fully selected: completely clear pixel */
                    src_pixel[0] = 0;
                    src_pixel[1] = 0;
                    src_pixel[2] = 0;
                    src_pixel[3] = 0;
                } else {
                    /* Partially selected (feathered): reduce alpha proportionally
                     * New alpha = original_alpha * (1 - mask_alpha/255)
                     * Need to handle premultiplied alpha correctly */
                    uint8_t new_alpha = (uint8_t)((src_a * (255 - mask_alpha)) / 255);

                    if (new_alpha < 1) {
                        /* Alpha becomes too small: clear completely */
                        src_pixel[0] = 0;
                        src_pixel[1] = 0;
                        src_pixel[2] = 0;
                        src_pixel[3] = 0;
                    } else if (src_a > 0) {
                        /* Un-premultiply source color, then re-premultiply with new alpha */
                        /* Un-premultiply: convert from premultiplied to straight alpha */
                        uint16_t r = (src_r * 255 + src_a / 2) / src_a;
                        uint16_t g = (src_g * 255 + src_a / 2) / src_a;
                        uint16_t b = (src_b * 255 + src_a / 2) / src_a;

                        /* Clamp to valid range */
                        if (r > 255)
                            r = 255;
                        if (g > 255)
                            g = 255;
                        if (b > 255)
                            b = 255;

                        /* Re-premultiply with new alpha */
                        src_pixel[0] = (b * new_alpha + 127) / 255; /* B */
                        src_pixel[1] = (g * new_alpha + 127) / 255; /* G */
                        src_pixel[2] = (r * new_alpha + 127) / 255; /* R */
                        src_pixel[3] = new_alpha;                   /* A */
                    } else {
                        /* Source was already transparent */
                        src_pixel[0] = 0;
                        src_pixel[1] = 0;
                        src_pixel[2] = 0;
                        src_pixel[3] = 0;
                    }
                }
            }
        }
    }

    cairo_surface_mark_dirty(source_layer->surface);
    selection_mask_free(region_mask);

    /* Invalidate caches */
    layer_invalidate_cache(source_layer);
    layer_invalidate_cache(new_layer);

    return new_layer;
}

/**
 * Move Tool state
 */
typedef struct {
    gboolean is_dragging;               /* Currently dragging? */
    gboolean selection_extracted;       /* TRUE if we extracted selection to a new layer */
    gdouble start_widget_x;             /* Mouse down position X in widget coordinates */
    gdouble start_widget_y;             /* Mouse down position Y in widget coordinates */
    gint initial_offset_x;              /* Layer offset at drag start */
    gint initial_offset_y;              /* Layer offset at drag start */
    gint last_offset_x;                 /* Last known offset (for dirty rect tracking) */
    gint last_offset_y;                 /* Last known offset (for dirty rect tracking) */
    struct ImageDocument* doc;          /* Document reference for coordinate conversion */
    struct ImageLayer* active_layer;    /* Layer being moved */
    struct ImageLayer* original_layer;  /* Original layer before extraction (for clearing) */
    struct ImageLayer* extracted_layer; /* Extracted layer (for command creation) */
    cairo_surface_t* original_snapshot; /* Snapshot of original layer BEFORE extraction (for undo) */
    /* Snap / smart-guide state (updated each mouse_move, cleared on mouse_up) */
    gboolean snap_guide_h; /* Horizontal guide line active */
    gboolean snap_guide_v; /* Vertical guide line active */
    gint snap_guide_h_pos; /* Document-space Y coordinate of horizontal guide */
    gint snap_guide_v_pos; /* Document-space X coordinate of vertical guide */
} MoveToolState;

/**
 * Find the topmost visible layer at a specific document coordinate
 * Returns the layer that has a visible pixel at the given position
 */
static struct ImageLayer* find_layer_at_point(struct ImageDocument* doc, gint doc_x, gint doc_y) {
    GList* iter;
    struct ImageLayer* layer;
    guchar* data;
    gint stride;
    gint layer_x, layer_y;
    guchar* pixel;
    guchar alpha;

    if (!doc || !doc->layers) {
        return NULL;
    }

    /* Iterate through layers from top to bottom */
    for (iter = g_list_last(doc->layers); iter; iter = iter->prev) {
        layer = (struct ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Text (vector) layers have no pixel surface; hit-test their box instead */
        if (layer->layer_type == LAYER_TYPE_TEXT && layer->text_data) {
            TextLayer* tl = (TextLayer*)layer->text_data;
            gint bx = (gint)tl->box_x + layer->offset_x;
            gint by = (gint)tl->box_y + layer->offset_y;
            gint bw = (gint)tl->box_width;
            gint bh = (gint)tl->box_height;
            if (doc_x >= bx && doc_x < bx + bw && doc_y >= by && doc_y < by + bh)
                return layer;
            continue;
        }

        if (!layer->surface) {
            continue;
        }

        /* Check if point is within layer bounds */
        if (doc_x < layer->offset_x || doc_y < layer->offset_y ||
            doc_x >= layer->offset_x + (gint)layer->width ||
            doc_y >= layer->offset_y + (gint)layer->height) {
            continue;
        }

        /* Convert to layer-local coordinates */
        layer_x = doc_x - layer->offset_x;
        layer_y = doc_y - layer->offset_y;

        /* Get pixel data */
        cairo_surface_flush(layer->surface);
        data = cairo_image_surface_get_data(layer->surface);
        stride = cairo_image_surface_get_stride(layer->surface);

        if (!data) {
            continue;
        }

        /* Check alpha at this pixel (Cairo uses BGRA format) */
        pixel = data + layer_y * stride + layer_x * 4;
        alpha = pixel[3]; /* Alpha channel */

        /* If pixel is visible (alpha > threshold), return this layer */
        if (alpha > 5) { /* Small threshold to handle anti-aliasing */
            return layer;
        }
    }

    return NULL;
}

/**
 * Try snapping a single axis value.
 *
 * For each target T the function checks whether the layer's leading edge,
 * trailing edge, or (optionally) center lies within @snap_dist of T.
 * The @allow_center array is parallel to @targets: allow_center[i]==TRUE
 * enables center-to-T snapping for targets[i].  Center snap should only be
 * enabled for *center-line* targets (canvas midpoint, another layer's center)
 * so that a large layer's center does not accidentally snap to a canvas edge.
 *
 * Within each target, trailing/center use <= so they beat an equidistant
 * leading-edge hit (intentional alignment wins over coincidence).
 * Across different targets, strict < is used so the first (highest-priority)
 * target in the array wins all ties.
 *
 * @param pos           Leading edge of the moving layer on this axis
 * @param size          Extent of the moving layer on this axis
 * @param targets       Array of snap target positions (document space)
 * @param allow_center  Parallel boolean array: TRUE enables center snap for that target
 * @param n_targets     Number of entries in @targets / @allow_center
 * @param snap_dist     Maximum pixel distance to trigger a snap
 * @param snapped_out   Receives the adjusted leading-edge position when snapping
 * @param guide_out     Receives the winning target T (where the guide line draws)
 * @return              TRUE if a snap was found, FALSE otherwise
 */
static gboolean snap_axis(gint pos, gint size,
                          const gint* targets, const gboolean* allow_center,
                          gint n_targets, gint snap_dist,
                          gint* snapped_out, gint* guide_out) {
    gint best_delta = snap_dist + 1; /* one beyond threshold — no snap yet */
    gint best_snapped = pos;
    gint best_guide = 0;

    for (gint i = 0; i < n_targets; i++) {
        gint T = targets[i];
        gint td = snap_dist + 1; /* best d for this T */
        gint ts = pos;           /* best snapped pos for this T */

        /* Leading edge → T */
        gint d = pos - T;
        if (d < 0)
            d = -d;
        if (d < td) {
            td = d;
            ts = T;
        }

        /* Trailing edge → T (preferred over leading within this T) */
        d = (pos + size) - T;
        if (d < 0)
            d = -d;
        if (d <= td) {
            td = d;
            ts = T - size;
        }

        /* Center → T — only for explicit center-line targets so that a wide
         * layer's center does not accidentally lock to a canvas edge. */
        if (allow_center[i]) {
            d = (pos + size / 2) - T;
            if (d < 0)
                d = -d;
            if (d <= td) {
                td = d;
                ts = T - size / 2;
            }
        }

        /* Strict < across targets: first (highest-priority) target wins ties */
        if (td < best_delta) {
            best_delta = td;
            best_snapped = ts;
            best_guide = T;
        }
    }

    if (best_delta <= snap_dist) {
        if (snapped_out)
            *snapped_out = best_snapped;
        if (guide_out)
            *guide_out = best_guide;
        return TRUE;
    }
    return FALSE;
}

/**
 * Apply magnetic snapping to the proposed layer position.
 * Reads snap settings from the document's AppContext and updates guide state
 * in @state.  Works for both raster and text layers.
 *
 * @param doc       Active document
 * @param state     Move tool state (guide fields written here)
 * @param new_x     Proposed X (leading edge, document space) — may be adjusted
 * @param new_y     Proposed Y (leading edge, document space) — may be adjusted
 * @param layer_w   Width of moving layer in document space
 * @param layer_h   Height of moving layer in document space
 * @param out_x     Snapped X written here
 * @param out_y     Snapped Y written here
 */
static void compute_snap(struct ImageDocument* doc, MoveToolState* state,
                         gint new_x, gint new_y,
                         gint layer_w, gint layer_h,
                         gint* out_x, gint* out_y) {
    /* Retrieve settings */
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    Settings* settings = ctx ? ctx->settings : NULL;

    state->snap_guide_h = FALSE;
    state->snap_guide_v = FALSE;

    if (!settings || !settings_get_mouse_snap(settings)) {
        *out_x = new_x;
        *out_y = new_y;
        return;
    }

    gint snap_dist = settings_get_mouse_snap_distance(settings);
    gboolean to_canvas = settings_get_mouse_snap_to_canvas_edges(settings);
    gboolean to_centerlines = settings_get_mouse_snap_to_centerlines(settings);
    gboolean to_layers = settings_get_mouse_snap_to_layers(settings);

    /* Build X and Y target arrays with parallel allow_center flags.
     *
     * Priority order within ties (first entry wins on equal distance):
     *   1. Canvas edges         — NO centre snap.  Evaluated first so that an
     *                             edge-to-edge tie beats a coincidental
     *                             trailing/leading-edge-to-centreline tie.
     *   2. Canvas centre-lines  — centre snap allowed.  Wins only when d < any
     *                             canvas-edge d (e.g. exact centre alignment).
     *   3. Other-layer edges    — NO centre snap.
     *   4. Other-layer centres  — centre snap allowed.
     *
     * max per-axis: 2 edges + 1 centreline + n_layers*(2 edges + 1 centre) = 3 + 3*n_layers */
    guint n_layers = doc->layers ? g_list_length(doc->layers) : 0;
    gint max_targets = 3 + (gint)(n_layers * 3) + 1;
    gint* x_targets = g_new(gint, max_targets);
    gint* y_targets = g_new(gint, max_targets);
    gboolean* x_allow_center = g_new(gboolean, max_targets);
    gboolean* y_allow_center = g_new(gboolean, max_targets);
    gint nx = 0, ny = 0;

#define ADD_X(val, cen)             \
    do {                            \
        x_targets[nx] = (val);      \
        x_allow_center[nx] = (cen); \
        nx++;                       \
    } while (0)
#define ADD_Y(val, cen)             \
    do {                            \
        y_targets[ny] = (val);      \
        y_allow_center[ny] = (cen); \
        ny++;                       \
    } while (0)

    /* 1. Canvas edges (centre snap disabled) — wins ties against centrelines */
    if (to_canvas) {
        ADD_X(0, FALSE);
        ADD_X((gint)doc->width, FALSE);
        ADD_Y(0, FALSE);
        ADD_Y((gint)doc->height, FALSE);
    }

    /* 2. Canvas centre-lines (centre snap enabled) */
    if (to_centerlines) {
        ADD_X((gint)doc->width / 2, TRUE);
        ADD_Y((gint)doc->height / 2, TRUE);
    }

    /* 3+4. Other visible layers */
    if (to_layers && doc->layers) {
        /* Pass A: layer edges (centre snap disabled) */
        for (GList* iter = doc->layers; iter; iter = iter->next) {
            struct ImageLayer* lyr = (struct ImageLayer*)iter->data;
            if (!lyr || !lyr->visible || lyr == state->active_layer)
                continue;
            gint lx, ly, lw, lh;
            if (lyr->layer_type == LAYER_TYPE_TEXT && lyr->text_data) {
                TextLayer* tl = (TextLayer*)lyr->text_data;
                lx = (gint)tl->box_x + lyr->offset_x;
                ly = (gint)tl->box_y + lyr->offset_y;
                lw = (gint)tl->box_width;
                lh = (gint)tl->box_height;
            } else {
                lx = lyr->offset_x;
                ly = lyr->offset_y;
                lw = (gint)lyr->width;
                lh = (gint)lyr->height;
            }
            ADD_X(lx,      FALSE);
            ADD_X(lx + lw, FALSE);
            ADD_Y(ly,      FALSE);
            ADD_Y(ly + lh, FALSE);
        }

        /* Pass B: layer centres (centre snap enabled) */
        if (to_centerlines) {
            for (GList* iter = doc->layers; iter; iter = iter->next) {
                struct ImageLayer* lyr = (struct ImageLayer*)iter->data;
                if (!lyr || !lyr->visible || lyr == state->active_layer)
                    continue;
                gint lx, ly, lw, lh;
                if (lyr->layer_type == LAYER_TYPE_TEXT && lyr->text_data) {
                    TextLayer* tl = (TextLayer*)lyr->text_data;
                    lx = (gint)tl->box_x + lyr->offset_x;
                    ly = (gint)tl->box_y + lyr->offset_y;
                    lw = (gint)tl->box_width;
                    lh = (gint)tl->box_height;
                } else {
                    lx = lyr->offset_x;
                    ly = lyr->offset_y;
                    lw = (gint)lyr->width;
                    lh = (gint)lyr->height;
                }
                ADD_X(lx + lw / 2, TRUE);
                ADD_Y(ly + lh / 2, TRUE);
            }
        }
    }

#undef ADD_X
#undef ADD_Y

    /* Snap each axis.  Use the boolean return value — not a position comparison
     * — to gate the guide, so the guide stays visible even when the layer is
     * already sitting exactly on a snap target. */
    gint snapped_x = new_x, guide_x = 0;
    gint snapped_y = new_y, guide_y = 0;
    gboolean x_snapped = snap_axis(new_x, layer_w, x_targets, x_allow_center, nx, snap_dist, &snapped_x, &guide_x);
    gboolean y_snapped = snap_axis(new_y, layer_h, y_targets, y_allow_center, ny, snap_dist, &snapped_y, &guide_y);

    if (x_snapped) {
        state->snap_guide_v = TRUE;
        state->snap_guide_v_pos = guide_x;
    }
    if (y_snapped) {
        state->snap_guide_h = TRUE;
        state->snap_guide_h_pos = guide_y;
    }

    g_free(x_targets);
    g_free(y_targets);
    g_free(x_allow_center);
    g_free(y_allow_center);

    *out_x = snapped_x;
    *out_y = snapped_y;
}

/**
 * Move tool: mouse down - start dragging
 */
static void move_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    struct ImageLayer* active_layer;
    ToolOptions* opts;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(MoveToolState));
    }
    state = (MoveToolState*)tool->user_data;

    /* Get tool options to check auto-select mode */
    opts = tool_options_get_for_tool(TOOL_MOVE);

    /* Determine which layer to operate on */
    if (opts && opts->move_auto_select_layer) {
        /* Auto-select: find layer under cursor based on pixel visibility */
        active_layer = find_layer_at_point(doc, event->x, event->y);
        if (!active_layer) {
            /* No visible layer at cursor, fall back to selected layer */
            active_layer = document_get_selected_layer(doc);
        } else {
            /* Update document's selected layer to match auto-detected layer */
            document_set_selected_layer(doc, active_layer);

            /* Update layers panel if available */
            if (tool->app_context) {
                LayersPanel* layers_panel = NULL;
                if (doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
                    GtkWidget* widget = doc->drawing_area;
                    while (widget && !GTK_IS_WINDOW(widget)) {
                        widget = gtk_widget_get_parent(widget);
                    }
                    if (widget && GTK_IS_WINDOW(widget)) {
                        layers_panel = (LayersPanel*)g_object_get_data(
                            G_OBJECT(widget), "layers_panel");
                    }
                }
                if (layers_panel) {
                    layers_panel_select_layer(layers_panel, doc, active_layer);
                }
            }
        }
    } else {
        /* Use currently selected layer */
        active_layer = document_get_selected_layer(doc);
    }

    if (!active_layer) {
        return;
    }

    /* Check if we have a selection - if so, extract it to a new layer */
    state->selection_extracted = FALSE;
    state->original_layer = active_layer;
    state->original_snapshot = NULL;

    if (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        /* Take snapshot of original layer BEFORE extraction (for undo) */
        if (active_layer->surface) {
            state->original_snapshot = create_surface_snapshot(active_layer->surface);
        }

        ImageLayer* extracted_layer = extract_selection_to_layer(doc, active_layer);
        if (extracted_layer) {
            /* Add new layer to document */
            doc->layers = g_list_append(doc->layers, extracted_layer);

            /* Store reference to command that will be created (will be finalized in mouse_up) */
            /* The command will be created and pushed in mouse_up after moving is complete */

            /* Set the extracted layer as active */
            document_set_selected_layer(doc, extracted_layer);

            /* Update layers panel if available */
            if (tool->app_context) {
                /* app_context is AppContext*, not a GObject, so we need to get window from it */
                /* We need to include ui.h to access AppContext, but to avoid circular deps,
                 * we'll get the window from the document's drawing_area instead */
                LayersPanel* layers_panel = NULL;
                if (doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
                    /* Get window from drawing_area's parent hierarchy */
                    GtkWidget* widget = doc->drawing_area;
                    while (widget && !GTK_IS_WINDOW(widget)) {
                        widget = gtk_widget_get_parent(widget);
                    }
                    if (widget && GTK_IS_WINDOW(widget)) {
                        layers_panel = (LayersPanel*)g_object_get_data(
                            G_OBJECT(widget), "layers_panel");
                    }
                }
                if (layers_panel) {
                    layers_panel_update(layers_panel, doc);
                    layers_panel_select_layer(layers_panel, doc, extracted_layer);
                }
            }

            /* Use the extracted layer for moving */
            active_layer = extracted_layer;
            state->selection_extracted = TRUE;
            state->extracted_layer = extracted_layer;

            /* Invalidate composite */
            document_invalidate_composite(doc);
        }
    }

    /* Start dragging - convert document coordinates back to widget coordinates for accurate delta calculation */
    state->is_dragging = TRUE;
    state->start_widget_x = (gdouble)event->x * doc->zoom_factor;
    state->start_widget_y = (gdouble)event->y * doc->zoom_factor;
    state->doc = doc;
    state->active_layer = active_layer;

    /* For text (vector) layers store the box origin as the initial position */
    if (active_layer->layer_type == LAYER_TYPE_TEXT && active_layer->text_data) {
        TextLayer* tl = (TextLayer*)active_layer->text_data;
        state->initial_offset_x = (gint)tl->box_x;
        state->initial_offset_y = (gint)tl->box_y;
    } else {
        state->initial_offset_x = active_layer->offset_x;
        state->initial_offset_y = active_layer->offset_y;
    }
    state->last_offset_x = state->initial_offset_x;
    state->last_offset_y = state->initial_offset_y;

    /* Trigger viewport redraw to show the outline overlay */
    if (doc->viewport) {
        gtk_widget_queue_draw(doc->viewport);
    }

    // printf("Move tool: started dragging layer at (%d, %d)\n", event->x, event->y);
}

/**
 * Move tool: mouse move - update layer offset
 * Optimized to only invalidate old and new regions
 */
static void move_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    gint dx, dy;
    gint old_x, old_y, new_x, new_y;
    DirtyRect old_rect, new_rect, union_rect;
    gint brush_margin = 2; /* Small margin for anti-aliasing */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState*)tool->user_data;

    if (!state->is_dragging || !state->active_layer) {
        return;
    }

    /* Convert current position to widget coordinates and calculate delta in widget space
     * This avoids accumulating rounding errors from coordinate conversion */
    gdouble current_widget_x = (gdouble)event->x * doc->zoom_factor;
    gdouble current_widget_y = (gdouble)event->y * doc->zoom_factor;
    gdouble delta_widget_x = current_widget_x - state->start_widget_x;
    gdouble delta_widget_y = current_widget_y - state->start_widget_y;

    /* Convert delta to document coordinates with proper rounding */
    gdouble delta_doc_x = delta_widget_x / doc->zoom_factor;
    gdouble delta_doc_y = delta_widget_y / doc->zoom_factor;

    /* Calculate new position with proper rounding */
    new_x = state->initial_offset_x + (gint)(delta_doc_x + 0.5);
    new_y = state->initial_offset_y + (gint)(delta_doc_y + 0.5);

    /* Determine layer size for snapping (text vs raster) */
    gint snap_layer_w, snap_layer_h;
    if (state->active_layer->layer_type == LAYER_TYPE_TEXT &&
        state->active_layer->text_data) {
        TextLayer* tl_snap = (TextLayer*)state->active_layer->text_data;
        snap_layer_w = (gint)tl_snap->box_width;
        snap_layer_h = (gint)tl_snap->box_height;
    } else {
        snap_layer_w = (gint)state->active_layer->width;
        snap_layer_h = (gint)state->active_layer->height;
    }
    compute_snap(doc, state, new_x, new_y, snap_layer_w, snap_layer_h, &new_x, &new_y);

    /* Get old position (from last update) */
    old_x = state->last_offset_x;
    old_y = state->last_offset_y;

    /* If position hasn't changed, do nothing */
    if (old_x == new_x && old_y == new_y) {
        return;
    }

    /* Calculate old region (where layer was last frame) */
    dirty_rect_set(&old_rect, old_x, old_y,
                   state->active_layer->width,
                   state->active_layer->height);
    dirty_rect_clamp(&old_rect, doc->width, doc->height);

    /* Calculate new region (where layer is now) */
    dirty_rect_set(&new_rect, new_x, new_y,
                   state->active_layer->width,
                   state->active_layer->height);
    dirty_rect_clamp(&new_rect, doc->width, doc->height);

    /* Union both regions */
    dirty_rect_union(&old_rect, &new_rect, &union_rect);

    /* Text (vector) layers: move by updating box coordinates directly */
    if (state->active_layer->layer_type == LAYER_TYPE_TEXT &&
        state->active_layer->text_data) {
        TextLayer* tl = (TextLayer*)state->active_layer->text_data;
        tl->box_x = (double)new_x;
        tl->box_y = (double)new_y;
        state->last_offset_x = new_x;
        state->last_offset_y = new_y;
        document_invalidate_composite(doc);
        if (doc->drawing_area)
            gtk_widget_queue_draw(doc->drawing_area);
        if (doc->viewport)
            gtk_widget_queue_draw(doc->viewport);
        return;
    }

    /* Update layer offset AFTER calculating dirty regions but BEFORE invalidation
     * This ensures tiles are composited with the layer at its final position */
    state->active_layer->offset_x = new_x;
    state->active_layer->offset_y = new_y;

    /* Update last known position */
    state->last_offset_x = new_x;
    state->last_offset_y = new_y;

    /* Invalidate the union of old and new regions */
    if (!dirty_rect_is_empty(&union_rect)) {
        document_invalidate_region(doc, &union_rect);
    }

    /* Also trigger viewport redraw to update the outline overlay */
    if (doc->viewport) {
        gtk_widget_queue_draw(doc->viewport);
    }
}

/**
 * Move tool: mouse up - end dragging and create undo command
 */
static void move_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    Command* cmd;
    AppContext* ctx;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState*)tool->user_data;

    if (!state->is_dragging) {
        return;
    }

    /* Text layers store position in box_x/y, not offset_x/y.
     * Mark document modified when a text layer was moved. */
    if (state->active_layer &&
        state->active_layer->layer_type == LAYER_TYPE_TEXT &&
        state->active_layer->text_data) {
        TextLayer* tl = (TextLayer*)state->active_layer->text_data;
        if ((gint)tl->box_x != state->initial_offset_x ||
            (gint)tl->box_y != state->initial_offset_y) {
            doc->modified = TRUE;
        }
    }

    /* Check if we actually moved (raster layers) */
    if (state->active_layer &&
        state->active_layer->layer_type != LAYER_TYPE_TEXT &&
        (state->active_layer->offset_x != state->initial_offset_x ||
         state->active_layer->offset_y != state->initial_offset_y)) {

        /* Create undo command for the move */
        /* Use move_selected_pixels command if selection was extracted, otherwise use regular move command */
        if (state->selection_extracted && state->extracted_layer) {
            cmd = command_create_move_selected_pixels_with_snapshot(
                doc,
                state->extracted_layer,
                state->original_layer,
                state->initial_offset_x,
                state->initial_offset_y,
                state->active_layer->offset_x,
                state->active_layer->offset_y,
                state->original_snapshot);
            /* Transfer ownership of snapshot to command */
            state->original_snapshot = NULL;
        } else {
            cmd = command_create_move(
                state->active_layer,
                state->initial_offset_x,
                state->initial_offset_y,
                state->active_layer->offset_x,
                state->active_layer->offset_y);
        }

        if (cmd) {
            document_push_undo_command(doc, cmd);

            /* Execute command to apply it (clears selection, etc.) */
            /* This is needed because commands are not auto-executed when pushed */
            command_execute(cmd, doc);

            /* Update UI */
            ctx = (AppContext*)tool->app_context;
            if (ctx) {
                ui_update_menu_and_button_states(ctx);
                ui_update_window_title(ctx, NULL);
            }
        } else if (state->original_snapshot) {
            /* Command creation failed, free the snapshot */
            cairo_surface_destroy(state->original_snapshot);
            state->original_snapshot = NULL;
        }

        /* Layer was moved - mark document as modified */
        doc->modified = TRUE;
        // printf("Move tool: layer moved - document marked as modified\n");
    }

    /* Clean up snapshot if not used (shouldn't happen, but be safe) */
    if (state->original_snapshot) {
        cairo_surface_destroy(state->original_snapshot);
        state->original_snapshot = NULL;
    }

    state->is_dragging = FALSE;
    state->selection_extracted = FALSE;
    state->active_layer = NULL;
    state->original_layer = NULL;
    state->extracted_layer = NULL;
    state->snap_guide_h = FALSE;
    state->snap_guide_v = FALSE;

    /* Clear the outline overlay by redrawing viewport */
    if (doc->viewport) {
        gtk_widget_queue_draw(doc->viewport);
    }
}

/**
 * Draw move tool preview - shows outline of layer being moved
 * Draws on top of viewport so entire layer bounds are visible even if partially off-canvas
 */
void tool_move_draw_preview(struct ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    ToolRegistry* tool_registry;
    Tool* active_tool;
    MoveToolState* state;
    AppContext* ctx;

    if (!doc || !doc->drawing_area || !cr) {
        return;
    }

    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");

    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return;
    }

    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_MOVE || !active_tool->user_data) {
        return;
    }

    state = (MoveToolState*)active_tool->user_data;

    if (!state->is_dragging || !state->active_layer) {
        return;
    }

    /* Determine which things need drawing before touching Cairo */
    gboolean draw_outline = ctx && ctx->settings && settings_get_show_layer_edges(ctx->settings);
    gboolean draw_guides = ctx && ctx->settings && settings_get_show_smart_guides(ctx->settings);

    if (!draw_outline && !draw_guides) {
        return;
    }

    /* Save Cairo state */
    cairo_save(cr);

    /* Resolve layer bounds — text layers store position in box_x/y. */
    gint layer_x, layer_y, layer_w, layer_h;
    if (state->active_layer->layer_type == LAYER_TYPE_TEXT &&
        state->active_layer->text_data) {
        TextLayer* tl = (TextLayer*)state->active_layer->text_data;
        layer_x = (gint)tl->box_x + state->active_layer->offset_x;
        layer_y = (gint)tl->box_y + state->active_layer->offset_y;
        layer_w = (gint)tl->box_width;
        layer_h = (gint)tl->box_height;
    } else {
        layer_x = state->active_layer->offset_x;
        layer_y = state->active_layer->offset_y;
        layer_w = (gint)state->active_layer->width;
        layer_h = (gint)state->active_layer->height;
    }

    /* Apply zoom transform */
    if (zoom != 1.0) {
        cairo_scale(cr, zoom, zoom);
    }

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Layer outline (only when Show Layer Edges is enabled) */
    if (draw_outline) {
        cairo_rectangle(cr, layer_x, layer_y, layer_w, layer_h);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 3.0 / zoom);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 1.0 / zoom);
        cairo_stroke(cr);
    }

    /* Smart guides (only when Show Smart Guides is enabled, independent of Show Layer Edges) */
    if (draw_guides && (state->snap_guide_h || state->snap_guide_v)) {
        /* Extend guide lines to cover the full layer extent, including any portion
         * that lies outside the canvas — matching how the layer outline itself is drawn. */
        gint ext_x_min = layer_x < 0 ? layer_x : 0;
        gint ext_x_max = (layer_x + layer_w) > (gint)doc->width ? (layer_x + layer_w) : (gint)doc->width;
        gint ext_y_min = layer_y < 0 ? layer_y : 0;
        gint ext_y_max = (layer_y + layer_h) > (gint)doc->height ? (layer_y + layer_h) : (gint)doc->height;

        if (state->snap_guide_v) {
            gdouble gx = (gdouble)state->snap_guide_v_pos;
            cairo_move_to(cr, gx, ext_y_min);
            cairo_line_to(cr, gx, ext_y_max);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_line_width(cr, 3.0 / zoom);
            cairo_stroke_preserve(cr);
            cairo_set_source_rgba(cr, 0.0, 0.502, 1.0, 1.0); /* #0080FF */
            cairo_set_line_width(cr, 1.0 / zoom);
            cairo_stroke(cr);
        }
        if (state->snap_guide_h) {
            gdouble gy = (gdouble)state->snap_guide_h_pos;
            cairo_move_to(cr, ext_x_min, gy);
            cairo_line_to(cr, ext_x_max, gy);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_line_width(cr, 3.0 / zoom);
            cairo_stroke_preserve(cr);
            cairo_set_source_rgba(cr, 0.0, 0.502, 1.0, 1.0); /* #0080FF */
            cairo_set_line_width(cr, 1.0 / zoom);
            cairo_stroke(cr);
        }
    }

    cairo_restore(cr);
}

/**
 * Create the Move Tool
 */
Tool* tool_move_create(void) {
    Tool* tool;

    /* Move tool has auto-select option */
    tool = tool_new("Move", TOOL_MOVE, GDK_FLEUR, TOOL_OPT_AUTO_SELECT);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = move_tool_mouse_down;
    tool->mouse_move = move_tool_mouse_move;
    tool->mouse_up = move_tool_mouse_up;

    // printf("Move tool created\n");

    return tool;
}
