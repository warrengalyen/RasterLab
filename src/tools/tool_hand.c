/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "tools/tool_hand.h"
#include "document.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <math.h>
#include "debug_logger.h"

/**
 * Create a cursor from resource
 */
static GdkCursor* cursor_from_resource(const char* resource_path, GdkCursorType fallback) {
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

    bytes = g_resources_lookup_data(resource_path,
                                    G_RESOURCE_LOOKUP_FLAGS_NONE,
                                    &error);
    if (!bytes) {
        if (error) {
            debug_log("WRN", "Failed to load cursor resource '%s': %s", resource_path, error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, fallback);
    }

    stream = g_memory_input_stream_new_from_bytes(bytes);
    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    g_bytes_unref(bytes);

    if (!pixbuf) {
        if (error) {
            debug_log("WRN", "Failed to parse cursor '%s': %s", resource_path, error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, fallback);
    }

    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);

    cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, fallback);
    }

    return cursor;
}

static GdkCursor* create_hand_up_cursor(void) {
    return cursor_from_resource("/cursors/hand_up_cursor.cur", GDK_HAND2);
}

static GdkCursor* create_hand_down_cursor(void) {
    /* When dragging, keep the prior behavior's fallback to something "move/pan"-ish */
    return cursor_from_resource("/cursors/hand_down_cursor.cur", GDK_FLEUR);
}

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
        GdkCursor* cursor = create_hand_down_cursor();
        gdk_window_set_cursor(window, cursor);
        if (cursor) {
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

    /* Get current mouse position (NOTE: event coordinates are in IMAGE space,
     * already divided by zoom factor via widget_to_image_coords) */
    gdouble current_x = (gdouble)event->x;
    gdouble current_y = (gdouble)event->y;

    /* Calculate delta from last mouse position (incremental approach) */
    /* This prevents jumps if something modifies scroll position between calls */
    gdouble image_delta_x = state->last_x - current_x;
    gdouble image_delta_y = state->last_y - current_y;

    /* Convert image-space delta to screen-space delta for scrolling.
     * The scroll adjustments work in screen pixels, but our coordinates
     * are in image pixels. Multiply by zoom to get screen pixels. */
    gdouble incremental_delta_x = image_delta_x * doc->zoom_factor;
    gdouble incremental_delta_y = image_delta_y * doc->zoom_factor;

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
    /* Convert screen-space scroll delta back to image-space for our coordinates */
    gdouble image_space_actual_h = actual_delta_h / doc->zoom_factor;
    gdouble image_space_actual_v = actual_delta_v / doc->zoom_factor;
    state->last_x = current_x + image_space_actual_h;
    state->last_y = current_y + image_space_actual_v;
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
        /* tool->cursor is our default "hand up" cursor */
        gdk_window_set_cursor(window, tool->cursor);
    }
}

/**
 * Create the Hand Tool
 */
Tool* tool_hand_create(void) {
    Tool* tool;

    /* Hand tool doesn't have options */
    /* Use a default cursor type for tool_new, then replace with custom cursor */
    tool = tool_new("Hand", TOOL_HAND, GDK_HAND2, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    /* Replace cursor with custom "hand up" cursor */
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }
    tool->cursor = create_hand_up_cursor();

    tool->mouse_down = hand_tool_mouse_down;
    tool->mouse_move = hand_tool_mouse_move;
    tool->mouse_up = hand_tool_mouse_up;

    // printf("Hand tool created\n");

    return tool;
}
