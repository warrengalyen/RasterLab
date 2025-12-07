#include "tool_fill.h"
#include "command.h"
#include "document.h"
#include "panels.h"
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext *ctx);
extern void ui_update_window_title(AppContext *ctx);

/**
 * Fill Tool state
 */
typedef struct {
    struct ImageLayer *active_layer; /* Layer being filled */
    Command *current_command;        /* Current fill command for undo */
} FillToolState;

/**
 * Simple flood fill implementation
 * Note: This is a basic implementation. A more advanced version could use
 * a queue-based flood fill for better performance and memory usage.
 */
static void fill_flood_fill(cairo_surface_t *surface, gint x, gint y)
{
    cairo_t *cr;
    GdkRGBA fg_color;

    if (!surface) {
        return;
    }

    cr = cairo_create(surface);

    /* Get foreground color, default to black if not available */
    if (!tools_panel_get_foreground_color(&fg_color)) {
        fg_color.red = 0.0;
        fg_color.green = 0.0;
        fg_color.blue = 0.0;
        fg_color.alpha = 1.0;
    }

    /* Set fill color to foreground color */
    cairo_set_source_rgba(cr, fg_color.red, fg_color.green, fg_color.blue, fg_color.alpha);

    /* Fill a circle at the clicked point as a simple flood fill approximation */
    cairo_arc(cr, x, y, 10.0, 0, 2 * 3.14159);
    cairo_fill(cr);

    cairo_destroy(cr);

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Fill tool: mouse down - perform fill
 */
static void fill_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    FillToolState *state;
    struct ImageLayer *active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(FillToolState));
    }
    state = (FillToolState *)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        printf("Fill tool: no selected layer with surface\n");
        return;
    }

    /* Create a draw command for undo/redo */
    state->current_command = command_create_draw(active_layer);

    /* Perform the fill at the clicked position,
       adjusted for layer offset */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;

    fill_flood_fill(active_layer->surface, layer_x, layer_y);

    /* Push fill command to undo stack */
    if (state->current_command && doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);
        printf("Fill tool: fill command pushed to undo stack\n");

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }

        /* Update UI */
        AppContext *ctx = (AppContext *)tool->app_context;
        if (ctx) {
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx);
        }
    }

    /* Mark document as modified */
    doc->modified = TRUE;

    /* Mark composite for redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    printf("Fill tool: filled at (%d, %d)\n", layer_x, layer_y);
}

/**
 * Fill tool: mouse move - no-op (fill is instant on click)
 */
static void fill_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;     /* Unused */
    (void)doc;      /* Unused */
    (void)event;    /* Unused */
    /* Fill tool doesn't do anything on mouse move */
}

/**
 * Fill tool: mouse up - no-op
 */
static void fill_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;     /* Unused */
    (void)doc;      /* Unused */
    (void)event;    /* Unused */
    /* Fill tool doesn't do anything on mouse up */
}

/**
 * Create the Fill Tool
 */
Tool* tool_fill_create(void)
{
    Tool *tool;

    /* Fill tool doesn't have size/opacity/hardness options yet */
    tool = tool_new("Fill", TOOL_FILL, GDK_CROSSHAIR, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = fill_tool_mouse_down;
    tool->mouse_move = fill_tool_mouse_move;
    tool->mouse_up = fill_tool_mouse_up;

    printf("Fill tool created\n");

    return tool;
}

