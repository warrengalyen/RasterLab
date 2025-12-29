#include "tools/tool_eraser.h"
#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "selection/selection_render.h"
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
    gboolean is_erasing;              /* Currently erasing? */
    gint last_x;                      /* Last mouse X position */
    gint last_y;                      /* Last mouse Y position */
    struct ImageLayer* active_layer;  /* Layer being erased from */
    TileUndoTransaction* transaction; /* Tile-based undo transaction */
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
 * Applies selection mask if present in document
 * @param layer_offset_x Layer's X offset in document coordinates
 * @param layer_offset_y Layer's Y offset in document coordinates
 */
static void eraser_erase_line(cairo_surface_t* surface, struct ImageDocument* doc,
                              gdouble x1, gdouble y1, gdouble x2, gdouble y2,
                              gint layer_offset_x, gint layer_offset_y) {
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

    /* Calculate bounding box for stroke */
    gint min_x_layer = (gint)((x1 < x2) ? x1 : x2) - (gint)(eraser_size / 2.0f) - 2;
    gint min_y_layer = (gint)((y1 < y2) ? y1 : y2) - (gint)(eraser_size / 2.0f) - 2;
    gint max_x_layer = (gint)((x1 > x2) ? x1 : x2) + (gint)(eraser_size / 2.0f) + 2;
    gint max_y_layer = (gint)((y1 > y2) ? y1 : y2) + (gint)(eraser_size / 2.0f) + 2;

    /* Clamp to surface bounds */
    gint surface_width = cairo_image_surface_get_width(surface);
    gint surface_height = cairo_image_surface_get_height(surface);
    if (min_x_layer < 0)
        min_x_layer = 0;
    if (min_y_layer < 0)
        min_y_layer = 0;
    if (max_x_layer > surface_width)
        max_x_layer = surface_width;
    if (max_y_layer > surface_height)
        max_y_layer = surface_height;

    gint stroke_width = max_x_layer - min_x_layer;
    gint stroke_height = max_y_layer - min_y_layer;

    /* Check if we need to apply selection mask */
    gboolean has_selection = (doc && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));
    cairo_surface_t* temp_surface = NULL;

    if (has_selection && stroke_width > 0 && stroke_height > 0) {
        /* Draw to temporary surface first, then apply mask and composite */
        temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, stroke_width, stroke_height);
        if (temp_surface) {
            cr = cairo_create(temp_surface);
            /* Copy existing layer content to temp surface (eraser needs destination pixels) */
            cairo_set_source_surface(cr, surface, -min_x_layer, -min_y_layer);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            /* Now translate for erasing operations */
            cairo_translate(cr, -min_x_layer, -min_y_layer);
        } else {
            /* Fallback to drawing directly if temp surface creation fails */
            temp_surface = NULL;
            cr = cairo_create(surface);
        }
    } else {
        /* No selection, draw directly to surface */
        cr = cairo_create(surface);
    }

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

    /* Apply selection mask if present */
    if (has_selection && temp_surface && stroke_width > 0 && stroke_height > 0) {
        /* Convert to document coordinates for selection mask */
        gint min_x_doc = min_x_layer + layer_offset_x;
        gint min_y_doc = min_y_layer + layer_offset_y;
        gint max_x_doc = max_x_layer + layer_offset_x;
        gint max_y_doc = max_y_layer + layer_offset_y;

        DirtyRect dirty_rect;
        dirty_rect_set(&dirty_rect, min_x_doc, min_y_doc,
                       max_x_doc - min_x_doc, max_y_doc - min_y_doc);

        /* Get selection mask for this region (in document coordinates) */
        DirtyRect actual_region;
        SelectionMask* region_mask = selection_build_combined_mask(
            doc->selection_mask, &dirty_rect, FEATHER_QUALITY_NORMAL, &actual_region);

        if (region_mask && region_mask->data) {
            /* Convert actual region to temp surface coordinates */
            gint mask_x_temp = (actual_region.x - min_x_doc);
            gint mask_y_temp = (actual_region.y - min_y_doc);

            /* For eraser: restore original pixels outside selection */
            render_utils_apply_selection_mask_to_eraser(
                temp_surface,
                surface,
                region_mask->data,
                mask_x_temp, mask_y_temp,
                region_mask->width, region_mask->height,
                region_mask->stride,
                min_x_layer, min_y_layer);

            selection_mask_free(region_mask);
        }

        /* Composite masked result onto layer surface using SOURCE to replace region */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_rectangle(composite_cr, min_x_layer, min_y_layer, stroke_width, stroke_height);
        cairo_clip(composite_cr);
        cairo_set_source_surface(composite_cr, temp_surface, min_x_layer, min_y_layer);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);

        cairo_surface_destroy(temp_surface);
    } else if (!has_selection && temp_surface) {
        /* No selection but temp surface was created - composite it */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_rectangle(composite_cr, min_x_layer, min_y_layer, stroke_width, stroke_height);
        cairo_clip(composite_cr);
        cairo_set_source_surface(composite_cr, temp_surface, min_x_layer, min_y_layer);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);
        cairo_surface_destroy(temp_surface);
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Register all tiles that an eraser stroke bounding box intersects
 * Helper function to ensure all affected tiles are captured for undo
 */
static void eraser_register_tiles_for_bounding_box(TileUndoTransaction* transaction,
                                                   struct ImageDocument* doc,
                                                   struct ImageLayer* layer,
                                                   gint layer_x1, gint layer_y1,
                                                   gint layer_x2, gint layer_y2,
                                                   gfloat eraser_size) {
    if (!transaction || !doc || !layer) {
        return;
    }

    gint tile_size = doc->tile_grid ? doc->tile_grid->tile_size : 128;
    gdouble radius = eraser_size / 2.0;

    /* Calculate bounding box of stroke accounting for eraser radius */
    gint min_x = ((layer_x1 < layer_x2) ? layer_x1 : layer_x2) - (gint)radius - 1;
    gint max_x = ((layer_x1 > layer_x2) ? layer_x1 : layer_x2) + (gint)radius + 1;
    gint min_y = ((layer_y1 < layer_y2) ? layer_y1 : layer_y2) - (gint)radius - 1;
    gint max_y = ((layer_y1 > layer_y2) ? layer_y1 : layer_y2) + (gint)radius + 1;

    /* Clamp to layer bounds */
    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x >= (gint)layer->width)
        max_x = layer->width - 1;
    if (max_y >= (gint)layer->height)
        max_y = layer->height - 1;

    /* Calculate tile bounds */
    gint start_tile_x = min_x / tile_size;
    gint start_tile_y = min_y / tile_size;
    gint end_tile_x = max_x / tile_size;
    gint end_tile_y = max_y / tile_size;

    /* Register all tiles in the bounding box */
    for (gint ty = start_tile_y; ty <= end_tile_y; ty++) {
        for (gint tx = start_tile_x; tx <= end_tile_x; tx++) {
            gint sample_x = tx * tile_size + tile_size / 2;
            gint sample_y = ty * tile_size + tile_size / 2;
            tile_undo_transaction_register_tile(transaction, doc, sample_x, sample_y);
        }
    }
}

/**
 * Erase a single dot (for initial mouse down)
 * Applies selection mask if present in document
 * @param layer_offset_x Layer's X offset in document coordinates
 * @param layer_offset_y Layer's Y offset in document coordinates
 */
static void eraser_erase_dot(cairo_surface_t* surface, struct ImageDocument* doc,
                             gint x, gint y, gint layer_offset_x, gint layer_offset_y) {
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

    /* Calculate bounding box of dot */
    gint margin = (gint)(eraser_size / 2.0f) + 2;
    gint min_x_layer = x - margin;
    gint min_y_layer = y - margin;
    gint max_x_layer = x + margin;
    gint max_y_layer = y + margin;

    /* Clamp to surface bounds */
    gint surface_width = cairo_image_surface_get_width(surface);
    gint surface_height = cairo_image_surface_get_height(surface);
    if (min_x_layer < 0)
        min_x_layer = 0;
    if (min_y_layer < 0)
        min_y_layer = 0;
    if (max_x_layer > surface_width)
        max_x_layer = surface_width;
    if (max_y_layer > surface_height)
        max_y_layer = surface_height;

    gint stroke_width = max_x_layer - min_x_layer;
    gint stroke_height = max_y_layer - min_y_layer;

    /* Check if we need to apply selection mask */
    gboolean has_selection = (doc && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));
    cairo_surface_t* temp_surface = NULL;

    if (has_selection && stroke_width > 0 && stroke_height > 0) {
        /* Draw to temporary surface first, then apply mask and composite */
        temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, stroke_width, stroke_height);
        if (temp_surface) {
            cr = cairo_create(temp_surface);
            /* Copy existing layer content to temp surface (eraser needs destination pixels) */
            cairo_set_source_surface(cr, surface, -min_x_layer, -min_y_layer);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            /* Now translate for erasing operations */
            cairo_translate(cr, -min_x_layer, -min_y_layer);
        } else {
            /* Fallback to drawing directly if temp surface creation fails */
            temp_surface = NULL;
            cr = cairo_create(surface);
        }
    } else {
        /* No selection, draw directly to surface */
        cr = cairo_create(surface);
    }

    eraser_stamp_at(cr, (gdouble)x + 0.5, (gdouble)y + 0.5, eraser_size, eraser_opacity, hardness, flow);

    cairo_destroy(cr);

    /* Apply selection mask if present */
    if (has_selection && temp_surface && stroke_width > 0 && stroke_height > 0) {
        /* Convert to document coordinates for selection mask */
        gint min_x_doc = min_x_layer + layer_offset_x;
        gint min_y_doc = min_y_layer + layer_offset_y;
        gint max_x_doc = max_x_layer + layer_offset_x;
        gint max_y_doc = max_y_layer + layer_offset_y;

        DirtyRect dirty_rect;
        dirty_rect_set(&dirty_rect, min_x_doc, min_y_doc, max_x_doc - min_x_doc, max_y_doc - min_y_doc);

        /* Get selection mask for this region (in document coordinates) */
        DirtyRect actual_region;
        SelectionMask* region_mask = selection_build_combined_mask(
            doc->selection_mask, &dirty_rect, FEATHER_QUALITY_NORMAL, &actual_region);

        if (region_mask && region_mask->data) {
            /* Convert actual region to temp surface coordinates */
            gint mask_x_temp = (actual_region.x - min_x_doc);
            gint mask_y_temp = (actual_region.y - min_y_doc);

            /* For eraser: restore original pixels outside selection */
            render_utils_apply_selection_mask_to_eraser(
                temp_surface,
                surface,
                region_mask->data,
                mask_x_temp, mask_y_temp,
                region_mask->width, region_mask->height,
                region_mask->stride,
                min_x_layer, min_y_layer);

            selection_mask_free(region_mask);
        }

        /* Composite masked result onto layer surface using SOURCE to replace region */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_rectangle(composite_cr, min_x_layer, min_y_layer, stroke_width, stroke_height);
        cairo_clip(composite_cr);
        cairo_set_source_surface(composite_cr, temp_surface, min_x_layer, min_y_layer);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);

        cairo_surface_destroy(temp_surface);
    } else if (!has_selection && temp_surface) {
        /* No selection but temp surface was created - composite it */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_rectangle(composite_cr, min_x_layer, min_y_layer, stroke_width, stroke_height);
        cairo_clip(composite_cr);
        cairo_set_source_surface(composite_cr, temp_surface, min_x_layer, min_y_layer);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);
        cairo_surface_destroy(temp_surface);
    }

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

    /* CRITICAL: Begin tile-based undo transaction BEFORE any erasing */
    state->transaction = tile_undo_transaction_begin(active_layer,
                                                     doc,
                                                     command_get_name_string(CMD_NAME_ERASE));
    if (!state->transaction) {
        state->is_erasing = FALSE;
        return;
    }

    /* Erase initial dot at mouse down position */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;

    /* Register all tiles that the initial eraser dot will affect */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat eraser_size = opts ? opts->size : 5.0f;
    eraser_register_tiles_for_bounding_box(state->transaction, doc, active_layer,
                                           layer_x, layer_y,
                                           layer_x, layer_y,
                                           eraser_size);

    eraser_erase_dot(active_layer->surface, doc, layer_x, layer_y,
                     active_layer->offset_x, active_layer->offset_y);

    /* CRITICAL: Flush Cairo surface to ensure drawing is written to pixel buffer
       Worker threads will read the raw pixels, so we must flush first */
    cairo_surface_flush(active_layer->surface);

    active_layer->cache_dirty = TRUE;

    /* Mark initial dot area as dirty */
    /* opts and eraser_size already declared above */
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
        /* Cancel transaction if layer was deleted */
        if (state->transaction) {
            tile_undo_transaction_cancel(state->transaction);
            state->transaction = NULL;
        }
        state->is_erasing = FALSE;
        return;
    }

    /* Erase line from last position to current,
       adjusted for layer offset */
    gint layer_x1 = state->last_x - active_layer->offset_x;
    gint layer_y1 = state->last_y - active_layer->offset_y;
    gint layer_x2 = event->x - active_layer->offset_x;
    gint layer_y2 = event->y - active_layer->offset_y;

    /* Get eraser size for tile registration and dirty rect calculation */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat eraser_size = opts ? opts->size : 5.0f;

    /* Register all tiles that this stroke segment will affect */
    if (state->transaction) {
        eraser_register_tiles_for_bounding_box(state->transaction, doc, active_layer,
                                               layer_x1, layer_y1,
                                               layer_x2, layer_y2,
                                               eraser_size);
    }

    eraser_erase_line(active_layer->surface, doc, (gdouble)layer_x1, (gdouble)layer_y1,
                      (gdouble)layer_x2, (gdouble)layer_y2,
                      active_layer->offset_x, active_layer->offset_y);

    /* CRITICAL: Flush Cairo surface to ensure drawing is written to pixel buffer
       Worker threads will read the raw pixels, so we must flush first */
    cairo_surface_flush(active_layer->surface);

    /* Mark layer cache as dirty but don't destroy it yet
       We'll regenerate it lazily only when needed for compositing
       This avoids expensive cache regeneration on every mouse move */
    active_layer->cache_dirty = TRUE;

    /* Calculate dirty rectangle BEFORE updating last_x/y
       Use the previous position and current position */
    /* eraser_size already declared above */
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

    /* Commit tile-based undo transaction (captures "after" state and creates command) */
    Command* cmd = NULL;
    if (state->transaction) {
        cmd = tile_undo_transaction_commit(state->transaction);
        state->transaction = NULL;
    }

    /* Push command to undo stack */
    if (cmd && doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

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
    } else if (cmd) {
        /* No undo stack, free the command */
        command_free(cmd);
    }

    /* Mark document as modified */
    doc->modified = TRUE;

    state->is_erasing = FALSE;
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
