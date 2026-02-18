#include "ui/ui_edit_menu.h"
#include "command.h"
#include "commands/command_layer.h"
#include "document.h"
#include "render/blend.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "ui.h"
#include "ui/dialogs/fill_dialog.h"
#include "ui/layers_panel.h"
#include <cairo/cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <stdint.h>

/* Forward declarations for helper functions */
static cairo_surface_t* extract_pixels_for_copy(ImageDocument* doc, ImageLayer* layer);
static cairo_surface_t* create_merged_surface(ImageDocument* doc);
static cairo_surface_t* extract_merged_pixels_for_copy(ImageDocument* doc);

/**
 * Edit > Undo callback
 */
void on_edit_undo(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;

    if (!ctx) {
        return;
    }

    /* Get current document */
    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    /* Perform undo */
    document_undo(doc);

    /* Invalidate document for redraw (marks tiles dirty) */
    document_invalidate_composite(doc);

    /* Update layers panel */
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update menu state and window title */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
}

/**
 * Edit > Redo callback
 */
void on_edit_redo(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;

    if (!ctx) {
        return;
    }

    /* Get current document */
    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    /* Perform redo */
    document_redo(doc);

    /* Invalidate document for redraw (marks tiles dirty) */
    document_invalidate_composite(doc);

    /* Update layers panel */
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update menu state and window title */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
}

/**
 * Helper function to extract pixels from a layer for copying
 * Returns a cairo surface with the pixels to copy (selection or entire layer)
 */
static cairo_surface_t* extract_pixels_for_copy(ImageDocument* doc, ImageLayer* layer) {
    if (!doc || !layer || !layer->surface) {
        return NULL;
    }

    gint width = layer->width;
    gint height = layer->height;

    /* Check if there's a selection */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (!has_selection) {
        /* No selection: copy entire layer */
        cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (!copy) {
            return NULL;
        }

        cairo_t* cr = cairo_create(copy);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);

        return copy;
    }

    /* Has selection: extract selected pixels */
    /* Calculate intersection of layer bounds and selection bounds in document coordinates */
    gint layer_x_min = layer->offset_x;
    gint layer_y_min = layer->offset_y;
    gint layer_x_max = layer->offset_x + layer->width;
    gint layer_y_max = layer->offset_y + layer->height;

    /* Clamp to document bounds */
    layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
    layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
    layer_x_max = (layer_x_max > (gint)doc->width) ? (gint)doc->width : layer_x_max;
    layer_y_max = (layer_y_max > (gint)doc->height) ? (gint)doc->height : layer_y_max;

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

    if (!region_mask || !region_mask->data || dirty_rect_is_empty(&actual_region)) {
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

    /* Create surface with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!copy) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy pixels from source layer to copy, masked by selection */
    cairo_surface_flush(layer->surface);
    cairo_surface_flush(copy);

    guchar* src_data = cairo_image_surface_get_data(layer->surface);
    gint src_stride = cairo_image_surface_get_stride(layer->surface);
    guchar* dst_data = cairo_image_surface_get_data(copy);
    gint dst_stride = cairo_image_surface_get_stride(copy);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint src_y = doc_y - layer->offset_y;
        gint mask_y = doc_y - actual_region.y;

        if (src_y < 0 || src_y >= (gint)layer->height) {
            continue; /* Outside source layer */
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue; /* Outside mask */
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint src_x = doc_x - layer->offset_x;
            gint mask_x = doc_x - actual_region.x;

            if (src_x < 0 || src_x >= (gint)layer->width) {
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
            guchar* src_pixel = src_data + src_y * src_stride + src_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
                dst_pixel[3] = src_pixel[3];
            } else {
                /* Partially selected (feathered): apply mask to alpha */
                uint8_t src_a = src_pixel[3];
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply, then re-premultiply with new alpha */
                    uint16_t r = (src_pixel[2] * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_pixel[1] * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_pixel[0] * 255 + src_a / 2) / src_a;

                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    dst_pixel[0] = (b * new_alpha + 127) / 255;
                    dst_pixel[1] = (g * new_alpha + 127) / 255;
                    dst_pixel[2] = (r * new_alpha + 127) / 255;
                    dst_pixel[3] = new_alpha;
                } else {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(copy);
    selection_mask_free(region_mask);

    return copy;
}

/**
 * Helper function to create a merged surface from all visible layers
 * Returns a cairo surface with all visible layers composited, or NULL on error
 */
static cairo_surface_t* create_merged_surface(ImageDocument* doc) {
    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Create surface for merged result */
    cairo_surface_t* merged = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    if (!merged) {
        return NULL;
    }

    /* Composite all visible layers */
    cairo_t* cr = cairo_create(merged);
    if (!cr) {
        cairo_surface_destroy(merged);
        return NULL;
    }

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Composite each visible layer */
    gboolean is_first_layer = TRUE;
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;

        if (!layer || !layer->surface) {
            continue;
        }

        /* Skip invisible layers */
        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw layer with offset */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op;
        if (is_first_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(merged);

    return merged;
}

/**
 * Helper function to extract pixels from merged layers for copying
 * Returns a cairo surface with the pixels to copy (selection or entire merged image)
 */
static cairo_surface_t* extract_merged_pixels_for_copy(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    /* Create merged surface from all visible layers */
    cairo_surface_t* merged = create_merged_surface(doc);
    if (!merged) {
        return NULL;
    }

    gint width = doc->width;
    gint height = doc->height;

    /* Check if there's a selection */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (!has_selection) {
        /* No selection: return merged surface as-is */
        return merged;
    }

    /* Has selection: extract selected pixels from merged surface */
    DirtyRect doc_rect;
    dirty_rect_set(&doc_rect, 0, 0, width, height);

    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &doc_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data || dirty_rect_is_empty(&actual_region)) {
        cairo_surface_destroy(merged);
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels */
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
        cairo_surface_destroy(merged);
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create surface with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!copy) {
        cairo_surface_destroy(merged);
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy pixels from merged surface to copy, masked by selection */
    cairo_surface_flush(merged);
    cairo_surface_flush(copy);

    guchar* src_data = cairo_image_surface_get_data(merged);
    gint src_stride = cairo_image_surface_get_stride(merged);
    guchar* dst_data = cairo_image_surface_get_data(copy);
    gint dst_stride = cairo_image_surface_get_stride(copy);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint mask_y = doc_y - actual_region.y;

        if (doc_y < 0 || doc_y >= height) {
            continue;
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue;
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint mask_x = doc_x - actual_region.x;

            if (doc_x < 0 || doc_x >= width) {
                continue;
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue;
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from merged to destination */
            guchar* src_pixel = src_data + doc_y * src_stride + doc_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
                dst_pixel[3] = src_pixel[3];
            } else {
                /* Partially selected (feathered): apply mask to alpha */
                uint8_t src_a = src_pixel[3];
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply, then re-premultiply with new alpha */
                    uint16_t r = (src_pixel[2] * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_pixel[1] * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_pixel[0] * 255 + src_a / 2) / src_a;

                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    dst_pixel[0] = (b * new_alpha + 127) / 255;
                    dst_pixel[1] = (g * new_alpha + 127) / 255;
                    dst_pixel[2] = (r * new_alpha + 127) / 255;
                    dst_pixel[3] = new_alpha;
                } else {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(copy);
    cairo_surface_destroy(merged);
    selection_mask_free(region_mask);

    return copy;
}

/**
 * Edit > Copy callback
 */
void on_edit_copy(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    cairo_surface_t* copy_surface;
    GdkPixbuf* pixbuf;
    GtkClipboard* clipboard;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    layer = document_get_selected_layer(doc);
    if (!layer || !layer->surface) {
        return;
    }

    /* Extract pixels to copy */
    copy_surface = extract_pixels_for_copy(doc, layer);
    if (!copy_surface) {
        return;
    }

    /* Convert to pixbuf */
    pixbuf = cairo_surface_to_pixbuf(copy_surface, TRUE);
    cairo_surface_destroy(copy_surface);

    if (!pixbuf) {
        return;
    }

    /* Copy to clipboard */
    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clipboard) {
        gtk_clipboard_set_image(clipboard, pixbuf);
    }

    g_object_unref(pixbuf);
}

/**
 * Edit > Cut callback
 */
void on_edit_cut(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    cairo_surface_t* copy_surface;
    GdkPixbuf* pixbuf;
    GtkClipboard* clipboard;
    Command* cmd;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    layer = document_get_selected_layer(doc);
    if (!layer || !layer->surface) {
        return;
    }

    /* First, copy to clipboard (same as copy operation) */
    copy_surface = extract_pixels_for_copy(doc, layer);
    if (!copy_surface) {
        return;
    }

    pixbuf = cairo_surface_to_pixbuf(copy_surface, TRUE);
    cairo_surface_destroy(copy_surface);

    if (!pixbuf) {
        return;
    }

    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clipboard) {
        gtk_clipboard_set_image(clipboard, pixbuf);
    }
    g_object_unref(pixbuf);

    /* Now create undo command and clear pixels */
    /* Create a draw command to track the cut operation */
    cmd = command_create_draw(layer, "Cut");
    if (!cmd) {
        return;
    }

    /* Clear pixels from layer */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (has_selection) {
        /* Clear selected pixels */
        DirtyRect layer_rect;
        dirty_rect_set(&layer_rect, layer->offset_x, layer->offset_y, layer->width, layer->height);

        DirtyRect actual_region;
        SelectionMask* region_mask = selection_build_combined_mask(
            doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

        if (region_mask && region_mask->data) {
            cairo_surface_flush(layer->surface);
            guchar* layer_data = cairo_image_surface_get_data(layer->surface);
            gint layer_stride = cairo_image_surface_get_stride(layer->surface);

            for (gint y = 0; y < (gint)layer->height; y++) {
                gint doc_y = layer->offset_y + y;
                gint mask_y = doc_y - actual_region.y;

                if (mask_y < 0 || mask_y >= region_mask->height) {
                    continue;
                }

                for (gint x = 0; x < (gint)layer->width; x++) {
                    gint doc_x = layer->offset_x + x;
                    gint mask_x = doc_x - actual_region.x;

                    if (mask_x < 0 || mask_x >= region_mask->width) {
                        continue;
                    }

                    uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
                    if (mask_alpha > 0) {
                        guchar* pixel = layer_data + y * layer_stride + x * 4;

                        if (mask_alpha == 255) {
                            /* Fully selected: clear completely */
                            pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                        } else {
                            /* Partially selected: reduce alpha */
                            uint8_t src_a = pixel[3];
                            uint8_t new_alpha = (uint8_t)((src_a * (255 - mask_alpha)) / 255);

                            if (new_alpha < 1) {
                                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                            } else if (src_a > 0) {
                                /* Un-premultiply, then re-premultiply */
                                uint16_t r = (pixel[2] * 255 + src_a / 2) / src_a;
                                uint16_t g = (pixel[1] * 255 + src_a / 2) / src_a;
                                uint16_t b = (pixel[0] * 255 + src_a / 2) / src_a;

                                if (r > 255)
                                    r = 255;
                                if (g > 255)
                                    g = 255;
                                if (b > 255)
                                    b = 255;

                                pixel[0] = (b * new_alpha + 127) / 255;
                                pixel[1] = (g * new_alpha + 127) / 255;
                                pixel[2] = (r * new_alpha + 127) / 255;
                                pixel[3] = new_alpha;
                            } else {
                                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                            }
                        }
                    }
                }
            }

            cairo_surface_mark_dirty(layer->surface);
            selection_mask_free(region_mask);
        }
    } else {
        /* No selection: clear entire layer */
        cairo_t* cr = cairo_create(layer->surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_mark_dirty(layer->surface);
    }

    /* Finalize command and push to undo stack */
    if (command_finalize_draw(cmd)) {
        command_execute(cmd, doc);
        command_stack_push(doc->undo_stack, cmd);
        command_stack_clear(doc->redo_stack); /* Clear redo stack */
    } else {
        command_free(cmd);
    }

    /* Invalidate document and update UI */
    layer_invalidate_cache(layer);
    document_invalidate_composite(doc);

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Edit > Clear callback
 */
void on_edit_clear(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    Command* cmd;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    layer = document_get_selected_layer(doc);
    if (!layer || !layer->surface) {
        return;
    }

    /* Create a draw command to track the clear operation */
    cmd = command_create_draw(layer, "Clear");
    if (!cmd) {
        return;
    }

    /* Clear pixels from layer */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (has_selection) {
        /* Clear selected pixels */
        DirtyRect layer_rect;
        dirty_rect_set(&layer_rect, layer->offset_x, layer->offset_y, layer->width, layer->height);

        DirtyRect actual_region;
        SelectionMask* region_mask = selection_build_combined_mask(
            doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

        if (region_mask && region_mask->data) {
            cairo_surface_flush(layer->surface);
            guchar* layer_data = cairo_image_surface_get_data(layer->surface);
            gint layer_stride = cairo_image_surface_get_stride(layer->surface);

            for (gint y = 0; y < (gint)layer->height; y++) {
                gint doc_y = layer->offset_y + y;
                gint mask_y = doc_y - actual_region.y;

                if (mask_y < 0 || mask_y >= region_mask->height) {
                    continue;
                }

                for (gint x = 0; x < (gint)layer->width; x++) {
                    gint doc_x = layer->offset_x + x;
                    gint mask_x = doc_x - actual_region.x;

                    if (mask_x < 0 || mask_x >= region_mask->width) {
                        continue;
                    }

                    uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
                    if (mask_alpha > 0) {
                        guchar* pixel = layer_data + y * layer_stride + x * 4;

                        if (mask_alpha == 255) {
                            /* Fully selected: clear completely */
                            pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                        } else {
                            /* Partially selected: reduce alpha */
                            uint8_t src_a = pixel[3];
                            uint8_t new_alpha = (uint8_t)((src_a * (255 - mask_alpha)) / 255);

                            if (new_alpha < 1) {
                                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                            } else if (src_a > 0) {
                                /* Un-premultiply, then re-premultiply */
                                uint16_t r = (pixel[2] * 255 + src_a / 2) / src_a;
                                uint16_t g = (pixel[1] * 255 + src_a / 2) / src_a;
                                uint16_t b = (pixel[0] * 255 + src_a / 2) / src_a;

                                if (r > 255)
                                    r = 255;
                                if (g > 255)
                                    g = 255;
                                if (b > 255)
                                    b = 255;

                                pixel[0] = (b * new_alpha + 127) / 255;
                                pixel[1] = (g * new_alpha + 127) / 255;
                                pixel[2] = (r * new_alpha + 127) / 255;
                                pixel[3] = new_alpha;
                            } else {
                                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                            }
                        }
                    }
                }
            }

            cairo_surface_mark_dirty(layer->surface);
            selection_mask_free(region_mask);
        }
    } else {
        /* No selection: clear entire layer */
        cairo_t* cr = cairo_create(layer->surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_mark_dirty(layer->surface);
    }

    /* Finalize command and push to undo stack */
    if (command_finalize_draw(cmd)) {
        command_execute(cmd, doc);
        command_stack_push(doc->undo_stack, cmd);
        command_stack_clear(doc->redo_stack); /* Clear redo stack */
    } else {
        command_free(cmd);
    }

    /* Invalidate document and update UI */
    layer_invalidate_cache(layer);
    document_invalidate_composite(doc);

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Edit > Paste callback
 */
void on_edit_paste(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    GtkClipboard* clipboard;
    GdkPixbuf* pixbuf;
    cairo_surface_t* surface;
    ImageLayer* new_layer;
    Command* cmd;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    /* Get image from clipboard */
    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (!clipboard) {
        return;
    }

    pixbuf = gtk_clipboard_wait_for_image(clipboard);
    if (!pixbuf) {
        /* No valid image in clipboard - notify user */
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "No valid image found in clipboard.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    /* Convert pixbuf to cairo surface */
    surface = pixbuf_to_cairo_surface(pixbuf);
    g_object_unref(pixbuf);

    if (!surface) {
        return;
    }

    /* Ensure surface is properly formatted and flushed */
    cairo_surface_flush(surface);
    cairo_format_t format = cairo_image_surface_get_format(surface);

    /* Create new layer from clipboard image */
    gint width = cairo_image_surface_get_width(surface);
    gint height = cairo_image_surface_get_height(surface);

    new_layer = layer_new("Clipboard Image", width, height, TRUE,
                          LAYER_BACKGROUND_TRANSPARENT,
                          LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!new_layer) {
        cairo_surface_destroy(surface);
        return;
    }

    /* Copy surface to layer with proper alpha handling */
    /* Use direct pixel copy like Move tool does, but need to premultiply alpha */
    /* because pixbuf has straight alpha but Cairo surfaces use premultiplied alpha */
    cairo_surface_flush(surface);
    cairo_surface_flush(new_layer->surface);

    guchar* src_data = cairo_image_surface_get_data(surface);
    gint src_stride = cairo_image_surface_get_stride(surface);
    guchar* dst_data = cairo_image_surface_get_data(new_layer->surface);
    gint dst_stride = cairo_image_surface_get_stride(new_layer->surface);

    /* Copy pixel data and premultiply alpha (pixbuf has straight alpha, Cairo needs premultiplied) */
    /* Cairo ARGB32 format: BGRA in memory (little-endian) */
    for (gint y = 0; y < height; y++) {
        guchar* src_row = src_data + y * src_stride;
        guchar* dst_row = dst_data + y * dst_stride;

        for (gint x = 0; x < width; x++) {
            guchar* src_pixel = src_row + x * 4;
            guchar* dst_pixel = dst_row + x * 4;

            /* Read BGRA from source (straight alpha from pixbuf conversion) */
            guchar src_b = src_pixel[0];
            guchar src_g = src_pixel[1];
            guchar src_r = src_pixel[2];
            guchar src_a = src_pixel[3];

            if (src_a == 0) {
                /* Fully transparent */
                dst_pixel[0] = 0;
                dst_pixel[1] = 0;
                dst_pixel[2] = 0;
                dst_pixel[3] = 0;
            } else if (src_a == 255) {
                /* Fully opaque - no premultiplication needed */
                dst_pixel[0] = src_b;
                dst_pixel[1] = src_g;
                dst_pixel[2] = src_r;
                dst_pixel[3] = src_a;
            } else {
                /* Partially transparent - premultiply alpha */
                dst_pixel[0] = (src_b * src_a + 127) / 255; /* B */
                dst_pixel[1] = (src_g * src_a + 127) / 255; /* G */
                dst_pixel[2] = (src_r * src_a + 127) / 255; /* R */
                dst_pixel[3] = src_a;                       /* A */
            }
        }
    }

    cairo_surface_mark_dirty(new_layer->surface);
    cairo_surface_destroy(surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, new_layer);
    document_set_selected_layer(doc, new_layer);

    /* Create paste command (not layer add command) */
    cmd = command_create_paste(doc, new_layer);
    if (cmd) {
        command_execute(cmd, doc);
        command_stack_push(doc->undo_stack, cmd);
        command_stack_clear(doc->redo_stack); /* Clear redo stack */
    }

    /* Update UI */
    document_invalidate_composite(doc);

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Edit > Paste to New Image callback
 */
void on_edit_paste_new_image(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    GtkClipboard* clipboard;
    GdkPixbuf* pixbuf;
    cairo_surface_t* surface;
    ImageDocument* new_doc;
    ImageLayer* new_layer;
    LayersPanel* layers_panel;

    if (!ctx) {
        return;
    }

    /* Get image from clipboard */
    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (!clipboard) {
        return;
    }

    pixbuf = gtk_clipboard_wait_for_image(clipboard);
    if (!pixbuf) {
        /* No valid image in clipboard - notify user */
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "No valid image found in clipboard.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    /* Get image dimensions */
    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);

    if (width <= 0 || height <= 0) {
        g_object_unref(pixbuf);
        return;
    }

    /* Create new document tab */
    new_doc = ui_create_document_tab(ctx, "Clipboard Image");
    if (!new_doc) {
        g_object_unref(pixbuf);
        return;
    }

    /* Set document dimensions */
    new_doc->width = (guint)width;
    new_doc->height = (guint)height;
    new_doc->channels = gdk_pixbuf_get_n_channels(pixbuf);
    new_doc->bit_depth = 8; /* GdkPixbuf always uses 8 bits per channel */
    new_doc->has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    /* Create tile grid */
    if (new_doc->tile_grid) {
        tile_grid_free(new_doc->tile_grid);
    }
    new_doc->tile_grid = tile_grid_create(new_doc->width, new_doc->height, 128);
    if (!new_doc->tile_grid) {
        g_warning("Failed to create tile grid");
        g_object_unref(pixbuf);
        return;
    }

    /* Create selection mask */
    if (new_doc->selection_mask) {
        selection_mask_free(new_doc->selection_mask);
    }
    new_doc->selection_mask = selection_mask_new(new_doc->width, new_doc->height);
    if (!new_doc->selection_mask) {
        g_warning("Failed to create selection mask");
        g_object_unref(pixbuf);
        return;
    }

    /* Convert pixbuf to cairo surface */
    surface = pixbuf_to_cairo_surface(pixbuf);
    g_object_unref(pixbuf);

    if (!surface) {
        return;
    }

    /* Ensure surface is properly formatted and flushed */
    cairo_surface_flush(surface);

    /* Create new layer from clipboard image */
    new_layer = layer_new("Clipboard Image", width, height, TRUE,
                          LAYER_BACKGROUND_TRANSPARENT,
                          LAYER_POSITION_ABOVE_CURRENT, NULL, new_doc);
    if (!new_layer) {
        cairo_surface_destroy(surface);
        return;
    }

    /* Copy surface to layer with proper alpha handling */
    /* Use direct pixel copy like Paste does, premultiplying alpha */
    cairo_surface_flush(surface);
    cairo_surface_flush(new_layer->surface);

    guchar* src_data = cairo_image_surface_get_data(surface);
    gint src_stride = cairo_image_surface_get_stride(surface);
    guchar* dst_data = cairo_image_surface_get_data(new_layer->surface);
    gint dst_stride = cairo_image_surface_get_stride(new_layer->surface);

    /* Copy pixel data and premultiply alpha (pixbuf has straight alpha, Cairo needs premultiplied) */
    /* Cairo ARGB32 format: BGRA in memory (little-endian) */
    for (gint y = 0; y < height; y++) {
        guchar* src_row = src_data + y * src_stride;
        guchar* dst_row = dst_data + y * dst_stride;

        for (gint x = 0; x < width; x++) {
            guchar* src_pixel = src_row + x * 4;
            guchar* dst_pixel = dst_row + x * 4;

            /* Read BGRA from source (straight alpha from pixbuf conversion) */
            guchar src_b = src_pixel[0];
            guchar src_g = src_pixel[1];
            guchar src_r = src_pixel[2];
            guchar src_a = src_pixel[3];

            if (src_a == 0) {
                /* Fully transparent */
                dst_pixel[0] = 0;
                dst_pixel[1] = 0;
                dst_pixel[2] = 0;
                dst_pixel[3] = 0;
            } else if (src_a == 255) {
                /* Fully opaque - no premultiplication needed */
                dst_pixel[0] = src_b;
                dst_pixel[1] = src_g;
                dst_pixel[2] = src_r;
                dst_pixel[3] = src_a;
            } else {
                /* Partially transparent - premultiply alpha */
                dst_pixel[0] = (src_b * src_a + 127) / 255; /* B */
                dst_pixel[1] = (src_g * src_a + 127) / 255; /* G */
                dst_pixel[2] = (src_r * src_a + 127) / 255; /* R */
                dst_pixel[3] = src_a;                       /* A */
            }
        }
    }

    cairo_surface_mark_dirty(new_layer->surface);
    cairo_surface_destroy(surface);

    /* Add layer to document */
    new_doc->layers = g_list_append(new_doc->layers, new_layer);
    document_set_selected_layer(new_doc, new_layer);

    /* Update drawing area size to match document dimensions */
    if (new_doc->drawing_area) {
        gint display_width = (gint)(new_doc->width * new_doc->zoom_factor);
        gint display_height = (gint)(new_doc->height * new_doc->zoom_factor);
        gtk_widget_set_size_request(new_doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(new_doc->drawing_area);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(new_doc);

    /* Update layers panel */
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, new_doc);
    }

    /* Mark document as modified */
    new_doc->modified = TRUE;

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    ui_update_status_bar(ctx, NULL);
}

/**
 * Edit > Copy Merged callback
 */
void on_edit_copy_merged(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    cairo_surface_t* copy_surface;
    GdkPixbuf* pixbuf;
    GtkClipboard* clipboard;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    /* Extract pixels from merged layers */
    copy_surface = extract_merged_pixels_for_copy(doc);
    if (!copy_surface) {
        return;
    }

    /* Convert to pixbuf */
    pixbuf = cairo_surface_to_pixbuf(copy_surface, TRUE);
    cairo_surface_destroy(copy_surface);

    if (!pixbuf) {
        return;
    }

    /* Copy to clipboard */
    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clipboard) {
        gtk_clipboard_set_image(clipboard, pixbuf);
    }

    g_object_unref(pixbuf);
}

/**
 * Edit > Cut Merged callback
 */
void on_edit_cut_merged(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    cairo_surface_t* copy_surface;
    GdkPixbuf* pixbuf;
    GtkClipboard* clipboard;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    /* First, copy merged layers to clipboard */
    copy_surface = extract_merged_pixels_for_copy(doc);
    if (!copy_surface) {
        return;
    }

    pixbuf = cairo_surface_to_pixbuf(copy_surface, TRUE);
    cairo_surface_destroy(copy_surface);

    if (!pixbuf) {
        return;
    }

    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clipboard) {
        gtk_clipboard_set_image(clipboard, pixbuf);
    }
    g_object_unref(pixbuf);

    /* Now create undo commands and clear pixels from all visible layers */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));
    GList* commands = NULL; /* List to collect commands before pushing */

    /* Iterate through all visible layers and cut from each */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* current_layer = (ImageLayer*)iter->data;

        if (!current_layer || !current_layer->surface) {
            continue;
        }

        /* Skip invisible layers */
        if (!current_layer->visible || current_layer->opacity <= 0.0) {
            continue;
        }

        /* Create a draw command to track the cut merged operation for this layer */
        Command* layer_cmd = command_create_draw(current_layer, "Cut Merged");
        if (!layer_cmd) {
            continue;
        }

        /* Clear pixels from this layer */
        if (has_selection) {
            /* Clear selected pixels */
            DirtyRect layer_rect;
            dirty_rect_set(&layer_rect, current_layer->offset_x, current_layer->offset_y,
                           current_layer->width, current_layer->height);

            DirtyRect actual_region;
            SelectionMask* region_mask = selection_build_combined_mask(
                doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

            if (region_mask && region_mask->data) {
                cairo_surface_flush(current_layer->surface);
                guchar* layer_data = cairo_image_surface_get_data(current_layer->surface);
                gint layer_stride = cairo_image_surface_get_stride(current_layer->surface);

                for (gint y = 0; y < (gint)current_layer->height; y++) {
                    gint doc_y = current_layer->offset_y + y;
                    gint mask_y = doc_y - actual_region.y;

                    if (mask_y < 0 || mask_y >= region_mask->height) {
                        continue;
                    }

                    for (gint x = 0; x < (gint)current_layer->width; x++) {
                        gint doc_x = current_layer->offset_x + x;
                        gint mask_x = doc_x - actual_region.x;

                        if (mask_x < 0 || mask_x >= region_mask->width) {
                            continue;
                        }

                        uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
                        if (mask_alpha > 0) {
                            guchar* pixel = layer_data + y * layer_stride + x * 4;

                            if (mask_alpha == 255) {
                                /* Fully selected: clear completely */
                                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                            } else {
                                /* Partially selected: reduce alpha */
                                uint8_t src_a = pixel[3];
                                uint8_t new_alpha = (uint8_t)((src_a * (255 - mask_alpha)) / 255);

                                if (new_alpha < 1) {
                                    pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                                } else if (src_a > 0) {
                                    /* Un-premultiply, then re-premultiply */
                                    uint16_t r = (pixel[2] * 255 + src_a / 2) / src_a;
                                    uint16_t g = (pixel[1] * 255 + src_a / 2) / src_a;
                                    uint16_t b = (pixel[0] * 255 + src_a / 2) / src_a;

                                    if (r > 255)
                                        r = 255;
                                    if (g > 255)
                                        g = 255;
                                    if (b > 255)
                                        b = 255;

                                    pixel[0] = (b * new_alpha + 127) / 255;
                                    pixel[1] = (g * new_alpha + 127) / 255;
                                    pixel[2] = (r * new_alpha + 127) / 255;
                                    pixel[3] = new_alpha;
                                } else {
                                    pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                                }
                            }
                        }
                    }
                }

                cairo_surface_mark_dirty(current_layer->surface);
                selection_mask_free(region_mask);
            }
        } else {
            /* No selection: clear entire layer */
            cairo_t* cr = cairo_create(current_layer->surface);
            cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
            cairo_paint(cr);
            cairo_destroy(cr);
            cairo_surface_mark_dirty(current_layer->surface);
        }

        /* Finalize command and add to list */
        if (command_finalize_draw(layer_cmd)) {
            command_execute(layer_cmd, doc);
            commands = g_list_prepend(commands, layer_cmd);
        } else {
            command_free(layer_cmd);
        }

        /* Invalidate layer cache */
        layer_invalidate_cache(current_layer);
    }

    /* Push all commands to undo stack (in reverse order so top layer is undone first) */
    for (GList* l = commands; l; l = l->next) {
        Command* cmd_to_push = (Command*)l->data;
        command_stack_push(doc->undo_stack, cmd_to_push);
    }
    g_list_free(commands);                /* Free list, but not commands (they're on the stack now) */
    command_stack_clear(doc->redo_stack); /* Clear redo stack */

    /* Invalidate document composite */
    document_invalidate_composite(doc);

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Edit > Fill callback: show fill dialog and fill layer (or selection) with chosen color/opacity/blend.
 */
static void on_edit_fill(GtkWidget* widget, gpointer data) {
    (void)widget;

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    FillDialogResult fill_result;
    Command* cmd;
    gdouble r, g, b;
    guint8 fill_a_byte;
    guint8 opacity_byte;

    if (!ctx) return;

    doc = ui_get_active_document(ctx);
    if (!doc) return;

    layer = document_get_selected_layer(doc);
    if (!layer || !layer->surface) return;

    if (!fill_dialog_run(GTK_WINDOW(ctx->window), &fill_result))
        return;

    r = fill_result.color.red;
    g = fill_result.color.green;
    b = fill_result.color.blue;
    opacity_byte = (guint8)((fill_result.opacity * 255) / 100);
    if (opacity_byte > 255) opacity_byte = 255;

    cmd = command_create_draw(layer, "Fill");
    if (!cmd) return;

    cairo_surface_flush(layer->surface);
    guchar* layer_data = cairo_image_surface_get_data(layer->surface);
    gint layer_stride = cairo_image_surface_get_stride(layer->surface);
    gint width = (gint)layer->width;
    gint height = (gint)layer->height;

    /* Premultiplied fill color: fill_a_byte is the opacity we apply */
    fill_a_byte = opacity_byte;
    guint8 fr = (guint8)(r * fill_a_byte + 0.5);
    guint8 fg = (guint8)(g * fill_a_byte + 0.5);
    guint8 fb = (guint8)(b * fill_a_byte + 0.5);
    if (fr > 255) fr = 255;
    if (fg > 255) fg = 255;
    if (fb > 255) fb = 255;

    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (has_selection) {
        DirtyRect layer_rect;
        dirty_rect_set(&layer_rect, layer->offset_x, layer->offset_y, layer->width, layer->height);
        DirtyRect actual_region;
        SelectionMask* region_mask = selection_build_combined_mask(
            doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

        if (region_mask && region_mask->data) {
            guint32* fill_row = (guint32*)g_malloc((size_t)width * sizeof(guint32));
            if (fill_row) {
                for (gint y = 0; y < height; y++) {
                    gint doc_y = layer->offset_y + y;
                    gint mask_y = doc_y - actual_region.y;

                    for (gint x = 0; x < width; x++) {
                        gint doc_x = layer->offset_x + x;
                        gint mask_x = doc_x - actual_region.x;
                        uint8_t ma = 0;
                        if (mask_y >= 0 && mask_y < region_mask->height && mask_x >= 0 && mask_x < region_mask->width)
                            ma = region_mask->data[mask_y * region_mask->stride + mask_x];
                        uint8_t fa = (uint8_t)((fill_a_byte * ma) / 255);
                        uint8_t pr = (uint8_t)(r * fa + 0.5);
                        uint8_t pg = (uint8_t)(g * fa + 0.5);
                        uint8_t pb = (uint8_t)(b * fa + 0.5);
                        if (pr > 255) pr = 255;
                        if (pg > 255) pg = 255;
                        if (pb > 255) pb = 255;
                        fill_row[x] = ((guint32)fa << 24) | ((guint32)pr << 16) | ((guint32)pg << 8) | pb;
                    }
                    guint32* dst_row = (guint32*)(layer_data + (gint64)y * layer_stride);
                    blend_composite_row(fill_row, dst_row, width, 0, y, 255, fill_result.blend_mode);
                }
                g_free(fill_row);
            }
            selection_mask_free(region_mask);
        }
    } else {
        /* No selection: fill entire layer using blend row by row */
        guint32 fill_px = ((guint32)fill_a_byte << 24) | ((guint32)fr << 16) | ((guint32)fg << 8) | fb;
        guint32* fill_row = (guint32*)g_malloc((size_t)width * sizeof(guint32));
        if (fill_row) {
            for (gint x = 0; x < width; x++)
                fill_row[x] = fill_px;
            for (gint y = 0; y < height; y++) {
                guint32* dst_row = (guint32*)(layer_data + (gint64)y * layer_stride);
                blend_composite_row(fill_row, dst_row, width, 0, y, 255, fill_result.blend_mode);
            }
            g_free(fill_row);
        }
    }

    cairo_surface_mark_dirty(layer->surface);

    if (command_finalize_draw(cmd)) {
        command_execute(cmd, doc);
        command_stack_push(doc->undo_stack, cmd);
        command_stack_clear(doc->redo_stack);
    } else {
        command_free(cmd);
    }

    layer_invalidate_cache(layer);
    document_invalidate_composite(doc);

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    if (doc->drawing_area)
        gtk_widget_queue_draw(doc->drawing_area);
}

/**
 * Setup Edit menu from Glade builder
 */
void ui_edit_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* edit_menu = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu"));
    GtkWidget* edit_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_item"));

    if (edit_menu && edit_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_menu_item), edit_menu);
    }

    /* Get menu items that need to be updated programmatically */
    ctx->edit_menu_undo = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_undo"));
    ctx->edit_menu_redo = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_redo"));

    /* Connect Edit menu signals */
    if (ctx->edit_menu_undo) {
        /* Connect signal handler */
        g_signal_connect(ctx->edit_menu_undo, "activate", G_CALLBACK(on_edit_undo), ctx);
        /* Add accelerator manually to ensure it works */
        gtk_widget_add_accelerator(ctx->edit_menu_undo, "activate", accel_group,
                                   GDK_KEY_z, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    } else {
        g_warning("Failed to get edit_menu_undo from builder");
    }
    if (ctx->edit_menu_redo) {
        /* Connect signal handler */
        g_signal_connect(ctx->edit_menu_redo, "activate", G_CALLBACK(on_edit_redo), ctx);
        /* Add accelerator manually to ensure it works */
        gtk_widget_add_accelerator(ctx->edit_menu_redo, "activate", accel_group,
                                   GDK_KEY_y, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    } else {
        g_warning("Failed to get edit_menu_redo from builder");
    }

    /* Connect Copy, Cut, Paste menu items */
    GtkWidget* edit_menu_copy = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_copy"));
    GtkWidget* edit_menu_cut = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_cut"));
    GtkWidget* edit_menu_paste = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_paste"));
    GtkWidget* edit_menu_paste_new_image = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_paste_new_image"));
    GtkWidget* edit_menu_copy_merged = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_copy_merged"));
    GtkWidget* edit_menu_cut_merged = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_cut_merged"));

    if (edit_menu_copy) {
        g_signal_connect(edit_menu_copy, "activate", G_CALLBACK(on_edit_copy), ctx);
    }
    if (edit_menu_cut) {
        g_signal_connect(edit_menu_cut, "activate", G_CALLBACK(on_edit_cut), ctx);
    }
    if (edit_menu_paste) {
        g_signal_connect(edit_menu_paste, "activate", G_CALLBACK(on_edit_paste), ctx);
    }
    if (edit_menu_paste_new_image) {
        g_signal_connect(edit_menu_paste_new_image, "activate", G_CALLBACK(on_edit_paste_new_image), ctx);
    }
    if (edit_menu_copy_merged) {
        g_signal_connect(edit_menu_copy_merged, "activate", G_CALLBACK(on_edit_copy_merged), ctx);
    }
    if (edit_menu_cut_merged) {
        g_signal_connect(edit_menu_cut_merged, "activate", G_CALLBACK(on_edit_cut_merged), ctx);
    }

    /* Connect Clear menu item */
    GtkWidget* edit_menu_clear = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_clear"));
    if (edit_menu_clear) {
        g_signal_connect(edit_menu_clear, "activate", G_CALLBACK(on_edit_clear), ctx);
    }

    /* Connect Fill menu item */
    GtkWidget* edit_menu_fill = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_fill"));
    if (edit_menu_fill) {
        g_signal_connect(edit_menu_fill, "activate", G_CALLBACK(on_edit_fill), ctx);
    }
}
