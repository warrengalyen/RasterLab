#include "tools.h"
#include "document.h"
#include "command.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Forward declarations */
typedef struct AppContext AppContext;
typedef struct ImageDocument ImageDocument;
typedef struct ImageLayer ImageLayer;
extern void ui_update_menu_and_button_states(AppContext *ctx);
extern void ui_update_window_title(AppContext *ctx);

/**
 * Move Tool state
 */
typedef struct {
    gboolean is_dragging;
    gint start_x;
    gint start_y;
    gint initial_offset_x;
    gint initial_offset_y;
    ImageLayer *active_layer;
} MoveToolState;

/**
 * Move tool: mouse down - start dragging
 */
static void move_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    MoveToolState *state;
    ImageLayer *active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(MoveToolState));
    }
    state = (MoveToolState *)tool->user_data;

    /* Get the top (active) layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer) {
        printf("Move tool: no active layer\n");
        return;
    }

    /* Start dragging */
    state->is_dragging = TRUE;
    state->start_x = event->x;
    state->start_y = event->y;
    state->initial_offset_x = active_layer->offset_x;
    state->initial_offset_y = active_layer->offset_y;
    state->active_layer = active_layer;

    printf("Move tool: started dragging layer at (%d, %d)\n", event->x, event->y);
}

/**
 * Move tool: mouse move - update layer offset
 */
static void move_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    MoveToolState *state;
    gint dx, dy;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState *)tool->user_data;

    if (!state->is_dragging || !state->active_layer) {
        return;
    }

    /* Calculate delta from start position */
    dx = event->x - state->start_x;
    dy = event->y - state->start_y;

    /* Update layer offset */
    state->active_layer->offset_x = state->initial_offset_x + dx;
    state->active_layer->offset_y = state->initial_offset_y + dy;

    /* Mark composite for redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    printf("Move tool: moved to offset (%d, %d)\n", 
           state->active_layer->offset_x, state->active_layer->offset_y);
}

/**
 * Move tool: mouse up - end dragging and create undo command
 */
static void move_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    MoveToolState *state;
    Command *cmd;
    AppContext *ctx;

    (void)event;  /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState *)tool->user_data;

    if (!state->is_dragging) {
        return;
    }

    /* Check if we actually moved */
    if (state->active_layer && 
        (state->active_layer->offset_x != state->initial_offset_x ||
         state->active_layer->offset_y != state->initial_offset_y)) {
        
        /* Create undo command for the move */
        cmd = command_create_move(
            state->active_layer,
            state->initial_offset_x,
            state->initial_offset_y,
            state->active_layer->offset_x,
            state->active_layer->offset_y);

        if (cmd && doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);
            printf("Move tool: move command pushed to undo stack\n");

            /* Clear redo stack since new action performed */
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }

            /* Update UI */
            ctx = (AppContext *)tool->app_context;
            if (ctx) {
                ui_update_menu_and_button_states(ctx);
                ui_update_window_title(ctx);
            }
        }

        /* Layer was moved - mark document as modified */
        doc->modified = TRUE;
        printf("Move tool: layer moved - document marked as modified\n");
    }

    state->is_dragging = FALSE;
    state->active_layer = NULL;

    printf("Move tool: finished dragging\n");
}

/**
 * Brush tool helpers - minimal drawing implementation for undo demo
 */
typedef struct {
    gint last_x;
    gint last_y;
    gboolean is_drawing;
    Command *current_command;
} BrushToolState;

static void brush_draw_line(cairo_surface_t *surface, int x1, int y1, int x2, int y2)
{
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);  /* Black opaque */
    cairo_set_line_width(cr, 3.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
    cairo_destroy(cr);
}

/**
 * Brush tool: mouse down - create snapshot for undo
 */
static void brush_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    BrushToolState *state;
    ImageLayer *active_layer;
    Command *cmd;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(BrushToolState));
    }
    state = (BrushToolState *)tool->user_data;

    /* Get the top (active) layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer) {
        printf("Brush tool: no active layer\n");
        return;
    }

    /* Create undo snapshot */
    cmd = command_create_draw(active_layer);
    if (!cmd) {
        printf("Brush tool: failed to create draw command\n");
        return;
    }

    state->is_drawing = TRUE;
    state->current_command = cmd;
    state->last_x = event->x;
    state->last_y = event->y;

    printf("Brush tool: started drawing at (%d, %d)\n", event->x, event->y);
}

/**
 * Brush tool: mouse move - draw line
 */
static void brush_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    BrushToolState *state;
    ImageLayer *active_layer;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (BrushToolState *)tool->user_data;

    if (!state->is_drawing) {
        return;
    }

    /* Get the top (active) layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer) {
        printf("Brush tool: active layer is NULL, aborting draw\n");
        state->is_drawing = FALSE;
        return;
    }

    if (!active_layer->surface) {
        printf("Brush tool: active layer surface is NULL, aborting draw\n");
        state->is_drawing = FALSE;
        return;
    }

    /* Draw line from last position to current */
    brush_draw_line(active_layer->surface, state->last_x, state->last_y, event->x, event->y);

    state->last_x = event->x;
    state->last_y = event->y;

    /* Mark composite for redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    printf("Brush tool: drawing line to (%d, %d)\n", event->x, event->y);
}

/**
 * Brush tool: mouse up - push command to undo stack
 */
static void brush_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    BrushToolState *state;
    AppContext *ctx;

    (void)event;  /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (BrushToolState *)tool->user_data;

    if (!state->is_drawing || !state->current_command) {
        return;
    }

    state->is_drawing = FALSE;

    /* Push command to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);
        printf("Brush tool: stroke added to undo stack\n");

        /* Clear redo stack since new action performed */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }

        /* Update UI menu states if we have access to AppContext */
        ctx = (AppContext *)tool->app_context;
        if (ctx) {
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx);  /* Update title to show dirty indicator */
        }
        
        /* Mark document as modified */
        doc->modified = TRUE;
    }

    state->current_command = NULL;

    printf("Brush tool: finished drawing\n");
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
    tool->app_context = NULL;

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

