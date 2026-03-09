#include "tools/tool_lasso_select.h"
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

typedef struct {
    Tool* tool;
    ImageDocument* doc;
} LassoSelectTimerData;

static gboolean on_lasso_select_animation_timer(gpointer user_data);

#define NODE_HIT_RADIUS 6.0

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

static void lasso_select_get_options(LassoSelectToolState* state) {
    ToolOptions* opts = tool_options_get_for_tool(TOOL_LASSO_SELECT);
    if (opts) {
        state->combine_mode = tool_options_get_lasso_select_combine(opts);
        state->smooth_mode = tool_options_get_lasso_select_smooth(opts);
        state->feather_radius = (gint)tool_options_get_lasso_select_feather(opts);
        state->area_mode = tool_options_get_lasso_select_area(opts);
        state->border_width = tool_options_get_lasso_select_border_width(opts);
    } else {
        state->combine_mode = SELECTION_COMBINE_NEW;
        state->smooth_mode = SELECTION_SMOOTH_NONE;
        state->feather_radius = 0;
        state->area_mode = 0;
        state->border_width = 1;
    }
}

static void lasso_select_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc)
        return;

    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(LassoSelectToolState));
        ((LassoSelectToolState*)tool->user_data)->points = g_array_new(FALSE, FALSE, sizeof(GdkPoint));
    }
    LassoSelectToolState* state = (LassoSelectToolState*)tool->user_data;

    lasso_select_get_options(state);

    if (state->completed && state->points && state->points->len >= 3) {
        /* Completed: check if click is inside path -> start drag; else finalize and start new */
        if (point_in_polygon(state->points, event->x, event->y)) {
            state->is_dragging = TRUE;
            state->drag_anchor_x = event->x;
            state->drag_anchor_y = event->y;
            gtk_widget_queue_draw(doc->drawing_area);
            return;
        }
        /* Click outside: finalize and start new path */
        tool_lasso_select_finalize(tool, doc);
        g_array_set_size(state->points, 0);
        state->completed = FALSE;
        state->has_been_finalized = FALSE;
        state->is_dragging = FALSE;
        if (state->animation_timer_id) {
            g_source_remove(state->animation_timer_id);
            state->animation_timer_id = 0;
        }
    }

    /* Add first point and start drawing */
    GdkPoint pt = {event->x, event->y};
    g_array_append_val(state->points, pt);
    state->cursor_x = event->x;
    state->cursor_y = event->y;
    gtk_widget_queue_draw(doc->drawing_area);
}

static void lasso_select_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc || !tool->user_data)
        return;

    LassoSelectToolState* state = (LassoSelectToolState*)tool->user_data;
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);
    state->cursor_x = event->x;
    state->cursor_y = event->y;

    if (state->is_dragging && state->points && state->points->len > 0) {
        /* Drag completed preview */
        gint dx = event->x - state->drag_anchor_x;
        gint dy = event->y - state->drag_anchor_y;
        state->drag_anchor_x = event->x;
        state->drag_anchor_y = event->y;
        for (guint i = 0; i < state->points->len; i++) {
            GdkPoint* p = &g_array_index(state->points, GdkPoint, i);
            p->x += dx;
            p->y += dy;
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

    if (state->completed && state->points && state->points->len >= 3) {
        /* Show move cursor when hovering over draggable selection */
        if (point_in_polygon(state->points, event->x, event->y) && window) {
            GdkCursor* move_cur = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_FLEUR);
            if (move_cur) {
                gdk_window_set_cursor(window, move_cur);
                g_object_unref(move_cur);
            }
        } else if (window && tool->cursor) {
            gdk_window_set_cursor(window, tool->cursor);
        }
    }

    if (!state->completed && state->points && state->points->len > 0) {
        /* Add point while drawing (sample at intervals) */
        GdkPoint* last = &g_array_index(state->points, GdkPoint, state->points->len - 1);
        gdouble dx = event->x - last->x;
        gdouble dy = event->y - last->y;
        gdouble dist = sqrt(dx * dx + dy * dy);
        if (dist >= 2.0) {
            GdkPoint pt = {event->x, event->y};
            g_array_append_val(state->points, pt);
        }
    }
    gtk_widget_queue_draw(doc->drawing_area);
}

static void lasso_select_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    if (!tool || !doc || !tool->user_data)
        return;

    LassoSelectToolState* state = (LassoSelectToolState*)tool->user_data;

    if (state->is_dragging) {
        state->is_dragging = FALSE;
        gtk_widget_queue_draw(doc->drawing_area);
        return;
    }

    if (state->completed || !state->points || state->points->len < 1)
        return;

    /* Add final point at release position if different from last */
    GdkPoint* last = &g_array_index(state->points, GdkPoint, state->points->len - 1);
    if (last->x != event->x || last->y != event->y) {
        GdkPoint pt = {event->x, event->y};
        g_array_append_val(state->points, pt);
    }

    if (state->points->len < 3)
        return;

    /* Complete the path (close it), do NOT finalize yet - keep draggable */
    state->completed = TRUE;
    state->animation_phase = 0;

    ToolOptions* opts = tool_options_get_for_tool(TOOL_LASSO_SELECT);
    if (opts && tool_options_get_lasso_select_animate(opts) && state->animation_timer_id == 0) {
        LassoSelectTimerData* td = g_malloc(sizeof(LassoSelectTimerData));
        td->tool = tool;
        td->doc = doc;
        state->animation_timer_id = g_timeout_add(ANT_DASH_SPEED_SLOW, on_lasso_select_animation_timer, td);
    }

    gtk_widget_queue_draw(doc->drawing_area);
}

static gboolean on_lasso_select_animation_timer(gpointer user_data) {
    LassoSelectTimerData* td = (LassoSelectTimerData*)user_data;
    if (!td || !td->tool || !td->doc || !td->tool->user_data) {
        if (td && td->tool && td->tool->user_data)
            ((LassoSelectToolState*)td->tool->user_data)->animation_timer_id = 0;
        g_free(td);
        return FALSE;
    }
    LassoSelectToolState* state = (LassoSelectToolState*)td->tool->user_data;
    if (!td->doc->drawing_area) {
        state->animation_timer_id = 0;
        g_free(td);
        return FALSE;
    }
    if (!state->completed) {
        state->animation_timer_id = 0;
        g_free(td);
        return FALSE;
    }
    state->animation_phase = (state->animation_phase > 0) ? state->animation_phase - 1 : 3;
    gtk_widget_queue_draw(td->doc->drawing_area);
    return TRUE;
}

void tool_lasso_select_finalize(Tool* tool, ImageDocument* doc) {
    if (!tool || !tool->user_data || !doc || !doc->selection_mask)
        return;

    LassoSelectToolState* state = (LassoSelectToolState*)tool->user_data;
    if (!state->points || state->points->len < 3 || state->has_been_finalized)
        return;

    state->has_been_finalized = TRUE;
    lasso_select_get_options(state);

    guint n = state->points->len;
    double* px = g_malloc(n * sizeof(double));
    double* py = g_malloc(n * sizeof(double));
    for (guint i = 0; i < n; i++) {
        GdkPoint* p = &g_array_index(state->points, GdkPoint, i);
        px[i] = p->x;
        py[i] = p->y;
    }

    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, "Lasso Select");
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

    /* Lasso uses curvature=0 (no curve) */
    selection_mask_fill_polygon(doc->selection_mask,
                                px, py, (int)n,
                                state->combine_mode,
                                state->smooth_mode,
                                (float)state->feather_radius,
                                0.0f, /* curvature - lasso has straight segments */
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

void tool_lasso_select_reset(Tool* tool) {
    if (!tool || !tool->user_data)
        return;
    LassoSelectToolState* state = (LassoSelectToolState*)tool->user_data;
    state->completed = FALSE;
    state->has_been_finalized = FALSE;
    state->is_dragging = FALSE;
    if (state->points)
        g_array_set_size(state->points, 0);
    if (state->animation_timer_id) {
        g_source_remove(state->animation_timer_id);
        state->animation_timer_id = 0;
    }
    if (state->preview_cache) {
        selection_mask_free(state->preview_cache);
        state->preview_cache = NULL;
    }
    g_free(state->cache_points_x);
    state->cache_points_x = NULL;
    g_free(state->cache_points_y);
    state->cache_points_y = NULL;
}

Tool* tool_lasso_select_create(void) {
    Tool* tool = tool_new("Lasso Select",
                          TOOL_LASSO_SELECT,
                          GDK_CROSSHAIR,
                          TOOL_OPT_SELECTION_MODE | TOOL_OPT_SELECTION_SMOOTH);
    if (tool) {
        tool->mouse_down = lasso_select_tool_mouse_down;
        tool->mouse_move = lasso_select_tool_mouse_move;
        tool->mouse_up = lasso_select_tool_mouse_up;
    }
    return tool;
}

void tool_lasso_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    if (!doc || !doc->drawing_area || !cr)
        return;

    ToolRegistry* reg = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!reg)
        return;

    Tool* active = tool_manager_get_active(reg);
    if (!active || active->type != TOOL_LASSO_SELECT || !active->user_data)
        return;

    LassoSelectToolState* state = (LassoSelectToolState*)active->user_data;
    if (!state->points || state->points->len == 0)
        return;

    cairo_save(cr);
    if (zoom != 1.0)
        cairo_scale(cr, zoom, zoom);

    gdouble handle_radius = NODE_HIT_RADIUS / zoom;
    if (handle_radius < 2.0)
        handle_radius = 2.0;

    ToolOptions* opts = tool_options_get_for_tool(TOOL_LASSO_SELECT);
    gint anim_phase = (state->completed && opts && tool_options_get_lasso_select_animate(opts))
                          ? state->animation_phase
                          : 0;

    if (state->completed && state->points->len >= 3) {
        /* Same style as polygon: SelectionMask + selection_mask_render_outline for live option updates */
        lasso_select_get_options(state);

        guint n = state->points->len;
        gboolean cache_valid = FALSE;
        if (state->preview_cache != NULL &&
            state->cache_n_points == (gint)n &&
            state->cache_feather_radius == state->feather_radius &&
            state->cache_smooth_mode == state->smooth_mode &&
            state->cache_combine_mode == state->combine_mode &&
            state->cache_area_mode == state->area_mode &&
            state->cache_border_width == state->border_width &&
            state->cache_points_x != NULL &&
            state->cache_points_y != NULL) {
            cache_valid = TRUE;
            for (guint i = 0; i < n; i++) {
                GdkPoint* pt = &g_array_index(state->points, GdkPoint, i);
                if (state->cache_points_x[i] != pt->x ||
                    state->cache_points_y[i] != pt->y) {
                    cache_valid = FALSE;
                    break;
                }
            }
        }

        if (!cache_valid) {
            if (state->preview_cache) {
                selection_mask_free(state->preview_cache);
                state->preview_cache = NULL;
            }
            g_free(state->cache_points_x);
            state->cache_points_x = NULL;
            g_free(state->cache_points_y);
            state->cache_points_y = NULL;

            double* px = g_malloc(n * sizeof(double));
            double* py = g_malloc(n * sizeof(double));
            for (guint i = 0; i < n; i++) {
                GdkPoint* pt = &g_array_index(state->points, GdkPoint, i);
                px[i] = pt->x;
                py[i] = pt->y;
            }

            gboolean use_bounded = (state->area_mode != 1);
            SelectionMask* preview_mask = NULL;
            double* px_fill = px;
            double* py_fill = py;
            double* px_local = NULL;
            double* py_local = NULL;

            if (use_bounded) {
                double bbox_min_x = px[0], bbox_max_x = px[0];
                double bbox_min_y = py[0], bbox_max_y = py[0];
                for (guint i = 1; i < n; i++) {
                    if (px[i] < bbox_min_x)
                        bbox_min_x = px[i];
                    if (px[i] > bbox_max_x)
                        bbox_max_x = px[i];
                    if (py[i] < bbox_min_y)
                        bbox_min_y = py[i];
                    if (py[i] > bbox_max_y)
                        bbox_max_y = py[i];
                }
                int pad = (int)ceilf((float)state->feather_radius) + 2;
                int mask_x2 = (int)ceil(bbox_max_x) + pad;
                int mask_y2 = (int)ceil(bbox_max_y) + pad;
                int mask_x = (int)floor(bbox_min_x) - pad;
                int mask_y = (int)floor(bbox_min_y) - pad;
                if (mask_x < 0)
                    mask_x = 0;
                if (mask_y < 0)
                    mask_y = 0;
                if (mask_x2 > (int)doc->width)
                    mask_x2 = (int)doc->width;
                if (mask_y2 > (int)doc->height)
                    mask_y2 = (int)doc->height;
                int mask_w = mask_x2 - mask_x;
                int mask_h = mask_y2 - mask_y;
                preview_mask = selection_mask_new_bounded(mask_x, mask_y, mask_w, mask_h);
                px_local = g_malloc(n * sizeof(double));
                py_local = g_malloc(n * sizeof(double));
                for (guint i = 0; i < n; i++) {
                    px_local[i] = px[i] - mask_x;
                    py_local[i] = py[i] - mask_y;
                }
                px_fill = px_local;
                py_fill = py_local;
            } else {
                preview_mask = selection_mask_new(doc->width, doc->height);
            }

            if (preview_mask) {
                selection_mask_fill_polygon(preview_mask, px_fill, py_fill, (int)n,
                                            SELECTION_COMBINE_NEW, state->smooth_mode, (float)state->feather_radius,
                                            0.0f, state->area_mode, state->border_width, FALSE);
                selection_mask_get_surface(preview_mask);
            }

            state->preview_cache = preview_mask;
            state->cache_n_points = (gint)n;
            state->cache_points_x = g_malloc(n * sizeof(gint));
            state->cache_points_y = g_malloc(n * sizeof(gint));
            for (guint i = 0; i < n; i++) {
                state->cache_points_x[i] = (gint)px[i];
                state->cache_points_y[i] = (gint)py[i];
            }
            state->cache_feather_radius = state->feather_radius;
            state->cache_smooth_mode = state->smooth_mode;
            state->cache_combine_mode = state->combine_mode;
            state->cache_area_mode = state->area_mode;
            state->cache_border_width = state->border_width;

            g_free(px);
            g_free(py);
            if (px_local)
                g_free(px_local);
            if (py_local)
                g_free(py_local);
        }

        if (state->preview_cache) {
            selection_mask_render_outline(cr, state->preview_cache, anim_phase, zoom, TRUE);
        }
    } else {
        /* Drawing: marching ants along path to cursor, single node at first click */
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        selection_draw_marching_ants_path(cr, state->points, FALSE,
                                          state->cursor_x, state->cursor_y, 0.0, zoom);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        GdkPoint* first = &g_array_index(state->points, GdkPoint, 0);
        cairo_arc(cr, first->x, first->y, handle_radius, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.8);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
        cairo_set_line_width(cr, 3.0 / zoom);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 1.0);
        cairo_set_line_width(cr, 1.0 / zoom);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}
