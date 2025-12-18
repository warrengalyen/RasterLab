#include "tools/tool_brush.h"
#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "tool_options.h"
#include "ui/tools_panel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx);

/**
 * Brush Tool state
 */
typedef struct {
    gboolean is_drawing;              /* Currently drawing? */
    gint last_x;                      /* Last mouse X position */
    gint last_y;                      /* Last mouse Y position */
    struct ImageLayer* active_layer;  /* Layer being drawn on */
    Command* current_command;         /* Current draw command for undo */
    cairo_surface_t* before_snapshot; /* Layer state snapshot taken at mouse_down (before any drawing) */
} BrushToolState;

/**
 * Stamp brush at a specific point with gradient based on hardness
 * Flow parameter controls the strength of each stamp (0.0-1.0)
 */
static void brush_stamp_at(cairo_t* cr, gdouble x, gdouble y, gfloat size,
                           GdkRGBA* color, gfloat opacity, gfloat hardness,
                           gfloat flow) {
    cairo_pattern_t* pattern;
    gdouble radius = size / 2.0;

    /* Flow modulates the effective opacity of this stamp
     * Flow 0.0 = no effect (doesn't paint)
     * Flow 1.0 = full opacity effect
     * This allows for buildable, gradual painting */
    gfloat effective_opacity = opacity * flow * color->alpha;

    /* Create radial gradient based on hardness
     * Hardness 0.0 = very soft (gradual falloff)
     * Hardness 1.0 = very hard (sharp edge) */
    pattern = cairo_pattern_create_radial(x, y, 0.0, x, y, radius);

    /* Inner circle: full opacity (modulated by flow) */
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, color->red, color->green,
                                      color->blue, effective_opacity);

    /* Outer edge: opacity based on hardness
     * Higher hardness = sharper transition */
    gdouble inner_radius = hardness; /* 0.0 to 1.0 */

    if (hardness < 1.0f) {
        cairo_pattern_add_color_stop_rgba(pattern, inner_radius, color->red,
                                          color->green, color->blue,
                                          effective_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, 1.0, color->red, color->green,
                                          color->blue, 0.0);
    } else {
        /* Hardness = 1.0: sharp edge */
        cairo_pattern_add_color_stop_rgba(pattern, 0.99, color->red, color->green,
                                          color->blue, effective_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, 1.0, color->red, color->green,
                                          color->blue, 0.0);
    }

    cairo_set_source(cr, pattern);

    /* Use OVER operator for normal painting */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Draw circle */
    cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_pattern_destroy(pattern);
}

/**
 * Draw a line from (x1, y1) to (x2, y2) on the layer surface
 * Uses tool options for size, opacity, hardness, flow, spacing, and foreground
 * color Interpolates stamps along the line for smooth strokes
 */
static void brush_draw_line(cairo_surface_t* surface, gdouble x1, gdouble y1,
                            gdouble x2, gdouble y2) {
    cairo_t* cr;
    ToolOptions* opts;
    GdkRGBA fg_color;
    gfloat brush_size;
    gfloat brush_opacity;
    gfloat hardness;
    gfloat flow;
    gfloat spacing;

    if (!surface) {
        return;
    }

    /* Get tool options - note: tool parameter not available here, use TOOL_BRUSH */
    opts = tool_options_get_for_tool(TOOL_BRUSH);
    brush_size = opts ? opts->size : 5.0f;
    brush_opacity = opts ? opts->opacity : 1.0f;
    hardness = opts ? opts->hardness : 0.5f;
    flow = opts ? opts->flow : 1.0f;
    spacing = opts ? opts->spacing : 0.25f; /* Default 25% spacing */

    /* Get foreground color, default to black if not available */
    if (!tools_panel_get_foreground_color(&fg_color)) {
        fg_color.red = 0.0;
        fg_color.green = 0.0;
        fg_color.blue = 0.0;
        fg_color.alpha = 1.0;
    }

    cr = cairo_create(surface);

    /* Calculate distance for interpolation */
    gdouble dx = x2 - x1;
    gdouble dy = y2 - y1;
    gdouble distance = sqrt(dx * dx + dy * dy);

    /* Calculate number of stamps needed based on spacing */
    gfloat stamp_spacing = brush_size * spacing;
    gint num_stamps = (gint)(distance / stamp_spacing) + 1;

    if (num_stamps < 2) {
        num_stamps = 2; /* At least draw start and end points */
    }

    /* Interpolate and stamp along the line */
    for (gint i = 0; i < num_stamps; i++) {
        gdouble t = (num_stamps > 1) ? (gdouble)i / (gdouble)(num_stamps - 1) : 0.0;
        gdouble x = x1 + t * dx;
        gdouble y = y1 + t * dy;

        brush_stamp_at(cr, x, y, brush_size, &fg_color, brush_opacity, hardness,
                       flow);
    }

    cairo_destroy(cr);

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Draw a single dot (for initial mouse down)
 */
static void brush_draw_dot(cairo_surface_t* surface, gint x, gint y) {
    ToolOptions* opts;
    GdkRGBA fg_color;
    gfloat brush_size;
    gfloat brush_opacity;
    gfloat hardness;
    gfloat flow;
    cairo_t* cr;

    if (!surface) {
        return;
    }

    /* Get tool options - note: tool parameter not available here, use TOOL_BRUSH */
    opts = tool_options_get_for_tool(TOOL_BRUSH);
    brush_size = opts ? opts->size : 5.0f;
    brush_opacity = opts ? opts->opacity : 1.0f;
    hardness = opts ? opts->hardness : 0.5f;
    flow = opts ? opts->flow : 1.0f;

    /* Get foreground color, default to black if not available */
    if (!tools_panel_get_foreground_color(&fg_color)) {
        fg_color.red = 0.0;
        fg_color.green = 0.0;
        fg_color.blue = 0.0;
        fg_color.alpha = 1.0;
    }

    cr = cairo_create(surface);
    brush_stamp_at(cr, (gdouble)x, (gdouble)y, brush_size, &fg_color,
                   brush_opacity, hardness, flow);

    cairo_destroy(cr);
    cairo_surface_mark_dirty(surface);
}

/**
 * Brush tool: mouse down - start drawing
 */
static void brush_tool_mouse_down(Tool* tool, struct ImageDocument* doc,
                                  MouseEvent* event) {
    BrushToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(BrushToolState));
    }
    state = (BrushToolState*)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        // printf("Brush tool: no selected layer with surface\n");
        return;
    }

    /* Start drawing */
    state->is_drawing = TRUE;
    state->last_x = event->x;
    state->last_y = event->y;
    state->active_layer = active_layer;

    /* CRITICAL: Create draw command BEFORE any drawing - captures the "before" state */
    state->current_command = command_create_draw(active_layer,
                                                 command_get_name_string(CMD_NAME_DRAW_BRUSH_STROKE));
    state->before_snapshot = NULL; /* Not used anymore */

    /* Draw initial dot at mouse down position */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;

    brush_draw_dot(active_layer->surface, layer_x, layer_y);

    /* CRITICAL: Flush Cairo surface to ensure drawing is written to pixel buffer
       Worker threads will read the raw pixels, so we must flush first */
    cairo_surface_flush(active_layer->surface);

    active_layer->cache_dirty = TRUE;

    /* Mark initial dot area as dirty */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat brush_size = opts ? opts->size : 5.0f;
    gint margin = (gint)(brush_size / 2.0f) + 3;

    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, event->x - margin, event->y - margin,
                   brush_size + 2 * margin, brush_size + 2 * margin);
    dirty_rect_clamp(&dirty_rect, doc->width, doc->height);

    if (!dirty_rect_is_empty(&dirty_rect)) {
        document_invalidate_region(doc, &dirty_rect);
    }

    // printf("Brush tool: started drawing at (%d, %d)\n", event->x, event->y);
}

/**
 * Brush tool: mouse move - draw strokes
 */
static void brush_tool_mouse_move(Tool* tool, struct ImageDocument* doc,
                                  MouseEvent* event) {
    BrushToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (BrushToolState*)tool->user_data;

    if (!state->is_drawing || !state->active_layer) {
        return;
    }

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        // printf("Brush tool: selected layer deleted during drawing\n");
        state->is_drawing = FALSE;
        return;
    }

    /* Draw line from last position to current,
        adjusted for layer offset */
    gdouble layer_x1 = (gdouble)(state->last_x - active_layer->offset_x);
    gdouble layer_y1 = (gdouble)(state->last_y - active_layer->offset_y);
    gdouble layer_x2 = (gdouble)(event->x - active_layer->offset_x);
    gdouble layer_y2 = (gdouble)(event->y - active_layer->offset_y);

    brush_draw_line(active_layer->surface, layer_x1, layer_y1, layer_x2,
                    layer_y2);

    /* CRITICAL: Flush Cairo surface to ensure drawing is written to pixel buffer
       Worker threads will read the raw pixels, so we must flush first */
    cairo_surface_flush(active_layer->surface);

    /* Mark layer cache as dirty but don't destroy it yet
        We'll regenerate it lazily only when needed for compositing
        This avoids expensive cache regeneration on every mouse move */
    active_layer->cache_dirty = TRUE;

    /* Calculate dirty rectangle BEFORE updating last_x/y
        Use the previous position and current position */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat brush_size = opts ? opts->size : 5.0f;
    gint margin =
        (gint)(brush_size / 2.0f) + 3; /* Add margin for anti-aliasing */

    /* Calculate bounding box of stroke in document coordinates
        Use state->last_x/y (previous position) and event->x/y (current position)
    */
    gint min_x = (state->last_x < event->x) ? state->last_x : event->x;
    gint max_x = (state->last_x > event->x) ? state->last_x : event->x;
    gint min_y = (state->last_y < event->y) ? state->last_y : event->y;
    gint max_y = (state->last_y > event->y) ? state->last_y : event->y;

    /* Store the image-space coordinates (not layer-relative)
        for next iteration - AFTER calculating dirty rect */
    state->last_x = event->x;
    state->last_y = event->y;

    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, min_x - margin, min_y - margin,
                   (max_x - min_x) + 2 * margin, (max_y - min_y) + 2 * margin);
    dirty_rect_clamp(&dirty_rect, doc->width, doc->height);

    /* Only invalidate the stroke region */
    if (!dirty_rect_is_empty(&dirty_rect)) {
        document_invalidate_region(doc, &dirty_rect);
    }

    // printf("Brush tool: drawing line from (%d, %d) to (%d, %d)\n",
    //        layer_x1, layer_y1, layer_x2, layer_y2);
}

/**
 * Brush tool: mouse up - end drawing
 */
static void brush_tool_mouse_up(Tool* tool, struct ImageDocument* doc,
                                MouseEvent* event) {
    BrushToolState* state;
    AppContext* ctx;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (BrushToolState*)tool->user_data;

    if (!state->is_drawing) {
        return;
    }

    /* Finalize draw command - captures the "after" state */
    if (state->current_command) {
        command_finalize_draw(state->current_command);
    }

    /* Push draw command to undo stack */
    if (state->current_command && doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }

        /* Update UI */
        ctx = (AppContext*)tool->app_context;
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

    // printf("Brush tool: finished drawing\n");
}

/**
 * Create the Brush Tool
 */
Tool* tool_brush_create(void) {
    Tool* tool;

    /* Brush tool supports size, opacity, hardness, flow, and spacing */
    tool = tool_new("Brush", TOOL_BRUSH, GDK_CROSSHAIR,
                    TOOL_OPT_SIZE | TOOL_OPT_OPACITY | TOOL_OPT_HARDNESS |
                        TOOL_OPT_FLOW | TOOL_OPT_SPACING);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = brush_tool_mouse_down;
    tool->mouse_move = brush_tool_mouse_move;
    tool->mouse_up = brush_tool_mouse_up;

    // printf("Brush tool created\n");

    return tool;
}
