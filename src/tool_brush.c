#include "tool_brush.h"
#include "command.h"
#include "document.h"
#include "tool_options.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext *ctx);
extern void ui_update_window_title(AppContext *ctx);

/**
 * Brush Tool state
 */
typedef struct {
    gboolean is_drawing;          /* Currently drawing? */
    gint last_x;                  /* Last mouse X position */
    gint last_y;                  /* Last mouse Y position */
    struct ImageLayer *active_layer; /* Layer being drawn on */
    Command *current_command;     /* Current draw command for undo */
} BrushToolState;

/**
 * Draw a single pixel on the layer surface
 */
static void brush_draw_pixel(cairo_surface_t *surface, gint x, gint y)
{
    cairo_t *cr;

    if (!surface || x < 0 || y < 0) {
        return;
    }

    cr = cairo_create(surface);

    /* Set brush color (black) */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    /* Set brush size and shape */
    cairo_set_line_width(cr, 3.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    /* Draw a small circle at this point */
    cairo_arc(cr, x, y, 1.5, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_destroy(cr);
}

/**
 * Draw a line from (x1, y1) to (x2, y2) on the layer surface
 * Uses tool options for size and opacity
 */
static void brush_draw_line(cairo_surface_t *surface, 
                            gint x1, gint y1, gint x2, gint y2)
{
    cairo_t *cr;
    ToolOptions *opts;
    gfloat brush_size;
    gfloat brush_opacity;

    if (!surface) {
        return;
    }

    /* Get tool options */
    opts = tool_options_get_global();

    cr = cairo_create(surface);

    /* Set brush color (black) with opacity from tool options */
    brush_opacity = opts ? opts->opacity : 1.0f;
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, brush_opacity);

    /* Set brush size from tool options */
    brush_size = opts ? opts->size : 5.0f;
    cairo_set_line_width(cr, brush_size);
    
    /* Set brush shape (affected by hardness)
       Hardness 0 = soft round cap
       Hardness 1 = hard square cap */
    if (opts && opts->hardness < 0.5f) {
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    } else {
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
    }
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    /* Draw line with smooth interpolation */
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);

    cairo_destroy(cr);

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Brush tool: mouse down - start drawing
 */
static void brush_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    BrushToolState *state;
    struct ImageLayer *active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(BrushToolState));
    }
    state = (BrushToolState *)tool->user_data;

    /* Get the active layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer || !active_layer->surface) {
        printf("Brush tool: no active layer with surface\n");
        return;
    }

    /* Start drawing */
    state->is_drawing = TRUE;
    state->last_x = event->x;
    state->last_y = event->y;
    state->active_layer = active_layer;

    /* Create a draw command for undo/redo */
    state->current_command = command_create_draw(active_layer);
    if (state->current_command && doc->undo_stack) {
        printf("Brush tool: draw command created\n");
    }

    printf("Brush tool: started drawing at (%d, %d)\n", event->x, event->y);
}

/**
 * Brush tool: mouse move - draw strokes
 */
static void brush_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    BrushToolState *state;
    struct ImageLayer *active_layer;
    gint layer_x1, layer_y1, layer_x2, layer_y2;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (BrushToolState *)tool->user_data;

    if (!state->is_drawing || !state->active_layer) {
        return;
    }

    /* Get the active layer */
    active_layer = document_get_active_layer(doc);
    if (!active_layer || !active_layer->surface) {
        printf("Brush tool: active layer deleted during drawing\n");
        state->is_drawing = FALSE;
        return;
    }

    /* Draw line from last position to current, 
       adjusted for layer offset */
    /* Convert image coordinates to layer-relative coordinates 
       by subtracting layer offset */
    layer_x1 = state->last_x - active_layer->offset_x;
    layer_y1 = state->last_y - active_layer->offset_y;
    layer_x2 = event->x - active_layer->offset_x;
    layer_y2 = event->y - active_layer->offset_y;

    brush_draw_line(active_layer->surface, layer_x1, layer_y1, layer_x2, layer_y2);

    /* Store the image-space coordinates (not layer-relative) 
       for next iteration */
    state->last_x = event->x;
    state->last_y = event->y;

    /* Mark composite for redraw */
    doc->composite_dirty = TRUE;
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    printf("Brush tool: drawing line from (%d, %d) to (%d, %d)\n",
           layer_x1, layer_y1, layer_x2, layer_y2);
}

/**
 * Brush tool: mouse up - end drawing
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

    if (!state->is_drawing) {
        return;
    }

    /* Push draw command to undo stack */
    if (state->current_command && doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);
        printf("Brush tool: draw command pushed to undo stack\n");

        /* Clear redo stack */
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

    /* Mark document as modified */
    doc->modified = TRUE;

    state->is_drawing = FALSE;
    state->current_command = NULL;
    state->active_layer = NULL;

    printf("Brush tool: finished drawing\n");
}

/**
 * Create the Brush Tool
 */
Tool* tool_brush_create(void)
{
    Tool *tool;

    /* Brush tool supports size, opacity, and hardness */
    tool = tool_new("Brush", TOOL_BRUSH, GDK_CROSSHAIR,
                    TOOL_OPT_SIZE | TOOL_OPT_OPACITY | TOOL_OPT_HARDNESS);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = brush_tool_mouse_down;
    tool->mouse_move = brush_tool_mouse_move;
    tool->mouse_up = brush_tool_mouse_up;

    printf("Brush tool created\n");

    return tool;
}

