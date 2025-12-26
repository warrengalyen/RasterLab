#include "tools/tool_move.h"
#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx);

/**
 * Create a snapshot of a Cairo surface
 * Returns a new surface with a copy of the source surface
 */
static cairo_surface_t* create_surface_snapshot(cairo_surface_t* source) {
    cairo_surface_t* snapshot;
    cairo_t* cr;
    int width, height;

    if (!source) {
        return NULL;
    }

    width = cairo_image_surface_get_width(source);
    height = cairo_image_surface_get_height(source);

    /* Create new surface with same format */
    snapshot = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    if (!snapshot) {
        return NULL;
    }

    /* Copy source to snapshot */
    cr = cairo_create(snapshot);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    return snapshot;
}

/**
 * Extract selected pixels from a layer to a new layer
 * Returns the new layer with extracted pixels, or NULL on error
 */
static ImageLayer* extract_selection_to_layer(struct ImageDocument* doc, struct ImageLayer* source_layer) {
    if (!doc || !source_layer || !source_layer->surface ||
        !doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        return NULL;
    }

    /* Calculate intersection of layer bounds and selection bounds in document coordinates */
    gint layer_x_min = source_layer->offset_x;
    gint layer_y_min = source_layer->offset_y;
    gint layer_x_max = source_layer->offset_x + source_layer->width;
    gint layer_y_max = source_layer->offset_y + source_layer->height;

    /* Clamp to document bounds */
    layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
    layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
    layer_x_max = (layer_x_max > doc->width) ? doc->width : layer_x_max;
    layer_y_max = (layer_y_max > doc->height) ? doc->height : layer_y_max;

    if (layer_x_max <= layer_x_min || layer_y_max <= layer_y_min) {
        return NULL; /* No intersection */
    }

    /* Create dirty rect for the layer region in document coordinates */
    DirtyRect layer_rect;
    dirty_rect_set(&layer_rect, layer_x_min, layer_y_min,
                   layer_x_max - layer_x_min, layer_y_max - layer_y_min);

    /* Get selection mask for this region */
    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data ||
        dirty_rect_is_empty(&actual_region)) {
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels within the region */
    gint sel_x_min = actual_region.x + actual_region.width;
    gint sel_y_min = actual_region.y + actual_region.height;
    gint sel_x_max = actual_region.x;
    gint sel_y_max = actual_region.y;

    /* Scan mask to find actual bounds */
    for (gint y = 0; y < region_mask->height; y++) {
        for (gint x = 0; x < region_mask->width; x++) {
            uint8_t mask_alpha = region_mask->data[y * region_mask->stride + x];
            if (mask_alpha > 0) {
                gint doc_x = actual_region.x + x;
                gint doc_y = actual_region.y + y;
                if (doc_x < sel_x_min)
                    sel_x_min = doc_x;
                if (doc_y < sel_y_min)
                    sel_y_min = doc_y;
                if (doc_x > sel_x_max)
                    sel_x_max = doc_x;
                if (doc_y > sel_y_max)
                    sel_y_max = doc_y;
            }
        }
    }

    if (sel_x_max < sel_x_min || sel_y_max < sel_y_min) {
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create new layer with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    /* Use original layer name for the new layer */
    const gchar* layer_name = source_layer->name ? source_layer->name : "Layer";
    ImageLayer* new_layer = layer_new(layer_name, new_width, new_height,
                                      TRUE, LAYER_BACKGROUND_TRANSPARENT,
                                      LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!new_layer) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Set new layer offset to the bounding box origin */
    new_layer->offset_x = sel_x_min;
    new_layer->offset_y = sel_y_min;

    /* Copy pixels from source layer to new layer, masked by selection */
    cairo_surface_flush(source_layer->surface);
    cairo_surface_flush(new_layer->surface);

    guchar* src_data = cairo_image_surface_get_data(source_layer->surface);
    gint src_stride = cairo_image_surface_get_stride(source_layer->surface);
    guchar* dst_data = cairo_image_surface_get_data(new_layer->surface);
    gint dst_stride = cairo_image_surface_get_stride(new_layer->surface);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint src_y = doc_y - source_layer->offset_y;
        gint mask_y = doc_y - actual_region.y;

        if (src_y < 0 || src_y >= source_layer->height) {
            continue; /* Outside source layer */
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue; /* Outside mask */
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint src_x = doc_x - source_layer->offset_x;
            gint mask_x = doc_x - actual_region.x;

            if (src_x < 0 || src_x >= source_layer->width) {
                continue; /* Outside source layer */
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue; /* Outside mask */
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from source to destination */
            /* Cairo ARGB32 format: BGRA in memory (little-endian) */
            guchar* src_pixel = src_data + src_y * src_stride + src_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            /* Read BGRA from source */
            guchar src_b = src_pixel[0];
            guchar src_g = src_pixel[1];
            guchar src_r = src_pixel[2];
            guchar src_a = src_pixel[3];

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_b;
                dst_pixel[1] = src_g;
                dst_pixel[2] = src_r;
                dst_pixel[3] = src_a;
            } else {
                /* Partially selected (feathered): apply mask to alpha
                 * Need to handle premultiplied alpha correctly */
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    /* Completely transparent */
                    dst_pixel[0] = 0;
                    dst_pixel[1] = 0;
                    dst_pixel[2] = 0;
                    dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply source color, then re-premultiply with new alpha */
                    /* Un-premultiply: convert from premultiplied to straight alpha */
                    uint16_t r = (src_r * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_g * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_b * 255 + src_a / 2) / src_a;

                    /* Clamp to valid range */
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    /* Re-premultiply with new alpha */
                    dst_pixel[0] = (b * new_alpha + 127) / 255; /* B */
                    dst_pixel[1] = (g * new_alpha + 127) / 255; /* G */
                    dst_pixel[2] = (r * new_alpha + 127) / 255; /* R */
                    dst_pixel[3] = new_alpha;                   /* A */
                } else {
                    /* Source was transparent */
                    dst_pixel[0] = 0;
                    dst_pixel[1] = 0;
                    dst_pixel[2] = 0;
                    dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(new_layer->surface);
    cairo_surface_mark_dirty(source_layer->surface);

    /* Clear pixels from source layer where selection is active
     * For feathered selections, reduce alpha proportionally based on mask alpha */
    for (gint y = 0; y < source_layer->height; y++) {
        gint doc_y = source_layer->offset_y + y;
        gint mask_y = doc_y - actual_region.y;

        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue;
        }

        for (gint x = 0; x < source_layer->width; x++) {
            gint doc_x = source_layer->offset_x + x;
            gint mask_x = doc_x - actual_region.x;

            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue;
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha > 0) {
                /* Cairo ARGB32 format: BGRA in memory (little-endian) */
                guchar* src_pixel = src_data + y * src_stride + x * 4;

                /* Read BGRA from source */
                guchar src_b = src_pixel[0];
                guchar src_g = src_pixel[1];
                guchar src_r = src_pixel[2];
                guchar src_a = src_pixel[3];

                if (mask_alpha == 255) {
                    /* Fully selected: completely clear pixel */
                    src_pixel[0] = 0;
                    src_pixel[1] = 0;
                    src_pixel[2] = 0;
                    src_pixel[3] = 0;
                } else {
                    /* Partially selected (feathered): reduce alpha proportionally
                     * New alpha = original_alpha * (1 - mask_alpha/255)
                     * Need to handle premultiplied alpha correctly */
                    uint8_t new_alpha = (uint8_t)((src_a * (255 - mask_alpha)) / 255);

                    if (new_alpha < 1) {
                        /* Alpha becomes too small: clear completely */
                        src_pixel[0] = 0;
                        src_pixel[1] = 0;
                        src_pixel[2] = 0;
                        src_pixel[3] = 0;
                    } else if (src_a > 0) {
                        /* Un-premultiply source color, then re-premultiply with new alpha */
                        /* Un-premultiply: convert from premultiplied to straight alpha */
                        uint16_t r = (src_r * 255 + src_a / 2) / src_a;
                        uint16_t g = (src_g * 255 + src_a / 2) / src_a;
                        uint16_t b = (src_b * 255 + src_a / 2) / src_a;

                        /* Clamp to valid range */
                        if (r > 255)
                            r = 255;
                        if (g > 255)
                            g = 255;
                        if (b > 255)
                            b = 255;

                        /* Re-premultiply with new alpha */
                        src_pixel[0] = (b * new_alpha + 127) / 255; /* B */
                        src_pixel[1] = (g * new_alpha + 127) / 255; /* G */
                        src_pixel[2] = (r * new_alpha + 127) / 255; /* R */
                        src_pixel[3] = new_alpha;                   /* A */
                    } else {
                        /* Source was already transparent */
                        src_pixel[0] = 0;
                        src_pixel[1] = 0;
                        src_pixel[2] = 0;
                        src_pixel[3] = 0;
                    }
                }
            }
        }
    }

    cairo_surface_mark_dirty(source_layer->surface);
    selection_mask_free(region_mask);

    /* Invalidate caches */
    layer_invalidate_cache(source_layer);
    layer_invalidate_cache(new_layer);

    return new_layer;
}

/**
 * Move Tool state
 */
typedef struct {
    gboolean is_dragging;               /* Currently dragging? */
    gboolean selection_extracted;       /* TRUE if we extracted selection to a new layer */
    gdouble start_widget_x;             /* Mouse down position X in widget coordinates */
    gdouble start_widget_y;             /* Mouse down position Y in widget coordinates */
    gint initial_offset_x;              /* Layer offset at drag start */
    gint initial_offset_y;              /* Layer offset at drag start */
    gint last_offset_x;                 /* Last known offset (for dirty rect tracking) */
    gint last_offset_y;                 /* Last known offset (for dirty rect tracking) */
    struct ImageDocument* doc;          /* Document reference for coordinate conversion */
    struct ImageLayer* active_layer;    /* Layer being moved */
    struct ImageLayer* original_layer;  /* Original layer before extraction (for clearing) */
    struct ImageLayer* extracted_layer; /* Extracted layer (for command creation) */
    cairo_surface_t* original_snapshot; /* Snapshot of original layer BEFORE extraction (for undo) */
} MoveToolState;

/**
 * Move tool: mouse down - start dragging
 */
static void move_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(MoveToolState));
    }
    state = (MoveToolState*)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer) {
        // printf("Move tool: no selected layer\n");
        return;
    }

    /* Check if we have a selection - if so, extract it to a new layer */
    state->selection_extracted = FALSE;
    state->original_layer = active_layer;
    state->original_snapshot = NULL;

    if (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        /* Take snapshot of original layer BEFORE extraction (for undo) */
        if (active_layer->surface) {
            state->original_snapshot = create_surface_snapshot(active_layer->surface);
        }

        ImageLayer* extracted_layer = extract_selection_to_layer(doc, active_layer);
        if (extracted_layer) {
            /* Add new layer to document */
            doc->layers = g_list_append(doc->layers, extracted_layer);

            /* Store reference to command that will be created (will be finalized in mouse_up) */
            /* The command will be created and pushed in mouse_up after moving is complete */

            /* Set the extracted layer as active */
            document_set_selected_layer(doc, extracted_layer);

            /* Update layers panel if available */
            if (tool->app_context) {
                /* app_context is AppContext*, not a GObject, so we need to get window from it */
                /* We need to include ui.h to access AppContext, but to avoid circular deps,
                 * we'll get the window from the document's drawing_area instead */
                LayersPanel* layers_panel = NULL;
                if (doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
                    /* Get window from drawing_area's parent hierarchy */
                    GtkWidget* widget = doc->drawing_area;
                    while (widget && !GTK_IS_WINDOW(widget)) {
                        widget = gtk_widget_get_parent(widget);
                    }
                    if (widget && GTK_IS_WINDOW(widget)) {
                        layers_panel = (LayersPanel*)g_object_get_data(
                            G_OBJECT(widget), "layers_panel");
                    }
                }
                if (layers_panel) {
                    layers_panel_update(layers_panel, doc);
                    layers_panel_select_layer(layers_panel, doc, extracted_layer);
                }
            }

            /* Use the extracted layer for moving */
            active_layer = extracted_layer;
            state->selection_extracted = TRUE;
            state->extracted_layer = extracted_layer;

            /* Invalidate composite */
            document_invalidate_composite(doc);
        }
    }

    /* Start dragging - convert document coordinates back to widget coordinates for accurate delta calculation */
    state->is_dragging = TRUE;
    state->start_widget_x = (gdouble)event->x * doc->zoom_factor;
    state->start_widget_y = (gdouble)event->y * doc->zoom_factor;
    state->doc = doc;
    state->initial_offset_x = active_layer->offset_x;
    state->initial_offset_y = active_layer->offset_y;
    state->last_offset_x = active_layer->offset_x;
    state->last_offset_y = active_layer->offset_y;
    state->active_layer = active_layer;

    // printf("Move tool: started dragging layer at (%d, %d)\n", event->x, event->y);
}

/**
 * Move tool: mouse move - update layer offset
 * Optimized to only invalidate old and new regions
 */
static void move_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    gint dx, dy;
    gint old_x, old_y, new_x, new_y;
    DirtyRect old_rect, new_rect, union_rect;
    gint brush_margin = 2; /* Small margin for anti-aliasing */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState*)tool->user_data;

    if (!state->is_dragging || !state->active_layer) {
        return;
    }

    /* Convert current position to widget coordinates and calculate delta in widget space
     * This avoids accumulating rounding errors from coordinate conversion */
    gdouble current_widget_x = (gdouble)event->x * doc->zoom_factor;
    gdouble current_widget_y = (gdouble)event->y * doc->zoom_factor;
    gdouble delta_widget_x = current_widget_x - state->start_widget_x;
    gdouble delta_widget_y = current_widget_y - state->start_widget_y;

    /* Convert delta to document coordinates with proper rounding */
    gdouble delta_doc_x = delta_widget_x / doc->zoom_factor;
    gdouble delta_doc_y = delta_widget_y / doc->zoom_factor;

    /* Calculate new position with proper rounding */
    new_x = state->initial_offset_x + (gint)(delta_doc_x + 0.5);
    new_y = state->initial_offset_y + (gint)(delta_doc_y + 0.5);

    /* Get old position (from last update) */
    old_x = state->last_offset_x;
    old_y = state->last_offset_y;

    /* If position hasn't changed, do nothing */
    if (old_x == new_x && old_y == new_y) {
        return;
    }

    /* Calculate old region (where layer was last frame) */
    dirty_rect_set(&old_rect, old_x, old_y,
                   state->active_layer->width,
                   state->active_layer->height);
    dirty_rect_clamp(&old_rect, doc->width, doc->height);

    /* Calculate new region (where layer is now) */
    dirty_rect_set(&new_rect, new_x, new_y,
                   state->active_layer->width,
                   state->active_layer->height);
    dirty_rect_clamp(&new_rect, doc->width, doc->height);

    /* Union both regions */
    dirty_rect_union(&old_rect, &new_rect, &union_rect);

    /* Update layer offset */
    state->active_layer->offset_x = new_x;
    state->active_layer->offset_y = new_y;

    /* Update last known position */
    state->last_offset_x = new_x;
    state->last_offset_y = new_y;

    /* Only invalidate the union of old and new regions */
    if (!dirty_rect_is_empty(&union_rect)) {
        document_invalidate_region(doc, &union_rect);
    }

    // printf("Move tool: moved to offset (%d, %d)\n",
    //        state->active_layer->offset_x, state->active_layer->offset_y);
}

/**
 * Move tool: mouse up - end dragging and create undo command
 */
static void move_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    MoveToolState* state;
    Command* cmd;
    AppContext* ctx;

    (void)event; /* Unused */

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (MoveToolState*)tool->user_data;

    if (!state->is_dragging) {
        return;
    }

    /* Check if we actually moved */
    if (state->active_layer &&
        (state->active_layer->offset_x != state->initial_offset_x ||
         state->active_layer->offset_y != state->initial_offset_y)) {

        /* Create undo command for the move */
        /* Use move_selected_pixels command if selection was extracted, otherwise use regular move command */
        if (state->selection_extracted && state->extracted_layer) {
            cmd = command_create_move_selected_pixels_with_snapshot(
                doc,
                state->extracted_layer,
                state->original_layer,
                state->initial_offset_x,
                state->initial_offset_y,
                state->active_layer->offset_x,
                state->active_layer->offset_y,
                state->original_snapshot);
            /* Transfer ownership of snapshot to command */
            state->original_snapshot = NULL;
        } else {
            cmd = command_create_move(
                state->active_layer,
                state->initial_offset_x,
                state->initial_offset_y,
                state->active_layer->offset_x,
                state->active_layer->offset_y);
        }

        if (cmd && doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);
            // printf("Move tool: move command pushed to undo stack\n");

            /* Execute command to apply it (clears selection, etc.) */
            /* This is needed because commands are not auto-executed when pushed */
            command_execute(cmd, doc);

            /* Clear redo stack since new action performed */
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }

            /* Update UI */
            ctx = (AppContext*)tool->app_context;
            if (ctx) {
                ui_update_menu_and_button_states(ctx);
                ui_update_window_title(ctx);
            }
        } else if (state->original_snapshot) {
            /* Command creation failed, free the snapshot */
            cairo_surface_destroy(state->original_snapshot);
            state->original_snapshot = NULL;
        }

        /* Layer was moved - mark document as modified */
        doc->modified = TRUE;
        // printf("Move tool: layer moved - document marked as modified\n");
    }

    /* Clean up snapshot if not used (shouldn't happen, but be safe) */
    if (state->original_snapshot) {
        cairo_surface_destroy(state->original_snapshot);
        state->original_snapshot = NULL;
    }

    state->is_dragging = FALSE;
    state->selection_extracted = FALSE;
    state->active_layer = NULL;
    state->original_layer = NULL;
    state->extracted_layer = NULL;

    // printf("Move tool: finished dragging\n");
}

/**
 * Create the Move Tool
 */
Tool* tool_move_create(void) {
    Tool* tool;

    /* Move tool doesn't have size/opacity/hardness options */
    tool = tool_new("Move", TOOL_MOVE, GDK_FLEUR, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = move_tool_mouse_down;
    tool->mouse_move = move_tool_mouse_move;
    tool->mouse_up = move_tool_mouse_up;

    // printf("Move tool created\n");

    return tool;
}
