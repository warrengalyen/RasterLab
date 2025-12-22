#ifndef TOOLS_H
#define TOOLS_H

#include <gtk/gtk.h>

/**
 * Forward declarations
 */
typedef struct _Tool Tool;
struct ImageDocument; /* Forward declaration - full definition in document.h */

/**
 * Tool enumeration for quick identification
 */
typedef enum {
    TOOL_HAND = 0,
    TOOL_ZOOM = 1,
    TOOL_MOVE = 2,
    TOOL_BRUSH = 3,
    TOOL_ERASER = 4,
    TOOL_PAINT_BUCKET = 5,
    TOOL_RECT_SELECT = 6,
    TOOL_COUNT = 7 /* Total number of tools */
} ToolType;

/**
 * Tool option flags - indicate which options a tool supports
 */
typedef enum {
    TOOL_OPT_NONE = 0,
    TOOL_OPT_SIZE = (1 << 0),             /* Tool supports size parameter */
    TOOL_OPT_OPACITY = (1 << 1),          /* Tool supports opacity parameter */
    TOOL_OPT_HARDNESS = (1 << 2),         /* Tool supports hardness parameter */
    TOOL_OPT_FLOW = (1 << 3),             /* Tool supports flow parameter */
    TOOL_OPT_SPACING = (1 << 4),          /* Tool supports spacing parameter */
    TOOL_OPT_SELECTION_MODE = (1 << 5),   /* Tool supports selection combine modes */
    TOOL_OPT_SELECTION_SMOOTH = (1 << 6), /* Tool supports selection smoothing modes */
} ToolOptionFlags;

/**
 * Mouse event data structure
 */
typedef struct {
    gint x;       /* X coordinate in image space */
    gint y;       /* Y coordinate in image space */
    guint button; /* Mouse button (1, 2, 3) */
    guint state;  /* Modifier keys (shift, ctrl, etc) */
} MouseEvent;

/* Forward declare ImageDocument */
struct ImageDocument;

/**
 * Tool event handler signatures
 */
typedef void (*ToolMouseDownHandler)(Tool* tool, struct ImageDocument* doc, MouseEvent* event);
typedef void (*ToolMouseMoveHandler)(Tool* tool, struct ImageDocument* doc, MouseEvent* event);
typedef void (*ToolMouseUpHandler)(Tool* tool, struct ImageDocument* doc, MouseEvent* event);

/**
 * Tool structure
 */
typedef struct _Tool {
    gchar* name;                     /* Tool name */
    ToolType type;                   /* Tool type identifier */
    GdkCursor* cursor;               /* Cursor for this tool */
    ToolMouseDownHandler mouse_down; /* Mouse down handler */
    ToolMouseMoveHandler mouse_move; /* Mouse move handler */
    ToolMouseUpHandler mouse_up;     /* Mouse up handler */
    gpointer user_data;              /* Tool-specific data */
    gpointer app_context;            /* App context for UI updates */
    ToolOptionFlags options;         /* Which options this tool supports */
} Tool;

/**
 * Tool registry (collection of tools)
 */
typedef struct {
    Tool* tools[TOOL_COUNT];           /* Array of tool pointers */
    Tool* active_tool;                 /* Currently active tool */
    struct ImageDocument* current_doc; /* Current document being edited */
} ToolRegistry;

/**
 * Create a new tool
 * @param name The tool name
 * @param type The tool type
 * @param cursor_type The cursor type for this tool
 * @param options The tool option flags (which options this tool supports)
 * @return Newly created Tool, or NULL on failure
 */
Tool* tool_new(const gchar* name, ToolType type, GdkCursorType cursor_type, ToolOptionFlags options);

/**
 * Free a tool and its resources
 * @param tool The tool to free
 */
void tool_free(Tool* tool);

/**
 * Create a custom brush cursor based on brush size
 * @param brush_size The brush size in pixels
 * @return Newly created GdkCursor, or NULL on failure. Use GDK_CROSSHAIR for sizes < 7
 */
GdkCursor* tool_create_brush_cursor(gfloat brush_size);

/**
 * Update a tool's cursor (for brush/eraser tools that need dynamic cursors)
 * @param tool The tool to update
 * @param brush_size The current brush size in pixels
 */
void tool_update_cursor(Tool* tool, gfloat brush_size);

/**
 * Create a new tool registry
 * @return Newly created ToolRegistry
 */
ToolRegistry* tool_registry_new(void);

/**
 * Register a tool in the registry
 * @param registry The tool registry
 * @param tool The tool to register
 * @param type The tool type
 * @return TRUE on success, FALSE on failure
 */
gboolean tool_registry_add(ToolRegistry* registry, Tool* tool, ToolType type);

/**
 * Get a tool by type
 * @param registry The tool registry
 * @param type The tool type
 * @return The tool, or NULL if not found
 */
Tool* tool_registry_get(ToolRegistry* registry, ToolType type);

/**
 * Activate a tool by type
 * @param registry The tool registry
 * @param type The tool type to activate
 * @return TRUE on success, FALSE on failure
 */
gboolean tool_registry_activate(ToolRegistry* registry, ToolType type);

/**
 * Get the currently active tool
 * @param registry The tool registry
 * @return The active tool, or NULL if none
 */
Tool* tool_registry_get_active(ToolRegistry* registry);

/**
 * Free the tool registry and all tools
 * @param registry The tool registry to free
 */
void tool_registry_free(ToolRegistry* registry);

/**
 * Initialize default tools in the registry
 * @param registry The tool registry
 * @return TRUE on success, FALSE on failure
 */
gboolean tool_registry_init_defaults(ToolRegistry* registry);

#endif /* TOOLS_H */
