#include "tools/tool_zoom.h"
#include "document.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <stdio.h>
#include "debug_logger.h"

/* Forward declaration */
typedef struct AppContext AppContext;
extern void ui_update_status_bar(AppContext* ctx, ImageDocument* doc);

/**
 * Zoom tool: mouse down - zoom in on left click, zoom out on right click
 */
static void zoom_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    AppContext* ctx;

    if (!doc || !tool) {
        return;
    }

    /* Primary button (left click) = zoom in */
    if (event->button == 1) {
        document_zoom_in(doc);
    }
    /* Secondary button (right click) = zoom out */
    else if (event->button == 3) {
        document_zoom_out(doc);
    }

    /* Update statusbar after zoom change */
    ctx = (AppContext*)tool->app_context;
    if (ctx) {
        ui_update_status_bar(ctx, doc);
    }
}

/**
 * Create a cursor from resource
 */
static GdkCursor* create_zoom_cursor(void) {
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

    /* Load cursor file from resource as bytes */
    bytes = g_resources_lookup_data("/cursors/zoom_cursor.cur",
                                    G_RESOURCE_LOOKUP_FLAGS_NONE,
                                    &error);
    if (!bytes) {
        if (error) {
            debug_log("WRN", "Failed to load zoom cursor resource: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_ARROW);
    }

    /* Create input stream from bytes */
    stream = g_memory_input_stream_new_from_bytes(bytes);

    /* Load pixbuf from stream */
    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

    g_object_unref(stream);
    g_bytes_unref(bytes);

    if (!pixbuf) {
        if (error) {
            debug_log("WRN", "Failed to parse zoom cursor: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_ARROW);
    }

    /* Get pixbuf dimensions for hotspot calculation */
    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);

    /* Create cursor from pixbuf with hotspot at center */
    cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, GDK_ARROW);
    }

    return cursor;
}

/**
 * Create the Zoom Tool
 */
Tool* tool_zoom_create(void) {
    Tool* tool;

    /* Zoom tool doesn't have options */
    tool = tool_new("Zoom", TOOL_ZOOM, GDK_ARROW, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    /* Replace cursor with custom zoom cursor */
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }
    tool->cursor = create_zoom_cursor();

    tool->mouse_down = zoom_tool_mouse_down;
    tool->mouse_move = NULL; /* Zoom tool doesn't need mouse move */
    tool->mouse_up = NULL;   /* Zoom tool doesn't need mouse up */

    return tool;
}
