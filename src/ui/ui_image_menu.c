#include "ui/ui_image_menu.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/tile.h"
#include "ui.h"
#include "ui/dialogs/canvas_size_dialog.h"
#include "ui/dialogs/rotate_dialog.h"
#include "ui/layers_panel.h"
#include <cairo/cairo.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdbool.h>

/**
 * Image > Canvas Size callback
 */
void on_image_canvas_size(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    LayersPanel* layers_panel;
    CanvasSizeDialog* dialog;
    CanvasSizeDialogResult* result;
    Command* cmd;
    guint old_width, old_height;
    guint new_width, new_height;
    gdouble old_resolution, new_resolution;
    gint offset_x, offset_y;
    gint delta_width, delta_height;
    CanvasAnchorPosition anchor;
    gint response;
    GtkWindow* parent_window = NULL;

    if (!ctx) {
        g_warning("Invalid application context");
        return;
    }

    if (!ctx->window || !GTK_IS_WIDGET(ctx->window)) {
        g_warning("Invalid main window");
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return;
    }

    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                   "layers_panel");

    /* Create and show dialog */
    dialog = canvas_size_dialog_new(doc);
    if (!dialog) {
        g_warning("Failed to create canvas size dialog");
        return;
    }

    /* Validate dialog was created properly */
    GtkWindow* dialog_window = canvas_size_dialog_get_window(dialog);
    if (!dialog_window || !GTK_IS_WINDOW(dialog_window)) {
        g_warning("Dialog window is invalid");
        canvas_size_dialog_free(dialog);
        return;
    }

    /* Get parent window if valid */
    if (GTK_IS_WINDOW(ctx->window)) {
        parent_window = GTK_WINDOW(ctx->window);
    }

    response = canvas_size_dialog_run(dialog, parent_window, &result);

    if (response == GTK_RESPONSE_OK && result) {
        old_width = doc->width;
        old_height = doc->height;
        new_width = result->width;
        new_height = result->height;
        old_resolution = 72.0; /* Default resolution */
        new_resolution = result->resolution;
        anchor = result->anchor;

        /* If dimensions haven't changed, nothing to do */
        if (old_width == new_width && old_height == new_height) {
            canvas_size_dialog_result_free(result);
            canvas_size_dialog_free(dialog);
            return;
        }

        /* Calculate offsets based on anchor position (same logic as document_resize_canvas) */
        delta_width = (gint)new_width - (gint)old_width;
        delta_height = (gint)new_height - (gint)old_height;

        switch (anchor) {
            case CANVAS_ANCHOR_TOP_LEFT:
                offset_x = 0;
                offset_y = 0;
                break;
            case CANVAS_ANCHOR_TOP_CENTER:
                offset_x = delta_width / 2;
                offset_y = 0;
                break;
            case CANVAS_ANCHOR_TOP_RIGHT:
                offset_x = delta_width;
                offset_y = 0;
                break;
            case CANVAS_ANCHOR_MIDDLE_LEFT:
                offset_x = 0;
                offset_y = delta_height / 2;
                break;
            case CANVAS_ANCHOR_CENTER:
                offset_x = delta_width / 2;
                offset_y = delta_height / 2;
                break;
            case CANVAS_ANCHOR_MIDDLE_RIGHT:
                offset_x = delta_width;
                offset_y = delta_height / 2;
                break;
            case CANVAS_ANCHOR_BOTTOM_LEFT:
                offset_x = 0;
                offset_y = delta_height;
                break;
            case CANVAS_ANCHOR_BOTTOM_CENTER:
                offset_x = delta_width / 2;
                offset_y = delta_height;
                break;
            case CANVAS_ANCHOR_BOTTOM_RIGHT:
                offset_x = delta_width;
                offset_y = delta_height;
                break;
            case CANVAS_ANCHOR_NONE:
            default:
                offset_x = 0;
                offset_y = 0;
                break;
        }

        /* Create undo command BEFORE resizing (to capture old state) */
        cmd = command_create_canvas_resize(old_width, old_height,
                                           new_width, new_height,
                                           old_resolution, new_resolution,
                                           offset_x, offset_y,
                                           doc);

        if (!cmd) {
            g_warning("Failed to create canvas size command");
            canvas_size_dialog_result_free(result);
            canvas_size_dialog_free(dialog);
            return;
        }

        /* Resize canvas */
        if (document_resize_canvas(doc, new_width, new_height, new_resolution, anchor)) {
            /* Push command to undo stack and clear redo stack */
            if (doc->undo_stack) {
                command_stack_push(doc->undo_stack, cmd);
                if (doc->redo_stack) {
                    command_stack_clear(doc->redo_stack);
                }
            } else {
                command_free(cmd);
            }

            /* Update layers panel */
            if (layers_panel) {
                layers_panel_update(layers_panel, doc);
            }

            /* Update UI state */
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx, NULL);
            ui_update_status_bar(ctx, NULL);
            doc->modified = TRUE;
        } else {
            g_warning("Failed to resize canvas");
            command_free(cmd);
        }

        canvas_size_dialog_result_free(result);
    }

    canvas_size_dialog_free(dialog);
}

/**
 * Image > Duplicate callback
 */
void on_image_duplicate(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* source_doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");

    if (!source_doc) {
        g_warning("No document open");
        return;
    }

    if (!source_doc->layers || g_list_length(source_doc->layers) == 0) {
        g_warning("Document has no layers to duplicate");
        return;
    }

    /* Generate duplicate filename */
    gchar* duplicate_filename;
    if (source_doc->filename) {
        duplicate_filename = g_strdup_printf("%s copy", source_doc->filename);
    } else {
        duplicate_filename = g_strdup("Untitled copy");
    }

    /* Create new document tab */
    ImageDocument* new_doc = ui_create_document_tab(ctx, duplicate_filename);
    g_free(duplicate_filename);

    if (!new_doc) {
        g_warning("Failed to create duplicate document");
        return;
    }

    /* Copy document properties */
    new_doc->width = source_doc->width;
    new_doc->height = source_doc->height;
    new_doc->channels = source_doc->channels;
    new_doc->bit_depth = source_doc->bit_depth;
    new_doc->has_alpha = source_doc->has_alpha;
    new_doc->zoom_factor = source_doc->zoom_factor;

    /* Create tile grid for the new document */
    if (new_doc->width > 0 && new_doc->height > 0) {
        if (new_doc->tile_grid) {
            tile_grid_free(new_doc->tile_grid);
        }
        new_doc->tile_grid = tile_grid_create(new_doc->width, new_doc->height, 128);
    }
    /* Note: tile_worker_pool is already created by document_new() with create_worker_pool=TRUE */

    /* Update drawing area size to match document dimensions */
    if (new_doc->drawing_area) {
        gint display_width = (gint)(new_doc->width * new_doc->zoom_factor);
        gint display_height = (gint)(new_doc->height * new_doc->zoom_factor);
        gtk_widget_set_size_request(new_doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(new_doc->drawing_area);
    }

    /* Copy all layers from source document */
    ImageLayer* source_selected_layer = source_doc->selected_layer;
    ImageLayer* new_selected_layer = NULL;

    for (GList* iter = source_doc->layers; iter; iter = iter->next) {
        ImageLayer* source_layer = (ImageLayer*)iter->data;
        if (!source_layer) {
            continue;
        }

        /* Create new layer with same dimensions */
        ImageLayer* new_layer = layer_new(source_layer->name, source_layer->width,
                                          source_layer->height, TRUE,
                                          LAYER_BACKGROUND_TRANSPARENT,
                                          LAYER_POSITION_ABOVE_CURRENT, NULL, new_doc);

        if (!new_layer) {
            g_warning("Failed to create duplicate layer: %s", source_layer->name);
            continue;
        }

        /* Copy layer surface content */
        cairo_t* cr = cairo_create(new_layer->surface);
        cairo_set_source_surface(cr, source_layer->surface, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);

        /* Copy all layer properties */
        new_layer->opacity = source_layer->opacity;
        new_layer->visible = source_layer->visible;
        new_layer->blend_mode = source_layer->blend_mode;
        new_layer->offset_x = source_layer->offset_x;
        new_layer->offset_y = source_layer->offset_y;

        /* Add layer to new document */
        new_doc->layers = g_list_append(new_doc->layers, new_layer);

        /* Track selected layer */
        if (source_layer == source_selected_layer) {
            new_selected_layer = new_layer;
        }
    }

    /* Set selected layer in new document */
    if (new_selected_layer) {
        document_set_selected_layer(new_doc, new_selected_layer);
    } else if (new_doc->layers) {
        /* Fallback to first layer if no match found */
        ImageLayer* first_layer = (ImageLayer*)g_list_first(new_doc->layers)->data;
        document_set_selected_layer(new_doc, first_layer);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(new_doc);

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, new_doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    ui_update_status_bar(ctx, NULL);

    /* Mark new document as modified */
    new_doc->modified = TRUE;
}

/**
 * Image > Fit canvas to active layer callback
 */
void on_image_fit_active_layer(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    ImageLayer* active_layer;
    guint old_width, old_height;
    guint new_width, new_height;
    gint offset_x, offset_y;
    GList* iter;
    Command* cmd;
    gdouble old_resolution = 72.0; /* Default resolution */
    gdouble new_resolution = 72.0;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Get the active/selected layer */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer) {
        g_warning("No active layer to fit canvas to");
        return;
    }

    /* Get layer dimensions */
    new_width = active_layer->width;
    new_height = active_layer->height;

    if (new_width == 0 || new_height == 0) {
        g_warning("Active layer has invalid dimensions");
        return;
    }

    old_width = doc->width;
    old_height = doc->height;

    /* If canvas already matches layer size and layer is at (0,0), nothing to do */
    if (old_width == new_width && old_height == new_height &&
        active_layer->offset_x == 0 && active_layer->offset_y == 0) {
        return;
    }

    /* Calculate offset adjustment: move active layer to (0,0) */
    offset_x = -active_layer->offset_x;
    offset_y = -active_layer->offset_y;

    /* Create undo command BEFORE resizing (to capture old state) */
    cmd = command_create_fit_active_layer(old_width, old_height,
                                          new_width, new_height,
                                          old_resolution, new_resolution,
                                          offset_x, offset_y,
                                          doc);

    if (!cmd) {
        g_warning("Failed to create fit active layer command");
        return;
    }

    /* Resize canvas to layer dimensions using TOP_LEFT anchor (no offset change from resize) */
    if (document_resize_canvas(doc, new_width, new_height, new_resolution, CANVAS_ANCHOR_TOP_LEFT)) {
        /* Adjust all layer offsets to move active layer to (0,0) */
        for (iter = doc->layers; iter; iter = iter->next) {
            ImageLayer* layer = (ImageLayer*)iter->data;
            if (layer) {
                layer->offset_x += offset_x;
                layer->offset_y += offset_y;
                layer_invalidate_cache(layer);
            }
        }

        /* Push command to undo stack and clear redo stack */
        if (doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }
        } else {
            command_free(cmd);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        ui_update_status_bar(ctx, NULL);
        doc->modified = TRUE;
    } else {
        g_warning("Failed to resize canvas to fit active layer");
        command_free(cmd);
    }
}

/**
 * Image > Fit canvas around all layers callback
 */
void on_image_fit_all_layers(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    GList* iter;
    ImageLayer* layer;
    guint old_width, old_height;
    guint new_width, new_height;
    gint min_x, min_y, max_x, max_y;
    gint offset_x, offset_y;
    gboolean has_layers = FALSE;
    Command* cmd;
    gdouble old_resolution = 72.0; /* Default resolution */
    gdouble new_resolution = 72.0;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        g_warning("Document has no layers");
        return;
    }

    /* Calculate bounding box of all layers */
    min_x = G_MAXINT;
    min_y = G_MAXINT;
    max_x = G_MININT;
    max_y = G_MININT;

    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (!layer || layer->width == 0 || layer->height == 0) {
            continue;
        }

        has_layers = TRUE;

        /* Calculate layer bounds */
        gint layer_left = layer->offset_x;
        gint layer_top = layer->offset_y;
        gint layer_right = layer->offset_x + (gint)layer->width;
        gint layer_bottom = layer->offset_y + (gint)layer->height;

        /* Update bounding box */
        if (layer_left < min_x) {
            min_x = layer_left;
        }
        if (layer_top < min_y) {
            min_y = layer_top;
        }
        if (layer_right > max_x) {
            max_x = layer_right;
        }
        if (layer_bottom > max_y) {
            max_y = layer_bottom;
        }
    }

    if (!has_layers) {
        g_warning("No valid layers to fit canvas around");
        return;
    }

    /* Calculate new canvas dimensions */
    new_width = (guint)(max_x - min_x);
    new_height = (guint)(max_y - min_y);

    if (new_width == 0 || new_height == 0) {
        g_warning("Calculated canvas size is invalid");
        return;
    }

    old_width = doc->width;
    old_height = doc->height;

    /* Calculate offset adjustment: move content so top-left is at (0,0) */
    offset_x = -min_x;
    offset_y = -min_y;

    /* If canvas already matches and layers are already at correct positions, nothing to do */
    if (old_width == new_width && old_height == new_height &&
        min_x == 0 && min_y == 0) {
        return;
    }

    /* Create undo command BEFORE resizing (to capture old state) */
    cmd = command_create_fit_all_layers(old_width, old_height,
                                        new_width, new_height,
                                        old_resolution, new_resolution,
                                        offset_x, offset_y,
                                        doc);

    if (!cmd) {
        g_warning("Failed to create fit all layers command");
        return;
    }

    /* Resize canvas using TOP_LEFT anchor (no offset change from resize) */
    if (document_resize_canvas(doc, new_width, new_height, new_resolution, CANVAS_ANCHOR_TOP_LEFT)) {
        /* Adjust all layer offsets to move content to start at (0,0) */
        for (iter = doc->layers; iter; iter = iter->next) {
            layer = (ImageLayer*)iter->data;
            if (layer) {
                layer->offset_x += offset_x;
                layer->offset_y += offset_y;
                layer_invalidate_cache(layer);
            }
        }

        /* Push command to undo stack and clear redo stack */
        if (doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }
        } else {
            command_free(cmd);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        ui_update_status_bar(ctx, NULL);
        doc->modified = TRUE;
    } else {
        g_warning("Failed to resize canvas to fit all layers");
        command_free(cmd);
    }
}

/**
 * Image > Flip horizontal callback
 */
void on_image_flip_horizontal(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        g_warning("Document has no layers");
        return;
    }

    /* Create flip horizontal command */
    cmd = command_create_flip_horizontal(doc);
    if (!cmd) {
        g_warning("Failed to create flip horizontal command");
        return;
    }

    /* Execute the command */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Image > Flip vertical callback
 */
void on_image_flip_vertical(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        g_warning("Document has no layers");
        return;
    }

    /* Create flip vertical command */
    cmd = command_create_flip_vertical(doc);
    if (!cmd) {
        g_warning("Failed to create flip vertical command");
        return;
    }

    /* Execute the command */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Image > Transpose callback
 */
void on_image_transpose(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        g_warning("Document has no layers");
        return;
    }

    /* Create transpose command */
    cmd = command_create_transpose(doc);
    if (!cmd) {
        g_warning("Failed to create transpose command");
        return;
    }

    /* Execute the command */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;
}

typedef struct {
    cairo_surface_t* before_surface; /* not owned */
} RotatePreviewData;

static cairo_surface_t* rotate_surface_ocular(cairo_surface_t* src_surface,
                                              gdouble angle_degrees,
                                              gboolean preserve_size,
                                              gboolean use_transparency,
                                              OcInterpolationMode interpolation,
                                              const GdkRGBA* fill_color) {
    gint width = 0, height = 0;
    if (!src_surface || !adjustments_validate_surface(src_surface, &width, &height)) {
        return NULL;
    }

    guchar fill_r = 0, fill_g = 0, fill_b = 0;
    if (fill_color) {
        fill_r = (guchar)CLAMP((gint)(fill_color->red * 255.0 + 0.5), 0, 255);
        fill_g = (guchar)CLAMP((gint)(fill_color->green * 255.0 + 0.5), 0, 255);
        fill_b = (guchar)CLAMP((gint)(fill_color->blue * 255.0 + 0.5), 0, 255);
    }

    guchar* rgba_input = (guchar*)g_malloc((gsize)width * (gsize)height * 4);
    if (!rgba_input) {
        return NULL;
    }
    if (!adjustments_cairo_to_rgba(src_surface, rgba_input)) {
        g_free(rgba_input);
        return NULL;
    }

    guint new_w = (guint)width;
    guint new_h = (guint)height;
    if (!preserve_size) {
        /* Avoid +1px from floating point error at exact right angles */
        gdouble a = fmod(fabs(angle_degrees), 360.0);
        if (fabs(a - 90.0) < 1e-6 || fabs(a - 270.0) < 1e-6) {
            new_w = (guint)height;
            new_h = (guint)width;
        } else if (fabs(a - 180.0) < 1e-6 || fabs(a) < 1e-6) {
            new_w = (guint)width;
            new_h = (guint)height;
        } else {
            gdouble rad = angle_degrees * (G_PI / 180.0);
            gdouble c = fabs(cos(rad));
            gdouble s = fabs(sin(rad));
            if (c < 1e-12)
                c = 0.0;
            if (s < 1e-12)
                s = 0.0;
            if (fabs(1.0 - c) < 1e-12)
                c = 1.0;
            if (fabs(1.0 - s) < 1e-12)
                s = 1.0;
            new_w = (guint)ceil((gdouble)width * c + (gdouble)height * s);
            new_h = (guint)ceil((gdouble)width * s + (gdouble)height * c);
        }
        if (new_w == 0)
            new_w = (guint)width;
        if (new_h == 0)
            new_h = (guint)height;
    }

    guchar* rgba_output = (guchar*)g_malloc((gsize)new_w * (gsize)new_h * 4);
    if (!rgba_output) {
        g_free(rgba_input);
        return NULL;
    }

    gint out_w = (gint)new_w;
    gint out_h = (gint)new_h;
    OC_STATUS status = ocularRotateImage(rgba_input, width, height, width * 4, rgba_output,
                                         &out_w, &out_h,
                                         (float)angle_degrees,
                                         preserve_size,
                                         use_transparency,
                                         interpolation,
                                         fill_r, fill_g, fill_b);

    g_free(rgba_input);

    if (status != OC_STATUS_OK) {
        g_warning("Rotate preview: Ocular rotate returned error %d", status);
        g_free(rgba_output);
        return NULL;
    }

    if (out_w <= 0 || out_h <= 0) {
        g_free(rgba_output);
        return NULL;
    }

    cairo_surface_t* out_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out_w, out_h);
    if (!out_surface || cairo_surface_status(out_surface) != CAIRO_STATUS_SUCCESS) {
        if (out_surface) {
            cairo_surface_destroy(out_surface);
        }
        g_free(rgba_output);
        return NULL;
    }

    if (!adjustments_rgba_to_cairo(out_surface, rgba_output)) {
        cairo_surface_destroy(out_surface);
        g_free(rgba_output);
        return NULL;
    }

    g_free(rgba_output);
    return out_surface;
}

static gboolean on_rotate_preview_update(RotateDialog* dialog,
                                         gdouble angle_degrees,
                                         gboolean preserve_size,
                                         OcInterpolationMode interpolation,
                                         gboolean use_transparency,
                                         const GdkRGBA* fill_color,
                                         gpointer user_data) {
    RotatePreviewData* pdata = (RotatePreviewData*)user_data;
    if (!dialog || !pdata || !pdata->before_surface) {
        return FALSE;
    }

    cairo_surface_t* after = rotate_surface_ocular(pdata->before_surface,
                                                   angle_degrees,
                                                   preserve_size,
                                                   use_transparency,
                                                   interpolation,
                                                   fill_color);
    rotate_dialog_set_after_surface(dialog, after);
    if (after) {
        cairo_surface_destroy(after);
    }
    return TRUE;
}

static void apply_fixed_rotation(AppContext* ctx, const gchar* command_name, gdouble angle_degrees) {
    if (!ctx) {
        return;
    }

    ImageDocument* doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");

    /* Defaults for fixed rotations: enlarge-to-fit, transparent borders, nearest-neighbor */
    const gboolean preserve_size = FALSE;
    const gboolean use_transparency = TRUE;
    const OcInterpolationMode interpolation = OC_INTERPOLATION_NEAREST;
    const guchar fill_r = 0, fill_g = 0, fill_b = 0;

    Command* cmd = command_create_rotate_arbitrary_named(command_name,
                                                         doc,
                                                         (gfloat)angle_degrees,
                                                         preserve_size,
                                                         use_transparency,
                                                         (gint)interpolation,
                                                         fill_r, fill_g, fill_b);
    if (!cmd) {
        return;
    }

    command_execute(cmd, doc);
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    doc->modified = TRUE;
    ui_update_window_title(ctx, NULL);
    ui_update_status_bar(ctx, NULL);
    ui_update_menu_and_button_states(ctx);
}

void on_image_rotate_90_cw(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    /* Clockwise is negative angle in standard mathematical convention */
    apply_fixed_rotation(ctx, "Rotate 90° clockwise", -90.0);
}

void on_image_rotate_90_ccw(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    apply_fixed_rotation(ctx, "Rotate 90° counter-clockwise", 90.0);
}

void on_image_rotate_180(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    apply_fixed_rotation(ctx, "Rotate 180°", 180.0);
}

/**
 * Image > Rotate > Rotate arbitrary... callback
 */
void on_image_rotate_arbitrary(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    if (!ctx) {
        return;
    }

    ImageDocument* doc = ui_get_active_document(ctx);
    if (!doc) {
        return;
    }

    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");

    /* Ensure composite is up to date for preview */
    cairo_surface_t* before_surface = document_export_composite_surface(doc);
    if (!before_surface) {
        return;
    }

    RotateDialog* dialog = rotate_dialog_new("Rotate image");
    if (!dialog) {
        cairo_surface_destroy(before_surface);
        return;
    }

    rotate_dialog_set_before_surface(dialog, before_surface);

    RotatePreviewData pdata = {.before_surface = before_surface};
    rotate_dialog_set_preview_callback(dialog, on_rotate_preview_update, &pdata);

    gdouble angle_degrees = 0.0;
    gboolean preserve_size = FALSE;
    OcInterpolationMode interpolation = OC_INTERPOLATION_NEAREST;
    gboolean use_transparency = TRUE;
    GdkRGBA fill_color = {0};

    gint response = rotate_dialog_run(dialog,
                                      GTK_IS_WINDOW(ctx->window) ? GTK_WINDOW(ctx->window) : NULL,
                                      &angle_degrees,
                                      &preserve_size,
                                      &interpolation,
                                      &use_transparency,
                                      &fill_color);

    if (response == GTK_RESPONSE_OK) {
        guchar fill_r = (guchar)CLAMP((gint)(fill_color.red * 255.0 + 0.5), 0, 255);
        guchar fill_g = (guchar)CLAMP((gint)(fill_color.green * 255.0 + 0.5), 0, 255);
        guchar fill_b = (guchar)CLAMP((gint)(fill_color.blue * 255.0 + 0.5), 0, 255);

        Command* cmd = command_create_rotate_arbitrary_named("Rotate image",
                                                             doc,
                                                             (gfloat)angle_degrees,
                                                             preserve_size,
                                                             use_transparency,
                                                             (gint)interpolation,
                                                             fill_r, fill_g, fill_b);
        if (cmd) {
            command_execute(cmd, doc);
            if (doc->undo_stack) {
                command_stack_push(doc->undo_stack, cmd);
                if (doc->redo_stack) {
                    command_stack_clear(doc->redo_stack);
                }
            } else {
                command_free(cmd);
            }

            /* Update layers panel thumbnails */
            if (layers_panel) {
                layers_panel_update(layers_panel, doc);
            }

            doc->modified = TRUE;
            ui_update_window_title(ctx, NULL);
            ui_update_status_bar(ctx, NULL);
            ui_update_menu_and_button_states(ctx);
        }
    }

    cairo_surface_destroy(before_surface);
    rotate_dialog_free(dialog);
}
/**
 * Image > Merge visible layers callback
 */
void on_image_merge_visible(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        g_warning("Document has no layers");
        return;
    }

    /* Create merge visible command */
    cmd = command_create_merge_visible(doc);
    if (!cmd) {
        g_warning("Failed to create merge visible command");
        return;
    }

    /* Execute the command */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Image > Flatten image callback
 */
void on_image_flatten(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    if (!doc->layers || g_list_length(doc->layers) == 0) {
        g_warning("Document has no layers");
        return;
    }

    if (g_list_length(doc->layers) == 1) {
        g_warning("Only one layer, nothing to flatten");
        return;
    }

    /* Create flatten command */
    cmd = command_create_flatten(doc);
    if (!cmd) {
        g_warning("Failed to create flatten command");
        return;
    }

    /* Execute the command */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Setup Image menu from Glade builder
 */
void ui_image_menu_setup(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* image_menu = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu"));
    GtkWidget* image_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_item"));

    if (image_menu && image_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(image_menu_item), image_menu);
    }

    /* Connect Image menu signals */
    GtkWidget* image_menu_canvas_size = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_canvas_size"));
    if (image_menu_canvas_size) {
        g_signal_connect(image_menu_canvas_size, "activate", G_CALLBACK(on_image_canvas_size), ctx);
    }

    GtkWidget* image_menu_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_duplicate"));
    if (image_menu_duplicate) {
        g_signal_connect(image_menu_duplicate, "activate", G_CALLBACK(on_image_duplicate), ctx);
    }

    GtkWidget* image_menu_fit_active_layer = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_fit_active_layer"));
    if (image_menu_fit_active_layer) {
        g_signal_connect(image_menu_fit_active_layer, "activate", G_CALLBACK(on_image_fit_active_layer), ctx);
    }

    GtkWidget* image_menu_fit_all_layer = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_fit_all_layer"));
    if (image_menu_fit_all_layer) {
        g_signal_connect(image_menu_fit_all_layer, "activate", G_CALLBACK(on_image_fit_all_layers), ctx);
    }

    GtkWidget* rotate_menu_arbitrary = GTK_WIDGET(gtk_builder_get_object(builder, "rotate_menu_arbitrary"));
    if (rotate_menu_arbitrary) {
        g_signal_connect(rotate_menu_arbitrary, "activate", G_CALLBACK(on_image_rotate_arbitrary), ctx);
    }

    GtkWidget* rotate_menu_90_cw = GTK_WIDGET(gtk_builder_get_object(builder, "rotate_menu_90_cw"));
    if (rotate_menu_90_cw) {
        g_signal_connect(rotate_menu_90_cw, "activate", G_CALLBACK(on_image_rotate_90_cw), ctx);
    }

    GtkWidget* rotate_menu_90_ccw = GTK_WIDGET(gtk_builder_get_object(builder, "rotate_menu_90_ccw"));
    if (rotate_menu_90_ccw) {
        g_signal_connect(rotate_menu_90_ccw, "activate", G_CALLBACK(on_image_rotate_90_ccw), ctx);
    }

    GtkWidget* rotate_menu_180 = GTK_WIDGET(gtk_builder_get_object(builder, "rotate_menu_180"));
    if (rotate_menu_180) {
        g_signal_connect(rotate_menu_180, "activate", G_CALLBACK(on_image_rotate_180), ctx);
    }

    GtkWidget* image_menu_flip_horizontal = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_flip_horizontal"));
    if (image_menu_flip_horizontal) {
        g_signal_connect(image_menu_flip_horizontal, "activate", G_CALLBACK(on_image_flip_horizontal), ctx);
    }

    GtkWidget* image_menu_flip_vertical = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_flip_vertical"));
    if (image_menu_flip_vertical) {
        g_signal_connect(image_menu_flip_vertical, "activate", G_CALLBACK(on_image_flip_vertical), ctx);
    }

    GtkWidget* image_menu_tranpose = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_tranpose"));
    if (image_menu_tranpose) {
        g_signal_connect(image_menu_tranpose, "activate", G_CALLBACK(on_image_transpose), ctx);
    }

    GtkWidget* image_menu_merge_visible = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_merge_visible"));
    if (image_menu_merge_visible) {
        g_signal_connect(image_menu_merge_visible, "activate", G_CALLBACK(on_image_merge_visible), ctx);
    }

    GtkWidget* image_menu_flatten = GTK_WIDGET(gtk_builder_get_object(builder, "image_menu_flatten"));
    if (image_menu_flatten) {
        g_signal_connect(image_menu_flatten, "activate", G_CALLBACK(on_image_flatten), ctx);
    }
}
