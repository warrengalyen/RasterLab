#include "tools/tool_hand.h"
#include "document.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <math.h>

/**
 * Hand Tool state
 */
typedef struct {
    gboolean is_dragging;     /* Currently dragging? */
    gdouble start_x;          /* Mouse down position X in viewport coordinates */
    gdouble start_y;          /* Mouse down position Y in viewport coordinates */
    gdouble last_x;           /* Last mouse position X (for incremental tracking) */
    gdouble last_y;           /* Last mouse position Y (for incremental tracking) */
    gdouble start_hadj_value; /* Horizontal adjustment value at drag start */
    gdouble start_vadj_value; /* Vertical adjustment value at drag start */
    GtkAdjustment* hadj;      /* Cached horizontal adjustment reference */
    GtkAdjustment* vadj;      /* Cached vertical adjustment reference */
} HandToolState;

/**
 * Hand tool: mouse down - start panning
 */
static void hand_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    HandToolState* state;
    GtkAdjustment* hadj = NULL;
    GtkAdjustment* vadj = NULL;

    if (!tool || !doc || !doc->scrolled_window) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(HandToolState));
    }
    state = (HandToolState*)tool->user_data;

    /* Get scroll adjustments */
    if (GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    }

    if (!hadj || !vadj) {
        return;
    }

    /* Start dragging */
    state->is_dragging = TRUE;
    /* Store adjustment references to ensure we use the same objects throughout the drag */
    state->hadj = hadj;
    state->vadj = vadj;
    /* Store start mouse position in viewport coordinates */
    state->start_x = (gdouble)event->x;
    state->start_y = (gdouble)event->y;
    state->last_x = state->start_x;
    state->last_y = state->start_y;
    /* Store start scroll position */
    state->start_hadj_value = gtk_adjustment_get_value(hadj);
    state->start_vadj_value = gtk_adjustment_get_value(vadj);

    /* Change cursor to closed hand */
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkCursor* cursor = gdk_cursor_new_from_name(display, "grabbing");
        if (!cursor) {
            /* Fallback to closedhand if grabbing not available */
            cursor = gdk_cursor_new_from_name(display, "closedhand");
        }
        if (!cursor) {
            /* Final fallback to fleur cursor */
            cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
        }
        if (cursor) {
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }
}

/**
 * Hand tool: mouse move - update pan position
 */
static void hand_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    HandToolState* state;
    GtkAdjustment* hadj = NULL;
    GtkAdjustment* vadj = NULL;
    gdouble new_hadj_value, new_vadj_value;

    if (!tool || !doc || !tool->user_data || !doc->scrolled_window) {
        return;
    }

    state = (HandToolState*)tool->user_data;

    if (!state->is_dragging) {
        return;
    }

    /* Use cached adjustment references from mouse_down */
    hadj = state->hadj;
    vadj = state->vadj;

    /* Verify adjustments are still valid */
    if (!hadj || !vadj || !GTK_IS_ADJUSTMENT(hadj) || !GTK_IS_ADJUSTMENT(vadj)) {
        /* If adjustments are invalid, try to get fresh ones */
        if (GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
            hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
            vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
            state->hadj = hadj;
            state->vadj = vadj;
        }
        if (!hadj || !vadj) {
            return;
        }
    }

    /* Get current mouse position in viewport coordinates */
    gdouble current_x = (gdouble)event->x;
    gdouble current_y = (gdouble)event->y;

    /* Calculate delta from last mouse position (incremental approach) */
    /* This prevents jumps if something modifies scroll position between calls */
    gdouble incremental_delta_x = state->last_x - current_x;
    gdouble incremental_delta_y = state->last_y - current_y;

    /* Get current scroll position */
    gdouble current_hadj_value = gtk_adjustment_get_value(hadj);
    gdouble current_vadj_value = gtk_adjustment_get_value(vadj);

    /* Calculate new scroll position: current position + incremental mouse delta */
    new_hadj_value = current_hadj_value + incremental_delta_x;
    new_vadj_value = current_vadj_value + incremental_delta_y;

    /* Clamp to adjustment bounds */
    gdouble hadj_lower = gtk_adjustment_get_lower(hadj);
    gdouble hadj_upper = gtk_adjustment_get_upper(hadj);
    gdouble hadj_page_size = gtk_adjustment_get_page_size(hadj);
    gdouble vadj_lower = gtk_adjustment_get_lower(vadj);
    gdouble vadj_upper = gtk_adjustment_get_upper(vadj);
    gdouble vadj_page_size = gtk_adjustment_get_page_size(vadj);

    if (new_hadj_value < hadj_lower) {
        new_hadj_value = hadj_lower;
    } else if (new_hadj_value > hadj_upper - hadj_page_size) {
        new_hadj_value = hadj_upper - hadj_page_size;
    }

    if (new_vadj_value < vadj_lower) {
        new_vadj_value = vadj_lower;
    } else if (new_vadj_value > vadj_upper - vadj_page_size) {
        new_vadj_value = vadj_upper - vadj_page_size;
    }

    /* Set new adjustment values */
    /* Use gtk_adjustment_set_value - it will automatically clamp to valid range */
    gtk_adjustment_set_value(hadj, new_hadj_value);
    gtk_adjustment_set_value(vadj, new_vadj_value);

    gdouble after_hadj = gtk_adjustment_get_value(hadj);
    gdouble after_vadj = gtk_adjustment_get_value(vadj);

    /* Update last mouse position based on actual scroll change, not mouse movement */
    /* This prevents jumps when scroll is clamped at boundaries */
    gdouble actual_delta_h = after_hadj - current_hadj_value;
    gdouble actual_delta_v = after_vadj - current_vadj_value;

    /* Adjust last mouse position to match the actual scroll change */
    /* If scroll was clamped, we need to adjust last_x/y so the next delta calculation is correct */
    /* The relationship is: mouse_delta = scroll_delta, so if scroll_delta != mouse_delta, adjust last_x */
    state->last_x = current_x + actual_delta_h;
    state->last_y = current_y + actual_delta_v;
}

/**
 * Hand tool: mouse up - end panning
 */
static void hand_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    HandToolState* state;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (HandToolState*)tool->user_data;

    if (!state->is_dragging) {
        return;
    }

    state->is_dragging = FALSE;

    /* Change cursor back to open hand */
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkCursor* cursor = gdk_cursor_new_from_name(display, "grab");
        if (!cursor) {
            /* Fallback to openhand if grab not available */
            cursor = gdk_cursor_new_from_name(display, "openhand");
        }
        if (!cursor) {
            /* Final fallback to hand2 cursor */
            cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
        }
        if (cursor) {
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }
}

/**
 * Create the Hand Tool
 */
Tool* tool_hand_create(void) {
    Tool* tool;
    GdkDisplay* display;
    GdkCursor* cursor;

    /* Hand tool doesn't have options */
    /* Use a default cursor type for tool_new, then replace with custom cursor */
    tool = tool_new("Hand", TOOL_HAND, GDK_HAND2, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    /* Replace cursor with open hand cursor */
    display = gdk_display_get_default();
    if (display) {
        if (tool->cursor) {
            g_object_unref(tool->cursor);
        }
        cursor = gdk_cursor_new_from_name(display, "grab");
        if (!cursor) {
            /* Fallback to openhand if grab not available */
            cursor = gdk_cursor_new_from_name(display, "openhand");
        }
        if (!cursor) {
            /* Final fallback to hand2 cursor */
            cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
        }
        tool->cursor = cursor;
    }

    tool->mouse_down = hand_tool_mouse_down;
    tool->mouse_move = hand_tool_mouse_move;
    tool->mouse_up = hand_tool_mouse_up;

    // printf("Hand tool created\n");

    return tool;
}
