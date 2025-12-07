#include "panels.h"
#include "tool_options.h"
#include <stdio.h>

/**
 * Panel header collapse button callback
 */
static void on_panel_collapse_clicked(GtkButton *button, gpointer user_data)
{
    PanelHeader *header = (PanelHeader *)user_data;
    panel_header_toggle_collapse(header);
}

/**
 * Create a collapsible panel header
 */
PanelHeader* panel_header_new(const gchar *title, GtkWidget *content)
{
    PanelHeader *header = (PanelHeader *)g_malloc(sizeof(PanelHeader));
    GtkWidget *header_box;
    GtkWidget *title_label;
    GtkWidget *collapse_button;
    GtkWidget *content_box;

    /* Main container */
    header->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header box with title and buttons */
    header->header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(header->header_box, 5);
    gtk_widget_set_margin_start(header->header_box, 5);
    gtk_widget_set_margin_end(header->header_box, 5);

    /* Title label */
    title_label = gtk_label_new(title);
    gtk_label_set_markup(GTK_LABEL(title_label), g_strdup_printf("<b>%s</b>", title));
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(header->header_box), title_label, TRUE, TRUE, 0);

    /* Collapse button */
    collapse_button = gtk_button_new_with_label("−");
    gtk_widget_set_size_request(collapse_button, 30, 30);
    g_signal_connect(collapse_button, "clicked", 
                     G_CALLBACK(on_panel_collapse_clicked), header);
    gtk_box_pack_end(GTK_BOX(header->header_box), collapse_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header->container), header->header_box, FALSE, FALSE, 0);

    /* Content area */
    header->content = content;
    gtk_box_pack_start(GTK_BOX(header->container), content, TRUE, TRUE, 0);

    /* Add separator */
    gtk_box_pack_start(GTK_BOX(header->container), 
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 
                       FALSE, FALSE, 0);

    header->is_collapsed = FALSE;

    gtk_widget_show_all(header->container);

    return header;
}

/**
 * Toggle panel collapse state
 */
void panel_header_toggle_collapse(PanelHeader *header)
{
    if (!header) {
        return;
    }

    header->is_collapsed = !header->is_collapsed;

    if (header->is_collapsed) {
        gtk_widget_hide(header->content);
    } else {
        gtk_widget_show(header->content);
    }
}

/**
 * Free a panel header
 */
void panel_header_free(PanelHeader *header)
{
    if (!header) {
        return;
    }

    g_free(header);
}

/**
 * Tool selection callback
 */
/**
 * Global reference to tool options panel for callbacks
 * This is set by create_tools_panel and used to update title
 */
static ToolOptionsPanel *g_tool_options_panel = NULL;

static void on_tool_button_clicked(GtkButton *button, gpointer user_data)
{
    ToolType tool_type = GPOINTER_TO_INT(user_data);
    ToolRegistry *registry = (ToolRegistry *)g_object_get_data(G_OBJECT(button), 
                                                               "tool_registry");

    if (registry) {
        if (tool_manager_activate(registry, tool_type)) {
            printf("Tool %d activated\n", tool_type);
            
            /* Update tool options panel title and visibility */
            Tool *active_tool = tool_manager_get_active(registry);
            if (active_tool && g_tool_options_panel) {
                tool_options_panel_update_title(g_tool_options_panel, active_tool->name);
                tool_options_panel_update_visibility(g_tool_options_panel, active_tool->options);
            }
        }
    }
}

/**
 * Set the tool options panel reference for callbacks
 */
void tools_panel_set_options_panel(ToolOptionsPanel *panel)
{
    g_tool_options_panel = panel;
}

/**
 * Create the tools panel (vertical icon list)
 */
GtkWidget* create_tools_panel(ToolRegistry *tool_registry)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(panel, 5);
    gtk_widget_set_margin_start(panel, 5);
    gtk_widget_set_margin_end(panel, 5);
    gtk_widget_set_margin_bottom(panel, 5);

    /* Tool buttons corresponding to available tools */
    const gchar *tool_labels[] = {
        "Move",
        "Brush",
        "Eraser",
        "Fill",
    };

    const gchar *tool_icons[] = {
        "transform-move-symbolic",
        "gtk-edit",
        "edit-clear-symbolic",
        "format-fill-color-symbolic",
    };

    const ToolType tool_types[] = {
        TOOL_MOVE,
        TOOL_BRUSH,
        TOOL_ERASER,
        TOOL_FILL,
    };

    for (int i = 0; i < TOOL_COUNT; i++) {
        GtkWidget *tool_button = gtk_button_new();
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        GtkWidget *icon = gtk_image_new_from_icon_name(tool_icons[i], GTK_ICON_SIZE_BUTTON);
        GtkWidget *label = gtk_label_new(tool_labels[i]);
        
        gtk_box_pack_start(GTK_BOX(vbox), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(tool_button), vbox);
        
        gtk_widget_set_size_request(tool_button, 70, 70);
        
        /* Store tool registry and type in button */
        g_object_set_data(G_OBJECT(tool_button), "tool_registry", tool_registry);
        
        /* Connect button click to tool activation */
        g_signal_connect(tool_button, "clicked",
                        G_CALLBACK(on_tool_button_clicked),
                        GINT_TO_POINTER(tool_types[i]));
        
        gtk_box_pack_start(GTK_BOX(panel), tool_button, FALSE, FALSE, 0);
    }

    /* Add expansion space */
    gtk_box_pack_start(GTK_BOX(panel), 
                       gtk_label_new(""), 
                       TRUE, TRUE, 0);

    gtk_widget_show_all(panel);

    return panel;
}

/**
 * Create the tool options panel (placeholder)
 */
/**
 * Tool options panel callback for size slider
 */
static void on_tool_size_changed(GtkScale *scale, gpointer user_data)
{
    (void)user_data;  /* Unused */
    gfloat size = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptions *opts = tool_options_get_global();
    if (opts) {
        tool_options_set_size(opts, size);
    }
}

/**
 * Tool options panel callback for opacity slider
 */
static void on_tool_opacity_changed(GtkScale *scale, gpointer user_data)
{
    (void)user_data;  /* Unused */
    gfloat opacity = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptions *opts = tool_options_get_global();
    if (opts) {
        tool_options_set_opacity(opts, opacity / 100.0f);
    }
}

/**
 * Tool options panel callback for hardness slider
 */
static void on_tool_hardness_changed(GtkScale *scale, gpointer user_data)
{
    (void)user_data;  /* Unused */
    gfloat hardness = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptions *opts = tool_options_get_global();
    if (opts) {
        tool_options_set_hardness(opts, hardness / 100.0f);
    }
}

ToolOptionsPanel* create_tool_options_panel(void)
{
    ToolOptionsPanel *tool_opts_panel = (ToolOptionsPanel *)g_malloc(sizeof(ToolOptionsPanel));
    ToolOptions *opts = tool_options_get_global();

    /* Main horizontal container */
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_top(panel, 8);
    gtk_widget_set_margin_start(panel, 10);
    gtk_widget_set_margin_end(panel, 10);
    gtk_widget_set_margin_bottom(panel, 8);

    /* Title label showing tool name */
    GtkWidget *title_label = gtk_label_new("Tool Options");
    gtk_label_set_markup(GTK_LABEL(title_label), "<b>Brush</b>");
    gtk_widget_set_size_request(title_label, 100, -1);
    gtk_box_pack_start(GTK_BOX(panel), title_label, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(panel), sep1, FALSE, FALSE, 5);

    /* Size section */
    GtkWidget *size_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *size_label = gtk_label_new("size");
    gtk_label_set_xalign(GTK_LABEL(size_label), 0.0);
    gtk_widget_set_tooltip_text(size_label, "Brush size in pixels");
    gtk_box_pack_start(GTK_BOX(size_box), size_label, FALSE, FALSE, 0);

    GtkWidget *size_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(size_scale), opts->size);
    gtk_scale_set_draw_value(GTK_SCALE(size_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(size_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(size_scale, 80, -1);
    gtk_scale_set_digits(GTK_SCALE(size_scale), 1);
    g_signal_connect(size_scale, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
    gtk_box_pack_start(GTK_BOX(size_box), size_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), size_box, FALSE, FALSE, 0);

    /* Opacity section */
    GtkWidget *opacity_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *opacity_label = gtk_label_new("opacity");
    gtk_label_set_xalign(GTK_LABEL(opacity_label), 0.0);
    gtk_widget_set_tooltip_text(opacity_label, "Tool opacity (0-100%)");
    gtk_box_pack_start(GTK_BOX(opacity_box), opacity_label, FALSE, FALSE, 0);

    GtkWidget *opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(opacity_scale), opts->opacity * 100.0f);
    gtk_scale_set_draw_value(GTK_SCALE(opacity_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(opacity_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(opacity_scale, 80, -1);
    gtk_scale_set_digits(GTK_SCALE(opacity_scale), 0);
    g_signal_connect(opacity_scale, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
    gtk_box_pack_start(GTK_BOX(opacity_box), opacity_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), opacity_box, FALSE, FALSE, 0);

    /* Hardness section */
    GtkWidget *hardness_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *hardness_label = gtk_label_new("hardness");
    gtk_label_set_xalign(GTK_LABEL(hardness_label), 0.0);
    gtk_widget_set_tooltip_text(hardness_label, "Brush hardness (0=soft, 100=hard)");
    gtk_box_pack_start(GTK_BOX(hardness_box), hardness_label, FALSE, FALSE, 0);

    GtkWidget *hardness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(hardness_scale), opts->hardness * 100.0f);
    gtk_scale_set_draw_value(GTK_SCALE(hardness_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(hardness_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(hardness_scale, 80, -1);
    gtk_scale_set_digits(GTK_SCALE(hardness_scale), 0);
    g_signal_connect(hardness_scale, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
    gtk_box_pack_start(GTK_BOX(hardness_box), hardness_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), hardness_box, FALSE, FALSE, 0);

    /* Store references */
    tool_opts_panel->panel = panel;
    tool_opts_panel->title_label = title_label;
    tool_opts_panel->size_scale = size_scale;
    tool_opts_panel->opacity_scale = opacity_scale;
    tool_opts_panel->hardness_scale = hardness_scale;

    gtk_widget_show_all(panel);

    return tool_opts_panel;
}

/**
 * Update tool options panel title
 */
void tool_options_panel_update_title(ToolOptionsPanel *panel, const gchar *tool_name)
{
    if (!panel || !tool_name) {
        return;
    }

    gchar *markup = g_strdup_printf("<b>%s</b>", tool_name);
    gtk_label_set_markup(GTK_LABEL(panel->title_label), markup);
    g_free(markup);
}

/**
 * Update tool options panel visibility based on tool capabilities
 */
void tool_options_panel_update_visibility(ToolOptionsPanel *panel, ToolOptionFlags options)
{
    if (!panel) {
        return;
    }

    /* Show/hide size slider based on TOOL_OPT_SIZE flag */
    if (options & TOOL_OPT_SIZE) {
        gtk_widget_show(gtk_widget_get_parent(panel->size_scale));
    } else {
        gtk_widget_hide(gtk_widget_get_parent(panel->size_scale));
    }

    /* Show/hide opacity slider based on TOOL_OPT_OPACITY flag */
    if (options & TOOL_OPT_OPACITY) {
        gtk_widget_show(gtk_widget_get_parent(panel->opacity_scale));
    } else {
        gtk_widget_hide(gtk_widget_get_parent(panel->opacity_scale));
    }

    /* Show/hide hardness slider based on TOOL_OPT_HARDNESS flag */
    if (options & TOOL_OPT_HARDNESS) {
        gtk_widget_show(gtk_widget_get_parent(panel->hardness_scale));
    } else {
        gtk_widget_hide(gtk_widget_get_parent(panel->hardness_scale));
    }
}

/**
 * Free a tool options panel
 */
void tool_options_panel_free(ToolOptionsPanel *panel)
{
    if (panel) {
        g_free(panel);
    }
}

/**
 * Layer visibility toggle callback
 */
static void on_layer_visibility_toggled(GtkCellRendererToggle *cell_renderer,
                                        gchar *path_str,
                                        gpointer user_data)
{
    LayersPanel *layers_panel = (LayersPanel *)user_data;
    GtkTreeIter iter;
    gboolean visible;

    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(layers_panel->store), 
                                             &iter, path_str)) {
        return;
    }

    /* Get current visibility value */
    gtk_tree_model_get(GTK_TREE_MODEL(layers_panel->store), &iter,
                      0, &visible,
                      -1);

    /* Toggle visibility */
    visible = !visible;
    gtk_list_store_set(layers_panel->store, &iter,
                      0, visible,
                      -1);

    printf("Layer visibility toggled\n");
}

/**
 * Layer name edited callback
 */
static void on_layer_name_edited(GtkCellRendererText *renderer,
                                 gchar *path_str,
                                 gchar *new_text,
                                 gpointer user_data)
{
    LayersPanel *layers_panel = (LayersPanel *)user_data;
    GtkTreeIter iter;

    (void)renderer;  /* Unused */

    if (!new_text || strlen(new_text) == 0) {
        return;  /* Don't allow empty names */
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
            ImageLayer *layer = document_get_layer(layers_panel->current_doc, layer_index);
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

    printf("Layer name changed to: %s\n", new_text);
}

/**
 * Layer tree view row activated callback
 */
static gboolean on_layer_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                        GtkTreeViewColumn *column, gpointer user_data)
{
    (void)tree_view;
    (void)path;
    (void)column;
    (void)user_data;

    printf("Layer selected\n");
    return FALSE;
}

/**
 * Panel button callbacks
 */
static void on_panel_btn_new_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;  /* Unused */
    (void)user_data;  /* Context passed differently */
    printf("New layer button clicked (handled by UI callback)\n");
}

static void on_panel_btn_delete_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;  /* Unused */
    (void)user_data;  /* Context passed differently */
    printf("Delete layer button clicked (handled by UI callback)\n");
}

static void on_panel_btn_duplicate_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;  /* Unused */
    (void)user_data;  /* Context passed differently */
    printf("Duplicate layer button clicked (handled by UI callback)\n");
}

/**
 * Create the layers panel with tree view
 */
LayersPanel* create_layers_panel(void)
{
    LayersPanel *layers_panel = (LayersPanel *)g_malloc(sizeof(LayersPanel));
    GtkWidget *scroll_window;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    /* Main panel */
    layers_panel->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Scrolled window for tree view */
    scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(layers_panel->panel), scroll_window, TRUE, TRUE, 0);

    /* Create list store for layers */
    layers_panel->store = gtk_list_store_new(4,
                                             G_TYPE_BOOLEAN,  /* Visible */
                                             GDK_TYPE_PIXBUF, /* Thumbnail */
                                             G_TYPE_STRING,   /* Name */
                                             G_TYPE_DOUBLE);  /* Opacity */

    /* Create tree view */
    layers_panel->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(layers_panel->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(layers_panel->tree_view), FALSE);
    g_signal_connect(layers_panel->tree_view, "row-activated",
                     G_CALLBACK(on_layer_row_activated), NULL);
    gtk_container_add(GTK_CONTAINER(scroll_window), layers_panel->tree_view);

    /* Visibility column (checkbox) */
    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes("Visible",
                                                      renderer,
                                                      "active", 0,
                                                      NULL);
    g_signal_connect(renderer, "toggled",
                     G_CALLBACK(on_layer_visibility_toggled), layers_panel);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Name column (editable) */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    column = gtk_tree_view_column_new_with_attributes("Layer",
                                                      renderer,
                                                      "text", 2,
                                                      NULL);
    g_signal_connect(renderer, "edited",
                     G_CALLBACK(on_layer_name_edited), layers_panel);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Opacity column (placeholder) */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Opacity",
                                                      renderer,
                                                      "text", 3,
                                                      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Add layer controls */
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(controls, 5);
    gtk_widget_set_margin_start(controls, 5);
    gtk_widget_set_margin_end(controls, 5);
    gtk_widget_set_margin_bottom(controls, 5);

    layers_panel->btn_new = gtk_button_new_with_label("New");
    layers_panel->btn_delete = gtk_button_new_with_label("Delete");
    layers_panel->btn_duplicate = gtk_button_new_with_label("Duplicate");

    /* Store panel reference in buttons for callback access */
    g_object_set_data(G_OBJECT(layers_panel->btn_new), "layers_panel", layers_panel);
    g_object_set_data(G_OBJECT(layers_panel->btn_delete), "layers_panel", layers_panel);
    g_object_set_data(G_OBJECT(layers_panel->btn_duplicate), "layers_panel", layers_panel);

    gtk_box_pack_start(GTK_BOX(controls), layers_panel->btn_new, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), layers_panel->btn_delete, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), layers_panel->btn_duplicate, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(layers_panel->panel), controls, FALSE, FALSE, 0);

    layers_panel->current_doc = NULL;
    layers_panel->app_context = NULL;

    gtk_widget_show_all(layers_panel->panel);

    return layers_panel;
}

/**
 * Update layers panel with document layers
 */
void layers_panel_update(LayersPanel *layers_panel, ImageDocument *doc)
{
    if (!layers_panel || !doc) {
        return;
    }

    layers_panel->current_doc = doc;

    /* Clear existing layers */
    gtk_list_store_clear(layers_panel->store);

    /* Add all layers from document */
    guint layer_count = document_get_layer_count(doc);

    for (gint i = layer_count - 1; i >= 0; i--) {
        ImageLayer *layer = document_get_layer(doc, i);

        if (layer) {
            GtkTreeIter iter;
            gchar opacity_str[16];
            snprintf(opacity_str, sizeof(opacity_str), "%.0f%%", layer->opacity * 100.0);

            gtk_list_store_append(layers_panel->store, &iter);
            gtk_list_store_set(layers_panel->store, &iter,
                              0, layer->visible,
                              2, layer->name,
                              3, g_strdup(opacity_str),
                              -1);
        }
    }

    printf("Layers panel updated with %u layers\n", layer_count);
}

/**
 * Get the currently selected layer from the panel
 */
ImageLayer* layers_panel_get_selected_layer(LayersPanel *layers_panel)
{
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gchar *layer_name = NULL;

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
        return NULL;  /* No selection */
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
        ImageLayer *layer = document_get_layer(layers_panel->current_doc, i);
        if (layer && g_strcmp0(layer->name, layer_name) == 0) {
            g_free(layer_name);
            return layer;
        }
    }

    g_free(layer_name);
    return NULL;
}

/**
 * Update button sensitivity based on state
 */
void layers_panel_update_button_sensitivity(LayersPanel *layers_panel,
                                            gboolean has_document,
                                            gboolean has_selection)
{
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
}

/**
 * Connect layers panel buttons to UI callbacks
 */
void layers_panel_connect_buttons(LayersPanel *layers_panel,
                                  GCallback new_callback,
                                  GCallback delete_callback,
                                  GCallback duplicate_callback,
                                  gpointer user_data)
{
    GtkTreeSelection *selection;

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
    layers_panel_update_button_sensitivity(layers_panel, FALSE, FALSE);
}

/**
 * Free a layers panel
 */
void layers_panel_free(LayersPanel *layers_panel)
{
    if (!layers_panel) {
        return;
    }

    g_object_unref(layers_panel->store);
    g_free(layers_panel);
}

