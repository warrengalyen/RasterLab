#include "tools/tool_polygon_select.h"
#include "command.h"
#include "document.h"
#include "selection.h"
#include "selection/selection_mask.h"
#include "selection/selection_undo.h"
#include "selection/selection_undo_helpers.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>

typedef struct {
    Tool* tool;
    ImageDocument* doc;
} PolygonSelectTimerData;

static gboolean on_polygon_select_animation_timer(gpointer user_data);

#define NODE_HIT_RADIUS 6.0 /* in screen pixels, scaled by zoom for image space */

/**
 * Create cursor from resource
 */
static GdkCursor* create_polygon_select_cursor(void) {
    GdkDisplay* display;
    GdkPixbuf* pixbuf;
    GdkCursor* cursor;
    GError* error = NULL;
    GBytes* bytes;
    GInputStream* stream;

    display = gdk_display_get_default();
    if (!display) {
        return NULL;
    }

    bytes = g_resources_lookup_data("/cursors/polygon_select_cursor.cur",
                                    G_RESOURCE_LOOKUP_FLAGS_NONE,
                                    &error);
    if (!bytes) {
        if (error) {
            g_warning("Failed to load polygon select cursor resource: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    stream = g_memory_input_stream_new_from_bytes(bytes);

    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

    g_object_unref(stream);
    g_bytes_unref(bytes);

    if (!pixbuf) {
        if (error) {
            g_warning("Failed to parse polygon select cursor: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);

    cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    return cursor;
}

/**
 * Get polygon tool options
 */
static void polygon_select_get_options(PolygonSelectToolState* state) {
    ToolOptions* opts = tool_options_get_for_tool(TOOL_POLYGON_SELECT);
    if (opts) {
        state->combine_mode = tool_options_get_polygon_select_combine(opts);
        state->smooth_mode = tool_options_get_polygon_select_smooth(opts);
        state->feather_radius = (gint)tool_options_get_polygon_select_feather(opts);
        state->curvature = tool_options_get_polygon_select_curvature(opts);
        state->area_mode = tool_options_get_polygon_select_area(opts);
        state->border_width = tool_options_get_polygon_select_border_width(opts);
    } else {
        state->combine_mode = SELECTION_COMBINE_NEW;
        state->smooth_mode = SELECTION_SMOOTH_NONE;
        state->feather_radius = 0;
        state->curvature = 0.0f;
        state->area_mode = 0;
        state->border_width = 1;
    }
}

/**
 * Hit test: which node is at (x,y) in image space? Returns node index or -1.
 */
static gint polygon_node_at(PolygonSelectToolState* state, gdouble x, gdouble y, gdouble zoom) {
    if (!state->points || state->points->len == 0)
        return -1;
    gdouble radius = NODE_HIT_RADIUS / zoom;
    if (radius < 1.0)
        radius = 1.0;
    for (guint i = 0; i < state->points->len; i++) {
        GdkPoint* p = &g_array_index(state->points, GdkPoint, i);
        gdouble dx = x - p->x;
        gdouble dy = y - p->y;
        if (dx * dx + dy * dy <= radius * radius)
            return (gint)i;
    }
    return -1;
}

/**
 * Point-in-polygon test (ray casting)
 */
static gboolean point_in_polygon(GArray* points, gdouble px, gdouble py) {
    guint n = points->len;
    if (n < 3)
        return FALSE;
    gboolean inside = FALSE;
    for (guint i = 0, j = n - 1; i < n; j = i++) {
        GdkPoint* a = &g_array_index(points, GdkPoint, i);
        GdkPoint* b = &g_array_index(points, GdkPoint, j);
        if (((a->y > py) != (b->y > py)) &&
            (px < (b->x - a->x) * (py - a->y) / (b->y - a->y) + a->x))
            inside = !inside;
    }
    return inside;
}

static void polygon_select_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc)
        return;

    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(PolygonSelectToolState));
        ((PolygonSelectToolState*)tool->user_data)->points = g_array_new(FALSE, FALSE, sizeof(GdkPoint));
    }
    PolygonSelectToolState* state = (PolygonSelectToolState*)tool->user_data;
    gdouble zoom = doc->zoom_factor;

    polygon_select_get_options(state);

    if (state->closed && state->is_editing) {
        /* Editing: check node hit then interior for move */
        gint node = polygon_node_at(state, event->x, event->y, zoom);
        if (node >= 0) {
            state->dragging_node = node;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            gtk_widget_queue_draw(doc->drawing_area);
            return;
        }
        if (point_in_polygon(state->points, event->x, event->y)) {
            state->dragging_node = -1; /* move whole */
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            gtk_widget_queue_draw(doc->drawing_area);
            return;
        }
        /* Click outside: finalize and start new */
        tool_polygon_select_finalize(tool, doc);
        g_array_set_size(state->points, 0);
        state->closed = FALSE;
        state->is_editing = FALSE;
        state->has_been_finalized = FALSE;
        if (state->animation_timer_id) {
            g_source_remove(state->animation_timer_id);
            state->animation_timer_id = 0;
        }
    }

    /* Building or after reset: add point or close or drag existing node */
    gint node = polygon_node_at(state, event->x, event->y, zoom);
    guint n = state->points->len;

    if (n >= 3 && node == 0) {
        /* Click on first node: close polygon */
        state->closed = TRUE;
        state->is_editing = TRUE;
        state->dragging_node = -2;
        state->animation_phase = 0;
        ToolOptions* opts = tool_options_get_for_tool(TOOL_POLYGON_SELECT);
        if (opts && tool_options_get_polygon_select_animate(opts) && state->animation_timer_id == 0) {
            PolygonSelectTimerData* td = g_malloc(sizeof(PolygonSelectTimerData));
            td->tool = tool;
            td->doc = doc;
            state->animation_timer_id = g_timeout_add(ANT_DASH_SPEED_SLOW, on_polygon_select_animation_timer, td);
        }
        gtk_widget_queue_draw(doc->drawing_area);
        return;
    }
    if (node > 0) {
        /* Drag existing node (not first) */
        state->dragging_node = node;
        state->anchor_x = event->x;
        state->anchor_y = event->y;
        gtk_widget_queue_draw(doc->drawing_area);
        return;
    }

    /* Add new point */
    GdkPoint pt = {event->x, event->y};
    g_array_append_val(state->points, pt);
    state->cursor_x = event->x;
    state->cursor_y = event->y;
    gtk_widget_queue_draw(doc->drawing_area);
}

static void polygon_select_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc || !tool->user_data)
        return;

    PolygonSelectToolState* state = (PolygonSelectToolState*)tool->user_data;
    state->cursor_x = event->x;
    state->cursor_y = event->y;
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);
    gdouble zoom = doc->zoom_factor;

    if (state->dragging_node >= -1) {
        /* Dragging node or whole polygon */
        if (state->dragging_node == -1) {
            gint dx = event->x - state->anchor_x;
            gint dy = event->y - state->anchor_y;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            for (guint i = 0; i < state->points->len; i++) {
                GdkPoint* p = &g_array_index(state->points, GdkPoint, i);
                p->x += dx;
                p->y += dy;
            }
        } else {
            GdkPoint* p = &g_array_index(state->points, GdkPoint, (guint)state->dragging_node);
            p->x = event->x;
            p->y = event->y;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
        }
        if (window) {
            GdkCursor* move_cur = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_FLEUR);
            if (move_cur) {
                gdk_window_set_cursor(window, move_cur);
                g_object_unref(move_cur);
            }
        }
        gtk_widget_queue_draw(doc->drawing_area);
        return;
    }

    /* Update hover and cursor */
    gint node = polygon_node_at(state, event->x, event->y, zoom);
    gboolean interior = (state->closed && state->points->len >= 3 && point_in_polygon(state->points, event->x, event->y));
    state->hovered_node = node;
    state->hovered_interior = interior;

    if (node >= 0 || interior) {
        if (window) {
            GdkCursor* move_cur = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_FLEUR);
            if (move_cur) {
                gdk_window_set_cursor(window, move_cur);
                g_object_unref(move_cur);
            }
        }
    } else {
        if (window && tool->cursor)
            gdk_window_set_cursor(window, tool->cursor);
    }
    gtk_widget_queue_draw(doc->drawing_area);
}

static void polygon_select_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    (void)event;
    if (!tool || !doc || !tool->user_data)
        return;

    PolygonSelectToolState* state = (PolygonSelectToolState*)tool->user_data;
    if (state->dragging_node >= -1) {
        state->dragging_node = -2;
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

static gboolean on_polygon_select_animation_timer(gpointer user_data) {
    PolygonSelectTimerData* td = (PolygonSelectTimerData*)user_data;
    if (!td || !td->tool || !td->doc || !td->tool->user_data) {
        g_free(td);
        return FALSE;
    }
    PolygonSelectToolState* state = (PolygonSelectToolState*)td->tool->user_data;
    if (!state->closed) {
        state->animation_timer_id = 0;
        g_free(td);
        return FALSE;
    }
    state->animation_phase = (state->animation_phase > 0) ? state->animation_phase - 1 : 3;
    gtk_widget_queue_draw(td->doc->drawing_area);
    return TRUE;
}

void tool_polygon_select_finalize(Tool* tool, ImageDocument* doc) {
    if (!tool || !tool->user_data || !doc || !doc->selection_mask)
        return;

    PolygonSelectToolState* state = (PolygonSelectToolState*)tool->user_data;
    if (!state->closed || state->points->len < 3 || state->has_been_finalized)
        return;

    state->has_been_finalized = TRUE;

    polygon_select_get_options(state);

    guint n = state->points->len;
    double* px = g_malloc(n * sizeof(double));
    double* py = g_malloc(n * sizeof(double));
    for (guint i = 0; i < n; i++) {
        GdkPoint* p = &g_array_index(state->points, GdkPoint, i);
        px[i] = p->x;
        py[i] = p->y;
    }

    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, "Polygon Select");
    if (transaction) {
        int x1 = doc->width, y1 = doc->height, x2 = 0, y2 = 0;
        for (guint i = 0; i < n; i++) {
            if (px[i] < x1)
                x1 = (int)px[i];
            if (py[i] < y1)
                y1 = (int)py[i];
            if (px[i] > x2)
                x2 = (int)px[i];
            if (py[i] > y2)
                y2 = (int)py[i];
        }
        if (x1 < 0)
            x1 = 0;
        if (y1 < 0)
            y1 = 0;
        if (x2 >= (int)doc->width)
            x2 = (int)doc->width - 1;
        if (y2 >= (int)doc->height)
            y2 = (int)doc->height - 1;
        selection_undo_transaction_register_region(transaction, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
    }

    selection_mask_fill_polygon(doc->selection_mask,
                                px, py, (int)n,
                                state->combine_mode,
                                state->smooth_mode,
                                (float)state->feather_radius,
                                state->curvature,
                                state->area_mode,
                                state->border_width,
                                FALSE);

    g_free(px);
    g_free(py);

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
    gtk_widget_queue_draw(doc->drawing_area);
}

void tool_polygon_select_reset(Tool* tool) {
    if (!tool || !tool->user_data)
        return;
    PolygonSelectToolState* state = (PolygonSelectToolState*)tool->user_data;
    state->dragging_node = -2;
    state->closed = FALSE;
    state->is_editing = FALSE;
    state->has_been_finalized = FALSE;
    state->hovered_node = -1;
    state->hovered_interior = FALSE;
    if (state->points)
        g_array_set_size(state->points, 0);
    if (state->animation_timer_id) {
        g_source_remove(state->animation_timer_id);
        state->animation_timer_id = 0;
    }
}

Tool* tool_polygon_select_create(void) {
    Tool* tool = tool_new("Polygon Select",
                          TOOL_POLYGON_SELECT,
                          GDK_CROSSHAIR,
                          TOOL_OPT_SELECTION_MODE | TOOL_OPT_SELECTION_SMOOTH);
    if (!tool)
        return NULL;

    /* Replace cursor with custom polygon select cursor */
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }
    tool->cursor = create_polygon_select_cursor();

    tool->mouse_down = polygon_select_tool_mouse_down;
    tool->mouse_move = polygon_select_tool_mouse_move;
    tool->mouse_up = polygon_select_tool_mouse_up;
    return tool;
}

void tool_polygon_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    if (!doc || !doc->drawing_area || !cr)
        return;

    ToolRegistry* reg = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!reg)
        return;

    Tool* active = tool_manager_get_active(reg);
    if (!active || active->type != TOOL_POLYGON_SELECT || !active->user_data)
        return;

    PolygonSelectToolState* state = (PolygonSelectToolState*)active->user_data;
    if (!state->points || state->points->len == 0)
        return;

    cairo_save(cr);
    if (zoom != 1.0)
        cairo_scale(cr, zoom, zoom);

    guint n = state->points->len;
    gdouble handle_radius = (NODE_HIT_RADIUS / zoom);
    if (handle_radius < 2.0)
        handle_radius = 2.0;

    ToolOptions* opts = tool_options_get_for_tool(TOOL_POLYGON_SELECT);
    gint anim_phase = (state->closed && opts && tool_options_get_polygon_select_animate(opts))
                          ? state->animation_phase
                          : 0;

    /* Outline: use same rendering as rect/ellipse - SelectionMask when closed, marching ants path when building */
    if (state->closed && n >= 3) {
        /* Same style as finalized: SelectionMask + selection_mask_render_outline */
        polygon_select_get_options(state);

        /* Collect points from state once; we need them for the bounding-box calculation. */
        double* px = g_malloc(n * sizeof(double));
        double* py = g_malloc(n * sizeof(double));
        for (guint i = 0; i < n; i++) {
            GdkPoint* pt = &g_array_index(state->points, GdkPoint, i);
            px[i] = pt->x;
            py[i] = pt->y;
        }

        /* --- Bounding-box mask allocation (same optimisation as rect/ellipse tools) ---
         *
         * For interior (area_mode==0) and border (area_mode==2) selections, allocate
         * only a region large enough to contain the polygon plus the feather falloff zone.
         * This reduces the SelectionMask, the Cairo rasterisation surface, and the
         * distance-field buffers from O(doc_area) to O(selection_area).
         *
         * Exterior mode (area_mode==1) inverts the entire mask, so every pixel outside
         * the polygon becomes selected.  Using a bounded mask there would create a
         * spurious rectangular outline at the mask boundary, so we fall back to the
         * full-document mask in that case.
         */
        gboolean use_bounded = (state->area_mode != 1);

        SelectionMask* preview_mask = NULL;
        double* px_fill = px; /* points passed to fill_polygon (local or doc coords) */
        double* py_fill = py;
        double* px_local = NULL;
        double* py_local = NULL;
        int mask_x = 0, mask_y = 0; /* bounded mask origin in document space */

        if (use_bounded) {
            /* Tight bounding box of polygon vertices */
            double bbox_min_x = px[0], bbox_max_x = px[0];
            double bbox_min_y = py[0], bbox_max_y = py[0];
            for (guint i = 1; i < n; i++) {
                if (px[i] < bbox_min_x) bbox_min_x = px[i];
                if (px[i] > bbox_max_x) bbox_max_x = px[i];
                if (py[i] < bbox_min_y) bbox_min_y = py[i];
                if (py[i] > bbox_max_y) bbox_max_y = py[i];
            }

            int pad = (int)ceilf((float)state->feather_radius) + 2;
            int mask_x2 = (int)ceil(bbox_max_x) + pad;
            int mask_y2 = (int)ceil(bbox_max_y) + pad;
            mask_x = (int)floor(bbox_min_x) - pad;
            mask_y = (int)floor(bbox_min_y) - pad;
            if (mask_x < 0)                    mask_x = 0;
            if (mask_y < 0)                    mask_y = 0;
            if (mask_x2 > (int)doc->width)     mask_x2 = (int)doc->width;
            if (mask_y2 > (int)doc->height)    mask_y2 = (int)doc->height;
            int mask_w = mask_x2 - mask_x;
            int mask_h = mask_y2 - mask_y;

            preview_mask = selection_mask_new_bounded(mask_x, mask_y, mask_w, mask_h);

            /* Translate points to LOCAL coordinates so fill_polygon rasterises into
             * the bounded surface at the correct position. */
            px_local = g_malloc(n * sizeof(double));
            py_local = g_malloc(n * sizeof(double));
            for (guint i = 0; i < n; i++) {
                px_local[i] = px[i] - mask_x;
                py_local[i] = py[i] - mask_y;
            }
            px_fill = px_local;
            py_fill = py_local;
        } else {
            /* Exterior mode: fall back to full-document mask */
            preview_mask = selection_mask_new(doc->width, doc->height);
        }

        if (preview_mask) {
            selection_mask_fill_polygon(preview_mask, px_fill, py_fill, (int)n,
                                        SELECTION_COMBINE_NEW, state->smooth_mode, (float)state->feather_radius,
                                        state->curvature, state->area_mode, state->border_width, FALSE);
            selection_mask_render_outline(cr, preview_mask, anim_phase, zoom, TRUE);
            selection_mask_free(preview_mask);
        }

        g_free(px);
        g_free(py);
        if (px_local) g_free(px_local);
        if (py_local) g_free(py_local);
    } else {
        /* Building: use selection_draw_marching_ants_path (same style as rect/ellipse) */
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        selection_draw_marching_ants_path(cr, state->points, state->closed,
                                          state->cursor_x, state->cursor_y, (gdouble)anim_phase, zoom);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    }

    /* Circular handles (nodes) - first node interior filled/highlighted until closed */
    for (guint i = 0; i < n; i++) {
        GdkPoint* p = &g_array_index(state->points, GdkPoint, i);
        gboolean first = (i == 0);
        gboolean highlight_first = first && !state->closed; /* first node highlighted until all connected */
        gboolean hovered = (state->hovered_node == (gint)i);

        cairo_arc(cr, p->x, p->y, handle_radius, 0, 2 * M_PI);
        if (highlight_first) {
            /* Fill interior of first node until polygon closed */
            cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.8);
            cairo_fill_preserve(cr);
        }
        cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
        cairo_set_line_width(cr, 3.0 / zoom);
        cairo_stroke_preserve(cr);
        if (highlight_first)
            cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 1.0);
        else if (hovered)
            cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 1.0);
        else
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 1.0 / zoom);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}
