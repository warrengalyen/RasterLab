#include "tools/tool_crop.h"
#include "document.h"
#include "selection.h"
#include "tool_manager.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <math.h>

/* Minimum crop rectangle dimension in pixels */
#define CROP_MIN_SIZE 1

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
            if (doc->drawing_area) {
                gtk_widget_queue_draw(doc->drawing_area);
            }
            return;
        }

        /* Check if clicking inside rect - move */
        if (event->x >= state->rect_x && event->x < state->rect_x + state->rect_w &&
            event->y >= state->rect_y && event->y < state->rect_y + state->rect_h) {
            state->drag_mode = -1;
            state->is_dragging = TRUE;
            state->start_x = event->x;
            state->start_y = event->y;
            if (doc->drawing_area) {
                gtk_widget_queue_draw(doc->drawing_area);
            }
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

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
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

        /* Clamp to image bounds */
        if (state->drag_mode == -1) {
            if (state->rect_x < 0) state->rect_x = 0;
            if (state->rect_y < 0) state->rect_y = 0;
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
            if (state->rect_w < CROP_MIN_SIZE) state->rect_w = CROP_MIN_SIZE;
            if (state->rect_h < CROP_MIN_SIZE) state->rect_h = CROP_MIN_SIZE;
        }

        state->current_x = event->x;
        state->current_y = event->y;

        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        return;
    }

    /* Not dragging: update hover cursor when we have active rect */
    if (!state->is_dragging && state->is_active && state->rect_w > 0 && state->rect_h > 0) {
        gint hovered = crop_detect_handle_at_point((gdouble)event->x, (gdouble)event->y,
                                                    (gdouble)state->rect_x, (gdouble)state->rect_y,
                                                    (gdouble)state->rect_w, (gdouble)state->rect_h,
                                                    doc->zoom_factor);

        if (hovered < 0 &&
            event->x >= state->rect_x && event->x < state->rect_x + state->rect_w &&
            event->y >= state->rect_y && event->y < state->rect_y + state->rect_h) {
            hovered = -1;
        }

        state->hovered_handle = hovered;

        if (window) {
            if (hovered >= -1) {
                crop_set_cursor_for_handle(window, hovered, tool->cursor);
            } else {
                gdk_window_set_cursor(window, tool->cursor);
            }
        }

        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        return;
    }

    /* New crop drag (drag_mode == -2) - just update current position */
    if (state->is_dragging && state->drag_mode == -2) {
        state->current_x = event->x;
        state->current_y = event->y;
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
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
        gint x = state->start_x;
        gint y = state->start_y;
        gint w = state->current_x - state->start_x;
        gint h = state->current_y - state->start_y;

        if (w < 0) {
            x += w;
            w = -w;
        }
        if (h < 0) {
            y += h;
            h = -h;
        }

        /* Clamp to image bounds */
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

        if (w < CROP_MIN_SIZE) w = CROP_MIN_SIZE;
        if (h < CROP_MIN_SIZE) h = CROP_MIN_SIZE;

        if (w > 0 && h > 0) {
            state->rect_x = x;
            state->rect_y = y;
            state->rect_w = w;
            state->rect_h = h;
            state->is_active = TRUE;
        }

        state->drag_mode = -1;
        state->hovered_handle = -1;
    } else {
        /* Move or resize completed - rect already updated by mouse_move */
        state->is_active = (state->rect_w > 0 && state->rect_h > 0);
        state->drag_mode = -1;
        state->hovered_handle = -1;
    }

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
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
 * Draw crop overlay during drag/edit - placeholder for Stage 3
 */
void tool_crop_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    (void)doc;
    (void)cr;
    (void)zoom;
    /* Stage 3: draw solid border, handles, darken outside, overlay guides */
}
