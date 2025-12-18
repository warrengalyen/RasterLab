#include "tools/tool_eraser.h"
#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "tool_options.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx);

/**
 * Eraser Tool state
 */
typedef struct {
    gboolean is_erasing;             /* Currently erasing? */
    gint last_x;                     /* Last mouse X position */
    gint last_y;                     /* Last mouse Y position */
    struct ImageLayer* active_layer; /* Layer being erased from */
    Command* current_command;        /* Current erase command for undo */
} EraserToolState;

/**
 * Stamp eraser at a specific point with gradient based on hardness
 * Flow parameter controls the strength of each stamp (0.0-1.0)
 */
static void eraser_stamp_at(cairo_t* cr, gdouble x, gdouble y, gfloat size,
                            gfloat opacity, gfloat hardness, gfloat flow) {
    cairo_pattern_t* pattern;
    gdouble radius = size / 2.0;

    /* Flow modulates the effective opacity of this stamp
     * Flow 0.0 = no effect (doesn't erase)
     * Flow 1.0 = full opacity effect
     * This allows for buildable, gradual erasure */
    gfloat effective_opacity = opacity * flow;

    /* Use DEST_OUT operator for erasing */
    if (effective_opacity >= 1.0f) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    } else {
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    }

    /* Create radial gradient based on hardness
     * Hardness 0.0 = very soft (gradual falloff)
     * Hardness 1.0 = very hard (sharp edge) */
    pattern = cairo_pattern_create_radial(x, y, 0.0, x, y, radius);

    /* Inner circle: fully opacity */
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, 0.0, 0.0, 0.0, effective_opacity);

    /* Outer edge: opacity based on hardness
     * Higher hardness = sharper transition */
    gdouble inner_radius = hardness; /* 0.0 to 1.0*/

    if (hardness < 1.0f) {
        cairo_pattern_add_color_stop_rgba(pattern, inner_radius, 0.0, 0.0, 0.0,
                                          effective_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, 1.0, 0.0, 0.0, 0.0, 0.0);
    } else {
        /* Hardness = 1.0: sharp edge */
        cairo_pattern_add_color_stop_rgba(pattern, 0.99, 0.0, 0.0, 0.0, effective_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, 1.0, 0.0, 0.0, 0.0, 0.0);
    }

    cairo_set_source(cr, pattern);

    /* Draw circle */
    cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_pattern_destroy(pattern);
}

/**
 * Erase pixels from the layer surface (make transparent)
 * Uses tool options for size, opacity, and hardness
 * Interpoltes stamps along the line for smooth strokes
 */
static void eraser_erase_line(cairo_surface_t* surface, gdouble x1, gdouble y1,
                              gdouble x2, gdouble y2) {
    cairo_t* cr;
    ToolOptions* opts;
    gfloat eraser_size;
    gfloat eraser_opacity;
    gfloat hardness;
    gfloat flow;
    gfloat spacing;

    if (!surface) {
        return;
    }

    /* Get tool options - note: tool parameter not available here, use TOOL_ERASER */
    opts = tool_options_get_for_tool(TOOL_ERASER);
    eraser_size = opts ? opts->size : 5.0f;
    eraser_opacity = opts ? opts->opacity : 1.0f;
    hardness = opts ? opts->hardness : 1.0f;
    flow = opts ? opts->flow : 1.0f;
    spacing = opts ? opts->spacing : 0.25f; /* Default 25% spacing */

    cr = cairo_create(surface);

    /* Calculate distance for interpolation */
    gdouble dx = x2 - x1;
    gdouble dy = y2 - y1;
    gdouble distance = sqrt(dx * dx + dy * dy);

    /* Calculate number of stamps needed based on spacing */
    gfloat stamp_spacing = eraser_size * spacing;
    gint num_stamps = (gint)(distance / stamp_spacing) + 1;

    if (num_stamps < 2) {
        num_stamps = 2; /* at least draw start and end points */
    }

    /* Interpolate and stamp along the line */
    for (gint i = 0; i < num_stamps; i++) {
        gdouble t = (num_stamps > 1) ? (gdouble)i / (gdouble)(num_stamps - 1) : 0.0;
        gdouble x = x1 + t * dx;
        gdouble y = y1 + t * dy;

        eraser_stamp_at(cr, x, y, eraser_size, eraser_opacity, hardness, flow);
    }

    cairo_destroy(cr);

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Erase a single dot (for initial mouse down)
 */
static void eraser_erase_dot(cairo_surface_t* surface, gint x, gint y) {
    ToolOptions* opts;
    gfloat eraser_size;
    gfloat eraser_opacity;
    gfloat hardness;
    gfloat flow;
    cairo_t* cr;

    if (!surface) {
        return;
    }

    /* Get tool options - note: tool parameter not available here, use TOOL_ERASER */
    opts = tool_options_get_for_tool(TOOL_ERASER);
    eraser_size = opts ? opts->size : 5.0f;
    eraser_opacity = opts ? opts->opacity : 1.0f;
    hardness = opts ? opts->hardness : 1.0f;
    flow = opts ? opts->flow : 1.0f;

    cr = cairo_create(surface);
    eraser_stamp_at(cr, (gdouble)x + 0.5, (gdouble)y + 0.5, eraser_size, eraser_opacity, hardness, flow);

    cairo_destroy(cr);
    cairo_surface_mark_dirty(surface);
}

/**
 * Eraser tool: mouse down - start erasing
 */
static void eraser_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    EraserToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(EraserToolState));
    }
    state = (EraserToolState*)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        // printf("Eraser tool: no selected layer with surface\n");
        return;
    }

    /* Start erasing */
    state->is_erasing = TRUE;
    state->last_x = event->x;
    state->last_y = event->y;
    state->active_layer = active_layer;

    /* CRITICAL: Create erase command BEFORE any erasing - captures the "before" state */
    state->current_command = command_create_draw(active_layer,
                                                 command_get_name_string(CMD_NAME_ERASE));

    /* Erase initial dot at mouse down position */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;

    eraser_erase_dot(active_layer->surface, layer_x, layer_y);

    /* CRITICAL: Flush Cairo surface to ensure drawing is written to pixel buffer
       Worker threads will read the raw pixels, so we must flush first */
    cairo_surface_flush(active_layer->surface);

    active_layer->cache_dirty = TRUE;

    /* Mark initial dot area as dirty */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat eraser_size = opts ? opts->size : 5.0f;
    gint margin = (gint)(eraser_size / 2.0f) + 3;
    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, event->x - margin, event->y - margin,
                   eraser_size + 2 * margin, eraser_size + 2 * margin);
    dirty_rect_clamp(&dirty_rect, doc->width, doc->height);

    if (!dirty_rect_is_empty(&dirty_rect)) {
        document_invalidate_region(doc, &dirty_rect);
    }
}

/**
 * Eraser tool: mouse move - erase strokes
 */
static void eraser_tool_mouse_move(Tool* tool, struct ImageDocument* doc,
                                   MouseEvent* event) {
    EraserToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (EraserToolState*)tool->user_data;

    if (!state->is_erasing || !state->active_layer) {
        return;
    }

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        // printf("Eraser tool: selected layer deleted during erasing\n");
        state->is_erasing = FALSE;
        return;
    }

    /* Erase line from last position to current,
       adjusted for layer offset */
    gdouble layer_x1 = (gdouble)(state->last_x - active_layer->offset_x);
    gdouble layer_y1 = (gdouble)(state->last_y - active_layer->offset_y);
    gdouble layer_x2 = (gdouble)(event->x - active_layer->offset_x);
    gdouble layer_y2 = (gdouble)(event->y - active_layer->offset_y);

    eraser_erase_line(active_layer->surface, layer_x1, layer_y1, layer_x2,
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
    gfloat eraser_size = opts ? opts->size : 5.0f;
    gint margin = (gint)(eraser_size / 2.0f) + 3; /* Add margin for anti-aliasing */

    /* Calculate bounding box of stroke in document coordinates
       Use state->last_x/y (previous position) and event->x/y (current position) */
    gint min_x = (state->last_x < event->x) ? state->last_x : event->x;
    gint max_x = (state->last_x > event->x) ? state->last_x : event->x;
    gint min_y = (state->last_y < event->y) ? state->last_y : event->y;
    gint max_y = (state->last_y > event->y) ? state->last_y : event->y;

    /* Store the image-space coordinates for next iteration - AFTER calculating dirty rect */
    state->last_x = event->x;
    state->last_y = event->y;

    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, min_x - margin, min_y - margin,
                   (max_x - min_x) + 2 * margin,
                   (max_y - min_y) + 2 * margin);
    dirty_rect_clamp(&dirty_rect, doc->width, doc->height);

    /* Only invalidate the stroke region */
    if (!dirty_rect_is_empty(&dirty_rect)) {
        document_invalidate_region(doc, &dirty_rect);
    }

    // printf("Eraser tool: erasing line from (%d, %d) to (%d, %d)\n",
    //        layer_x1, layer_y1, layer_x2, layer_y2);
}

/**
 * Eraser tool: mouse up - end erasing
 */
static void eraser_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    EraserToolState* state;
    AppContext* ctx;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (EraserToolState*)tool->user_data;

    if (!state->is_erasing) {
        return;
    }

    /* Finalize erase command - captures the "after" state */
    if (state->current_command) {
        command_finalize_draw(state->current_command);
    }

    /* Push erase command to undo stack */
    if (state->current_command && doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);
        // printf("Eraser tool: erase command pushed to undo stack\n");

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

    state->is_erasing = FALSE;
    state->current_command = NULL;
    state->active_layer = NULL;

    // printf("Eraser tool: finished erasing\n");
}

/**
 * Create the Eraser Tool
 */
Tool* tool_eraser_create(void) {
    Tool* tool;

    /* Eraser tool supports size, opacity, hardness, flow, and spacing */
    tool = tool_new("Eraser", TOOL_ERASER, GDK_CROSSHAIR,
                    TOOL_OPT_SIZE | TOOL_OPT_OPACITY | TOOL_OPT_HARDNESS |
                        TOOL_OPT_FLOW | TOOL_OPT_SPACING);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = eraser_tool_mouse_down;
    tool->mouse_move = eraser_tool_mouse_move;
    tool->mouse_up = eraser_tool_mouse_up;

    // printf("Eraser tool created\n");

    return tool;
}
