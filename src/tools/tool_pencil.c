#include "tools/tool_pencil.h"
#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "selection/selection_render.h"
#include "tool_options.h"
#include "ui/tools_panel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
typedef struct ImageDocument ImageDocument;
extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx, ImageDocument* doc);

/**
 * Pencil Tool state
 */
typedef struct {
    gboolean is_drawing;              /* Currently drawing? */
    gint last_x;                      /* Last mouse X position */
    gint last_y;                      /* Last mouse Y position */
    struct ImageLayer* active_layer;  /* Layer being drawn on */
    TileUndoTransaction* transaction; /* Tile-based undo transaction */
} PencilToolState;

/**
 * Align coordinate to pixel grid centerpoint
 * This snaps coordinates to 0.5, 1.5, 2.5, etc. for pixel-perfect drawing
 */
static gdouble align_to_pixel_grid(gdouble coord) {
    return floor(coord) + 0.5;
}

/**
 * Draw pencil stroke at a specific point
 * Unlike brush, pencil has hard edges and optional antialiasing
 */
static void pencil_stamp_at(cairo_t* cr, gdouble x, gdouble y, gfloat size,
                            GdkRGBA* color, gfloat opacity, gboolean antialias) {
    gdouble radius = size / 2.0;
    gfloat effective_opacity = opacity * color->alpha;

    /* Set antialiasing mode */
    if (antialias) {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    } else {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    }

    /* Set color with opacity */
    cairo_set_source_rgba(cr, color->red, color->green, color->blue, effective_opacity);

    /* Use OVER operator for normal painting */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Draw circle - pencil has hard edges, no gradient */
    cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
    cairo_fill(cr);
}

/**
 * Draw a line from (x1, y1) to (x2, y2) on the layer surface
 * Uses tool options for size, opacity, and foreground color
 * Interpolates stamps along the line for smooth strokes
 * Applies selection mask if present in document
 * @param layer_offset_x Layer's X offset in document coordinates
 * @param layer_offset_y Layer's Y offset in document coordinates
 */
static void pencil_draw_line(cairo_surface_t* surface, struct ImageDocument* doc,
                             gdouble x1, gdouble y1, gdouble x2, gdouble y2,
                             gint layer_offset_x, gint layer_offset_y) {
    cairo_t* cr;
    ToolOptions* opts;
    GdkRGBA fg_color;
    gfloat pencil_size;
    gfloat pencil_opacity;
    gboolean antialias;
    gboolean align_pixel_grid;

    if (!surface) {
        return;
    }

    /* Get tool options */
    opts = tool_options_get_for_tool(TOOL_PENCIL);
    pencil_size = opts ? opts->size : 1.0f;
    pencil_opacity = opts ? opts->opacity : 1.0f;
    antialias = opts ? opts->pencil_antialias : FALSE;
    align_pixel_grid = opts ? opts->pencil_align_pixel_grid : TRUE;

    /* Apply pixel grid alignment if enabled */
    if (align_pixel_grid) {
        x1 = align_to_pixel_grid(x1);
        y1 = align_to_pixel_grid(y1);
        x2 = align_to_pixel_grid(x2);
        y2 = align_to_pixel_grid(y2);
    }

    /* Get foreground color, default to black if not available */
    if (!tools_panel_get_foreground_color(&fg_color)) {
        fg_color.red = 0.0;
        fg_color.green = 0.0;
        fg_color.blue = 0.0;
        fg_color.alpha = 1.0;
    }

    /* Calculate bounding box for stroke */
    gint min_x_layer = (gint)((x1 < x2) ? x1 : x2) - (gint)(pencil_size / 2.0f) - 2;
    gint min_y_layer = (gint)((y1 < y2) ? y1 : y2) - (gint)(pencil_size / 2.0f) - 2;
    gint max_x_layer = (gint)((x1 > x2) ? x1 : x2) + (gint)(pencil_size / 2.0f) + 2;
    gint max_y_layer = (gint)((y1 > y2) ? y1 : y2) + (gint)(pencil_size / 2.0f) + 2;

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
            /* Translate to draw in temp surface coordinates */
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

    /* Calculate number of stamps needed - pencil uses tight spacing (10% of size) for solid lines */
    gfloat stamp_spacing = pencil_size * 0.1f;
    gint num_stamps = (gint)(distance / stamp_spacing) + 1;

    if (num_stamps < 2) {
        num_stamps = 2; /* At least draw start and end points */
    }

    /* Interpolate and stamp along the line */
    for (gint i = 0; i < num_stamps; i++) {
        gdouble t = (num_stamps > 1) ? (gdouble)i / (gdouble)(num_stamps - 1) : 0.0;
        gdouble x = x1 + t * dx;
        gdouble y = y1 + t * dy;

        pencil_stamp_at(cr, x, y, pencil_size, &fg_color, pencil_opacity, antialias);
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

            /* Apply mask to temp surface */
            render_utils_apply_selection_mask(
                temp_surface,
                region_mask->data,
                mask_x_temp, mask_y_temp,
                region_mask->width, region_mask->height,
                region_mask->stride);

            selection_mask_free(region_mask);
        }

        /* Composite masked result onto layer surface */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_set_source_surface(composite_cr, temp_surface, min_x_layer, min_y_layer);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_OVER);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);

        cairo_surface_destroy(temp_surface);
    } else if (!has_selection && temp_surface) {
        /* No selection but temp surface was created - composite it */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_set_source_surface(composite_cr, temp_surface, min_x_layer, min_y_layer);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_OVER);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);
        cairo_surface_destroy(temp_surface);
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Register all tiles that a pencil stroke bounding box intersects
 * Helper function to ensure all affected tiles are captured for undo
 */
static void pencil_register_tiles_for_bounding_box(TileUndoTransaction* transaction,
                                                   struct ImageDocument* doc,
                                                   struct ImageLayer* layer,
                                                   gint layer_x1, gint layer_y1,
                                                   gint layer_x2, gint layer_y2,
                                                   gfloat pencil_size) {
    if (!transaction || !doc || !layer) {
        return;
    }

    gint tile_size = doc->tile_grid ? doc->tile_grid->tile_size : 128;
    gdouble radius = pencil_size / 2.0;

    /* Calculate bounding box of stroke accounting for pencil radius */
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
 * Draw a single dot (for initial mouse down)
 * Applies selection mask if present in document
 * @param layer_offset_x Layer's X offset in document coordinates
 * @param layer_offset_y Layer's Y offset in document coordinates
 */
static void pencil_draw_dot(cairo_surface_t* surface, struct ImageDocument* doc,
                            gint x, gint y, gint layer_offset_x, gint layer_offset_y) {
    ToolOptions* opts;
    GdkRGBA fg_color;
    gfloat pencil_size;
    gfloat pencil_opacity;
    gboolean antialias;
    gboolean align_pixel_grid;
    cairo_t* cr;

    if (!surface) {
        return;
    }

    /* Get tool options */
    opts = tool_options_get_for_tool(TOOL_PENCIL);
    pencil_size = opts ? opts->size : 1.0f;
    pencil_opacity = opts ? opts->opacity : 1.0f;
    antialias = opts ? opts->pencil_antialias : FALSE;
    align_pixel_grid = opts ? opts->pencil_align_pixel_grid : TRUE;

    /* Apply pixel grid alignment if enabled */
    gdouble draw_x = (gdouble)x;
    gdouble draw_y = (gdouble)y;
    if (align_pixel_grid) {
        draw_x = align_to_pixel_grid(draw_x);
        draw_y = align_to_pixel_grid(draw_y);
    }

    /* Get foreground color, default to black if not available */
    if (!tools_panel_get_foreground_color(&fg_color)) {
        fg_color.red = 0.0;
        fg_color.green = 0.0;
        fg_color.blue = 0.0;
        fg_color.alpha = 1.0;
    }

    /* Calculate bounding box of dot */
    gint margin = (gint)(pencil_size / 2.0f) + 2;
    gint min_x = x - margin;
    gint min_y = y - margin;
    gint max_x = x + margin;
    gint max_y = y + margin;

    /* Clamp to surface bounds */
    gint surface_width = cairo_image_surface_get_width(surface);
    gint surface_height = cairo_image_surface_get_height(surface);
    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x > surface_width)
        max_x = surface_width;
    if (max_y > surface_height)
        max_y = surface_height;

    gint dot_width = max_x - min_x;
    gint dot_height = max_y - min_y;

    /* Check if we need to apply selection mask */
    gboolean has_selection = (doc && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));
    cairo_surface_t* temp_surface = NULL;

    if (has_selection && dot_width > 0 && dot_height > 0) {
        /* Draw to temporary surface first, then apply mask and composite */
        temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dot_width, dot_height);
        if (temp_surface) {
            cr = cairo_create(temp_surface);
            /* Translate to draw in temp surface coordinates */
            cairo_translate(cr, -min_x, -min_y);
        } else {
            /* Fallback to drawing directly if temp surface creation fails */
            temp_surface = NULL;
            cr = cairo_create(surface);
        }
    } else {
        /* No selection, draw directly to surface */
        cr = cairo_create(surface);
    }

    pencil_stamp_at(cr, draw_x, draw_y, pencil_size, &fg_color, pencil_opacity, antialias);
    cairo_destroy(cr);

    /* Apply selection mask if present */
    if (has_selection && temp_surface && dot_width > 0 && dot_height > 0) {
        /* Convert to document coordinates for selection mask */
        gint min_x_doc = min_x + layer_offset_x;
        gint min_y_doc = min_y + layer_offset_y;
        gint max_x_doc = max_x + layer_offset_x;
        gint max_y_doc = max_y + layer_offset_y;

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

            /* Apply mask to temp surface */
            render_utils_apply_selection_mask(
                temp_surface,
                region_mask->data,
                mask_x_temp, mask_y_temp,
                region_mask->width, region_mask->height,
                region_mask->stride);

            selection_mask_free(region_mask);
        }

        /* Composite masked result onto layer surface */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_set_source_surface(composite_cr, temp_surface, min_x, min_y);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_OVER);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);

        cairo_surface_destroy(temp_surface);
    } else if (!has_selection && temp_surface) {
        /* No selection but temp surface was created - composite it */
        cairo_surface_flush(temp_surface);
        cairo_t* composite_cr = cairo_create(surface);
        cairo_set_source_surface(composite_cr, temp_surface, min_x, min_y);
        cairo_set_operator(composite_cr, CAIRO_OPERATOR_OVER);
        cairo_paint(composite_cr);
        cairo_destroy(composite_cr);
        cairo_surface_destroy(temp_surface);
    }

    cairo_surface_mark_dirty(surface);
}

/**
 * Pencil tool: mouse down - start drawing
 */
static void pencil_tool_mouse_down(Tool* tool, struct ImageDocument* doc,
                                   MouseEvent* event) {
    PencilToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(PencilToolState));
    }
    state = (PencilToolState*)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        return;
    }

    /* Start drawing */
    state->is_drawing = TRUE;
    state->last_x = event->x;
    state->last_y = event->y;
    state->active_layer = active_layer;

    /* Begin tile-based undo transaction BEFORE any drawing */
    state->transaction = tile_undo_transaction_begin(active_layer,
                                                     doc,
                                                     command_get_name_string(CMD_NAME_DRAW_PENCIL_STROKE));
    if (!state->transaction) {
        state->is_drawing = FALSE;
        return;
    }

    /* Draw initial dot at mouse down position */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;

    /* Register all tiles that the initial pencil dot will affect */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat pencil_size = opts ? opts->size : 1.0f;
    pencil_register_tiles_for_bounding_box(state->transaction, doc, active_layer,
                                           layer_x, layer_y,
                                           layer_x, layer_y,
                                           pencil_size);

    pencil_draw_dot(active_layer->surface, doc, layer_x, layer_y,
                    active_layer->offset_x, active_layer->offset_y);

    /* Flush Cairo surface to ensure drawing is written to pixel buffer */
    cairo_surface_flush(active_layer->surface);

    active_layer->cache_dirty = TRUE;

    /* Mark initial dot area as dirty */
    gint margin = (gint)(pencil_size / 2.0f) + 3;

    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, event->x - margin, event->y - margin,
                   pencil_size + 2 * margin, pencil_size + 2 * margin);
    dirty_rect_clamp(&dirty_rect, doc->width, doc->height);

    if (!dirty_rect_is_empty(&dirty_rect)) {
        document_invalidate_region(doc, &dirty_rect);
    }
}

/**
 * Pencil tool: mouse move - draw strokes
 */
static void pencil_tool_mouse_move(Tool* tool, struct ImageDocument* doc,
                                   MouseEvent* event) {
    PencilToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (PencilToolState*)tool->user_data;

    if (!state->is_drawing || !state->active_layer) {
        return;
    }

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        /* Cancel transaction if layer was deleted */
        if (state->transaction) {
            tile_undo_transaction_cancel(state->transaction);
            state->transaction = NULL;
        }
        state->is_drawing = FALSE;
        return;
    }

    /* Draw line from last position to current, adjusted for layer offset */
    gint layer_x1 = state->last_x - active_layer->offset_x;
    gint layer_y1 = state->last_y - active_layer->offset_y;
    gint layer_x2 = event->x - active_layer->offset_x;
    gint layer_y2 = event->y - active_layer->offset_y;

    /* Get pencil size for tile registration and dirty rect calculation */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat pencil_size = opts ? opts->size : 1.0f;

    /* Register all tiles that this stroke segment will affect */
    if (state->transaction) {
        pencil_register_tiles_for_bounding_box(state->transaction, doc, active_layer,
                                               layer_x1, layer_y1,
                                               layer_x2, layer_y2,
                                               pencil_size);
    }

    pencil_draw_line(active_layer->surface, doc, (gdouble)layer_x1, (gdouble)layer_y1,
                     (gdouble)layer_x2, (gdouble)layer_y2,
                     active_layer->offset_x, active_layer->offset_y);

    /* Flush Cairo surface to ensure drawing is written to pixel buffer */
    cairo_surface_flush(active_layer->surface);

    /* Mark layer cache as dirty */
    active_layer->cache_dirty = TRUE;

    /* Calculate dirty rectangle */
    gint margin = (gint)(pencil_size / 2.0f) + 3;

    /* Calculate bounding box of stroke in document coordinates */
    gint min_x = (state->last_x < event->x) ? state->last_x : event->x;
    gint max_x = (state->last_x > event->x) ? state->last_x : event->x;
    gint min_y = (state->last_y < event->y) ? state->last_y : event->y;
    gint max_y = (state->last_y > event->y) ? state->last_y : event->y;

    /* Store the image-space coordinates for next iteration */
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
}

/**
 * Pencil tool: mouse up - end drawing
 */
static void pencil_tool_mouse_up(Tool* tool, struct ImageDocument* doc,
                                 MouseEvent* event) {
    PencilToolState* state;
    AppContext* ctx;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (PencilToolState*)tool->user_data;

    if (!state->is_drawing) {
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
            ui_update_window_title(ctx, NULL);
        }
    } else if (cmd) {
        /* No undo stack, free the command */
        command_free(cmd);
    }

    /* Mark document as modified */
    doc->modified = TRUE;

    state->is_drawing = FALSE;
    state->active_layer = NULL;
}

/**
 * Create the Pencil Tool
 */
Tool* tool_pencil_create(void) {
    Tool* tool;

    /* Pencil tool supports size, opacity, antialias, and align_pixel_grid */
    tool = tool_new("Pencil", TOOL_PENCIL, GDK_CROSSHAIR,
                    TOOL_OPT_SIZE | TOOL_OPT_OPACITY);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = pencil_tool_mouse_down;
    tool->mouse_move = pencil_tool_mouse_move;
    tool->mouse_up = pencil_tool_mouse_up;

    return tool;
}
