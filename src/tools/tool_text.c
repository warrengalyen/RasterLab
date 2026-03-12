#include "tools/tool_text.h"
#include "document.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include "selection.h"
#include "tool_manager.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include "ui/tool_options_panel.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>

/* Minimum box size in document pixels */
#define TEXT_BOX_MIN_SIZE 4

/* Default box size applied when the user clicks without dragging */
#define TEXT_DEFAULT_BOX_W    200
#define TEXT_DEFAULT_BOX_H     60

/* Drag displacement smaller than this is treated as a plain click */
#define TEXT_CLICK_THRESHOLD    8

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

/**
 * Lightweight redraw: only the viewport overlay (handles + bounding box).
 * Use this during interactive drag so the full compositor is NOT triggered.
 */
static void text_tool_queue_overlay(ImageDocument* doc) {
    if (doc->viewport)
        gtk_widget_queue_draw(doc->viewport);
}

/**
 * Full redraw: viewport overlay AND drawing area (compositor).
 * Use this only when the text layer geometry is finalised (mouse_up).
 */
static void text_tool_queue_full(ImageDocument* doc) {
    if (doc->drawing_area)
        gtk_widget_queue_draw(doc->drawing_area);
    if (doc->viewport)
        gtk_widget_queue_draw(doc->viewport);
}

/**
 * Return TRUE if @layer is still present in doc->layers (safe dereference
 * guard against layers deleted while the tool is active).
 */
static gboolean text_tool_layer_valid(ImageDocument* doc, ImageLayer* layer) {
    if (!layer)
        return FALSE;
    return g_list_find(doc->layers, layer) != NULL;
}

/**
 * Detect which of the 8 handles (if any) lies under (x, y).
 * Coordinates are in document (image) space.
 * Returns 0-7 on hit, or -1 when no handle is hit.
 *
 * Handle order: 0=TL, 1=TR, 2=BL, 3=BR, 4=Top, 5=Right, 6=Bottom, 7=Left
 */
static gint text_detect_handle(gdouble x, gdouble y,
                                gdouble rx, gdouble ry,
                                gdouble rw, gdouble rh,
                                gdouble zoom) {
    gdouble half = 6.0 / zoom;
    if (half < 0.5) half = 0.5;

    /* Corner positions */
    const gdouble cx[4] = { rx,      rx + rw, rx,      rx + rw };
    const gdouble cy[4] = { ry,      ry,      ry + rh, ry + rh };
    for (gint i = 0; i < 4; i++) {
        if (fabs(x - cx[i]) <= half && fabs(y - cy[i]) <= half)
            return i;
    }

    /* Edge mid-point positions */
    gdouble li = rx + half, ri = rx + rw - half;
    gdouble ti = ry + half, bi = ry + rh - half;

    if (rh > 2 * half && fabs(y - ry)        <= half && x >= li && x <= ri) return 4;
    if (rw > 2 * half && fabs(x - (rx + rw)) <= half && y >= ti && y <= bi) return 5;
    if (rh > 2 * half && fabs(y - (ry + rh)) <= half && x >= li && x <= ri) return 6;
    if (rw > 2 * half && fabs(x - rx)        <= half && y >= ti && y <= bi) return 7;

    return -1;
}

/**
 * Set cursor for the given drag mode.
 */
static void text_set_cursor(GdkWindow* window, gint handle, GdkCursor* default_cursor) {
    if (!window) return;

    if (handle >= -1 && handle <= 3) {
        /* Reuse selection cursor logic for move (-1) and corners (0-3) */
        selection_set_cursor_for_handle(window, handle, default_cursor);
        return;
    }

    GdkDisplay* display = gdk_window_get_display(window);
    GdkCursor* cursor = NULL;

    if (handle == 4 || handle == 6)
        cursor = gdk_cursor_new_from_name(display, "ns-resize");
    else if (handle == 5 || handle == 7)
        cursor = gdk_cursor_new_from_name(display, "ew-resize");

    if (cursor) {
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    } else {
        gdk_window_set_cursor(window, default_cursor);
    }
}

/**
 * Push box geometry into the text layer and trigger a compositor redraw.
 * Called once in mouse_up, NOT during mouse_move, to avoid re-compositing
 * the entire document on every drag event.
 */
static void text_sync_layer_box(ImageDocument* doc, TextToolState* state) {
    if (!state->layer || !text_tool_layer_valid(doc, state->layer))
        return;
    if (state->layer->layer_type != LAYER_TYPE_TEXT || !state->layer->text_data)
        return;

    TextLayer* tl = (TextLayer*)state->layer->text_data;
    tl->box_x      = (double)state->box_x;
    tl->box_y      = (double)state->box_y;
    tl->box_width  = (double)state->box_w;
    tl->box_height = (double)state->box_h;

    layer_invalidate_cache(state->layer);
    document_invalidate_composite(doc);
    /* Full redraw is triggered by the caller via text_tool_queue_full() */
}

/**
 * Search doc->layers (top-to-bottom) for a visible text layer whose bounding
 * box contains the given document-space point.  Returns the first match or NULL.
 */
static ImageLayer* text_tool_find_layer_at_point(ImageDocument* doc, gint x, gint y) {
    for (GList* it = g_list_last(doc->layers); it; it = it->prev) {
        ImageLayer* layer = (ImageLayer*)it->data;
        if (!layer || !layer->visible || layer->layer_type != LAYER_TYPE_TEXT || !layer->text_data)
            continue;
        TextLayer* tl = (TextLayer*)layer->text_data;
        gint bx = (gint)tl->box_x + layer->offset_x;
        gint by = (gint)tl->box_y + layer->offset_y;
        gint bw = (gint)tl->box_width;
        gint bh = (gint)tl->box_height;
        if (x >= bx && x < bx + bw && y >= by && y < by + bh)
            return layer;
    }
    return NULL;
}

/**
 * Update the layers panel to reflect a newly added layer.
 * Walks up the widget tree from the drawing area to find the LayersPanel.
 */
static void text_tool_notify_layers_panel(ImageDocument* doc, ImageLayer* layer) {
    if (!doc || !doc->drawing_area || !GTK_IS_WIDGET(doc->drawing_area))
        return;
    GtkWidget* w = doc->drawing_area;
    while (w && !GTK_IS_WINDOW(w))
        w = gtk_widget_get_parent(w);
    if (!w)
        return;
    LayersPanel* lp = (LayersPanel*)g_object_get_data(G_OBJECT(w), "layers_panel");
    if (!lp)
        return;
    layers_panel_update(lp, doc);
    if (layer)
        layers_panel_select_layer(lp, doc, layer);
}

/* -----------------------------------------------------------------------
 * Text editing helpers
 * --------------------------------------------------------------------- */

/**
 * Blink-cursor GLib timeout callback.
 * Toggles cursor visibility and queues an overlay redraw.
 */
static gboolean text_cursor_blink(gpointer user_data) {
    TextToolState* state = (TextToolState*)user_data;

    if (!state->is_editing) {
        state->cursor_blink_tag = 0;
        return G_SOURCE_REMOVE;
    }

    state->cursor_visible = !state->cursor_visible;
    if (state->blink_doc && state->blink_doc->viewport)
        gtk_widget_queue_draw(state->blink_doc->viewport);

    return G_SOURCE_CONTINUE;
}

/**
 * Enter text-editing mode for the currently active text layer.
 * Grabs keyboard focus and starts the cursor blink timer.
 */
static void text_tool_enter_editing(TextToolState* state, ImageDocument* doc) {
    if (state->is_editing)
        return;

    state->is_editing    = TRUE;
    state->cursor_visible = TRUE;
    state->blink_doc     = doc;

    /* Place cursor at end of existing text */
    if (state->layer && state->layer->text_data) {
        TextLayer* tl = (TextLayer*)state->layer->text_data;
        state->cursor_pos = tl->text ? (gint)strlen(tl->text) : 0;
    } else {
        state->cursor_pos = 0;
    }

    /* Grab keyboard focus so key-press-events reach the drawing area */
    if (doc->drawing_area)
        gtk_widget_grab_focus(doc->drawing_area);

    /* Start blink timer (530 ms half-period) */
    if (state->cursor_blink_tag == 0)
        state->cursor_blink_tag = g_timeout_add(530, text_cursor_blink, state);
}

/**
 * Exit text-editing mode, stop the blink timer, and hide the cursor.
 */
static void text_tool_exit_editing(TextToolState* state) {
    if (!state->is_editing)
        return;

    state->is_editing     = FALSE;
    state->cursor_visible = FALSE;

    if (state->cursor_blink_tag) {
        g_source_remove(state->cursor_blink_tag);
        state->cursor_blink_tag = 0;
    }
    state->blink_doc = NULL;
}

/**
 * Insert @insert_text at the current cursor position in tl->text.
 * Advances cursor_pos past the inserted bytes.
 */
static void text_tool_insert(TextToolState* state, TextLayer* tl,
                              const gchar* insert_text) {
    gint ins_len  = (gint)strlen(insert_text);
    gint text_len = tl->text ? (gint)strlen(tl->text) : 0;

    /* Clamp cursor_pos to valid range */
    if (state->cursor_pos < 0)           state->cursor_pos = 0;
    if (state->cursor_pos > text_len)    state->cursor_pos = text_len;

    gchar* new_text = g_malloc(text_len + ins_len + 1);
    if (tl->text)
        memcpy(new_text, tl->text, state->cursor_pos);
    memcpy(new_text + state->cursor_pos, insert_text, ins_len);
    if (tl->text)
        memcpy(new_text + state->cursor_pos + ins_len,
               tl->text + state->cursor_pos,
               text_len - state->cursor_pos);
    new_text[text_len + ins_len] = '\0';

    g_free(tl->text);
    tl->text = new_text;
    state->cursor_pos += ins_len;
}

/**
 * Invalidate the text layer cache and trigger a full canvas + overlay redraw.
 */
static void text_tool_invalidate(ImageDocument* doc, TextToolState* state) {
    if (state->layer && text_tool_layer_valid(doc, state->layer)) {
        layer_invalidate_cache(state->layer);
        document_invalidate_composite(doc);
    }
    text_tool_queue_full(doc);
}

/* -----------------------------------------------------------------------
 * Mouse handlers
 * --------------------------------------------------------------------- */

static void text_tool_mouse_down(Tool* tool, struct ImageDocument* doc,
                                 MouseEvent* event) {
    if (!tool || !doc || !tool->user_data) return;

    TextToolState* state = (TextToolState*)tool->user_data;

    if (state->has_box && state->box_w > 0 && state->box_h > 0) {
        /* Hit-test handles first */
        gint handle = text_detect_handle((gdouble)event->x, (gdouble)event->y,
                                         (gdouble)state->box_x, (gdouble)state->box_y,
                                         (gdouble)state->box_w, (gdouble)state->box_h,
                                         doc->zoom_factor);
        if (handle >= 0) {
            /* Clicking a resize handle — exit editing, start resize */
            text_tool_exit_editing(state);
            state->drag_mode   = handle;
            state->is_dragging = TRUE;
            state->start_x     = event->x;
            state->start_y     = event->y;
            text_tool_queue_overlay(doc);
            return;
        }

        if (event->x >= state->box_x && event->x < state->box_x + state->box_w &&
            event->y >= state->box_y && event->y < state->box_y + state->box_h) {
            /* Click inside the text box */
            if (event->click_count >= 2) {
                /* Double-click: enter editing mode and position cursor at click */
                if (state->layer && text_tool_layer_valid(doc, state->layer) &&
                    state->layer->layer_type == LAYER_TYPE_TEXT &&
                    state->layer->text_data) {
                    if (!state->is_editing)
                        text_tool_enter_editing(state, doc);

                    TextLayer* tl = (TextLayer*)state->layer->text_data;
                    if (tl->text) {
                        cairo_surface_t* tmp = cairo_image_surface_create(
                            CAIRO_FORMAT_ARGB32, 1, 1);
                        cairo_t* tmp_cr = cairo_create(tmp);
                        cairo_scale(tmp_cr, doc->zoom_factor, doc->zoom_factor);
                        PangoLayout* layout = text_layer_create_layout(tl, tmp_cr);

                        gint x_pango = (gint)((event->x - tl->box_x) * PANGO_SCALE);
                        gint y_pango = (gint)((event->y - tl->box_y) * PANGO_SCALE);
                        gint idx = 0, trailing = 0;
                        pango_layout_xy_to_index(layout, x_pango, y_pango,
                                                 &idx, &trailing);
                        if (trailing && tl->text[idx] != '\0') {
                            const gchar* next = g_utf8_next_char(tl->text + idx);
                            idx = (gint)(next - tl->text);
                        }
                        state->cursor_pos = CLAMP(idx, 0, (gint)strlen(tl->text));

                        g_object_unref(layout);
                        cairo_destroy(tmp_cr);
                        cairo_surface_destroy(tmp);
                    }
                    state->cursor_visible = TRUE;
                    text_tool_queue_overlay(doc);
                }
            } else {
                /* Single-click: exit editing and start a move drag */
                text_tool_exit_editing(state);
                state->drag_mode   = -1;
                state->is_dragging = TRUE;
                state->start_x     = event->x;
                state->start_y     = event->y;
                text_tool_queue_overlay(doc);
            }
            return;
        }

        /* Clicked outside the box: deselect, fall through to find/create */
        text_tool_exit_editing(state);
        state->has_box = FALSE;
        text_tool_queue_overlay(doc);
    }

    /* ── No active box: check if click lands on an existing text layer ─ */
    {
        ImageLayer* found = text_tool_find_layer_at_point(doc, event->x, event->y);
        if (found) {
            TextLayer* tl = (TextLayer*)found->text_data;
            state->layer   = found;
            state->box_x   = (gint)(tl->box_x + found->offset_x);
            state->box_y   = (gint)(tl->box_y + found->offset_y);
            state->box_w   = (gint)tl->box_width;
            state->box_h   = (gint)tl->box_height;
            state->has_box = TRUE;
            doc->selected_layer = found;

            /* Sync the text tool options panel to this layer's properties */
            if (doc->drawing_area) {
                GtkWidget* win = gtk_widget_get_toplevel(doc->drawing_area);
                if (win) {
                    AppContext* ctx =
                        (AppContext*)g_object_get_data(G_OBJECT(win), "app_context");
                    if (ctx && ctx->tool_options_panel)
                        tool_options_panel_sync_text_layer(ctx->tool_options_panel, found);
                }
            }

            if (event->click_count >= 2) {
                text_tool_enter_editing(state, doc);
                text_tool_queue_overlay(doc);
            } else {
                /* Single-click: start move drag */
                state->drag_mode   = -1;
                state->is_dragging = TRUE;
                state->start_x     = event->x;
                state->start_y     = event->y;
                text_tool_queue_overlay(doc);
            }
            return;
        }
    }

    /* ── Empty canvas: start rubber-band to define a new text box ───── */
    text_tool_exit_editing(state);
    state->has_box        = FALSE;
    state->is_dragging    = TRUE;
    state->drag_mode      = -2;
    state->start_x        = event->x;
    state->start_y        = event->y;
    state->current_x      = event->x;
    state->current_y      = event->y;
    state->hovered_handle = -2;
    text_tool_queue_overlay(doc);
}

static void text_tool_mouse_move(Tool* tool, struct ImageDocument* doc,
                                 MouseEvent* event) {
    if (!tool || !doc || !tool->user_data) return;

    TextToolState* state = (TextToolState*)tool->user_data;
    GdkWindow* window = doc->drawing_area
                        ? gtk_widget_get_window(doc->drawing_area) : NULL;

    if (state->is_dragging) {
        if (window)
            text_set_cursor(window, state->drag_mode, tool->cursor);

        gint dx = event->x - state->start_x;
        gint dy = event->y - state->start_y;

        if (state->drag_mode == -2) {
            /* Rubber-band: just track cursor; overlay draw handles the preview */
            state->current_x = event->x;
            state->current_y = event->y;
        } else if (state->drag_mode == -1) {
            /* Move */
            state->box_x  += dx;
            state->box_y  += dy;
            state->start_x = event->x;
            state->start_y = event->y;
        } else {
            /* Resize via handle */
            switch (state->drag_mode) {
                case 0: state->box_x += dx; state->box_y += dy;
                        state->box_w -= dx; state->box_h -= dy; break;
                case 1: state->box_y += dy;
                        state->box_w += dx; state->box_h -= dy; break;
                case 2: state->box_x += dx;
                        state->box_w -= dx; state->box_h += dy; break;
                case 3: state->box_w += dx; state->box_h += dy; break;
                case 4: state->box_y += dy; state->box_h -= dy; break;
                case 5: state->box_w += dx;                     break;
                case 6: state->box_h += dy;                     break;
                case 7: state->box_x += dx; state->box_w -= dx; break;
                default: break;
            }
            state->start_x = event->x;
            state->start_y = event->y;

            /* Normalise negative dimensions */
            if (state->box_w < 0) { state->box_x += state->box_w; state->box_w = -state->box_w; }
            if (state->box_h < 0) { state->box_y += state->box_h; state->box_h = -state->box_h; }
            if (state->box_w < TEXT_BOX_MIN_SIZE) state->box_w = TEXT_BOX_MIN_SIZE;
            if (state->box_h < TEXT_BOX_MIN_SIZE) state->box_h = TEXT_BOX_MIN_SIZE;
        }

        /* Propagate updated box geometry directly into the TextLayer struct.
         *
         * At zoom > 1.0, document_render_layers_at_zoom reads box_x/y directly
         * from the struct on every draw event (the drawing area is already queued
         * for redraw by on_drawing_area_motion_notify), so the text follows the
         * bounding box with zero cache/tile overhead.
         *
         * At zoom ≤ 1.0, the tile cache is not rebuilt here; the tile compositing
         * path uses the stale cached surface during drag and catches up fully in
         * mouse_up via text_sync_layer_box. */
        if (state->drag_mode != -2 &&
            state->layer && text_tool_layer_valid(doc, state->layer) &&
            state->layer->layer_type == LAYER_TYPE_TEXT && state->layer->text_data) {
            TextLayer* tl = (TextLayer*)state->layer->text_data;
            tl->box_x      = (double)state->box_x;
            tl->box_y      = (double)state->box_y;
            tl->box_width  = (double)state->box_w;
            tl->box_height = (double)state->box_h;
        }

        /* Overlay-only redraw so handles repaint without a full compositor pass */
        text_tool_queue_overlay(doc);
        return;
    }

    /* Not dragging: update hover state for cursor and handle highlight */
    if (state->has_box && state->box_w > 0 && state->box_h > 0) {
        gint hovered = text_detect_handle((gdouble)event->x, (gdouble)event->y,
                                          (gdouble)state->box_x, (gdouble)state->box_y,
                                          (gdouble)state->box_w, (gdouble)state->box_h,
                                          doc->zoom_factor);
        if (hovered >= 0) {
            state->hovered_handle = hovered;
        } else if (event->x >= state->box_x && event->x < state->box_x + state->box_w &&
                   event->y >= state->box_y && event->y < state->box_y + state->box_h) {
            state->hovered_handle = -1;
        } else {
            state->hovered_handle = -2;
        }

        if (window) {
            if (state->hovered_handle >= -1)
                text_set_cursor(window, state->hovered_handle, tool->cursor);
            else
                gdk_window_set_cursor(window, tool->cursor);
        }
        text_tool_queue_overlay(doc);
    }
}

static void text_tool_mouse_up(Tool* tool, struct ImageDocument* doc,
                               MouseEvent* event) {
    if (!tool || !doc || !tool->user_data) return;

    TextToolState* state = (TextToolState*)tool->user_data;

    (void)event;

    if (!state->is_dragging) return;
    state->is_dragging = FALSE;

    if (state->drag_mode == -2) {
        /* Finalise rubber-band: compute box from drag, create the layer */
        gint x = state->start_x;
        gint y = state->start_y;
        gint w = state->current_x - state->start_x;
        gint h = state->current_y - state->start_y;

        if (w < 0) { x += w; w = -w; }
        if (h < 0) { y += h; h = -h; }

        /* A plain click (no meaningful drag) → sensible default size */
        if (w < TEXT_CLICK_THRESHOLD && h < TEXT_CLICK_THRESHOLD) {
            w = TEXT_DEFAULT_BOX_W;
            h = TEXT_DEFAULT_BOX_H;
            if (doc->width  > 0 && x + w > (gint)doc->width)
                x = MAX(0, (gint)doc->width  - w);
            if (doc->height > 0 && y + h > (gint)doc->height)
                y = MAX(0, (gint)doc->height - h);
            w = MIN(w, (gint)doc->width);
            h = MIN(h, (gint)doc->height);
        }

        if (w < TEXT_BOX_MIN_SIZE) w = TEXT_BOX_MIN_SIZE;
        if (h < TEXT_BOX_MIN_SIZE) h = TEXT_BOX_MIN_SIZE;

        state->box_x   = x;
        state->box_y   = y;
        state->box_w   = w;
        state->box_h   = h;
        state->has_box = TRUE;

        /* Create the text layer on release */
        if (doc->width > 0 && doc->height > 0) {
            ImageLayer* layer = layer_create_text("Text Layer",
                                                   doc->width, doc->height, doc);
            if (layer) {
                TextLayer* tl = (TextLayer*)layer->text_data;
                tl->box_x      = (double)x;
                tl->box_y      = (double)y;
                tl->box_width  = (double)w;
                tl->box_height = (double)h;
                g_free(tl->text);
                tl->text = g_strdup("Text");

                doc->layers         = g_list_append(doc->layers, layer);
                doc->selected_layer = layer;
                state->layer        = layer;

                document_invalidate_composite(doc);
                text_tool_notify_layers_panel(doc, layer);

                /* Sync the tool-options panel to the new layer's defaults */
                if (doc->drawing_area) {
                    GtkWidget* win = gtk_widget_get_toplevel(doc->drawing_area);
                    if (win) {
                        AppContext* ctx =
                            (AppContext*)g_object_get_data(G_OBJECT(win), "app_context");
                        if (ctx && ctx->tool_options_panel)
                            tool_options_panel_sync_text_layer(ctx->tool_options_panel,
                                                               layer);
                    }
                }

                text_tool_enter_editing(state, doc);
            }
        }
    } else {
        /* Move / resize complete — push final geometry to the layer once */
        state->has_box = (state->box_w > 0 && state->box_h > 0);
        text_sync_layer_box(doc, state);
    }

    state->drag_mode      = -1;
    state->hovered_handle = -2;
    text_tool_queue_full(doc);
}

/* -----------------------------------------------------------------------
 * Keyboard handler
 * --------------------------------------------------------------------- */

static gboolean text_tool_key_press(Tool* tool, struct ImageDocument* doc,
                                    GdkEventKey* event) {
    if (!tool || !doc || !tool->user_data) return FALSE;

    TextToolState* state = (TextToolState*)tool->user_data;

    /* ESC always exits editing mode (or is a no-op when already idle).
     * Handled here before the is_editing guard so pressing Escape is never
     * silently ignored while the text tool is the active tool. */
    if (event->keyval == GDK_KEY_Escape) {
        if (state->is_editing) {
            text_tool_exit_editing(state);
            text_tool_queue_overlay(doc);
        }
        return TRUE;
    }

    if (!state->is_editing)
        return FALSE;

    if (!state->layer || !text_tool_layer_valid(doc, state->layer) ||
        state->layer->layer_type != LAYER_TYPE_TEXT || !state->layer->text_data)
        return FALSE;

    TextLayer* tl = (TextLayer*)state->layer->text_data;
    if (!tl->text) {
        tl->text = g_strdup("");
        state->cursor_pos = 0;
    }

    gint text_len = (gint)strlen(tl->text);
    /* Keep cursor in bounds */
    state->cursor_pos = CLAMP(state->cursor_pos, 0, text_len);

    switch (event->keyval) {

        /* ---- Delete backwards ---- */
        case GDK_KEY_BackSpace:
            if (state->cursor_pos > 0) {
                const gchar* prev =
                    g_utf8_prev_char(tl->text + state->cursor_pos);
                gint prev_pos = (gint)(prev - tl->text);
                gint del_len  = state->cursor_pos - prev_pos;

                gchar* new_text = g_malloc(text_len - del_len + 1);
                memcpy(new_text, tl->text, prev_pos);
                memcpy(new_text + prev_pos,
                       tl->text + state->cursor_pos,
                       text_len - state->cursor_pos + 1);
                g_free(tl->text);
                tl->text = new_text;
                state->cursor_pos = prev_pos;
                text_tool_invalidate(doc, state);
            } else {
                /* Nothing to delete but still consume the key */
                text_tool_queue_overlay(doc);
            }
            return TRUE;

        /* ---- Delete forwards ---- */
        case GDK_KEY_Delete:
            if (tl->text[state->cursor_pos] != '\0') {
                const gchar* next =
                    g_utf8_next_char(tl->text + state->cursor_pos);
                gint next_pos = (gint)(next - tl->text);
                gint del_len  = next_pos - state->cursor_pos;

                gchar* new_text = g_malloc(text_len - del_len + 1);
                memcpy(new_text, tl->text, state->cursor_pos);
                memcpy(new_text + state->cursor_pos,
                       tl->text + next_pos,
                       text_len - next_pos + 1);
                g_free(tl->text);
                tl->text = new_text;
                text_tool_invalidate(doc, state);
            } else {
                text_tool_queue_overlay(doc);
            }
            return TRUE;

        /* ---- Cursor movement ---- */
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if (state->cursor_pos > 0) {
                const gchar* prev =
                    g_utf8_prev_char(tl->text + state->cursor_pos);
                state->cursor_pos = (gint)(prev - tl->text);
            }
            state->cursor_visible = TRUE;
            text_tool_queue_overlay(doc);
            return TRUE;

        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if (tl->text[state->cursor_pos] != '\0') {
                const gchar* next =
                    g_utf8_next_char(tl->text + state->cursor_pos);
                state->cursor_pos = (gint)(next - tl->text);
            }
            state->cursor_visible = TRUE;
            text_tool_queue_overlay(doc);
            return TRUE;

        case GDK_KEY_Home:
        case GDK_KEY_KP_Home: {
            /* Move to start of current line */
            gint pos = state->cursor_pos;
            while (pos > 0 && tl->text[pos - 1] != '\n')
                pos--;
            state->cursor_pos = pos;
            state->cursor_visible = TRUE;
            text_tool_queue_overlay(doc);
            return TRUE;
        }

        case GDK_KEY_End:
        case GDK_KEY_KP_End: {
            /* Move to end of current line */
            gint pos = state->cursor_pos;
            while (tl->text[pos] != '\0' && tl->text[pos] != '\n')
                pos++;
            state->cursor_pos = pos;
            state->cursor_visible = TRUE;
            text_tool_queue_overlay(doc);
            return TRUE;
        }

        /* ---- Newline ---- */
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            text_tool_insert(state, tl, "\n");
            text_tool_invalidate(doc, state);
            return TRUE;

        default:
            break;
    }

    /* ---- Printable characters ---- */
    gunichar uc = gdk_keyval_to_unicode(event->keyval);
    if (uc >= 0x20 && uc != 0x7f) {
        gchar buf[7];
        gint  len = g_unichar_to_utf8(uc, buf);
        buf[len]  = '\0';
        text_tool_insert(state, tl, buf);
        text_tool_invalidate(doc, state);
        return TRUE;
    }

    return FALSE; /* Key not consumed by text editing */
}

/* -----------------------------------------------------------------------
 * Overlay drawing
 * --------------------------------------------------------------------- */

/* Snap a document coordinate to the nearest device-pixel centre. */
#define TEXT_SNAP(c, z) (floor((c) * (z) + 0.5) / (z))

void tool_text_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    if (!doc || !cr) return;

    ToolRegistry* reg = (ToolRegistry*)g_object_get_data(
                            G_OBJECT(doc->drawing_area), "tool_registry");
    if (!reg) return;

    Tool* active = tool_manager_get_active(reg);
    if (!active || active->type != TOOL_TEXT || !active->user_data) return;

    TextToolState* state = (TextToolState*)active->user_data;

    gint rx, ry, rw, rh;

    /* During a new-box drag: show live rubber-band */
    if (state->is_dragging && state->drag_mode == -2) {
        rx = state->start_x;
        ry = state->start_y;
        rw = state->current_x - state->start_x;
        rh = state->current_y - state->start_y;
        if (rw < 0) { rx += rw; rw = -rw; }
        if (rh < 0) { ry += rh; rh = -rh; }
    } else if (state->has_box) {
        rx = state->box_x;
        ry = state->box_y;
        rw = state->box_w;
        rh = state->box_h;
    } else {
        return;
    }

    if (rw <= 0 || rh <= 0) return;

    gdouble handle_size     = 12.0 / zoom;
    gdouble half_handle     = handle_size * 0.5;
    gdouble line_width      = 1.0 / zoom;
    if (line_width < 0.5) line_width = 0.5;

    cairo_save(cr);
    if (zoom != 1.0)
        cairo_scale(cr, zoom, zoom);

    /* ---- Bounding box border ---- */
    gdouble dash[] = { 5.0 / zoom, 3.0 / zoom };
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_rectangle(cr,
        TEXT_SNAP(rx,      zoom), TEXT_SNAP(ry,      zoom),
        TEXT_SNAP(rx + rw, zoom) - TEXT_SNAP(rx, zoom),
        TEXT_SNAP(ry + rh, zoom) - TEXT_SNAP(ry, zoom));

    /* Dark shadow stroke */
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.85);
    cairo_set_line_width(cr, line_width * 3.0);
    cairo_stroke_preserve(cr);
    /* Bright blue stroke */
    cairo_set_source_rgba(cr, 0.2, 0.6, 1.0, 1.0);
    cairo_set_line_width(cr, line_width);
    cairo_stroke(cr);

    cairo_set_dash(cr, NULL, 0, 0);

    /* ---- Only draw handles when we have a finalised box ---- */
    if (!state->has_box) {
        cairo_restore(cr);
        return;
    }

    /* 8 handle positions */
    gdouble hx[8], hy[8];
    hx[0] = TEXT_SNAP(rx,       zoom); hy[0] = TEXT_SNAP(ry,       zoom);
    hx[1] = TEXT_SNAP(rx + rw,  zoom); hy[1] = TEXT_SNAP(ry,       zoom);
    hx[2] = TEXT_SNAP(rx,       zoom); hy[2] = TEXT_SNAP(ry + rh,  zoom);
    hx[3] = TEXT_SNAP(rx + rw,  zoom); hy[3] = TEXT_SNAP(ry + rh,  zoom);
    hx[4] = TEXT_SNAP(rx + rw * 0.5, zoom); hy[4] = TEXT_SNAP(ry,       zoom);
    hx[5] = TEXT_SNAP(rx + rw,  zoom); hy[5] = TEXT_SNAP(ry + rh * 0.5, zoom);
    hx[6] = TEXT_SNAP(rx + rw * 0.5, zoom); hy[6] = TEXT_SNAP(ry + rh,  zoom);
    hx[7] = TEXT_SNAP(rx,       zoom); hy[7] = TEXT_SNAP(ry + rh * 0.5, zoom);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

    for (gint i = 0; i < 8; i++) {
        gboolean hovered = (state->hovered_handle == i);
        gdouble bx  = TEXT_SNAP(hx[i] - half_handle, zoom);
        gdouble by  = TEXT_SNAP(hy[i] - half_handle, zoom);
        gdouble bw  = TEXT_SNAP(hx[i] + half_handle, zoom) - bx;
        gdouble bh  = TEXT_SNAP(hy[i] + half_handle, zoom) - by;

        cairo_rectangle(cr, bx, by, bw, bh);
        /* Dark border */
        cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
        cairo_set_line_width(cr, line_width * 3.0);
        cairo_stroke_preserve(cr);
        /* Fill: bright blue when hovered, white otherwise */
        cairo_set_source_rgba(cr,
            hovered ? 0.2 : 1.0,
            hovered ? 0.6 : 1.0,
            1.0, 1.0);
        cairo_set_line_width(cr, line_width);
        cairo_stroke(cr);
    }

    /* ---- Text cursor (caret) ---- */
    if (state->is_editing && state->cursor_visible &&
        state->layer && text_tool_layer_valid(doc, state->layer) &&
        state->layer->layer_type == LAYER_TYPE_TEXT &&
        state->layer->text_data) {

        TextLayer* tl = (TextLayer*)state->layer->text_data;
        if (tl->text) {
            gint text_len   = (gint)strlen(tl->text);
            gint cursor_pos = CLAMP(state->cursor_pos, 0, text_len);

            /* Create a layout with the same settings used for rendering.
             * The cairo context already has cairo_scale(cr, zoom, zoom) so
             * Pango computes metrics at the effective screen resolution.
             * Positions returned by pango_layout_get_cursor_pos are in
             * Pango units relative to the layout origin; divide by PANGO_SCALE
             * to get user-space (document) coordinates. */
            PangoLayout* layout = text_layer_create_layout(tl, cr);
            PangoRectangle strong_pos;
            pango_layout_get_cursor_pos(layout, cursor_pos, &strong_pos, NULL);
            g_object_unref(layout);

            gdouble cx = tl->box_x + (gdouble)strong_pos.x      / PANGO_SCALE;
            gdouble cy = tl->box_y + (gdouble)strong_pos.y      / PANGO_SCALE;
            gdouble ch =             (gdouble)strong_pos.height  / PANGO_SCALE;
            if (ch < 1.0) ch = 1.0;

            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_line_width(cr, 1.5 / zoom);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

            /* White backing for visibility on dark text */
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
            cairo_move_to(cr, cx + 1.0 / zoom, cy);
            cairo_line_to(cr, cx + 1.0 / zoom, cy + ch);
            cairo_stroke(cr);

            /* Black caret */
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.95);
            cairo_move_to(cr, cx, cy);
            cairo_line_to(cr, cx, cy + ch);
            cairo_stroke(cr);
        }
    }

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_restore(cr);
}

/* -----------------------------------------------------------------------
 * Tool lifecycle
 * --------------------------------------------------------------------- */

void tool_text_reset(Tool* tool) {
    if (!tool || !tool->user_data) return;
    TextToolState* state = (TextToolState*)tool->user_data;

    text_tool_exit_editing(state);

    state->has_box        = FALSE;
    state->is_dragging    = FALSE;
    state->drag_mode      = -2;
    state->hovered_handle = -2;
    state->layer          = NULL;
}

gboolean tool_text_is_editing(Tool* tool) {
    if (!tool || tool->type != TOOL_TEXT || !tool->user_data)
        return FALSE;
    TextToolState* state = (TextToolState*)tool->user_data;
    return state->is_editing;
}

Tool* tool_text_create(void) {
    Tool* tool = tool_new("Text", TOOL_TEXT, GDK_XTERM, TOOL_OPT_NONE);
    if (!tool) return NULL;

    tool->mouse_down = text_tool_mouse_down;
    tool->mouse_move = text_tool_mouse_move;
    tool->mouse_up   = text_tool_mouse_up;
    tool->key_press  = text_tool_key_press;

    tool->user_data = g_malloc0(sizeof(TextToolState));
    if (!tool->user_data) {
        tool_free(tool);
        return NULL;
    }

    TextToolState* state = (TextToolState*)tool->user_data;
    state->drag_mode      = -2;
    state->hovered_handle = -2;

    return tool;
}
