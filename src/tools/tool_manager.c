#include "tool_manager.h"
#include "tool_options.h"
#include "tools/tool_brush.h"
#include "tools/tool_eraser.h"
#include "tools/tool_fill.h"
#include "tools/tool_hand.h"
#include "tools/tool_move.h"
#include "tools/tool_zoom.h"

#include <stdio.h>
#include <stdlib.h>

/**
 * Create a new tool manager instance
 */
ToolRegistry* tool_manager_new(void) {
    ToolRegistry* registry = (ToolRegistry*)g_malloc(sizeof(ToolRegistry));

    for (int i = 0; i < TOOL_COUNT; i++) {
        registry->tools[i] = NULL;
    }

    registry->active_tool = NULL;
    registry->current_doc = NULL;

    return registry;
}

/**
 * Initialize default tools
 */
gboolean tool_manager_init_defaults(ToolRegistry* registry) {
    Tool* tool;

    if (!registry) {
        return FALSE;
    }

    /* Create Hand tool */
    tool = tool_hand_create();
    if (!tool) {
        return FALSE;
    }
    tool_manager_register(registry, tool, TOOL_HAND);

    /* Create Zoom tool */
    tool = tool_zoom_create();
    if (!tool) {
        return FALSE;
    }
    tool_manager_register(registry, tool, TOOL_ZOOM);

    /* Create Move tool */
    tool = tool_move_create();
    if (!tool) {
        return FALSE;
    }
    tool_manager_register(registry, tool, TOOL_MOVE);

    /* Create Brush tool */
    tool = tool_brush_create();
    if (!tool) {
        return FALSE;
    }
    tool_manager_register(registry, tool, TOOL_BRUSH);

    /* Create Eraser tool */
    tool = tool_eraser_create();
    if (!tool) {
        return FALSE;
    }
    tool_manager_register(registry, tool, TOOL_ERASER);

    /* Create Fill tool */
    tool = tool_fill_create();
    if (!tool) {
        return FALSE;
    }
    tool_manager_register(registry, tool, TOOL_PAINT_BUCKET);

    /* Activate Hand tool by default */
    tool_manager_activate(registry, TOOL_HAND);

    // printf("Tool manager initialized with %d default tools\n", TOOL_COUNT);

    return TRUE;
}

/**
 * Register a tool with the manager
 */
gboolean tool_manager_register(ToolRegistry* registry, Tool* tool, ToolType type) {
    if (!registry || !tool || type < 0 || type >= TOOL_COUNT) {
        return FALSE;
    }

    registry->tools[type] = tool;
    // printf("Tool registered: %s (type=%d)\n", tool->name, type);

    return TRUE;
}

/**
 * Activate a tool by type
 */
gboolean tool_manager_activate(ToolRegistry* registry, ToolType type) {
    Tool* tool;

    if (!registry || type < 0 || type >= TOOL_COUNT) {
        return FALSE;
    }

    tool = registry->tools[type];
    if (!tool) {
        return FALSE;
    }

    registry->active_tool = tool;

    /* Update cursor for brush/eraser tools based on current size */
    if (tool->type == TOOL_BRUSH || tool->type == TOOL_ERASER) {
        ToolOptions* opts = tool_options_get_for_tool(tool->type);
        if (opts) {
            tool_update_cursor(tool, opts->size);
        }
    }

    // printf("Tool activated: %s\n", tool->name);

    return TRUE;
}

/**
 * Get the currently active tool
 */
Tool* tool_manager_get_active(ToolRegistry* registry) {
    if (!registry) {
        return NULL;
    }

    return registry->active_tool;
}

/**
 * Get a tool by type
 */
Tool* tool_manager_get(ToolRegistry* registry, ToolType type) {
    if (!registry || type < 0 || type >= TOOL_COUNT) {
        return NULL;
    }

    return registry->tools[type];
}

/**
 * Free the tool manager and all tools
 */
void tool_manager_free(ToolRegistry* registry) {
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
