#include "tools/tool_rect_select.h"
#include "document.h"
#include "selection.h"
#include "tool_manager.h"
#include "tool_options.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Selection animation constant */
#define ANT_DASH_SPEED_NORMAL 67 /* Frame time in ms (15 fps) */
#define ANT_DASH_SPEED_FAST 33   /* Frame time in ms (30 fps) */
#define ANT_DASH_SPEED_MIN 16    /* Frame time in ms (60 fps) */

/* Forward declaration */
static gboolean on_rect_select_tool_animation_timer(gpointer user_data);

/**
 * Animation timer callback for rect select tool marching ants
 */
static gboolean on_rect_select_tool_animation_timer(gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    ToolRegistry* tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    Tool* active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_RECT_SELECT || !active_tool->user_data) {
        return FALSE;
    }

    RectSelectToolState* state = (RectSelectToolState*)active_tool->user_data;

    /* Only animate if editing */
    if (!state->is_editing) {
        state->animation_timer_id = 0;
        return FALSE; /* Stop timer */
    }

    /* Update animation phase (cycle backwards 0-3 for clockwise rotation of 4-pixel dash) */
    state->animation_phase = (state->animation_phase > 0) ? (state->animation_phase - 1) : 3;

    /* Queue redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE; /* Keep timer running */
}

/**
 * Rectangular Selection Tool: mouse down - start selection drag
 */
static void rect_select_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    RectSelectToolState* state;

    if (!tool || !doc) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(RectSelectToolState));
    }
    state = (RectSelectToolState*)tool->user_data;

    /* Check if clicking on existing editable selection */
    if (state->is_editing) {
        /* Check if clicking on a handle or inside selection for move/resize */
        gint tolerance = 7; /* Increased from 5 to match larger handles */
        gdouble corners[4][2] = {
            {state->selection_x, state->selection_y},                                          /* top-left */
            {state->selection_x + state->selection_w, state->selection_y},                     /* top-right */
            {state->selection_x, state->selection_y + state->selection_h},                     /* bottom-left */
            {state->selection_x + state->selection_w, state->selection_y + state->selection_h} /* bottom-right */
        };

        /* Check if clicking on a handle */
        for (gint i = 0; i < 4; i++) {
            gint dx = event->x - (gint)corners[i][0];
            gint dy = event->y - (gint)corners[i][1];
            if (dx * dx + dy * dy <= tolerance * tolerance) {
                /* Clicking on handle - start resize */
                state->dragging_handle = i;
                state->is_dragging = TRUE;
                state->anchor_x = event->x;
                state->anchor_y = event->y;
                return;
            }
        }

        /* Check if clicking inside selection - move it */
        if (event->x >= state->selection_x && event->x < state->selection_x + state->selection_w &&
            event->y >= state->selection_y && event->y < state->selection_y + state->selection_h) {
            /* Clicking inside - start move */
            state->dragging_handle = -1; /* -1 means move mode */
            state->is_dragging = TRUE;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            return;
        }

        /* Clicking outside - finalize and start new selection */
        /* Create a finalized Selection object from the current edit selection */
        if (state->selection_w > 0 && state->selection_h > 0) {
            if (doc->selection) {
                selection_free(doc->selection);
            }
            doc->selection = selection_create_rectangle(
                state->selection_x,
                state->selection_y,
                state->selection_w,
                state->selection_h,
                state->smooth_mode,
                state->feather_radius);
            if (doc->selection) {
                selection_set_animated(doc->selection, TRUE);
            }
        }

        state->is_editing = FALSE;
        if (state->animation_timer_id > 0) {
            g_source_remove(state->animation_timer_id);
            state->animation_timer_id = 0;
        }
    }

    /* Start new selection drag */
    state->is_dragging = TRUE;
    state->dragging_handle = -2; /* -2 means new selection */
    state->anchor_x = event->x;
    state->anchor_y = event->y;
    state->current_x = event->x;
    state->current_y = event->y;
    state->hovered_handle = -1;

    /* Get current tool options for selection */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (opts) {
        /* Note: For now we use fixed defaults. These will be configurable via tool options panel */
        state->combine_mode = SELECTION_COMBINE_NEW;
        state->smooth_mode = SELECTION_SMOOTH_NONE;
        state->feather_radius = 0;
    } else {
        state->combine_mode = SELECTION_COMBINE_NEW;
        state->smooth_mode = SELECTION_SMOOTH_NONE;
        state->feather_radius = 0;
    }

    /* Change cursor to crosshair */
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkCursor* cursor = gdk_cursor_new_from_name(display, "crosshair");
        if (!cursor) {
            cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
        }
        if (cursor) {
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }

    /* Request redraw of selection overlay */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Helper to set cursor based on handle
 */
static void set_cursor_for_handle(GdkWindow* window, gint handle) {
    if (!window)
        return;

    GdkDisplay* display = gdk_window_get_display(window);
    GdkCursor* cursor = NULL;

    if (handle == -1) {
        /* Move cursor */
        cursor = gdk_cursor_new_from_name(display, "move");
        if (!cursor) {
            cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
        }
    } else if (handle == 0) {
        /* Top-left: NW-SE diagonal */
        cursor = gdk_cursor_new_from_name(display, "nwse-resize");
    } else if (handle == 1) {
        /* Top-right: NE-SW diagonal */
        cursor = gdk_cursor_new_from_name(display, "nesw-resize");
    } else if (handle == 2) {
        /* Bottom-left: NE-SW diagonal */
        cursor = gdk_cursor_new_from_name(display, "nesw-resize");
    } else if (handle == 3) {
        /* Bottom-right: NW-SE diagonal */
        cursor = gdk_cursor_new_from_name(display, "nwse-resize");
    } else {
        /* Default cursor */
        gdk_window_set_cursor(window, NULL);
        return;
    }

    if (cursor) {
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }
}

/**
 * Rectangular Selection Tool: mouse move - update selection preview
 */
static void rect_select_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    RectSelectToolState* state;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (RectSelectToolState*)tool->user_data;
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);

    /* Handle resize/move during edit drag */
    if (state->dragging_handle >= -1 && state->is_dragging) {
        /* Update cursor while dragging */
        set_cursor_for_handle(window, state->dragging_handle);

        if (state->dragging_handle == -1) {
            /* Move mode - translate selection */
            gint dx = event->x - state->anchor_x;
            gint dy = event->y - state->anchor_y;
            state->selection_x += dx;
            state->selection_y += dy;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            /* Update current position for consistency */
            state->current_x = event->x;
            state->current_y = event->y;
        } else if (state->dragging_handle >= 0) {
            /* Resize mode - adjust based on which handle */
            gint dx = event->x - state->anchor_x;
            gint dy = event->y - state->anchor_y;

            switch (state->dragging_handle) {
                case 0: /* top-left */
                    state->selection_x += dx;
                    state->selection_y += dy;
                    state->selection_w -= dx;
                    state->selection_h -= dy;
                    break;
                case 1: /* top-right */
                    state->selection_y += dy;
                    state->selection_w += dx;
                    state->selection_h -= dy;
                    break;
                case 2: /* bottom-left */
                    state->selection_x += dx;
                    state->selection_w -= dx;
                    state->selection_h += dy;
                    break;
                case 3: /* bottom-right */
                    state->selection_w += dx;
                    state->selection_h += dy;
                    break;
            }

            state->anchor_x = event->x;
            state->anchor_y = event->y;
            /* Update current position for consistency */
            state->current_x = event->x;
            state->current_y = event->y;
        }

        /* Clamp selection to valid bounds */
        /* First, handle negative dimensions (can happen during resize) */
        if (state->selection_w < 0) {
            state->selection_x += state->selection_w;
            state->selection_w = -state->selection_w;
        }
        if (state->selection_h < 0) {
            state->selection_y += state->selection_h;
            state->selection_h = -state->selection_h;
        }

        /* For MOVE operations (dragging_handle == -1), only clamp position */
        if (state->dragging_handle == -1) {
            /* Clamp position to keep selection within bounds */
            if (state->selection_x < 0)
                state->selection_x = 0;
            if (state->selection_y < 0)
                state->selection_y = 0;
            if (state->selection_x + state->selection_w > (gint)doc->width)
                state->selection_x = doc->width - state->selection_w;
            if (state->selection_y + state->selection_h > (gint)doc->height)
                state->selection_y = doc->height - state->selection_h;
        } else {
            /* For RESIZE operations, clamp both position and dimensions */
            if (state->selection_x < 0)
                state->selection_x = 0;
            if (state->selection_y < 0)
                state->selection_y = 0;
            if (state->selection_x + state->selection_w > (gint)doc->width) {
                state->selection_w = doc->width - state->selection_x;
            }
            if (state->selection_y + state->selection_h > (gint)doc->height) {
                state->selection_h = doc->height - state->selection_y;
            }
        }

        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        return;
    }

    /* When not dragging but in edit mode, update cursor based on hover */
    if (!state->is_dragging && state->is_editing) {
        gint tolerance = 7; /* Increased from 5 to match larger handles */
        gdouble corners[4][2] = {
            {state->selection_x, state->selection_y},                                          /* top-left */
            {state->selection_x + state->selection_w, state->selection_y},                     /* top-right */
            {state->selection_x, state->selection_y + state->selection_h},                     /* bottom-left */
            {state->selection_x + state->selection_w, state->selection_y + state->selection_h} /* bottom-right */
        };

        /* Check if hovering over a handle */
        gint hovered_handle = -2;
        for (gint i = 0; i < 4; i++) {
            gint dx = event->x - (gint)corners[i][0];
            gint dy = event->y - (gint)corners[i][1];
            if (dx * dx + dy * dy <= tolerance * tolerance) {
                hovered_handle = i;
                break;
            }
        }

        /* Check if hovering inside selection for move */
        if (hovered_handle == -2 &&
            event->x >= state->selection_x && event->x < state->selection_x + state->selection_w &&
            event->y >= state->selection_y && event->y < state->selection_y + state->selection_h) {
            hovered_handle = -1;
        }

        /* Update cursor based on hover */
        if (hovered_handle >= -1) {
            set_cursor_for_handle(window, hovered_handle);
        } else {
            /* Default cursor outside selection */
            GdkDisplay* display = gdk_window_get_display(window);
            GdkCursor* cursor = gdk_cursor_new_from_name(display, "crosshair");
            if (!cursor) {
                cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
            }
            if (cursor) {
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
        }

        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        return;
    }

    /* Handle new selection drag (dragging_handle == -2) */
    if (state->dragging_handle != -2 || !state->is_dragging) {
        return;
    }

    /* Update current position */
    state->current_x = event->x;
    state->current_y = event->y;

    /* Calculate rectangle bounds */
    gint x = state->anchor_x;
    gint y = state->anchor_y;
    gint width = state->current_x - state->anchor_x;
    gint height = state->current_y - state->anchor_y;

    /* Normalize rectangle coordinates */
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }

    /* Check which handle is under mouse (tolerance of 5 pixels) */
    gdouble tolerance = 5.0;
    gint hovered_handle = -1;

    /* Corner positions: top-left, top-right, bottom-left, bottom-right */
    gdouble corners[4][2] = {
        {x, y},                 /* 0: top-left */
        {x + width, y},         /* 1: top-right */
        {x, y + height},        /* 2: bottom-left */
        {x + width, y + height} /* 3: bottom-right */
    };

    for (gint i = 0; i < 4; i++) {
        gdouble dx = event->x - corners[i][0];
        gdouble dy = event->y - corners[i][1];
        gdouble distance = sqrt(dx * dx + dy * dy);

        if (distance <= tolerance) {
            hovered_handle = i;
            break;
        }
    }

    state->hovered_handle = hovered_handle;

    /* Request redraw of selection overlay only */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Rectangular Selection Tool: mouse up - finalize selection drag
 */
static void rect_select_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    RectSelectToolState* state;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (RectSelectToolState*)tool->user_data;

    (void)event; /* Unused */

    if (!state->is_dragging) {
        return;
    }

    state->is_dragging = FALSE;

    /* Only handle NEW selections (dragging_handle == -2)
     * For moves/resizes, the bounds are already updated by mouse_move */
    if (state->dragging_handle != -2) {
        /* Move or resize just completed - don't recalculate bounds */
        state->is_editing = TRUE; /* Ensure we stay in edit mode */
        return;
    }

    /* Handle NEW selection drag (dragging_handle == -2) */
    /* Calculate rectangle from anchor to current position
     * Handle negative width/height (dragging backwards) */
    gint x = state->anchor_x;
    gint y = state->anchor_y;
    gint width = state->current_x - state->anchor_x;
    gint height = state->current_y - state->anchor_y;

    /* Normalize rectangle coordinates */
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }

    /* Clamp to image bounds */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x + width > (gint)doc->width) {
        width = doc->width - x;
    }
    if (y + height > (gint)doc->height) {
        height = doc->height - y;
    }

    /* Only proceed if rectangle has non-zero area */
    if (width > 0 && height > 0) {
        /* Store selection bounds for editing */
        state->selection_x = x;
        state->selection_y = y;
        state->selection_w = width;
        state->selection_h = height;
        state->is_editing = TRUE;
        state->dragging_handle = -1;
        state->hovered_handle = -1;
        state->animation_phase = 0; /* Start animation */

        /* Start animation timer if not already running */
        if (state->animation_timer_id == 0) {
            state->animation_timer_id = g_timeout_add(ANT_DASH_SPEED_NORMAL,
                                                      on_rect_select_tool_animation_timer,
                                                      (gpointer)doc);
        }
    }

    /* Request final redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Create the Rectangular Selection Tool
 */
Tool* tool_rect_select_create(void) {
    Tool* tool = tool_new("Rectangular Select", TOOL_RECT_SELECT, GDK_CROSSHAIR,
                          TOOL_OPT_SELECTION_MODE | TOOL_OPT_SELECTION_SMOOTH);

    if (!tool) {
        return NULL;
    }

    tool->mouse_down = rect_select_tool_mouse_down;
    tool->mouse_move = rect_select_tool_mouse_move;
    tool->mouse_up = rect_select_tool_mouse_up;

    return tool;
}
