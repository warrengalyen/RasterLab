#include "tools.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * Create a new tool
 */
Tool* tool_new(const gchar *name, ToolType type, GdkCursorType cursor_type, ToolOptionFlags options)
{
    Tool *tool;
    GdkDisplay *display;

    if (!name) {
        return NULL;
    }

    tool = (Tool *)g_malloc(sizeof(Tool));
    tool->name = g_strdup(name);
    tool->type = type;
    tool->user_data = NULL;
    tool->app_context = NULL;
    tool->options = options;

    /* Create cursor */
    display = gdk_display_get_default();
    if (display) {
        tool->cursor = gdk_cursor_new_for_display(display, cursor_type);
        if (!tool->cursor) {
            tool->cursor = gdk_cursor_new_for_display(display, GDK_ARROW);
        }
    } else {
        tool->cursor = NULL;
    }

    /* Set default handlers to NULL (will be assigned per tool) */
    tool->mouse_down = NULL;
    tool->mouse_move = NULL;
    tool->mouse_up = NULL;

    return tool;
}

/**
 * Free a tool
 */
void tool_free(Tool *tool)
{
    if (!tool) {
        return;
    }

    g_free(tool->name);
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }

    g_free(tool);
}

