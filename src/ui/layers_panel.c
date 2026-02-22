#include "ui/layers_panel.h"
#include "document.h"
#include "ui/ui_layer_menu.h"
#include "ocular.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "ui.h"
#include "ui/tools_panel.h"
#include "ui/ui_utils.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Forward declarations */
static GdkPixbuf* create_layer_thumbnail(cairo_surface_t* layer_surface, gint thumb_size, gboolean visible);
static GdkPixbuf* get_visibility_icon(gboolean visible);

/**
 * Treeview button press event handler - handles visibility icon clicks
 */
static gboolean on_treeview_button_press(GtkWidget* widget,
                                         GdkEventButton* event,
                                         gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    GtkTreeView* tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath* path = NULL;
    GtkTreeViewColumn* column = NULL;
    gint cell_x, cell_y;
    gint row;
    guint layer_count;
    gint layer_index;
    ImageLayer* layer;
    AppContext* ctx;

    if (event->button != 1 || event->type != GDK_BUTTON_PRESS) {
        return FALSE;
    }

    /* Check if click is in visibility column */
    if (!gtk_tree_view_get_path_at_pos(tree_view, (gint)event->x, (gint)event->y,
                                       &path, &column, &cell_x, &cell_y)) {
        return FALSE;
    }

    GtkTreeViewColumn* visibility_column = GTK_TREE_VIEW_COLUMN(
        g_object_get_data(G_OBJECT(widget), "visibility_column"));

    if (column != visibility_column) {
        gtk_tree_path_free(path);
        return FALSE;
    }

    if (!layers_panel->current_doc || !layers_panel->app_context) {
        gtk_tree_path_free(path);
        return TRUE;
    }

    /* Get the layer from the clicked row */
    row = gtk_tree_path_get_indices(path)[0];
    layer_count = document_get_layer_count(layers_panel->current_doc);
    layer_index = layer_count - 1 - row;

    if (layer_index < 0 || layer_index >= (gint)layer_count) {
        gtk_tree_path_free(path);
        return TRUE;
    }

    layer = document_get_layer(layers_panel->current_doc, layer_index);
    gtk_tree_path_free(path);

    if (!layer) return TRUE;

    ctx = (AppContext*)layers_panel->app_context;
    layer_visibility_toggle_execute(ctx, layers_panel->current_doc, layer);

    return TRUE;
}

/* Forward declarations for opacity callbacks */
static void on_opacity_scale_changed(GtkRange* range, gpointer user_data);
static void on_opacity_spin_changed(GtkSpinButton* spin_button, gpointer user_data);
static void on_opacity_reset_clicked(GtkButton* button, gpointer user_data);

/* Forward declaration for blend mode callback */
static void on_blend_mode_changed(GtkComboBox* combo, gpointer user_data);

/**
 * Blend mode changed callback
 */
static void on_blend_mode_changed(GtkComboBox* combo, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    gint active = gtk_combo_box_get_active(combo);
    ImageLayer* selected_layer;
    BlendMode blend_mode;

    if (!layers_panel || !layers_panel->current_doc) {
        return;
    }

    if (active < 0) {
        return; /* No selection */
    }

    /* Combo box index maps directly to BlendMode enum value */
    if (active >= 0 && active < BLEND_MODE_COUNT) {
        blend_mode = (BlendMode)active;
    } else {
        blend_mode = BLEND_MODE_NORMAL;
    }

    selected_layer = layers_panel_get_selected_layer(layers_panel);
    if (!selected_layer) {
        return;
    }

    /* Update layer blend mode */
    selected_layer->blend_mode = blend_mode;

    /* Mark cache as dirty but don't destroy it - will be regenerated lazily */
    selected_layer->cache_dirty = TRUE;

    /* Invalidate entire layer region (blend mode affects how layer composites) */
    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, selected_layer->offset_x, selected_layer->offset_y,
                   selected_layer->width, selected_layer->height);
    dirty_rect_clamp(&dirty_rect, layers_panel->current_doc->width,
                     layers_panel->current_doc->height);
    document_invalidate_region(layers_panel->current_doc, &dirty_rect);
}

/* Track whether we're currently dragging the opacity slider.
 * During drag, canvas updates are deferred until release for responsiveness.
 * 
 * TODO: Investigate ways to provide live preview during drag without lag:
 * - Low-resolution preview rendering during drag
 * - GPU-accelerated compositing for real-time updates
 * - Approximate opacity by adjusting layer surface alpha at draw time
 *   (without full recomposite) - would need special handling in draw callback
 * See TODO.md "Opacity slider live preview" for details.
 */
static gboolean opacity_slider_dragging = FALSE;

/**
 * Opacity scale button press callback
 * Starts deferred update mode - no canvas updates during drag
 */
static gboolean on_opacity_scale_button_press(GtkWidget* widget, GdkEventButton* event,
                                              gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;

    /* Mark that we're dragging - canvas updates will be deferred */
    opacity_slider_dragging = TRUE;

    return FALSE; /* Let event propagate */
}

/**
 * Opacity scale button release callback
 * Ends deferred mode and does the actual canvas update
 */
static gboolean on_opacity_scale_button_release(GtkWidget* widget, GdkEventButton* event,
                                                gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageLayer* selected_layer;
    (void)widget;
    (void)event;

    /* Mark that dragging has ended */
    opacity_slider_dragging = FALSE;

    if (!layers_panel || !layers_panel->current_doc) {
        return FALSE;
    }

    selected_layer = layers_panel_get_selected_layer(layers_panel);
    if (selected_layer) {
        /* Now do the actual expensive update */
        layer_invalidate_cache(selected_layer);
        document_invalidate_composite(layers_panel->current_doc);

        if (layers_panel->current_doc->drawing_area) {
            gtk_widget_queue_draw(layers_panel->current_doc->drawing_area);
        }
    }

    return FALSE; /* Let event propagate */
}

/**
 * Opacity scale changed callback
 * During drag: only updates the value, no canvas redraw (deferred)
 * After release: full update happens in button_release handler
 */
static void on_opacity_scale_changed(GtkRange* range, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    gdouble value = gtk_range_get_value(range);
    ImageLayer* selected_layer;

    if (!layers_panel || !layers_panel->current_doc) {
        return;
    }

    selected_layer = layers_panel_get_selected_layer(layers_panel);
    if (!selected_layer) {
        return;
    }

    /* Update layer opacity value immediately (lightweight) */
    selected_layer->opacity = value / 100.0;

    /* Update spin button to stay in sync */
    if (layers_panel->spin_opacity) {
        g_signal_handlers_block_by_func(layers_panel->spin_opacity,
                                        G_CALLBACK(on_opacity_spin_changed),
                                        layers_panel);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(layers_panel->spin_opacity), value);
        g_signal_handlers_unblock_by_func(layers_panel->spin_opacity,
                                          G_CALLBACK(on_opacity_spin_changed),
                                          layers_panel);
    }

    /* If we're dragging, defer the expensive canvas update until release */
    if (opacity_slider_dragging) {
        return; /* Skip redraw during drag - will update on release */
    }

    /* Not dragging (e.g., keyboard/scroll adjustment) - do immediate update */
    layer_invalidate_cache(selected_layer);
    document_invalidate_composite(layers_panel->current_doc);

    if (layers_panel->current_doc->drawing_area) {
        gtk_widget_queue_draw(layers_panel->current_doc->drawing_area);
    }
}

/**
 * Opacity spin button changed callback
 */
static void on_opacity_spin_changed(GtkSpinButton* spin_button, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    gdouble value = gtk_spin_button_get_value(spin_button);
    ImageLayer* selected_layer;

    if (!layers_panel || !layers_panel->current_doc) {
        return;
    }

    selected_layer = layers_panel_get_selected_layer(layers_panel);
    if (!selected_layer) {
        return;
    }

    /* Update layer opacity (convert from 0-100 to 0.0-1.0) */
    selected_layer->opacity = value / 100.0;

    /* Mark cache as dirty but don't destroy it - will be regenerated lazily */
    selected_layer->cache_dirty = TRUE;

    /* Update scale to stay in sync */
    if (layers_panel->scale_opacity) {
        g_signal_handlers_block_by_func(layers_panel->scale_opacity,
                                        G_CALLBACK(on_opacity_scale_changed),
                                        layers_panel);
        gtk_range_set_value(GTK_RANGE(layers_panel->scale_opacity), value);
        g_signal_handlers_unblock_by_func(layers_panel->scale_opacity,
                                          G_CALLBACK(on_opacity_scale_changed),
                                          layers_panel);
    }

    /* Invalidate entire layer region (opacity affects how layer composites) */
    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, selected_layer->offset_x, selected_layer->offset_y,
                   selected_layer->width, selected_layer->height);
    dirty_rect_clamp(&dirty_rect, layers_panel->current_doc->width,
                     layers_panel->current_doc->height);
    document_invalidate_region(layers_panel->current_doc, &dirty_rect);

    /* Queue redraw */
    if (layers_panel->current_doc->drawing_area) {
        gtk_widget_queue_draw(layers_panel->current_doc->drawing_area);
    }
}

/**
 * Opacity reset button clicked callback
 */
static void on_opacity_reset_clicked(GtkButton* button, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageLayer* selected_layer;

    (void)button; /* Unused */

    if (!layers_panel || !layers_panel->current_doc) {
        return;
    }

    selected_layer = layers_panel_get_selected_layer(layers_panel);
    if (!selected_layer) {
        return;
    }

    /* Reset opacity to 100% (1.0) */
    selected_layer->opacity = 1.0;

    /* Mark cache as dirty but don't destroy it - will be regenerated lazily */
    selected_layer->cache_dirty = TRUE;

    /* Update controls */
    if (layers_panel->scale_opacity) {
        g_signal_handlers_block_by_func(layers_panel->scale_opacity,
                                        G_CALLBACK(on_opacity_scale_changed),
                                        layers_panel);
        gtk_range_set_value(GTK_RANGE(layers_panel->scale_opacity), 100.0);
        g_signal_handlers_unblock_by_func(layers_panel->scale_opacity,
                                          G_CALLBACK(on_opacity_scale_changed),
                                          layers_panel);
    }

    if (layers_panel->spin_opacity) {
        g_signal_handlers_block_by_func(layers_panel->spin_opacity,
                                        G_CALLBACK(on_opacity_spin_changed),
                                        layers_panel);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(layers_panel->spin_opacity), 100.0);
        g_signal_handlers_unblock_by_func(layers_panel->spin_opacity,
                                          G_CALLBACK(on_opacity_spin_changed),
                                          layers_panel);
    }

    /* Invalidate entire layer region (opacity affects how layer composites) */
    DirtyRect dirty_rect;
    dirty_rect_set(&dirty_rect, selected_layer->offset_x, selected_layer->offset_y,
                   selected_layer->width, selected_layer->height);
    dirty_rect_clamp(&dirty_rect, layers_panel->current_doc->width,
                     layers_panel->current_doc->height);
    document_invalidate_region(layers_panel->current_doc, &dirty_rect);

    /* Queue redraw */
    if (layers_panel->current_doc->drawing_area) {
        gtk_widget_queue_draw(layers_panel->current_doc->drawing_area);
    }
}

/**
 * Layer name edited callback
 */
static void on_layer_name_edited(GtkCellRendererText* renderer,
                                 gchar* path_str,
                                 gchar* new_text,
                                 gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    GtkTreeIter iter;

    (void)renderer; /* Unused */

    if (!new_text || strlen(new_text) == 0) {
        return; /* Don't allow empty names */
    }

    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(layers_panel->store),
                                             &iter, path_str)) {
        return;
    }

    /* Update layer name in document */
    if (layers_panel->current_doc) {
        /* Get the layer from the position */
        gint row = atoi(path_str);
        guint layer_count = document_get_layer_count(layers_panel->current_doc);

        /* Layers are displayed in reverse order */
        gint layer_index = layer_count - 1 - row;

        if (layer_index >= 0 && layer_index < (gint)layer_count) {
            ImageLayer* layer = document_get_layer(layers_panel->current_doc, layer_index);
            if (layer) {
                g_free(layer->name);
                layer->name = g_strdup(new_text);
            }
        }
    }

    /* Update name in list store */
    gtk_list_store_set(layers_panel->store, &iter,
                       2, new_text,
                       -1);
}

/**
 * Layer tree view row activated callback - start editing layer name on double-click
 */
static gboolean on_layer_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                                       GtkTreeViewColumn* column, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    GtkTreeViewColumn* name_column;
    GtkCellRenderer* name_renderer;

    (void)column; /* Unused - we always edit the name column */

    if (!layers_panel || !path) {
        return FALSE;
    }

    name_column = (GtkTreeViewColumn*)g_object_get_data(G_OBJECT(tree_view), "name_column");
    name_renderer = (GtkCellRenderer*)g_object_get_data(G_OBJECT(tree_view), "name_renderer");
    if (name_column && name_renderer) {
        /* Temporarily enable editing so set_cursor_on_cell can start it */
        g_object_set(name_renderer, "editable", TRUE, NULL);
        gtk_tree_view_set_cursor_on_cell(tree_view, path, name_column, name_renderer, TRUE);
        /* Restore FALSE so single-click won't start editing next time */
        g_object_set(name_renderer, "editable", FALSE, NULL);
    }

    return TRUE; /* Event handled */
}

/**
 * Create a thumbnail from a layer surface
 */
static GdkPixbuf* create_layer_thumbnail(cairo_surface_t* layer_surface, gint thumb_max_size, gboolean visible) {
    GdkPixbuf* thumbnail = NULL;
    GdkPixbuf* full_pixbuf = NULL;
    cairo_surface_t* thumb_surface = NULL;
    cairo_t* cr = NULL;
    gint layer_width, layer_height;
    gdouble scale_x, scale_y, scale;

    if (!layer_surface) {
        return NULL;
    }

    layer_width = cairo_image_surface_get_width(layer_surface);
    layer_height = cairo_image_surface_get_height(layer_surface);

    if (layer_width <= 0 || layer_height <= 0) {
        return NULL;
    }

    /* Scale to fit within max size, keeping aspect ratio */
    scale_x = (gdouble)thumb_max_size / layer_width;
    scale_y = (gdouble)thumb_max_size / layer_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    gint thumb_width = (gint)(layer_width * scale);
    gint thumb_height = (gint)(layer_height * scale);
    if (thumb_width < 1)
        thumb_width = 1;
    if (thumb_height < 1)
        thumb_height = 1;

    /* Create fixed-size surface (column width) so pixbuf fills column and centers in treeview */
    gint surface_width = thumb_max_size;
    gint surface_height = thumb_max_size;
    gint offset_x = (surface_width - thumb_width) / 2;
    gint offset_y = (surface_height - thumb_height) / 2;

    thumb_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_width, surface_height);
    if (!thumb_surface) {
        return NULL;
    }

    cr = cairo_create(thumb_surface);

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Draw checkered background only within the layer image bounds (not the padding) */
    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);
    cairo_rectangle(cr, 0, 0, thumb_width, thumb_height);
    cairo_clip(cr);
    draw_checkered_background(cr, thumb_width, thumb_height);
    cairo_restore(cr);

    /* Draw layer scaled and centered within the surface */
    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, layer_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);
    cairo_restore(cr);

    /* If not visible, apply grayscale/disabled effect over layer region only */
    if (!visible) {
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        /* Convert to grayscale and reduce opacity */
        cairo_save(cr);
        cairo_translate(cr, offset_x, offset_y);
        cairo_rectangle(cr, 0, 0, thumb_width, thumb_height);
        cairo_clip(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);

    /* Convert to pixbuf */
    full_pixbuf = cairo_surface_to_pixbuf(thumb_surface, TRUE);
    cairo_surface_destroy(thumb_surface);

    return full_pixbuf;
}

/**
 * Get visibility icon pixbuf
 */
static GdkPixbuf* get_visibility_icon(gboolean visible) {
    const gchar* resource_path = visible
                                     ? "/icons/visibility-on.png"
                                     : "/icons/visibility-off.png";

    GError* error = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_resource(resource_path, &error);
    if (pixbuf) {
        /* Ensure pixbuf is fully loaded by accessing its data
         * This forces librsvg to complete all operations before we unref
         * For SVG files, this ensures the critical section is unlocked */
        (void)gdk_pixbuf_get_width(pixbuf);
        (void)gdk_pixbuf_get_height(pixbuf);
        (void)gdk_pixbuf_get_has_alpha(pixbuf);
        (void)gdk_pixbuf_get_pixels(pixbuf);

        /* Scale to 16x16 for treeview */
        GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 16, 16, GDK_INTERP_BILINEAR);

        /* Unref original pixbuf before returning scaled
         * This ensures librsvg cleanup happens while we still have control */
        g_object_unref(pixbuf);

        return scaled;
    } else if (error) {
        g_warning("Failed to load visibility icon from resource %s: %s", resource_path, error->message);
        g_error_free(error);
    }
    return NULL;
}

/**
 * Panel button callbacks
 */
static void on_panel_btn_new_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Context passed differently */
}

static void on_panel_btn_delete_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Context passed differently */
}

static void on_panel_btn_duplicate_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Context passed differently */
}

/**
 * Helper function to set icon on a button from SVG resource and remove padding
 */
static void set_button_icon(GtkButton* button, const gchar* resource_path, gint width, gint height) {
    if (!button) {
        return;
    }

    /* Remove button padding and styling */
    gtk_button_set_relief(button, GTK_RELIEF_NONE);
    gtk_widget_set_margin_start(GTK_WIDGET(button), 0);
    gtk_widget_set_margin_end(GTK_WIDGET(button), 0);
    gtk_widget_set_margin_top(GTK_WIDGET(button), 0);
    gtk_widget_set_margin_bottom(GTK_WIDGET(button), 0);

    /* Use CSS to remove inner padding and add hover effect with border */
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "button { padding: 0px; border: 1px solid transparent; } "
                                    "button:hover { background-color: #e0e0e0; border: 1px solid #b0b0b0; } "
                                    "button:active { background-color: #d0d0d0; border: 1px solid #909090; }",
                                    -1, NULL);
    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(button));
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GError* error = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_resource(resource_path, &error);
    if (pixbuf) {
        /* Ensure pixbuf is fully loaded by accessing its data
         * This forces librsvg to complete all operations before we unref
         * For SVG files, this ensures the critical section is unlocked */
        (void)gdk_pixbuf_get_width(pixbuf);
        (void)gdk_pixbuf_get_height(pixbuf);
        (void)gdk_pixbuf_get_has_alpha(pixbuf);
        (void)gdk_pixbuf_get_pixels(pixbuf);

        /* Scale to specified size */
        GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, width, height, GDK_INTERP_BILINEAR);

        /* Unref original pixbuf BEFORE creating image
         * This ensures librsvg cleanup happens while we still have control */
        g_object_unref(pixbuf);

        if (scaled) {
            /* Create image from scaled pixbuf - image takes ownership of the reference */
            GtkWidget* image = gtk_image_new_from_pixbuf(scaled);
            gtk_button_set_image(button, image);
            gtk_button_set_image_position(button, GTK_POS_TOP);

            /* Unref scaled pixbuf - image now owns it, so this just releases our reference */
            g_object_unref(scaled);
        }
    } else if (error) {
        g_warning("Failed to load icon from resource %s: %s", resource_path, error->message);
        g_error_free(error);
    }
}

/**
 * Create the layers panel with tree view (loads from Glade file)
 */
LayersPanel* create_layers_panel(AppContext* ctx) {
    LayersPanel* layers_panel = (LayersPanel*)g_malloc(sizeof(LayersPanel));
    GtkBuilder* builder;
    GError* error = NULL;
    GtkWidget* scroll_window;
    GtkCellRenderer* renderer;
    GtkTreeViewColumn* column;

    /* Load the Glade file from resources */
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/layers_panel.glade", &error)) {
        g_warning("Failed to load layers_panel.glade: %s", error->message);
        g_error_free(error);
        g_object_unref(builder);

        /* Fallback: create empty structure */
        layers_panel->store = NULL;
        layers_panel->tree_view = NULL;
        layers_panel->btn_new = NULL;
        layers_panel->btn_delete = NULL;
        layers_panel->btn_up = NULL;
        layers_panel->btn_down = NULL;
        layers_panel->btn_duplicate = NULL;
        layers_panel->scale_opacity = NULL;
        layers_panel->spin_opacity = NULL;
        layers_panel->btn_opacity_reset = NULL;
        layers_panel->combo_blend = NULL;
        layers_panel->current_doc = NULL;
        layers_panel->app_context = ctx; /* Store app context */
        return layers_panel;
    }

    /* Get scrolled window from builder for tree view population */
    scroll_window = GTK_WIDGET(gtk_builder_get_object(builder, "layers_scroll"));

    /* Get buttons from builder */
    layers_panel->btn_new = GTK_WIDGET(gtk_builder_get_object(builder, "btn_new_layer"));
    layers_panel->btn_delete = GTK_WIDGET(gtk_builder_get_object(builder, "btn_delete_layer"));
    layers_panel->btn_up = GTK_WIDGET(gtk_builder_get_object(builder, "btn_move_layer_up"));
    layers_panel->btn_down = GTK_WIDGET(gtk_builder_get_object(builder, "btn_move_layer_down"));
    layers_panel->btn_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "btn_duplicate_layer"));

    /* Get opacity controls from builder */
    layers_panel->scale_opacity = GTK_WIDGET(gtk_builder_get_object(builder, "scale_opacity"));
    layers_panel->spin_opacity = GTK_WIDGET(gtk_builder_get_object(builder, "spin_opacity"));
    layers_panel->btn_opacity_reset = GTK_WIDGET(gtk_builder_get_object(builder, "btn_opacity_reset"));

    /* Get blend mode combo box from builder */
    layers_panel->combo_blend = GTK_WIDGET(gtk_builder_get_object(builder, "combo_blend"));

    /* Create list store for layers */
    /* Column 0: Visibility icon (pixbuf) */
    /* Column 1: Thumbnail (pixbuf) */
    /* Column 2: Layer name (string) */
    /* Column 3: Visible state (boolean, for internal use) */
    layers_panel->store = gtk_list_store_new(4,
                                             GDK_TYPE_PIXBUF, /* Visibility icon */
                                             GDK_TYPE_PIXBUF, /* Thumbnail */
                                             G_TYPE_STRING,   /* Name */
                                             G_TYPE_BOOLEAN); /* Visible state */

    /* Create tree view */
    layers_panel->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(layers_panel->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(layers_panel->tree_view), FALSE);
    g_signal_connect(layers_panel->tree_view, "row-activated",
                     G_CALLBACK(on_layer_row_activated), layers_panel);
    g_signal_connect(layers_panel->tree_view, "button-press-event",
                     G_CALLBACK(on_treeview_button_press), layers_panel);
    gtk_container_add(GTK_CONTAINER(scroll_window), layers_panel->tree_view);

    /* Keep builder alive by storing it on the tree view as object data */
    /* This allows workspace to retrieve the panel widget later */
    g_object_set_data_full(G_OBJECT(layers_panel->tree_view), "builder", builder, g_object_unref);

    /* Visibility column (icon) - clickable */
    renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(renderer, "xpad", 4, "ypad", 4, NULL);
    column = gtk_tree_view_column_new_with_attributes("Visible",
                                                      renderer,
                                                      "pixbuf", 0,
                                                      NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 24);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Store column reference for click detection */
    g_object_set_data(G_OBJECT(layers_panel->tree_view), "visibility_column", column);

    /* Thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(renderer, "xpad", 4, "ypad", 4, "xalign", 0.5, NULL);
    column = gtk_tree_view_column_new_with_attributes("Thumbnail",
                                                      renderer,
                                                      "pixbuf", 1,
                                                      NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 48);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Name column (editable on double-click only - editable=FALSE prevents single-click) */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
                 "editable", FALSE,
                 "yalign", 0.5, /* Center vertically */
                 "ypad", 0,     /* No vertical padding */
                 "height", -1,  /* Let height fit content */
                 NULL);
    column = gtk_tree_view_column_new_with_attributes("Layer",
                                                      renderer,
                                                      "text", 2,
                                                      NULL);
    g_signal_connect(renderer, "edited",
                     G_CALLBACK(on_layer_name_edited), layers_panel);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);
    g_object_set_data(G_OBJECT(layers_panel->tree_view), "name_column", column);
    g_object_set_data(G_OBJECT(layers_panel->tree_view), "name_renderer", renderer);

    /* Note: We don't use gtk_tree_view_set_fixed_height_mode because the Name column
     * needs to expand, which conflicts with fixed height mode. The yalign and ypad
     * properties on the cell renderer control the vertical alignment instead. */

    /* Store panel reference in buttons for callback access and set icons */
    if (layers_panel->btn_new) {
        g_object_set_data(G_OBJECT(layers_panel->btn_new), "layers_panel", layers_panel);
        set_button_icon(GTK_BUTTON(layers_panel->btn_new), "/icons/layer-add.svg", 32, 32);
    }
    if (layers_panel->btn_delete) {
        g_object_set_data(G_OBJECT(layers_panel->btn_delete), "layers_panel", layers_panel);
        set_button_icon(GTK_BUTTON(layers_panel->btn_delete), "/icons/layer-delete.svg", 32, 32);
    }
    if (layers_panel->btn_duplicate) {
        g_object_set_data(G_OBJECT(layers_panel->btn_duplicate), "layers_panel", layers_panel);
        set_button_icon(GTK_BUTTON(layers_panel->btn_duplicate), "/icons/layer-duplicate.svg", 32, 32);
    }
    if (layers_panel->btn_up) {
        set_button_icon(GTK_BUTTON(layers_panel->btn_up), "/icons/layer-up.svg", 32, 32);
    }
    if (layers_panel->btn_down) {
        set_button_icon(GTK_BUTTON(layers_panel->btn_down), "/icons/layer-down.svg", 32, 32);
    }

    /* Set up opacity controls */
    if (layers_panel->scale_opacity) {
        gtk_range_set_range(GTK_RANGE(layers_panel->scale_opacity), 0.0, 100.0);
        gtk_range_set_value(GTK_RANGE(layers_panel->scale_opacity), 100.0);

        /* Enable button events for async compositing control */
        gtk_widget_add_events(layers_panel->scale_opacity,
                              GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

        /* Connect value-changed for opacity updates */
        g_signal_connect(layers_panel->scale_opacity, "value-changed",
                         G_CALLBACK(on_opacity_scale_changed), layers_panel);

        /* Connect button press/release for async compositing mode:
         * - Press: enable async mode (allow stale tiles) for responsiveness
         * - Release: disable async mode and do final sync update */
        g_signal_connect(layers_panel->scale_opacity, "button-press-event",
                         G_CALLBACK(on_opacity_scale_button_press), layers_panel);
        g_signal_connect(layers_panel->scale_opacity, "button-release-event",
                         G_CALLBACK(on_opacity_scale_button_release), layers_panel);
    }

    if (layers_panel->spin_opacity) {
        GtkAdjustment* adj = gtk_adjustment_new(100.0, 0.0, 100.0, 1.0, 10.0, 0.0);
        gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(layers_panel->spin_opacity), adj);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(layers_panel->spin_opacity), 0);
        g_signal_connect(layers_panel->spin_opacity, "value-changed",
                         G_CALLBACK(on_opacity_spin_changed), layers_panel);
    }

    if (layers_panel->btn_opacity_reset) {
        set_button_icon(GTK_BUTTON(layers_panel->btn_opacity_reset), "/icons/reset.svg", 16, 16);
        g_signal_connect(layers_panel->btn_opacity_reset, "clicked",
                         G_CALLBACK(on_opacity_reset_clicked), layers_panel);
    }

    /* Set up blend mode combo box */
    if (layers_panel->combo_blend) {
        /* Use list-style popup (same as zoom dropdown) to avoid empty space when
         * near screen edge */
        ui_apply_list_combobox_style(layers_panel->combo_blend);
        gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(layers_panel->combo_blend), TRUE);
        g_signal_connect(layers_panel->combo_blend, "notify::popup-shown",
                         G_CALLBACK(ui_combo_popup_shown_fix), NULL);

        /* Create list store for blend modes */
        GtkListStore* blend_store = gtk_list_store_new(1, G_TYPE_STRING);
        GtkTreeIter iter;

        /* Add all 27 Photoshop-compatible blend mode options
         * Order matches BlendMode enum in document.h */
        const char* blend_modes[] = {
            /* Normal modes */
            "Normal",   /* 0  - BLEND_MODE_NORMAL */
            "Dissolve", /* 1  - BLEND_MODE_DISSOLVE */
            /* Darken modes */
            "Darken",       /* 2  - BLEND_MODE_DARKEN */
            "Multiply",     /* 3  - BLEND_MODE_MULTIPLY */
            "Color Burn",   /* 4  - BLEND_MODE_COLOR_BURN */
            "Linear Burn",  /* 5  - BLEND_MODE_LINEAR_BURN */
            "Darker Color", /* 6  - BLEND_MODE_DARKER_COLOR */
            /* Lighten modes */
            "Lighten",       /* 7  - BLEND_MODE_LIGHTEN */
            "Screen",        /* 8  - BLEND_MODE_SCREEN */
            "Color Dodge",   /* 9  - BLEND_MODE_COLOR_DODGE */
            "Linear Dodge",  /* 10 - BLEND_MODE_LINEAR_DODGE (Add) */
            "Lighter Color", /* 11 - BLEND_MODE_LIGHTER_COLOR */
            /* Contrast modes */
            "Overlay",      /* 12 - BLEND_MODE_OVERLAY */
            "Soft Light",   /* 13 - BLEND_MODE_SOFT_LIGHT */
            "Hard Light",   /* 14 - BLEND_MODE_HARD_LIGHT */
            "Vivid Light",  /* 15 - BLEND_MODE_VIVID_LIGHT */
            "Linear Light", /* 16 - BLEND_MODE_LINEAR_LIGHT */
            "Pin Light",    /* 17 - BLEND_MODE_PIN_LIGHT */
            "Hard Mix",     /* 18 - BLEND_MODE_HARD_MIX */
            /* Inversion modes */
            "Difference", /* 19 - BLEND_MODE_DIFFERENCE */
            "Exclusion",  /* 20 - BLEND_MODE_EXCLUSION */
            /* Cancellation modes */
            "Subtract", /* 21 - BLEND_MODE_SUBTRACT */
            "Divide",   /* 22 - BLEND_MODE_DIVIDE */
            /* Component (HSL) modes */
            "Hue",        /* 23 - BLEND_MODE_HUE */
            "Saturation", /* 24 - BLEND_MODE_SATURATION */
            "Color",      /* 25 - BLEND_MODE_COLOR */
            "Luminosity"  /* 26 - BLEND_MODE_LUMINOSITY */
        };

        for (int i = 0; i < BLEND_MODE_COUNT; i++) {
            gtk_list_store_append(blend_store, &iter);
            gtk_list_store_set(blend_store, &iter, 0, blend_modes[i], -1);
        }

        /* Set the model */
        gtk_combo_box_set_model(GTK_COMBO_BOX(layers_panel->combo_blend), GTK_TREE_MODEL(blend_store));
        g_object_unref(blend_store);

        /* Set up cell renderer */
        GtkCellRenderer* cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(layers_panel->combo_blend), cell, TRUE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(layers_panel->combo_blend), cell, "text", 0, NULL);

        /* Set default to Normal */
        gtk_combo_box_set_active(GTK_COMBO_BOX(layers_panel->combo_blend), 0);

        /* Connect signal handler */
        g_signal_connect(layers_panel->combo_blend, "changed",
                         G_CALLBACK(on_blend_mode_changed), layers_panel);
    }

    layers_panel->current_doc = NULL;
    layers_panel->app_context = ctx; /* Store app context for GPU acceleration check */

    return layers_panel;
}

/**
 * Update layers panel with document layers
 */
void layers_panel_update(LayersPanel* layers_panel, ImageDocument* doc) {
    if (!layers_panel) {
        return;
    }

    /* Always set current_doc, even if doc is NULL (when closing document) */
    layers_panel->current_doc = doc;

    if (!doc) {
        /* Clear existing layers */
        gtk_list_store_clear(layers_panel->store);

        /* Disable opacity controls */
        if (layers_panel->scale_opacity) {
            gtk_widget_set_sensitive(layers_panel->scale_opacity, FALSE);
        }
        if (layers_panel->spin_opacity) {
            gtk_widget_set_sensitive(layers_panel->spin_opacity, FALSE);
        }
        if (layers_panel->btn_opacity_reset) {
            gtk_widget_set_sensitive(layers_panel->btn_opacity_reset, FALSE);
        }

        return;
    }

    /* Clear existing layers */
    gtk_list_store_clear(layers_panel->store);

    /* Add all layers from document */
    guint layer_count = document_get_layer_count(doc);

    for (gint i = layer_count - 1; i >= 0; i--) {
        ImageLayer* layer = document_get_layer(doc, i);

        if (layer) {
            GtkTreeIter iter;
            GdkPixbuf* visibility_icon = get_visibility_icon(layer->visible);
            GdkPixbuf* thumbnail = create_layer_thumbnail(layer->surface, 48, layer->visible);

            gtk_list_store_append(layers_panel->store, &iter);
            gtk_list_store_set(layers_panel->store, &iter,
                               0, visibility_icon, /* Visibility icon */
                               1, thumbnail,       /* Thumbnail */
                               2, layer->name,     /* Name */
                               3, layer->visible,  /* Visible state */
                               -1);

            /* Unref pixbufs - the store will take ownership */
            if (visibility_icon) {
                g_object_unref(visibility_icon);
            }
            if (thumbnail) {
                g_object_unref(thumbnail);
            }
        }
    }

    /* Always select the layer at index 0 (last row in tree view since layers are displayed in reverse) */
    if (layer_count > 0 && layers_panel->tree_view) {
        GtkTreeSelection* selection = gtk_tree_view_get_selection(
            GTK_TREE_VIEW(layers_panel->tree_view));
        GtkTreeIter iter;

        /* Navigate to the last row (which corresponds to index 0) */
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(layers_panel->store), &iter)) {
            /* Move to the last row */
            for (guint i = 0; i < layer_count - 1; i++) {
                if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(layers_panel->store), &iter)) {
                    break;
                }
            }
            gtk_tree_selection_select_iter(selection, &iter);
        }

        /* Ensure document's selected layer is set to index 0 */
        ImageLayer* layer_0 = document_get_layer(doc, 0);
        if (layer_0) {
            document_set_selected_layer(doc, layer_0);
        }
    }
}

/**
 * Update thumbnails for all layers in the panel
 * Call this after layer content changes to refresh thumbnails
 */
void layers_panel_refresh_thumbnails(LayersPanel* layers_panel) {
    if (!layers_panel || !layers_panel->current_doc || !layers_panel->store) {
        return;
    }

    guint layer_count = document_get_layer_count(layers_panel->current_doc);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(layers_panel->store), &iter);
    gint row = 0;

    while (valid) {
        /* Get layer from row (layers are displayed in reverse order) */
        gint layer_index = layer_count - 1 - row;

        if (layer_index >= 0 && layer_index < (gint)layer_count) {
            ImageLayer* layer = document_get_layer(layers_panel->current_doc, layer_index);

            if (layer) {
                GdkPixbuf* thumbnail = create_layer_thumbnail(layer->surface, 48, layer->visible);
                GdkPixbuf* visibility_icon = get_visibility_icon(layer->visible);

                if (thumbnail && visibility_icon) {
                    gtk_list_store_set(layers_panel->store, &iter,
                                       0, visibility_icon,
                                       1, thumbnail,
                                       3, layer->visible,
                                       -1);
                    g_object_unref(visibility_icon);
                    g_object_unref(thumbnail);
                }
            }
        }

        row++;
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(layers_panel->store), &iter);
    }
}

/**
 * Update thumbnail for the currently selected layer only
 * Call this after drawing operations to update the selected layer's thumbnail
 */
void layers_panel_update_selected_thumbnail(LayersPanel* layers_panel) {
    ImageLayer* selected_layer;
    GdkPixbuf* thumbnail;
    GdkPixbuf* visibility_icon;
    ImageDocument* doc;
    GtkTreeSelection* selection;
    GtkTreeIter iter;
    gchar* layer_name = NULL;
    ImageLayer* tree_layer = NULL;

    if (!layers_panel || !layers_panel->tree_view || !layers_panel->store) {
        return;
    }

    /* Get the current document from the layers panel */
    doc = layers_panel->current_doc;
    if (!doc) {
        return;
    }

    /* Get the document's selected layer directly */
    selected_layer = document_get_selected_layer(doc);
    if (!selected_layer || !selected_layer->surface) {
        return;
    }

    /* Get the treeview selection to find the row */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(layers_panel->tree_view));
    if (!selection) {
        return;
    }

    if (!gtk_tree_selection_get_selected(selection, NULL, &iter)) {
        return; /* No selection in treeview */
    }

    /* Get the layer name from the selected row */
    gtk_tree_model_get(GTK_TREE_MODEL(layers_panel->store), &iter,
                       2, &layer_name,
                       -1);

    if (!layer_name) {
        return;
    }

    /* Verify this is the same layer as the document's selected layer */
    if (g_strcmp0(layer_name, selected_layer->name) != 0) {
        g_free(layer_name);
        return; /* Treeview selection doesn't match document selection */
    }

    g_free(layer_name);

    /* Generate new thumbnail and visibility icon */
    thumbnail = create_layer_thumbnail(selected_layer->surface, 48, selected_layer->visible);
    visibility_icon = get_visibility_icon(selected_layer->visible);

    if (thumbnail && visibility_icon) {
        gtk_list_store_set(layers_panel->store, &iter,
                           0, visibility_icon,
                           1, thumbnail,
                           3, selected_layer->visible,
                           -1);
        g_object_unref(visibility_icon);
        g_object_unref(thumbnail);
    }
}

/**
 * Select a specific layer in the layers panel
 */
void layers_panel_select_layer(LayersPanel* layers_panel, ImageDocument* doc, ImageLayer* layer) {
    if (!layers_panel || !doc || !layer || !layers_panel->tree_view || !layers_panel->store) {
        return;
    }

    /* Find the layer's index in the document */
    guint layer_count = document_get_layer_count(doc);
    guint layer_index = 0;
    gboolean found = FALSE;

    for (guint i = 0; i < layer_count; i++) {
        if (document_get_layer(doc, i) == layer) {
            layer_index = i;
            found = TRUE;
            break;
        }
    }

    if (!found) {
        return;
    }

    /* Select the corresponding row in the tree view */
    /* Note: layers are displayed in reverse order (index 0 is last row) */
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(layers_panel->tree_view));
    GtkTreeIter iter;

    /* Navigate to the row corresponding to layer_index */
    /* Since layers are displayed in reverse, index 0 is at position (layer_count - 1) */
    guint row_position = layer_count - 1 - layer_index;

    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(layers_panel->store), &iter)) {
        /* Move to the target row */
        for (guint i = 0; i < row_position; i++) {
            if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(layers_panel->store), &iter)) {
                return;
            }
        }
        gtk_tree_selection_select_iter(selection, &iter);
    }

    /* Ensure document's selected layer is set */
    document_set_selected_layer(doc, layer);
}

/**
 * Get the currently selected layer from the panel
 */
ImageLayer* layers_panel_get_selected_layer(LayersPanel* layers_panel) {
    GtkTreeSelection* selection;
    GtkTreeIter iter;
    gchar* layer_name = NULL;

    if (!layers_panel || !layers_panel->current_doc || !layers_panel->tree_view) {
        return NULL;
    }

    if (!layers_panel->store) {
        return NULL;
    }

    /* Get the selection */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(layers_panel->tree_view));

    if (!selection) {
        return NULL;
    }

    if (!gtk_tree_selection_get_selected(selection, NULL, &iter)) {
        return NULL; /* No selection */
    }

    /* Get the layer name from the selected row */
    gtk_tree_model_get(GTK_TREE_MODEL(layers_panel->store), &iter,
                       2, &layer_name,
                       -1);

    if (!layer_name) {
        return NULL;
    }

    /* Find the layer in the document by name */
    guint layer_count = document_get_layer_count(layers_panel->current_doc);
    for (guint i = 0; i < layer_count; i++) {
        ImageLayer* layer = document_get_layer(layers_panel->current_doc, i);
        if (layer && g_strcmp0(layer->name, layer_name) == 0) {
            g_free(layer_name);
            return layer;
        }
    }

    g_free(layer_name);
    return NULL;
}

/**
 * Update opacity controls based on selected layer
 */
void layers_panel_update_opacity_controls(LayersPanel* layers_panel) {
    ImageLayer* selected_layer;
    gdouble opacity_percent;

    if (!layers_panel) {
        return;
    }

    selected_layer = layers_panel_get_selected_layer(layers_panel);

    if (selected_layer) {
        /* Convert opacity from 0.0-1.0 to 0-100 */
        opacity_percent = selected_layer->opacity * 100.0;

        /* Update scale */
        if (layers_panel->scale_opacity) {
            g_signal_handlers_block_by_func(layers_panel->scale_opacity,
                                            G_CALLBACK(on_opacity_scale_changed),
                                            layers_panel);
            gtk_range_set_value(GTK_RANGE(layers_panel->scale_opacity), opacity_percent);
            gtk_widget_set_sensitive(layers_panel->scale_opacity, TRUE);
            g_signal_handlers_unblock_by_func(layers_panel->scale_opacity,
                                              G_CALLBACK(on_opacity_scale_changed),
                                              layers_panel);
        }

        /* Update spin button */
        if (layers_panel->spin_opacity) {
            g_signal_handlers_block_by_func(layers_panel->spin_opacity,
                                            G_CALLBACK(on_opacity_spin_changed),
                                            layers_panel);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(layers_panel->spin_opacity), opacity_percent);
            gtk_widget_set_sensitive(layers_panel->spin_opacity, TRUE);
            g_signal_handlers_unblock_by_func(layers_panel->spin_opacity,
                                              G_CALLBACK(on_opacity_spin_changed),
                                              layers_panel);
        }

        /* Enable reset button */
        if (layers_panel->btn_opacity_reset) {
            gtk_widget_set_sensitive(layers_panel->btn_opacity_reset, TRUE);
        }

        /* Update blend mode combo box */
        if (layers_panel->combo_blend) {
            /* BlendMode enum values map directly to combo box indices */
            gint blend_index = (gint)selected_layer->blend_mode;
            if (blend_index < 0 || blend_index >= BLEND_MODE_COUNT) {
                blend_index = 0; /* Default to Normal */
            }

            g_signal_handlers_block_by_func(layers_panel->combo_blend,
                                            G_CALLBACK(on_blend_mode_changed),
                                            layers_panel);
            gtk_combo_box_set_active(GTK_COMBO_BOX(layers_panel->combo_blend), blend_index);
            gtk_widget_set_sensitive(layers_panel->combo_blend, TRUE);
            g_signal_handlers_unblock_by_func(layers_panel->combo_blend,
                                              G_CALLBACK(on_blend_mode_changed),
                                              layers_panel);
        }
    } else {
        /* No layer selected - disable controls */
        if (layers_panel->scale_opacity) {
            gtk_widget_set_sensitive(layers_panel->scale_opacity, FALSE);
        }
        if (layers_panel->spin_opacity) {
            gtk_widget_set_sensitive(layers_panel->spin_opacity, FALSE);
        }
        if (layers_panel->btn_opacity_reset) {
            gtk_widget_set_sensitive(layers_panel->btn_opacity_reset, FALSE);
        }
        if (layers_panel->combo_blend) {
            gtk_widget_set_sensitive(layers_panel->combo_blend, FALSE);
        }
    }
}

/**
 * Update button sensitivity based on state
 */
void layers_panel_update_button_sensitivity(LayersPanel* layers_panel,
                                            gboolean has_document,
                                            gboolean has_selection,
                                            ImageDocument* doc,
                                            ImageLayer* selected_layer) {
    if (!layers_panel) {
        return;
    }

    /* New button: enabled when document exists */
    if (layers_panel->btn_new) {
        gtk_widget_set_sensitive(layers_panel->btn_new, has_document);
    }

    /* Delete and Duplicate: enabled when document exists AND layer is selected */
    gboolean delete_dup_enabled = has_document && has_selection;

    if (layers_panel->btn_delete) {
        gtk_widget_set_sensitive(layers_panel->btn_delete, delete_dup_enabled);
    }

    if (layers_panel->btn_duplicate) {
        gtk_widget_set_sensitive(layers_panel->btn_duplicate, delete_dup_enabled);
    }

    /* Move Up/Down: enabled when document exists, layer is selected, AND move is possible */
    gboolean can_move_up = FALSE;
    gboolean can_move_down = FALSE;

    if (has_document && has_selection && doc && selected_layer) {
        can_move_up = document_layer_can_move_up(doc, selected_layer);
        can_move_down = document_layer_can_move_down(doc, selected_layer);
    }

    if (layers_panel->btn_up) {
        gtk_widget_set_sensitive(layers_panel->btn_up, can_move_up);
    }

    if (layers_panel->btn_down) {
        gtk_widget_set_sensitive(layers_panel->btn_down, can_move_down);
    }
}

/**
 * Connect layers panel buttons to UI callbacks
 */
void layers_panel_connect_buttons(LayersPanel* layers_panel,
                                  GCallback new_callback,
                                  GCallback delete_callback,
                                  GCallback duplicate_callback,
                                  gpointer user_data) {
    GtkTreeSelection* selection;

    if (!layers_panel) {
        return;
    }

    /* Store app context for use in callbacks */
    layers_panel->app_context = user_data;

    g_signal_connect(layers_panel->btn_new, "clicked",
                     new_callback, user_data);
    g_signal_connect(layers_panel->btn_delete, "clicked",
                     delete_callback, user_data);
    g_signal_connect(layers_panel->btn_duplicate, "clicked",
                     duplicate_callback, user_data);

    /* Note: Layer selection change callback is connected in ui.c's ui_create_main_window */
    /* This avoids double-connecting and signature mismatches */

    /* Initialize buttons as disabled (no document open) */
    layers_panel_update_button_sensitivity(layers_panel, FALSE, FALSE, NULL, NULL);
}

/**
 * Free a layers panel
 */
void layers_panel_free(LayersPanel* layers_panel) {
    if (!layers_panel) {
        return;
    }

    if (layers_panel->store) {
        g_object_unref(layers_panel->store);
    }
    g_free(layers_panel);
}

/**
 * Get the main panel widget from layers panel
 */
GtkWidget* layers_panel_get_panel(LayersPanel* layers_panel) {
    if (!layers_panel || !layers_panel->tree_view) {
        return NULL;
    }

    /* Get builder from tree view */
    GtkBuilder* builder = GTK_BUILDER(g_object_get_data(G_OBJECT(layers_panel->tree_view), "builder"));
    if (!builder) {
        return NULL;
    }

    /* Get the panel widget from builder */
    return GTK_WIDGET(gtk_builder_get_object(builder, "layers_panel"));
}
