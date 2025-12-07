#include "tools.h"
#include "document.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * Stub handlers for Move tool
 */
static void move_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Move tool: mouse down at (%d, %d)\n", event->x, event->y);
}

static void move_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Move tool: mouse move at (%d, %d)\n", event->x, event->y);
}

static void move_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Move tool: mouse up at (%d, %d)\n", event->x, event->y);
}

/**
 * Stub handlers for Brush tool
 */
static void brush_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Brush tool: mouse down at (%d, %d)\n", event->x, event->y);
}

static void brush_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Brush tool: mouse move at (%d, %d)\n", event->x, event->y);
}

static void brush_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Brush tool: mouse up at (%d, %d)\n", event->x, event->y);
}

/**
 * Stub handlers for Eraser tool
 */
static void eraser_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Eraser tool: mouse down at (%d, %d)\n", event->x, event->y);
}

static void eraser_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Eraser tool: mouse move at (%d, %d)\n", event->x, event->y);
}

static void eraser_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Eraser tool: mouse up at (%d, %d)\n", event->x, event->y);
}

/**
 * Stub handlers for Fill tool
 */
static void fill_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Fill tool: mouse down at (%d, %d)\n", event->x, event->y);
}

static void fill_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Fill tool: mouse move at (%d, %d)\n", event->x, event->y);
}

static void fill_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;
    (void)doc;
    printf("Fill tool: mouse up at (%d, %d)\n", event->x, event->y);
}

/**
 * Create a new tool
 */
Tool* tool_new(const gchar *name, ToolType type, GdkCursorType cursor_type)
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

/**
 * Create a new tool registry
 */
ToolRegistry* tool_registry_new(void)
{
    ToolRegistry *registry = (ToolRegistry *)g_malloc(sizeof(ToolRegistry));
    
    for (int i = 0; i < TOOL_COUNT; i++) {
        registry->tools[i] = NULL;
    }

    registry->active_tool = NULL;
    registry->current_doc = NULL;

    return registry;
}

/**
 * Register a tool in the registry
 */
gboolean tool_registry_add(ToolRegistry *registry, Tool *tool, ToolType type)
{
    if (!registry || !tool || type < 0 || type >= TOOL_COUNT) {
        return FALSE;
    }

    registry->tools[type] = tool;
    printf("Tool registered: %s (type=%d)\n", tool->name, type);

    return TRUE;
}

/**
 * Get a tool by type
 */
Tool* tool_registry_get(ToolRegistry *registry, ToolType type)
{
    if (!registry || type < 0 || type >= TOOL_COUNT) {
        return NULL;
    }

    return registry->tools[type];
}

/**
 * Activate a tool by type
 */
gboolean tool_registry_activate(ToolRegistry *registry, ToolType type)
{
    Tool *tool;

    if (!registry || type < 0 || type >= TOOL_COUNT) {
        return FALSE;
    }

    tool = registry->tools[type];
    if (!tool) {
        return FALSE;
    }

    registry->active_tool = tool;
    printf("Tool activated: %s\n", tool->name);

    return TRUE;
}

/**
 * Get the currently active tool
 */
Tool* tool_registry_get_active(ToolRegistry *registry)
{
    if (!registry) {
        return NULL;
    }

    return registry->active_tool;
}

/**
 * Free the tool registry and all tools
 */
void tool_registry_free(ToolRegistry *registry)
{
    if (!registry) {
        return;
    }

    for (int i = 0; i < TOOL_COUNT; i++) {
        if (registry->tools[i]) {
            tool_free(registry->tools[i]);
        }
    }

    g_free(registry);
}

/**
 * Initialize default tools in the registry
 */
gboolean tool_registry_init_defaults(ToolRegistry *registry)
{
    Tool *tool;

    if (!registry) {
        return FALSE;
    }

    /* Create Move tool */
    tool = tool_new("Move", TOOL_MOVE, GDK_FLEUR);
    if (!tool) {
        return FALSE;
    }
    tool->mouse_down = move_tool_mouse_down;
    tool->mouse_move = move_tool_mouse_move;
    tool->mouse_up = move_tool_mouse_up;
    tool_registry_add(registry, tool, TOOL_MOVE);

    /* Create Brush tool */
    tool = tool_new("Brush", TOOL_BRUSH, GDK_CROSSHAIR);
    if (!tool) {
        return FALSE;
    }
    tool->mouse_down = brush_tool_mouse_down;
    tool->mouse_move = brush_tool_mouse_move;
    tool->mouse_up = brush_tool_mouse_up;
    tool_registry_add(registry, tool, TOOL_BRUSH);

    /* Create Eraser tool */
    tool = tool_new("Eraser", TOOL_ERASER, GDK_CROSSHAIR);
    if (!tool) {
        return FALSE;
    }
    tool->mouse_down = eraser_tool_mouse_down;
    tool->mouse_move = eraser_tool_mouse_move;
    tool->mouse_up = eraser_tool_mouse_up;
    tool_registry_add(registry, tool, TOOL_ERASER);

    /* Create Fill tool */
    tool = tool_new("Fill", TOOL_FILL, GDK_CROSSHAIR);
    if (!tool) {
        return FALSE;
    }
    tool->mouse_down = fill_tool_mouse_down;
    tool->mouse_move = fill_tool_mouse_move;
    tool->mouse_up = fill_tool_mouse_up;
    tool_registry_add(registry, tool, TOOL_FILL);

    /* Activate Move tool by default */
    tool_registry_activate(registry, TOOL_MOVE);

    printf("Tool registry initialized with %d default tools\n", TOOL_COUNT);

    return TRUE;
}

