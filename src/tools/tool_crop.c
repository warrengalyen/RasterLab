#include "tools/tool_crop.h"
#include "document.h"
#include "selection.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui/tool_options_panel.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <math.h>

/* Minimum crop rectangle dimension in pixels */
#define CROP_MIN_SIZE 1

/**
 * Queue redraw of drawing area and viewport (crop overlay is drawn on viewport)
 */
static void crop_queue_redraw(ImageDocument* doc) {
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
    if (doc->viewport) {
        gtk_widget_queue_draw(doc->viewport);
    }
}

/**
 * Detect which handle (if any) is at the given point
 * 8 handles: 0-3 corners (TL, TR, BL, BR), 4-7 edges (T, R, B, L)
 * Same handle size as selection tool (12 screen pixels = 6/zoom image pixels)
 */
static gint crop_detect_handle_at_point(gdouble x, gdouble y,
                                        gdouble rect_x, gdouble rect_y,
                                        gdouble rect_w, gdouble rect_h,
                                        gdouble zoom_factor) {
    gdouble half_handle = 6.0 / zoom_factor;
    if (half_handle < 0.5) {
        half_handle = 0.5;
    }

    /* Check corners first (0-3) */
    gdouble corners[4][2] = {
        {rect_x, rect_y},                  /* 0: top-left */
        {rect_x + rect_w, rect_y},         /* 1: top-right */
        {rect_x, rect_y + rect_h},         /* 2: bottom-left */
        {rect_x + rect_w, rect_y + rect_h} /* 3: bottom-right */
    };
    for (gint i = 0; i < 4; i++) {
        gdouble dx = x - corners[i][0];
        gdouble dy = y - corners[i][1];
        if (fabs(dx) <= half_handle && fabs(dy) <= half_handle) {
            return i;
        }
    }

    /* Check edges (4-7): top, right, bottom, left
     * Edge hit: point within half_handle of the edge line, excluding corner overlap */
    gdouble left_inner = rect_x + half_handle;
    gdouble right_inner = rect_x + rect_w - half_handle;
    gdouble top_inner = rect_y + half_handle;
    gdouble bottom_inner = rect_y + rect_h - half_handle;

    /* Top edge (4) */
    if (rect_h > 2 * half_handle &&
        fabs(y - rect_y) <= half_handle &&
        x >= left_inner && x <= right_inner) {
        return 4;
    }
    /* Right edge (5) */
    if (rect_w > 2 * half_handle &&
        fabs(x - (rect_x + rect_w)) <= half_handle &&
        y >= top_inner && y <= bottom_inner) {
        return 5;
    }
    /* Bottom edge (6) */
    if (rect_h > 2 * half_handle &&
        fabs(y - (rect_y + rect_h)) <= half_handle &&
        x >= left_inner && x <= right_inner) {
        return 6;
    }
    /* Left edge (7) */
    if (rect_w > 2 * half_handle &&
        fabs(x - rect_x) <= half_handle &&
        y >= top_inner && y <= bottom_inner) {
        return 7;
    }

    return -1;
}

/**
 * Set cursor based on crop handle type (8 handles + move)
 */
static void crop_set_cursor_for_handle(GdkWindow* window, gint handle, GdkCursor* default_cursor) {
    if (!window) {
        return;
    }

    /* Use selection's cursor for move (-1) and corners (0-3) */
    if (handle >= -1 && handle <= 3) {
        selection_set_cursor_for_handle(window, handle, default_cursor);
        return;
    }

    /* Edge handles 4-7 */
    GdkDisplay* display = gdk_window_get_display(window);
    GdkCursor* cursor = NULL;

    if (handle == 4 || handle == 6) {
        cursor = gdk_cursor_new_from_name(display, "ns-resize");
    } else if (handle == 5 || handle == 7) {
        cursor = gdk_cursor_new_from_name(display, "ew-resize");
    }

    if (cursor) {
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    } else {
        gdk_window_set_cursor(window, default_cursor);
    }
}

/**
 * Crop Tool: mouse down - start new crop, move, or resize
 */
static void crop_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    CropToolState* state;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    if (doc->width == 0 || doc->height == 0) {
        return;
    }

    state = (CropToolState*)tool->user_data;

    /* If we have an active crop rect, check handle or inside for move */
    if (state->is_active && state->rect_w > 0 && state->rect_h > 0) {
        gint handle = crop_detect_handle_at_point((gdouble)event->x, (gdouble)event->y,
                                                  (gdouble)state->rect_x, (gdouble)state->rect_y,
                                                  (gdouble)state->rect_w, (gdouble)state->rect_h,
                                                  doc->zoom_factor);

        if (handle >= 0) {
            /* Clicking on handle - start resize */
            state->drag_mode = handle;
            state->is_dragging = TRUE;
            state->start_x = event->x;
            state->start_y = event->y;
            crop_queue_redraw(doc);
            return;
        }

        /* Check if clicking inside rect - move */
        if (event->x >= state->rect_x && event->x < state->rect_x + state->rect_w &&
            event->y >= state->rect_y && event->y < state->rect_y + state->rect_h) {
            state->drag_mode = -1;
            state->is_dragging = TRUE;
            state->start_x = event->x;
            state->start_y = event->y;
            crop_queue_redraw(doc);
            return;
        }

        /* Clicking outside - start new crop (replace existing) */
    }

    /* Start new crop rectangle */
    state->is_active = FALSE; /* Will become TRUE on mouse_up if rect has area */
    state->is_dragging = TRUE;
    state->drag_mode = -2;
    state->start_x = event->x;
    state->start_y = event->y;
    state->current_x = event->x;
    state->current_y = event->y;
    state->hovered_handle = -1;

    crop_queue_redraw(doc);
}

/**
 * Crop Tool: mouse move - update rectangle based on drag mode
 */
static void crop_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    CropToolState* state;
    GdkWindow* window;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (CropToolState*)tool->user_data;
    window = doc->drawing_area ? gtk_widget_get_window(doc->drawing_area) : NULL;

    /* Handle drag (move or resize) */
    if (state->is_dragging && state->drag_mode >= -1) {
        if (window) {
            crop_set_cursor_for_handle(window, state->drag_mode, tool->cursor);
        }

        gint dx = event->x - state->start_x;
        gint dy = event->y - state->start_y;

        if (state->drag_mode == -1) {
            /* Move */
            state->rect_x += dx;
            state->rect_y += dy;
            state->start_x = event->x;
            state->start_y = event->y;
        } else {
            /* Resize by handle 0-7 */
            switch (state->drag_mode) {
                case 0: /* top-left */
                    state->rect_x += dx;
                    state->rect_y += dy;
                    state->rect_w -= dx;
                    state->rect_h -= dy;
                    break;
                case 1: /* top-right */
                    state->rect_y += dy;
                    state->rect_w += dx;
                    state->rect_h -= dy;
                    break;
                case 2: /* bottom-left */
                    state->rect_x += dx;
                    state->rect_w -= dx;
                    state->rect_h += dy;
                    break;
                case 3: /* bottom-right */
                    state->rect_w += dx;
                    state->rect_h += dy;
                    break;
                case 4: /* top edge */
                    state->rect_y += dy;
                    state->rect_h -= dy;
                    break;
                case 5: /* right edge */
                    state->rect_w += dx;
                    break;
                case 6: /* bottom edge */
                    state->rect_h += dy;
                    break;
                case 7: /* left edge */
                    state->rect_x += dx;
                    state->rect_w -= dx;
                    break;
            }
            state->start_x = event->x;
            state->start_y = event->y;

            /* Fixed Ratio: constrain dimensions during corner resize (0-3) */
            if (state->drag_mode >= 0 && state->drag_mode <= 3) {
                ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
                if (opts && tool_options_get_crop_constraint_mode(opts) == 1) {
                    gint rw, rh;
                    tool_options_get_crop_ratio(opts, &rw, &rh);
                    if (rw > 0 && rh > 0) {
                        gint w = state->rect_w;
                        gint h = state->rect_h;
                        if (w < 1)
                            w = 1;
                        if (h < 1)
                            h = 1;
                        if ((gdouble)w / h > (gdouble)rw / rh) {
                            h = (gint)((gdouble)w * rh / rw + 0.5);
                            if (h < 1)
                                h = 1;
                        } else {
                            w = (gint)((gdouble)h * rw / rh + 0.5);
                            if (w < 1)
                                w = 1;
                        }
                        /* Anchor opposite corner: 0=BR, 1=BL, 2=TR, 3=TL */
                        if (state->drag_mode == 0) {
                            state->rect_x += state->rect_w - w;
                            state->rect_y += state->rect_h - h;
                        } else if (state->drag_mode == 1) {
                            state->rect_y += state->rect_h - h;
                        } else if (state->drag_mode == 2) {
                            state->rect_x += state->rect_w - w;
                        }
                        state->rect_w = w;
                        state->rect_h = h;
                    }
                }
            }
        }

        /* Normalize negative dimensions (flip rect) */
        if (state->rect_w < 0) {
            state->rect_x += state->rect_w;
            state->rect_w = -state->rect_w;
        }
        if (state->rect_h < 0) {
            state->rect_y += state->rect_h;
            state->rect_h = -state->rect_h;
        }

        /* Min width/height */
        if (state->rect_w < CROP_MIN_SIZE) {
            state->rect_w = CROP_MIN_SIZE;
        }
        if (state->rect_h < CROP_MIN_SIZE) {
            state->rect_h = CROP_MIN_SIZE;
        }

        /* Clamp to image bounds unless grow canvas is enabled */
        {
            ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
            gboolean grow_canvas = opts ? tool_options_get_crop_grow_canvas(opts) : FALSE;
            if (!grow_canvas) {
                if (state->drag_mode == -1) {
                    if (state->rect_x < 0)
                        state->rect_x = 0;
                    if (state->rect_y < 0)
                        state->rect_y = 0;
                    if (state->rect_x + state->rect_w > (gint)doc->width) {
                        state->rect_x = doc->width - state->rect_w;
                    }
                    if (state->rect_y + state->rect_h > (gint)doc->height) {
                        state->rect_y = doc->height - state->rect_h;
                    }
                } else if (state->drag_mode >= 0) {
                    if (state->rect_x < 0) {
                        state->rect_w += state->rect_x;
                        state->rect_x = 0;
                    }
                    if (state->rect_y < 0) {
                        state->rect_h += state->rect_y;
                        state->rect_y = 0;
                    }
                    if (state->rect_x + state->rect_w > (gint)doc->width) {
                        state->rect_w = doc->width - state->rect_x;
                    }
                    if (state->rect_y + state->rect_h > (gint)doc->height) {
                        state->rect_h = doc->height - state->rect_y;
                    }
                    if (state->rect_w < CROP_MIN_SIZE)
                        state->rect_w = CROP_MIN_SIZE;
                    if (state->rect_h < CROP_MIN_SIZE)
                        state->rect_h = CROP_MIN_SIZE;
                }
            }
        }

        state->current_x = event->x;
        state->current_y = event->y;

        /* Sync ratio and fixed size during resize */
        if (state->drag_mode >= 0 && doc->drawing_area && state->rect_w > 0 && state->rect_h > 0) {
            tool_options_panel_sync_crop_from_rect(doc->drawing_area, state->rect_w, state->rect_h);
        }

        crop_queue_redraw(doc);
        return;
    }

    /* Not dragging: update hover cursor when we have active rect */
    if (!state->is_dragging && state->is_active && state->rect_w > 0 && state->rect_h > 0) {
        gint hovered = crop_detect_handle_at_point((gdouble)event->x, (gdouble)event->y,
                                                   (gdouble)state->rect_x, (gdouble)state->rect_y,
                                                   (gdouble)state->rect_w, (gdouble)state->rect_h,
                                                   doc->zoom_factor);

        if (hovered >= 0) {
            state->hovered_handle = hovered; /* on handle */
        } else if (event->x >= state->rect_x && event->x < state->rect_x + state->rect_w &&
                   event->y >= state->rect_y && event->y < state->rect_y + state->rect_h) {
            state->hovered_handle = -1; /* inside rect (move) */
        } else {
            state->hovered_handle = -2; /* outside */
        }

        if (window) {
            if (state->hovered_handle >= -1) {
                crop_set_cursor_for_handle(window, state->hovered_handle, tool->cursor);
            } else {
                gdk_window_set_cursor(window, tool->cursor);
            }
        }

        crop_queue_redraw(doc);
        return;
    }

    /* New crop drag (drag_mode == -2) - update current position, apply constraint */
    if (state->is_dragging && state->drag_mode == -2) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
        gint mode = opts ? tool_options_get_crop_constraint_mode(opts) : 0;
        gint rw = 0, rh = 0;
        if (opts) {
            tool_options_get_crop_ratio(opts, &rw, &rh);
        }
        if (rw <= 0)
            rw = 16;
        if (rh <= 0)
            rh = 9;

        if (mode == 2 && opts) {
            /* Fixed Size: rect at start with fixed dimensions */
            gint fw, fh;
            tool_options_get_crop_size(opts, &fw, &fh);
            if (fw <= 0)
                fw = 1;
            if (fh <= 0)
                fh = 1;
            state->current_x = state->start_x + fw;
            state->current_y = state->start_y + fh;
        } else if (mode == 1) {
            /* Fixed Ratio: constrain current to maintain ratio */
            gint dx = event->x - state->start_x;
            gint dy = event->y - state->start_y;
            gint w, h;
            if (dx < 0 && dy < 0) {
                w = -dx;
                h = -dy;
            } else if (dx < 0) {
                w = -dx;
                h = dy;
            } else if (dy < 0) {
                w = dx;
                h = -dy;
            } else {
                w = dx;
                h = dy;
            }
            if (w < 1)
                w = 1;
            if (h < 1)
                h = 1;
            if ((gdouble)w / h > (gdouble)rw / rh) {
                h = (gint)((gdouble)w * rh / rw + 0.5);
                if (h < 1)
                    h = 1;
            } else {
                w = (gint)((gdouble)h * rw / rh + 0.5);
                if (w < 1)
                    w = 1;
            }
            state->current_x = state->start_x + (dx >= 0 ? w : -w);
            state->current_y = state->start_y + (dy >= 0 ? h : -h);
            /* Sync ratio and fixed size during new crop drag */
            if (doc->drawing_area && w > 0 && h > 0) {
                tool_options_panel_sync_crop_from_rect(doc->drawing_area, w, h);
            }
        } else {
            /* Free */
            gint w = event->x - state->start_x;
            gint h = event->y - state->start_y;
            if (w < 0)
                w = -w;
            if (h < 0)
                h = -h;
            if (w < 1)
                w = 1;
            if (h < 1)
                h = 1;
            state->current_x = event->x;
            state->current_y = event->y;
            if (doc->drawing_area) {
                tool_options_panel_sync_crop_from_rect(doc->drawing_area, w, h);
            }
        }
        crop_queue_redraw(doc);
    }
}

/**
 * Crop Tool: mouse up - finalize rect state, do NOT apply crop
 */
static void crop_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    CropToolState* state;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (CropToolState*)tool->user_data;

    (void)event;

    if (!state->is_dragging) {
        return;
    }

    state->is_dragging = FALSE;

    if (state->drag_mode == -2) {
        /* New crop drag - compute rectangle from start to current */
        ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
        gint mode = opts ? tool_options_get_crop_constraint_mode(opts) : 0;
        gint x = state->start_x;
        gint y = state->start_y;
        gint w = state->current_x - state->start_x;
        gint h = state->current_y - state->start_y;

        if (mode == 2 && opts) {
            /* Fixed Size: use fixed dimensions */
            gint fw, fh;
            tool_options_get_crop_size(opts, &fw, &fh);
            w = fw > 0 ? fw : 1;
            h = fh > 0 ? fh : 1;
            x = state->start_x;
            y = state->start_y;
        } else if (mode == 1 && opts) {
            /* Fixed Ratio: constrain to ratio */
            gint rw, rh;
            tool_options_get_crop_ratio(opts, &rw, &rh);
            if (rw <= 0)
                rw = 16;
            if (rh <= 0)
                rh = 9;
            if (w < 0) {
                x += w;
                w = -w;
            }
            if (h < 0) {
                y += h;
                h = -h;
            }
            if (w < 1)
                w = 1;
            if (h < 1)
                h = 1;
            if ((gdouble)w / h > (gdouble)rw / rh) {
                h = (gint)((gdouble)w * rh / rw + 0.5);
                if (h < 1)
                    h = 1;
            } else {
                w = (gint)((gdouble)h * rw / rh + 0.5);
                if (w < 1)
                    w = 1;
            }
        } else {
            if (w < 0) {
                x += w;
                w = -w;
            }
            if (h < 0) {
                y += h;
                h = -h;
            }
        }

        /* Clamp to image bounds unless grow canvas is enabled */
        {
            ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
            gboolean grow_canvas = opts ? tool_options_get_crop_grow_canvas(opts) : FALSE;
            if (!grow_canvas) {
                if (x < 0) {
                    w += x;
                    x = 0;
                }
                if (y < 0) {
                    h += y;
                    y = 0;
                }
                if (x + w > (gint)doc->width) {
                    w = doc->width - x;
                }
                if (y + h > (gint)doc->height) {
                    h = doc->height - y;
                }
            }
        }

        if (w < CROP_MIN_SIZE)
            w = CROP_MIN_SIZE;
        if (h < CROP_MIN_SIZE)
            h = CROP_MIN_SIZE;

        if (w > 0 && h > 0) {
            state->rect_x = x;
            state->rect_y = y;
            state->rect_w = w;
            state->rect_h = h;
            state->is_active = TRUE;
            /* Sync ratio and fixed size to tool options panel */
            if (doc->drawing_area) {
                tool_options_panel_sync_crop_from_rect(doc->drawing_area, w, h);
            }
        }

        state->drag_mode = -1;
        state->hovered_handle = -1;
    } else {
        /* Move or resize completed - rect already updated by mouse_move, sync already done during drag */
        state->is_active = (state->rect_w > 0 && state->rect_h > 0);
        state->drag_mode = -1;
        state->hovered_handle = -1;
    }

    crop_queue_redraw(doc);
}

/**
 * Get the crop rectangle from the crop tool (when active)
 */
gboolean tool_crop_get_rect(Tool* tool, gint* out_x, gint* out_y, gint* out_w, gint* out_h) {
    CropToolState* state;

    if (!tool || !tool->user_data || !out_x || !out_y || !out_w || !out_h) {
        return FALSE;
    }

    state = (CropToolState*)tool->user_data;
    if (!state->is_active || state->is_dragging || state->rect_w <= 0 || state->rect_h <= 0) {
        return FALSE;
    }

    *out_x = state->rect_x;
    *out_y = state->rect_y;
    *out_w = state->rect_w;
    *out_h = state->rect_h;
    return TRUE;
}

/**
 * Reset crop tool state (called when tool is deactivated)
 */
void tool_crop_reset(Tool* tool) {
    if (!tool || !tool->user_data) {
        return;
    }

    CropToolState* state = (CropToolState*)tool->user_data;

    state->is_active = FALSE;
    state->is_dragging = FALSE;
    state->drag_mode = -2;
    state->hovered_handle = -1;
}

/**
 * Create the Crop Tool
 */
Tool* tool_crop_create(void) {
    Tool* tool = tool_new("Crop", TOOL_CROP, GDK_CROSSHAIR, TOOL_OPT_NONE);

    if (!tool) {
        return NULL;
    }

    tool->mouse_down = crop_tool_mouse_down;
    tool->mouse_move = crop_tool_mouse_move;
    tool->mouse_up = crop_tool_mouse_up;

    /* Allocate and initialize tool state */
    tool->user_data = g_malloc0(sizeof(CropToolState));
    if (!tool->user_data) {
        tool_free(tool);
        return NULL;
    }

    return tool;
}

/**
 * Update crop rectangle to match current ratio/size options.
 * Only applies when there is an active, finalized crop rect (not during drag).
 */
void tool_crop_update_rect_from_options(ImageDocument* doc, void* registry) {
    CropToolState* state;
    Tool* crop_tool;
    ToolRegistry* tool_registry = (ToolRegistry*)registry;
    ToolOptions* opts;

    if (!doc || !tool_registry) {
        return;
    }

    crop_tool = tool_manager_get(tool_registry, TOOL_CROP);
    if (!crop_tool || !crop_tool->user_data) {
        return;
    }

    state = (CropToolState*)crop_tool->user_data;

    /* Only update when we have a finalized rect (not during drag) */
    if (!state->is_active || state->is_dragging || state->rect_w <= 0 || state->rect_h <= 0) {
        return;
    }

    opts = tool_options_get_for_tool(TOOL_CROP);
    if (!opts) {
        return;
    }

    if (tool_options_get_crop_constraint_mode(opts) == 1) {
        /* Fixed Ratio: constrain rect to new ratio, keep center */
        gint rw, rh;
        tool_options_get_crop_ratio(opts, &rw, &rh);
        if (rw <= 0 || rh <= 0) {
            return;
        }
        gint cx = state->rect_x + state->rect_w / 2;
        gint cy = state->rect_y + state->rect_h / 2;
        gdouble target_ratio = (gdouble)rw / rh;
        gdouble cur_ratio = (gdouble)state->rect_w / state->rect_h;
        gint new_w, new_h;

        if (cur_ratio > target_ratio) {
            new_w = (gint)((gdouble)state->rect_h * rw / rh + 0.5);
            new_h = state->rect_h;
        } else {
            new_w = state->rect_w;
            new_h = (gint)((gdouble)state->rect_w * rh / rw + 0.5);
        }
        if (new_w < CROP_MIN_SIZE)
            new_w = CROP_MIN_SIZE;
        if (new_h < CROP_MIN_SIZE)
            new_h = CROP_MIN_SIZE;

        state->rect_x = cx - new_w / 2;
        state->rect_y = cy - new_h / 2;
        state->rect_w = new_w;
        state->rect_h = new_h;
    } else if (tool_options_get_crop_constraint_mode(opts) == 2) {
        /* Fixed Size: resize rect to fixed dimensions, keep center */
        gint fw, fh;
        tool_options_get_crop_size(opts, &fw, &fh);
        if (fw <= 0)
            fw = 1;
        if (fh <= 0)
            fh = 1;

        gint cx = state->rect_x + state->rect_w / 2;
        gint cy = state->rect_y + state->rect_h / 2;

        state->rect_x = cx - fw / 2;
        state->rect_y = cy - fh / 2;
        state->rect_w = fw;
        state->rect_h = fh;
    }

    /* Clamp to image bounds */
    if (state->rect_x < 0)
        state->rect_x = 0;
    if (state->rect_y < 0)
        state->rect_y = 0;
    if (state->rect_x + state->rect_w > (gint)doc->width) {
        state->rect_x = doc->width - state->rect_w;
    }
    if (state->rect_y + state->rect_h > (gint)doc->height) {
        state->rect_y = doc->height - state->rect_h;
    }
}

/* Golden ratio φ ≈ 1.618, 1/(1+φ) ≈ 0.382, φ/(1+φ) ≈ 0.618 */
#define CROP_GOLDEN_RATIO 1.618033988749895
#define CROP_GOLDEN_LOW (1.0 / (1.0 + CROP_GOLDEN_RATIO))
#define CROP_GOLDEN_HIGH (CROP_GOLDEN_RATIO / (1.0 + CROP_GOLDEN_RATIO))

/* Stroke path with outline style: dark gray 3px, white 1px center */
static void overlay_stroke_path(cairo_t* cr, gdouble line_width) {
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width(cr, line_width * 3.0);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, line_width);
    cairo_stroke(cr);
}

/* Offset to stop lines short of intersections (avoids double stroke at overlaps) */
#define OVERLAY_GAP (1.5)

/**
 * Draw overlay guides inside crop rect (Rule of Thirds, Center Lines, etc).
 * Grid lines are broken at intersections so overlap areas get no stroke (connected look).
 * Diagonal mode uses antialiasing (non-axis-aligned).
 */
static void crop_draw_overlay_guides(cairo_t* cr, gdouble rect_x, gdouble rect_y,
                                     gdouble rect_w, gdouble rect_h,
                                     gint overlay_mode, gdouble line_width) {
    gdouble x1, x2, y1, y2;
    gdouble gap;

    cairo_set_dash(cr, NULL, 0, 0);
    gap = line_width * OVERLAY_GAP;

    switch (overlay_mode) {
        case 1: { /* Rule of Thirds */
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            x1 = rect_x + rect_w / 3.0;
            x2 = rect_x + 2.0 * rect_w / 3.0;
            y1 = rect_y + rect_h / 3.0;
            y2 = rect_y + 2.0 * rect_h / 3.0;
            /* Vertical at x1: 3 segments with gaps at y1, y2 */
            cairo_move_to(cr, x1, rect_y);
            cairo_line_to(cr, x1, y1 - gap);
            cairo_move_to(cr, x1, y1 + gap);
            cairo_line_to(cr, x1, y2 - gap);
            cairo_move_to(cr, x1, y2 + gap);
            cairo_line_to(cr, x1, rect_y + rect_h);
            /* Vertical at x2 */
            cairo_move_to(cr, x2, rect_y);
            cairo_line_to(cr, x2, y1 - gap);
            cairo_move_to(cr, x2, y1 + gap);
            cairo_line_to(cr, x2, y2 - gap);
            cairo_move_to(cr, x2, y2 + gap);
            cairo_line_to(cr, x2, rect_y + rect_h);
            /* Horizontal at y1: 3 segments with gaps at x1, x2 */
            cairo_move_to(cr, rect_x, y1);
            cairo_line_to(cr, x1 - gap, y1);
            cairo_move_to(cr, x1 + gap, y1);
            cairo_line_to(cr, x2 - gap, y1);
            cairo_move_to(cr, x2 + gap, y1);
            cairo_line_to(cr, rect_x + rect_w, y1);
            /* Horizontal at y2 */
            cairo_move_to(cr, rect_x, y2);
            cairo_line_to(cr, x1 - gap, y2);
            cairo_move_to(cr, x1 + gap, y2);
            cairo_line_to(cr, x2 - gap, y2);
            cairo_move_to(cr, x2 + gap, y2);
            cairo_line_to(cr, rect_x + rect_w, y2);
            overlay_stroke_path(cr, line_width);
            break;
        }
        case 2: { /* Phi Grid */
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            x1 = rect_x + rect_w * CROP_GOLDEN_LOW;
            x2 = rect_x + rect_w * CROP_GOLDEN_HIGH;
            y1 = rect_y + rect_h * CROP_GOLDEN_LOW;
            y2 = rect_y + rect_h * CROP_GOLDEN_HIGH;
            cairo_move_to(cr, x1, rect_y);
            cairo_line_to(cr, x1, y1 - gap);
            cairo_move_to(cr, x1, y1 + gap);
            cairo_line_to(cr, x1, y2 - gap);
            cairo_move_to(cr, x1, y2 + gap);
            cairo_line_to(cr, x1, rect_y + rect_h);
            cairo_move_to(cr, x2, rect_y);
            cairo_line_to(cr, x2, y1 - gap);
            cairo_move_to(cr, x2, y1 + gap);
            cairo_line_to(cr, x2, y2 - gap);
            cairo_move_to(cr, x2, y2 + gap);
            cairo_line_to(cr, x2, rect_y + rect_h);
            cairo_move_to(cr, rect_x, y1);
            cairo_line_to(cr, x1 - gap, y1);
            cairo_move_to(cr, x1 + gap, y1);
            cairo_line_to(cr, x2 - gap, y1);
            cairo_move_to(cr, x2 + gap, y1);
            cairo_line_to(cr, rect_x + rect_w, y1);
            cairo_move_to(cr, rect_x, y2);
            cairo_line_to(cr, x1 - gap, y2);
            cairo_move_to(cr, x1 + gap, y2);
            cairo_line_to(cr, x2 - gap, y2);
            cairo_move_to(cr, x2 + gap, y2);
            cairo_line_to(cr, rect_x + rect_w, y2);
            overlay_stroke_path(cr, line_width);
            break;
        }
        case 3: { /* Golden Spiral - DISABLED (to fix later) */
#if 0
            gdouble cx, cy, a, theta, r, step;
            gdouble max_extent, sy;
            gint n, i;
            gboolean portrait;
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
            /* Pole at phi grid intersection: bottom-right of top-left quadrant (0.382, 0.382) */
            cx = rect_x + rect_w * CROP_GOLDEN_LOW;
            cy = rect_y + rect_h * CROP_GOLDEN_LOW;
            max_extent = fmin(rect_w * CROP_GOLDEN_HIGH, rect_h * CROP_GOLDEN_HIGH);
            a = max_extent / pow(CROP_GOLDEN_RATIO, 9.0);
            portrait = (rect_h > rect_w);
            sy = portrait ? -1.0 : 1.0;
            n = 180;
            step = 4.5 * G_PI / (gdouble)n;
            cairo_save(cr);
            cairo_rectangle(cr, rect_x, rect_y, rect_w, rect_h);
            cairo_clip(cr);
            cairo_move_to(cr, cx + a, cy);
            for (i = 1; i <= n; i++) {
                theta = step * (gdouble)i;
                r = a * pow(CROP_GOLDEN_RATIO, 2.0 * theta / G_PI);
                cairo_line_to(cr, cx + r * cos(theta), cy + sy * r * sin(theta));
            }
            overlay_stroke_path(cr, line_width);
            cairo_restore(cr);
#endif
            break;
        }
        case 4: /* Diagonal */
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
            cairo_move_to(cr, rect_x, rect_y);
            cairo_line_to(cr, rect_x + rect_w, rect_y + rect_h);
            cairo_move_to(cr, rect_x + rect_w, rect_y);
            cairo_line_to(cr, rect_x, rect_y + rect_h);
            overlay_stroke_path(cr, line_width);
            break;
        case 5: /* Center Lines */
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            x1 = rect_x + rect_w / 2.0;
            y1 = rect_y + rect_h / 2.0;
            cairo_move_to(cr, x1, rect_y);
            cairo_line_to(cr, x1, y1 - gap);
            cairo_move_to(cr, x1, y1 + gap);
            cairo_line_to(cr, x1, rect_y + rect_h);
            cairo_move_to(cr, rect_x, y1);
            cairo_line_to(cr, x1 - gap, y1);
            cairo_move_to(cr, x1 + gap, y1);

            cairo_line_to(cr, rect_x + rect_w, y1);
            overlay_stroke_path(cr, line_width);
            break;
        default:
            break;
    }
}

/**
 * Draw crop overlay during drag/edit
 * Reuses selection tool style: solid lines instead of dashed, same handle appearance
 */
void tool_crop_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    CropToolState* state;
    Tool* active_tool;
    ToolRegistry* tool_registry;
    ToolOptions* opts;
    gint rect_x, rect_y, rect_w, rect_h;
    gdouble handle_size, half_handle, handle_line_width;
    gdouble corners[8][2];
    gint i;

    if (!doc || !doc->drawing_area || !cr) {
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        return;
    }

    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return;
    }

    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_CROP || !active_tool->user_data) {
        return;
    }

    state = (CropToolState*)active_tool->user_data;

    if (!state->is_dragging && !state->is_active) {
        return;
    }

    /* Determine rectangle to draw */
    if (state->is_dragging && state->drag_mode == -2) {
        rect_x = state->start_x;
        rect_y = state->start_y;
        rect_w = state->current_x - state->start_x;
        rect_h = state->current_y - state->start_y;
        if (rect_w < 0) {
            rect_x += rect_w;
            rect_w = -rect_w;
        }
        if (rect_h < 0) {
            rect_y += rect_h;
            rect_h = -rect_h;
        }
    } else {
        rect_x = state->rect_x;
        rect_y = state->rect_y;
        rect_w = state->rect_w;
        rect_h = state->rect_h;
    }

    if (rect_w <= 0 || rect_h <= 0) {
        return;
    }

    opts = tool_options_get_for_tool(TOOL_CROP);
    handle_size = 12.0 / zoom;
    half_handle = handle_size / 2.0;
    handle_line_width = 1.0 / zoom;
    if (handle_line_width < 0.5) {
        handle_line_width = 0.5;
    }

/* Snap coord to device pixel center for crisp 1-pixel strokes (avoids antialiased fade) */
#define CROP_SNAP(c) (floor((c)*zoom + 0.5) / zoom)

    cairo_save(cr);

    if (zoom != 1.0) {
        cairo_scale(cr, zoom, zoom);
    }

    /* 1. Darken outside (if enabled) */
    if (opts && opts->crop_darken_outside && opts->crop_darken_opacity > 0.0f) {
        gdouble alpha = (gdouble)opts->crop_darken_opacity / 100.0;
        guint w = doc->width;
        guint h = doc->height;

        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, alpha);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        /* Top strip */
        if (rect_y > 0) {
            cairo_rectangle(cr, 0, 0, (gdouble)w, (gdouble)rect_y);
            cairo_fill(cr);
        }
        /* Bottom strip */
        if ((gint)h > rect_y + rect_h) {
            cairo_rectangle(cr, 0, rect_y + rect_h, (gdouble)w, (gdouble)h - (rect_y + rect_h));
            cairo_fill(cr);
        }
        /* Left strip */
        if (rect_x > 0 && rect_h > 0) {
            cairo_rectangle(cr, 0, rect_y, (gdouble)rect_x, (gdouble)rect_h);
            cairo_fill(cr);
        }
        /* Right strip */
        if ((gint)w > rect_x + rect_w && rect_h > 0) {
            cairo_rectangle(cr, rect_x + rect_w, rect_y, (gdouble)w - (rect_x + rect_w), (gdouble)rect_h);
            cairo_fill(cr);
        }
    }

    /* 2. Rectangle border */
    gboolean outline_highlight = (state->hovered_handle == -1); /* cursor inside rect (move) */
    cairo_set_dash(cr, NULL, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(cr, CROP_SNAP(rect_x), CROP_SNAP(rect_y),
                    CROP_SNAP(rect_x + rect_w) - CROP_SNAP(rect_x),
                    CROP_SNAP(rect_y + rect_h) - CROP_SNAP(rect_y));
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width(cr, handle_line_width * 3.0);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, outline_highlight ? 0.0 : 1.0, outline_highlight ? 0.5 : 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, handle_line_width);
    cairo_stroke(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    /* 3. Overlay guides (inside crop rect) */
    if (opts && opts->crop_overlay_mode > 0) {
        crop_draw_overlay_guides(cr, rect_x, rect_y, rect_w, rect_h,
                                 opts->crop_overlay_mode, handle_line_width);
    }

    /* 4. Resize handles (8: corners + edges) - snap to pixel centers for crisp strokes */
    corners[0][0] = CROP_SNAP(rect_x);
    corners[0][1] = CROP_SNAP(rect_y);
    corners[1][0] = CROP_SNAP(rect_x + rect_w);
    corners[1][1] = CROP_SNAP(rect_y);
    corners[2][0] = CROP_SNAP(rect_x);
    corners[2][1] = CROP_SNAP(rect_y + rect_h);
    corners[3][0] = CROP_SNAP(rect_x + rect_w);
    corners[3][1] = CROP_SNAP(rect_y + rect_h);
    corners[4][0] = CROP_SNAP(rect_x + rect_w / 2.0);
    corners[4][1] = CROP_SNAP(rect_y);
    corners[5][0] = CROP_SNAP(rect_x + rect_w);
    corners[5][1] = CROP_SNAP(rect_y + rect_h / 2.0);
    corners[6][0] = CROP_SNAP(rect_x + rect_w / 2.0);
    corners[6][1] = CROP_SNAP(rect_y + rect_h);
    corners[7][0] = CROP_SNAP(rect_x);
    corners[7][1] = CROP_SNAP(rect_y + rect_h / 2.0);

    cairo_set_dash(cr, NULL, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

    for (i = 0; i < 8; i++) {
        gdouble cx = corners[i][0];
        gdouble cy = corners[i][1];
        gboolean hovered = (state->hovered_handle == i);
        gdouble hx = CROP_SNAP(cx - half_handle);
        gdouble hy = CROP_SNAP(cy - half_handle);
        gdouble hw = CROP_SNAP(cx + half_handle) - hx;
        gdouble hh = CROP_SNAP(cy + half_handle) - hy;

        cairo_rectangle(cr, hx, hy, hw, hh);
        cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
        cairo_set_line_width(cr, handle_line_width * 3.0);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgba(cr, hovered ? 0.0 : 1.0, hovered ? 0.5 : 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, handle_line_width);
        cairo_stroke(cr);
    }
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    cairo_restore(cr);
}
