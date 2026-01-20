#include "ui/layers_panel.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "ui.h"
#include "ui/swatches.h"
#include "ui/tools_panel.h"
#include "ui/ui_utils.h"
#include "ui/widgets/accordion.h"
#include "ui/widgets/swatches_widget.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Static references to swatches widgets */
static SwatchesWidget* g_recent_colors_widget = NULL;
static SwatchesWidget* g_main_swatches_widget = NULL;

/* Forward declarations */
static void on_recent_color_selected(SwatchesWidget* widget, gint index, gpointer user_data);
static void on_swatch_selected(SwatchesWidget* widget, gint index, gpointer user_data);
static GdkPixbuf* create_layer_thumbnail(cairo_surface_t* layer_surface, gint thumb_size, gboolean visible);
static GdkPixbuf* get_visibility_icon(gboolean visible);
static GtkWidget* create_overview_widget(LayersPanel* layers_panel);
static gboolean on_overview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data);
static gboolean on_overview_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_overview_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_overview_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_overview_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);

/**
 * Overview widget state structure
 */
typedef struct {
    gboolean is_dragging;          /* Whether user is dragging the rectangle */
    gboolean is_hovering;          /* Whether mouse is hovering over rectangle */
    gdouble drag_start_x;          /* Starting X position of drag (in widget coordinates) */
    gdouble drag_start_y;          /* Starting Y position of drag (in widget coordinates) */
    gdouble drag_start_viewport_x; /* Starting viewport X position */
    gdouble drag_start_viewport_y; /* Starting viewport Y position */
} OverviewWidgetState;

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
    GtkTreeIter iter;
    gboolean visible;
    GdkPixbuf* visibility_icon;
    ImageLayer* layer;

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

    /* Get the row */
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(layers_panel->store), &iter, path)) {
        gtk_tree_path_free(path);
        return FALSE;
    }

    /* Get current visibility value */
    gtk_tree_model_get(GTK_TREE_MODEL(layers_panel->store), &iter,
                       3, &visible,
                       -1);

    /* Toggle visibility */
    visible = !visible;

    /* Update layer visibility in document */
    if (layers_panel->current_doc) {
        /* Get the layer from the row */
        gint* indices = gtk_tree_path_get_indices(path);
        gint row = indices[0];
        guint layer_count = document_get_layer_count(layers_panel->current_doc);

        /* Layers are displayed in reverse order */
        gint layer_index = layer_count - 1 - row;

        if (layer_index >= 0 && layer_index < (gint)layer_count) {
            layer = document_get_layer(layers_panel->current_doc, layer_index);
            if (layer) {
                layer->visible = visible;
                /* Mark composite as dirty and trigger redraw */
                document_invalidate_composite(layers_panel->current_doc);

                /* Update thumbnail with new visibility state */
                GdkPixbuf* thumbnail = create_layer_thumbnail(layer->surface, 48, visible);
                visibility_icon = get_visibility_icon(visible);

                if (thumbnail && visibility_icon) {
                    gtk_list_store_set(layers_panel->store, &iter,
                                       0, visibility_icon,
                                       1, thumbnail,
                                       3, visible,
                                       -1);
                    g_object_unref(visibility_icon);
                    g_object_unref(thumbnail);
                }
            }
        }
    }

    gtk_tree_path_free(path);
    // printf("Layer visibility toggled to %s\n", visible ? "visible" : "hidden");
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

    /* Map combo box index to BlendMode enum */
    switch (active) {
        case 0:
            blend_mode = BLEND_MODE_NORMAL;
            break;
        case 1:
            blend_mode = BLEND_MODE_DARKEN;
            break;
        case 2:
            blend_mode = BLEND_MODE_MULTIPLY;
            break;
        case 3:
            blend_mode = BLEND_MODE_COLOR_BURN;
            break;
        case 4:
            blend_mode = BLEND_MODE_LIGHTEN;
            break;
        case 5:
            blend_mode = BLEND_MODE_SCREEN;
            break;
        case 6:
            blend_mode = BLEND_MODE_COLOR_DODGE;
            break;
        case 7:
            blend_mode = BLEND_MODE_OVERLAY;
            break;
        case 8:
            blend_mode = BLEND_MODE_SOFT_LIGHT;
            break;
        case 9:
            blend_mode = BLEND_MODE_HARD_LIGHT;
            break;
        case 10:
            blend_mode = BLEND_MODE_DIFFERENCE;
            break;
        default:
            blend_mode = BLEND_MODE_NORMAL;
            break;
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

    // printf("Layer blend mode changed to %d\n", blend_mode);
}

/**
 * Opacity scale changed callback
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

    /* Update layer opacity (convert from 0-100 to 0.0-1.0) */
    selected_layer->opacity = value / 100.0;

    /* Invalidate layer cache since opacity affects rendering */
    layer_invalidate_cache(selected_layer);

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

    /* Invalidate entire composite (opacity affects how layer composites) */
    document_invalidate_composite(layers_panel->current_doc);

    /* Queue redraw */
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

    // printf("Layer name changed to: %s\n", new_text);
}

/**
 * Layer tree view row activated callback
 */
static gboolean on_layer_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                                       GtkTreeViewColumn* column, gpointer user_data) {
    (void)tree_view;
    (void)path;
    (void)column;
    (void)user_data;

    // printf("Layer selected\n");
    return FALSE;
}

/**
 * Create a thumbnail from a layer surface
 */
static GdkPixbuf* create_layer_thumbnail(cairo_surface_t* layer_surface, gint thumb_size, gboolean visible) {
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

    /* Calculate scale to fit thumbnail size */
    scale_x = (gdouble)thumb_size / layer_width;
    scale_y = (gdouble)thumb_size / layer_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    gint thumb_width = (gint)(layer_width * scale);
    gint thumb_height = (gint)(layer_height * scale);

    /* Create thumbnail surface */
    thumb_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, thumb_size, thumb_size);
    if (!thumb_surface) {
        return NULL;
    }

    cr = cairo_create(thumb_surface);

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Draw layer scaled and centered */
    cairo_save(cr);
    cairo_translate(cr, (thumb_size - thumb_width) / 2.0, (thumb_size - thumb_height) / 2.0);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, layer_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);
    cairo_restore(cr);

    /* If not visible, apply grayscale/disabled effect */
    if (!visible) {
        /* Convert to grayscale and reduce opacity */
        cairo_save(cr);
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
 * Overview widget draw callback
 */
static gboolean on_overview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageDocument* doc;
    cairo_surface_t* composite;
    GdkPixbuf* thumbnail_pixbuf = NULL;
    GdkPixbuf* scaled_thumb = NULL;
    gint widget_width, widget_height;
    gint doc_width, doc_height;
    gdouble scale_x, scale_y, scale;
    gint thumb_width, thumb_height;
    gint thumb_x, thumb_y;
    gint viewport_x, viewport_y;
    gint viewport_width, viewport_height;
    GtkAdjustment *hadj = NULL, *vadj = NULL;
    gdouble zoom_factor;

    if (!layers_panel) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc) {
        /* Draw empty state */
        widget_width = gtk_widget_get_allocated_width(widget);
        widget_height = gtk_widget_get_allocated_height(widget);
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_rectangle(cr, 0, 0, widget_width, widget_height);
        cairo_fill(cr);
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    doc_width = doc->width;
    doc_height = doc->height;

    if (doc_width <= 0 || doc_height <= 0) {
        return FALSE;
    }

    /* Calculate scale to fit thumbnail */
    scale_x = (gdouble)(widget_width - 8) / doc_width;
    scale_y = (gdouble)(widget_height - 8) / doc_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    thumb_width = (gint)(doc_width * scale);
    thumb_height = (gint)(doc_height * scale);
    thumb_x = (widget_width - thumb_width) / 2;
    thumb_y = (widget_height - thumb_height) / 2;

    /* TILE-BASED: Generate thumbnail directly from tiles at thumbnail size (much faster) */
    composite = document_generate_thumbnail_from_tiles(doc, thumb_width, thumb_height);
    if (!composite) {
        /* Draw empty state if thumbnail generation failed */
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_rectangle(cr, 0, 0, widget_width, widget_height);
        cairo_fill(cr);
        return FALSE;
    }

    /* Convert thumbnail surface to pixbuf */
    scaled_thumb = cairo_surface_to_pixbuf(composite, TRUE);
    cairo_surface_destroy(composite);

    if (!scaled_thumb) {
        return FALSE;
    }

    /* Draw checkered background behind thumbnail (clipped to thumbnail bounds) */
    cairo_save(cr);
    cairo_rectangle(cr, thumb_x, thumb_y, thumb_width, thumb_height);
    cairo_clip(cr);
    cairo_translate(cr, thumb_x, thumb_y);
    draw_checkered_background(cr, thumb_width, thumb_height);
    cairo_restore(cr);

    /* Draw thumbnail */
    gdk_cairo_set_source_pixbuf(cr, scaled_thumb, thumb_x, thumb_y);
    cairo_paint(cr);
    g_object_unref(scaled_thumb);

    /* Get viewport information from scrolled window */
    if (doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        zoom_factor = document_get_zoom(doc);

        if (hadj && vadj) {
            /* Calculate viewport rectangle in document coordinates */
            viewport_x = (gint)(gtk_adjustment_get_value(hadj) / zoom_factor);
            viewport_y = (gint)(gtk_adjustment_get_value(vadj) / zoom_factor);
            viewport_width = (gint)(gtk_adjustment_get_page_size(hadj) / zoom_factor);
            viewport_height = (gint)(gtk_adjustment_get_page_size(vadj) / zoom_factor);

            /* Convert to thumbnail coordinates */
            gint rect_x = thumb_x + (gint)(viewport_x * scale);
            gint rect_y = thumb_y + (gint)(viewport_y * scale);
            gint rect_w = (gint)(viewport_width * scale);
            gint rect_h = (gint)(viewport_height * scale);

            /* Clamp rectangle to thumbnail boundaries */
            if (rect_x < thumb_x) {
                rect_w -= (thumb_x - rect_x);
                rect_x = thumb_x;
            }
            if (rect_y < thumb_y) {
                rect_h -= (thumb_y - rect_y);
                rect_y = thumb_y;
            }
            if (rect_x + rect_w > thumb_x + thumb_width) {
                rect_w = (thumb_x + thumb_width) - rect_x;
            }
            if (rect_y + rect_h > thumb_y + thumb_height) {
                rect_h = (thumb_y + thumb_height) - rect_y;
            }

            /* Only draw rectangle if it's within thumbnail bounds */
            if (rect_w > 0 && rect_h > 0 &&
                rect_x >= thumb_x && rect_y >= thumb_y &&
                rect_x + rect_w <= thumb_x + thumb_width &&
                rect_y + rect_h <= thumb_y + thumb_height) {

                /* Get hover state */
                OverviewWidgetState* state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
                gboolean is_hovering = FALSE;
                if (state) {
                    is_hovering = state->is_hovering || state->is_dragging;
                }

                /* Draw selection rectangle */
                cairo_save(cr);

                /* Draw hover highlight fill if hovering */
                if (is_hovering) {
                    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.2); /* Light blue fill */
                    cairo_rectangle(cr, rect_x, rect_y, rect_w, rect_h);
                    cairo_fill(cr);
                }

                /* Draw border - brighter when hovering */
                cairo_set_line_width(cr, 2.0);
                if (is_hovering) {
                    cairo_set_source_rgba(cr, 0.0, 0.7, 1.0, 1.0); /* Brighter blue when hovering */
                } else {
                    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.8); /* Blue with transparency */
                }
                cairo_rectangle(cr, rect_x, rect_y, rect_w, rect_h);
                cairo_stroke(cr);

                /* Draw inner border for better visibility */
                cairo_set_line_width(cr, 1.0);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9); /* White inner border */
                cairo_rectangle(cr, rect_x + 1, rect_y + 1, rect_w - 2, rect_h - 2);
                cairo_stroke(cr);
                cairo_restore(cr);
            }
        }
    }

    return FALSE;
}

/**
 * Calculate selection rectangle coordinates in widget space
 * Returns TRUE if rectangle is valid, FALSE otherwise
 */
static gboolean get_selection_rectangle(LayersPanel* layers_panel, GtkWidget* widget,
                                        gint* rect_x, gint* rect_y, gint* rect_w, gint* rect_h,
                                        gdouble* scale_out) {
    ImageDocument* doc;
    gint widget_width, widget_height;
    gint doc_width, doc_height;
    gdouble scale_x, scale_y, scale;
    gint thumb_width, thumb_height;
    gint thumb_x, thumb_y;
    gint viewport_x, viewport_y;
    gint viewport_width, viewport_height;
    GtkAdjustment *hadj = NULL, *vadj = NULL;
    gdouble zoom_factor;

    if (!layers_panel || !widget) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc || !doc->scrolled_window || !GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);
    doc_width = doc->width;
    doc_height = doc->height;

    if (doc_width <= 0 || doc_height <= 0) {
        return FALSE;
    }

    /* Calculate scale to fit thumbnail */
    scale_x = (gdouble)(widget_width - 8) / doc_width;
    scale_y = (gdouble)(widget_height - 8) / doc_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    thumb_width = (gint)(doc_width * scale);
    thumb_height = (gint)(doc_height * scale);
    thumb_x = (widget_width - thumb_width) / 2;
    thumb_y = (widget_height - thumb_height) / 2;

    hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    zoom_factor = document_get_zoom(doc);

    if (!hadj || !vadj) {
        return FALSE;
    }

    /* Calculate viewport rectangle in document coordinates */
    viewport_x = (gint)(gtk_adjustment_get_value(hadj) / zoom_factor);
    viewport_y = (gint)(gtk_adjustment_get_value(vadj) / zoom_factor);
    viewport_width = (gint)(gtk_adjustment_get_page_size(hadj) / zoom_factor);
    viewport_height = (gint)(gtk_adjustment_get_page_size(vadj) / zoom_factor);

    /* Convert to thumbnail coordinates */
    *rect_x = thumb_x + (gint)(viewport_x * scale);
    *rect_y = thumb_y + (gint)(viewport_y * scale);
    *rect_w = (gint)(viewport_width * scale);
    *rect_h = (gint)(viewport_height * scale);

    /* Clamp rectangle to thumbnail boundaries */
    if (*rect_x < thumb_x) {
        *rect_w -= (thumb_x - *rect_x);
        *rect_x = thumb_x;
    }
    if (*rect_y < thumb_y) {
        *rect_h -= (thumb_y - *rect_y);
        *rect_y = thumb_y;
    }
    if (*rect_x + *rect_w > thumb_x + thumb_width) {
        *rect_w = (thumb_x + thumb_width) - *rect_x;
    }
    if (*rect_y + *rect_h > thumb_y + thumb_height) {
        *rect_h = (thumb_y + thumb_height) - *rect_y;
    }

    if (scale_out) {
        *scale_out = scale;
    }

    /* Check if rectangle is valid */
    return (*rect_w > 0 && *rect_h > 0 &&
            *rect_x >= thumb_x && *rect_y >= thumb_y &&
            *rect_x + *rect_w <= thumb_x + thumb_width &&
            *rect_y + *rect_h <= thumb_y + thumb_height);
}

/**
 * Check if point is inside selection rectangle
 */
static gboolean point_in_selection_rectangle(LayersPanel* layers_panel, GtkWidget* widget,
                                             gdouble x, gdouble y) {
    gint rect_x, rect_y, rect_w, rect_h;

    if (!get_selection_rectangle(layers_panel, widget, &rect_x, &rect_y, &rect_w, &rect_h, NULL)) {
        return FALSE;
    }

    return (x >= rect_x && x <= rect_x + rect_w &&
            y >= rect_y && y <= rect_y + rect_h);
}

/**
 * Overview widget button press handler
 */
static gboolean on_overview_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageDocument* doc;
    OverviewWidgetState* state;
    gint rect_x, rect_y, rect_w, rect_h;
    gdouble scale;

    if (!layers_panel || event->button != 1) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get or create state */
    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (!state) {
        state = (OverviewWidgetState*)g_malloc0(sizeof(OverviewWidgetState));
        g_object_set_data_full(G_OBJECT(widget), "overview_state", state, g_free);
    }

    /* Check if click is inside selection rectangle */
    if (get_selection_rectangle(layers_panel, widget, &rect_x, &rect_y, &rect_w, &rect_h, &scale)) {
        if (event->x >= rect_x && event->x <= rect_x + rect_w &&
            event->y >= rect_y && event->y <= rect_y + rect_h) {
            /* Start dragging */
            state->is_dragging = TRUE;
            state->drag_start_x = event->x;
            state->drag_start_y = event->y;

            /* Store current viewport position */
            GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
            GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));

            if (hadj && vadj) {
                state->drag_start_viewport_x = gtk_adjustment_get_value(hadj);
                state->drag_start_viewport_y = gtk_adjustment_get_value(vadj);
            }

            gtk_widget_queue_draw(widget);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * Overview widget button release handler
 */
static gboolean on_overview_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    OverviewWidgetState* state;

    (void)user_data; /* Unused */

    if (event->button != 1) {
        return FALSE;
    }

    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (state && state->is_dragging) {
        state->is_dragging = FALSE;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    return FALSE;
}

/**
 * Overview widget motion notify handler
 */
static gboolean on_overview_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageDocument* doc;
    OverviewWidgetState* state;
    gboolean was_hovering;
    gint rect_x, rect_y, rect_w, rect_h;
    gdouble scale;
    gdouble new_viewport_x, new_viewport_y;

    if (!layers_panel) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get or create state */
    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (!state) {
        state = (OverviewWidgetState*)g_malloc0(sizeof(OverviewWidgetState));
        g_object_set_data_full(G_OBJECT(widget), "overview_state", state, g_free);
    }

    was_hovering = state->is_hovering;

    /* Check if mouse is over selection rectangle */
    state->is_hovering = point_in_selection_rectangle(layers_panel, widget, event->x, event->y);

    /* Update hover state if changed */
    if (was_hovering != state->is_hovering) {
        gtk_widget_queue_draw(widget);
    }

    /* Handle dragging */
    if (state->is_dragging && get_selection_rectangle(layers_panel, widget, &rect_x, &rect_y, &rect_w, &rect_h, &scale)) {
        /* Calculate mouse movement in widget coordinates */
        gdouble delta_x = event->x - state->drag_start_x;
        gdouble delta_y = event->y - state->drag_start_y;

        /* Convert to document coordinates */
        gdouble delta_doc_x = delta_x / scale;
        gdouble delta_doc_y = delta_y / scale;

        /* Calculate new viewport position */
        gdouble zoom_factor = document_get_zoom(doc);
        new_viewport_x = state->drag_start_viewport_x + (delta_doc_x * zoom_factor);
        new_viewport_y = state->drag_start_viewport_y + (delta_doc_y * zoom_factor);

        /* Update scroll adjustments */
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));

        if (hadj && vadj) {
            /* Clamp to valid range */
            gdouble h_min = gtk_adjustment_get_lower(hadj);
            gdouble h_max = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
            gdouble v_min = gtk_adjustment_get_lower(vadj);
            gdouble v_max = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);

            if (new_viewport_x < h_min)
                new_viewport_x = h_min;
            if (new_viewport_x > h_max)
                new_viewport_x = h_max;
            if (new_viewport_y < v_min)
                new_viewport_y = v_min;
            if (new_viewport_y > v_max)
                new_viewport_y = v_max;

            /* Update scroll position */
            gtk_adjustment_set_value(hadj, new_viewport_x);
            gtk_adjustment_set_value(vadj, new_viewport_y);
        }

        return TRUE;
    }

    return FALSE;
}

/**
 * Overview widget leave notify handler
 */
static gboolean on_overview_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    OverviewWidgetState* state;

    (void)event;     /* Unused */
    (void)user_data; /* Unused */

    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (state && state->is_hovering) {
        state->is_hovering = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

/**
 * Create overview widget for composite thumbnail
 */
static GtkWidget* create_overview_widget(LayersPanel* layers_panel) {
    GtkWidget* drawing_area;
    OverviewWidgetState* state;

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 100); /* Fixed height of 100 */
    gtk_widget_set_vexpand(drawing_area, FALSE);        /* Don't expand vertically */
    gtk_widget_set_hexpand(drawing_area, TRUE);         /* Expand horizontally */

    /* Enable mouse events */
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    /* Connect signals */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_overview_draw), layers_panel);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_overview_button_press), layers_panel);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_overview_button_release), layers_panel);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_overview_motion_notify), layers_panel);
    g_signal_connect(drawing_area, "leave-notify-event", G_CALLBACK(on_overview_leave_notify), layers_panel);

    /* Create and store state */
    state = (OverviewWidgetState*)g_malloc0(sizeof(OverviewWidgetState));
    g_object_set_data_full(G_OBJECT(drawing_area), "overview_state", state, g_free);

    /* Store layers panel reference */
    g_object_set_data(G_OBJECT(drawing_area), "layers_panel", layers_panel);

    return drawing_area;
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
    // printf("New layer button clicked (handled by UI callback)\n");
}

static void on_panel_btn_delete_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Context passed differently */
    // printf("Delete layer button clicked (handled by UI callback)\n");
}

static void on_panel_btn_duplicate_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Context passed differently */
    // printf("Duplicate layer button clicked (handled by UI callback)\n");
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

        /* Fallback: create empty panel */
        layers_panel->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
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
        layers_panel->overview_widget = NULL;
        layers_panel->current_doc = NULL;
        layers_panel->app_context = NULL;
        return layers_panel;
    }

    /* Get the main Glade panel container */
    GtkWidget* glade_panel = GTK_WIDGET(gtk_builder_get_object(builder, "layers_panel"));
    if (!glade_panel) {
        g_warning("Failed to get layers_panel from builder");
        g_object_unref(builder);
        layers_panel->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
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
        layers_panel->app_context = NULL;
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

    /* Create accordion widget - this becomes the main panel container */
    layers_panel->accordion = accordion_new();
    layers_panel->panel = accordion_get_widget(layers_panel->accordion);

    /* Keep builder alive by storing it on the panel as object data */
    g_object_set_data_full(G_OBJECT(layers_panel->panel), "builder", builder, g_object_unref);

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
                     G_CALLBACK(on_layer_row_activated), NULL);
    g_signal_connect(layers_panel->tree_view, "button-press-event",
                     G_CALLBACK(on_treeview_button_press), layers_panel);
    gtk_container_add(GTK_CONTAINER(scroll_window), layers_panel->tree_view);

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
    g_object_set(renderer, "xpad", 4, "ypad", 4, NULL);
    column = gtk_tree_view_column_new_with_attributes("Thumbnail",
                                                      renderer,
                                                      "pixbuf", 1,
                                                      NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 48);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Name column (editable) */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
                 "editable", TRUE,
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
        g_signal_connect(layers_panel->scale_opacity, "value-changed",
                         G_CALLBACK(on_opacity_scale_changed), layers_panel);
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
        /* Create list store for blend modes */
        GtkListStore* blend_store = gtk_list_store_new(1, G_TYPE_STRING);
        GtkTreeIter iter;

        /* Add blend mode options */
        const char* blend_modes[] = {
            "Normal",      /* 0 */
            "Darken",      /* 1 */
            "Multiply",    /* 2 */
            "Color burn",  /* 3 */
            "Lighten",     /* 4 */
            "Screen",      /* 5 */
            "Color dodge", /* 6 */
            "Overlay",     /* 7 */
            "Soft light",  /* 8 */
            "Hard light",  /* 9 */
            "Difference"   /* 10 */
        };

        for (int i = 0; i < 11; i++) {
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

    /* Create overview widget for composite thumbnail */
    layers_panel->overview_widget = create_overview_widget(layers_panel);

    /* Load swatches panel from Glade */
    GtkBuilder* swatches_builder = gtk_builder_new();
    GtkWidget* swatches_panel = NULL;
    error = NULL; /* Clear error before loading swatches panel */
    if (gtk_builder_add_from_resource(swatches_builder, "/ui/swatches_panel.glade", &error)) {
        swatches_panel = GTK_WIDGET(gtk_builder_get_object(swatches_builder, "swatches_panel"));
        if (swatches_panel) {
            /* Keep builder alive by storing it on the widget as object data */
            g_object_set_data_full(G_OBJECT(swatches_panel), "builder", swatches_builder, g_object_unref);
            /* Ensure swatches panel content expands vertically */
            gtk_widget_set_vexpand(swatches_panel, TRUE);
            gtk_widget_set_hexpand(swatches_panel, TRUE);

            /* Get swatches_recent_colors_box and create recent colors widget */
            GtkWidget* swatches_recent_colors_box = GTK_WIDGET(gtk_builder_get_object(swatches_builder, "swatches_recent_colors_box"));
            if (swatches_recent_colors_box) {

                /* Create recent colors widget (up to 9 colors) */
                SwatchesWidget* recent_colors_widget = SWATCHES_WIDGET(swatches_widget_new());
                if (recent_colors_widget) {
                    swatches_widget_set_spacing(recent_colors_widget, 1.0);
                    swatches_widget_set_padding(recent_colors_widget, 2.0);
                    swatches_widget_set_max_swatch_size(recent_colors_widget, 20.0);

                    gtk_widget_set_size_request(GTK_WIDGET(recent_colors_widget), -1, 28);

                    /* Make widget expand horizontally but not vertically */
                    gtk_widget_set_hexpand(GTK_WIDGET(recent_colors_widget), TRUE);
                    gtk_widget_set_vexpand(GTK_WIDGET(recent_colors_widget), FALSE);

                    /* Store widget reference in swatches panel for color tracking */
                    g_object_set_data(G_OBJECT(swatches_panel), "recent_colors_widget", recent_colors_widget);

                    /* Store in static variable for easy access */
                    g_recent_colors_widget = recent_colors_widget;

                    /* Also store in main window for global access */
                    if (ctx && ctx->window) {
                        g_object_set_data(G_OBJECT(ctx->window), "recent_colors_widget", recent_colors_widget);
                        /* Sync recent colors from app context to widget */
                        swatches_sync_to_widgets(&ctx->swatches, NULL, GTK_WIDGET(recent_colors_widget));
                    }

                    /* Connect swatch selection signal to set foreground color */
                    g_signal_connect(recent_colors_widget, "swatch-selected",
                                     G_CALLBACK(on_recent_color_selected), ctx);

                    /* Add to swatches_recent_colors_box */
                    gtk_container_add(GTK_CONTAINER(swatches_recent_colors_box), GTK_WIDGET(recent_colors_widget));

                    /* Ensure the box expands horizontally */
                    gtk_widget_set_hexpand(swatches_recent_colors_box, TRUE);
                    gtk_widget_set_vexpand(swatches_recent_colors_box, FALSE);

                    /* Show all widgets in the hierarchy */
                    gtk_widget_show_all(swatches_recent_colors_box);

                    /* Force a queue resize to ensure proper layout */
                    gtk_widget_queue_resize(GTK_WIDGET(recent_colors_widget));
                    gtk_widget_queue_resize(swatches_recent_colors_box);
                } else {
                    g_warning("Failed to create recent colors widget");
                }
            } else {
                g_warning("Failed to get swatches_recent_colors_box from builder");
            }

            /* Apply CSS to remove padding from swatches buttons */
            /* Remove padding from all swatches control buttons */
            const gchar* button_ids[] = {
                "swatches_add_button",
                "swatches_delete_button",
                "swatches_reset_button",
                "swatches_import_button",
                "swatches_export_button",
            };

            GtkCssProvider* provider = gtk_css_provider_new();
            gtk_css_provider_load_from_data(provider, "button { padding: 0px; padding-right: 5px; padding-left: 5px; margin: 0px; }", -1, NULL);

            for (gint i = 0; i < 5; i++) {
                GtkWidget* button = GTK_WIDGET(gtk_builder_get_object(swatches_builder, button_ids[i]));
                if (button) {
                    GtkStyleContext* context = gtk_widget_get_style_context(button);
                    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                }
            }

            g_object_unref(provider);

            /* Get scrolled window and create swatches widget */
            GtkWidget* swatches_scroll = GTK_WIDGET(gtk_builder_get_object(swatches_builder, "swatches_scroll"));
            if (swatches_scroll) {
                SwatchesWidget* swatches_widget = SWATCHES_WIDGET(swatches_widget_new());
                if (swatches_widget) {
                    /* Configure widget */
                    /* Columns are now calculated automatically based on widget size */
                    swatches_widget_set_spacing(swatches_widget, 1.0);
                    swatches_widget_set_padding(swatches_widget, 4.0);

                    /* Store reference */
                    g_main_swatches_widget = swatches_widget;

                    /* Connect swatch selection signal to update foreground color */
                    g_signal_connect(swatches_widget, "swatch-selected",
                                     G_CALLBACK(on_swatch_selected), ctx);

                    /* Store reference */
                    g_main_swatches_widget = swatches_widget;

                    /* Store in main window for global access */
                    if (ctx && ctx->window) {
                        g_object_set_data(G_OBJECT(ctx->window), "main_swatches_widget", swatches_widget);

                        /* If no swatches loaded, add default gradient */
                        if (ctx->swatches.main_swatch_count == 0) {
                            /* Add default swatches - create a color gradient */
                            GdkRGBA color;
                            for (gint row = 0; row < 8; row++) {
                                for (gint col = 0; col < 16; col++) {
                                    /* Calculate hue (0-360) based on column */
                                    gdouble hue = (col / 15.0) * 360.0;
                                    /* Calculate saturation and value based on row */
                                    gdouble saturation = 0.3 + (row / 7.0) * 0.7; /* 0.3 to 1.0 */
                                    gdouble value = 1.0 - (row / 7.0) * 0.5;      /* 1.0 to 0.5 */

                                    /* Convert HSV to RGB */
                                    gdouble c = value * saturation;
                                    gdouble x = c * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
                                    gdouble m = value - c;

                                    gdouble r = 0.0, g = 0.0, b = 0.0;
                                    if (hue < 60.0) {
                                        r = c;
                                        g = x;
                                        b = 0.0;
                                    } else if (hue < 120.0) {
                                        r = x;
                                        g = c;
                                        b = 0.0;
                                    } else if (hue < 180.0) {
                                        r = 0.0;
                                        g = c;
                                        b = x;
                                    } else if (hue < 240.0) {
                                        r = 0.0;
                                        g = x;
                                        b = c;
                                    } else if (hue < 300.0) {
                                        r = x;
                                        g = 0.0;
                                        b = c;
                                    } else {
                                        r = c;
                                        g = 0.0;
                                        b = x;
                                    }

                                    color.red = r + m;
                                    color.green = g + m;
                                    color.blue = b + m;
                                    color.alpha = 1.0;

                                    swatches_add_main(&ctx->swatches, &color, NULL);
                                }
                            }
                        }
                        /* Always sync swatches data to widget (either loaded or defaults) */
                        swatches_sync_to_widgets(&ctx->swatches, GTK_WIDGET(swatches_widget), NULL);
                    }

                    /* Add widget to scrolled window */
                    gtk_container_add(GTK_CONTAINER(swatches_scroll), GTK_WIDGET(swatches_widget));
                    gtk_widget_show_all(GTK_WIDGET(swatches_widget));
                }
            }
        } else {
            g_warning("Failed to get swatches_panel from builder");
            g_object_unref(swatches_builder);
        }
    } else {
        g_warning("Failed to load swatches_panel.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(swatches_builder);
    }

    /* Note: Swatches will be loaded after UI is fully created via layers_panel_load_swatches() */
    /* This is called from ui.c after the layers panel is added to the main window */

    /* Ensure layers panel content expands vertically */
    gtk_widget_set_vexpand(glade_panel, TRUE);
    gtk_widget_set_hexpand(glade_panel, TRUE);

    /* Add overview section first, then swatches section, then layers section */
    accordion_add_section(layers_panel->accordion, "Overview", layers_panel->overview_widget);
    if (swatches_panel) {
        accordion_add_section(layers_panel->accordion, "Swatches", swatches_panel);
    }
    accordion_add_section(layers_panel->accordion, "Layers", glade_panel);

    /* Make accordion expand to fill available vertical space */
    gtk_widget_set_vexpand(layers_panel->panel, TRUE);
    gtk_widget_set_hexpand(layers_panel->panel, TRUE);

    layers_panel->current_doc = NULL;
    layers_panel->app_context = NULL;

    gtk_widget_show_all(layers_panel->panel);

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

        /* Update overview widget */
        if (layers_panel->overview_widget) {
            gtk_widget_queue_draw(layers_panel->overview_widget);
        }

        return;
    }

    /* Clear existing layers */
    gtk_list_store_clear(layers_panel->store);

    /* Update overview widget to show new document */
    if (layers_panel->overview_widget) {
        gtk_widget_queue_draw(layers_panel->overview_widget);
    }

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

    // printf("Layers panel updated with %u layers\n", layer_count);

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
            gint blend_index = 0;

            /* Map BlendMode enum to combo box index */
            switch (selected_layer->blend_mode) {
                case BLEND_MODE_NORMAL:
                    blend_index = 0;
                    break;
                case BLEND_MODE_DARKEN:
                    blend_index = 1;
                    break;
                case BLEND_MODE_MULTIPLY:
                    blend_index = 2;
                    break;
                case BLEND_MODE_COLOR_BURN:
                    blend_index = 3;
                    break;
                case BLEND_MODE_LIGHTEN:
                    blend_index = 4;
                    break;
                case BLEND_MODE_SCREEN:
                    blend_index = 5;
                    break;
                case BLEND_MODE_COLOR_DODGE:
                    blend_index = 6;
                    break;
                case BLEND_MODE_OVERLAY:
                    blend_index = 7;
                    break;
                case BLEND_MODE_SOFT_LIGHT:
                    blend_index = 8;
                    break;
                case BLEND_MODE_HARD_LIGHT:
                    blend_index = 9;
                    break;
                case BLEND_MODE_DIFFERENCE:
                    blend_index = 10;
                    break;
                default:
                    blend_index = 0;
                    break;
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
 * Callback when a recent color swatch is selected
 */
static void on_recent_color_selected(SwatchesWidget* widget, gint index, gpointer user_data) {
    if (!widget || index < 0) {
        return;
    }

    /* Get the selected color */
    GdkRGBA color;
    if (swatches_widget_get_swatch(widget, index, &color, NULL)) {
        /* Set foreground color using tools_panel function */
        tools_panel_set_foreground_color(&color);

        /* Also add to recent colors directly if we have AppContext */
        /* Note: tools_panel_set_foreground_color should handle this, but if it fails,
         * we do it here as a fallback */
        AppContext* ctx = (AppContext*)user_data;
        if (ctx) {
            swatches_add_recent(&ctx->swatches, &color);
            /* Sync to widget */
            GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
            if (recent_widget && SWATCHES_IS_WIDGET(recent_widget)) {
                swatches_sync_to_widgets(&ctx->swatches, NULL, recent_widget);
            }
        }
    }
}

/**
 * Callback when a swatch is selected in the main swatches widget
 */
static void on_swatch_selected(SwatchesWidget* widget, gint index, gpointer user_data) {
    if (!widget || index < 0) {
        return;
    }

    /* Get the selected color */
    GdkRGBA color;
    if (swatches_widget_get_swatch(widget, index, &color, NULL)) {
        /* Set foreground color using tools_panel function */
        /* This will also add to recent colors */
        tools_panel_set_foreground_color(&color);

        /* Also add to recent colors directly if we have AppContext */
        AppContext* ctx = (AppContext*)user_data;
        if (ctx) {
            swatches_add_recent(&ctx->swatches, &color);
            /* Sync to widget */
            GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
            if (recent_widget && SWATCHES_IS_WIDGET(recent_widget)) {
                swatches_sync_to_widgets(&ctx->swatches, NULL, recent_widget);
            }
        }
    }
}

/**
 * Add a color to recent colors
 */
void layers_panel_add_recent_color(GdkRGBA* color) {
    if (!color) {
        g_warning("layers_panel_add_recent_color: color is NULL");
        return;
    }

    /* Try static reference first */
    SwatchesWidget* recent_widget = g_recent_colors_widget;

    /* If not found, try to find from toplevel windows */
    if (!recent_widget) {
        GList* toplevels = gtk_window_list_toplevels();
        GList* iter;
        for (iter = toplevels; iter != NULL; iter = iter->next) {
            GtkWidget* window = GTK_WIDGET(iter->data);
            if (GTK_IS_WINDOW(window)) {
                recent_widget = SWATCHES_WIDGET(g_object_get_data(G_OBJECT(window), "recent_colors_widget"));
                if (recent_widget) {
                    /* Cache it for next time */
                    g_recent_colors_widget = recent_widget;
                    g_list_free(toplevels);
                    break;
                }
            }
        }
        if (!recent_widget) {
            g_list_free(toplevels);
        }
    }

    if (!recent_widget) {
        g_warning("layers_panel_add_recent_color: recent_colors_widget not found. Widget may not be initialized yet.");
        return;
    }

    /* Get AppContext to access swatches data */
    GtkWidget* main_window = gtk_widget_get_toplevel(GTK_WIDGET(recent_widget));
    if (!main_window || !GTK_IS_WINDOW(main_window)) {
        g_warning("layers_panel_add_recent_color: Failed to get main window");
        return;
    }

    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(main_window), "app_context");
    if (!ctx) {
        g_warning("layers_panel_add_recent_color: AppContext not found");
        return;
    }

    /* Add to swatches data */
    swatches_add_recent(&ctx->swatches, color);

    /* Sync to widget */
    swatches_sync_to_widgets(&ctx->swatches, NULL, GTK_WIDGET(recent_widget));
}

/**
 * Free a layers panel
 */
void layers_panel_free(LayersPanel* layers_panel) {
    if (!layers_panel) {
        return;
    }

    /* Clear static widget references before widgets are destroyed */
    /* This prevents segfaults if save is called after widgets are destroyed */
    g_main_swatches_widget = NULL;
    g_recent_colors_widget = NULL;

    if (layers_panel->accordion) {
        accordion_free(layers_panel->accordion);
    }

    g_object_unref(layers_panel->store);
    g_free(layers_panel);
}
