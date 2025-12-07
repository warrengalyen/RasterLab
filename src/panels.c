#include "panels.h"
#include "tool_options.h"
#include "accordion.h"
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

/**
 * Global references to color buttons for swap functionality
 */
static GtkWidget *g_fg_color_button = NULL;
static GtkWidget *g_bg_color_button = NULL;

/**
 * Swap foreground and background colors
 */
static void on_swap_colors_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;   /* Unused */
    (void)user_data; /* Unused */
    
    if (!g_fg_color_button || !g_bg_color_button) {
        return;
    }
    
    /* Get current colors */
    GdkRGBA fg_rgba, bg_rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_fg_color_button), &fg_rgba);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_bg_color_button), &bg_rgba);
    
    /* Swap them */
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_fg_color_button), &bg_rgba);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_bg_color_button), &fg_rgba);
}

/**
 * Global references to tool buttons for toggle state management
 */
static GtkWidget *g_tool_buttons[TOOL_COUNT] = {NULL};

/**
 * Flag to prevent recursive signal handling
 */
static gboolean g_updating_tools = FALSE;

static void on_tool_button_clicked(GtkToggleButton *button, gpointer user_data)
{
    ToolType tool_type = GPOINTER_TO_INT(user_data);
    ToolRegistry *registry = (ToolRegistry *)g_object_get_data(G_OBJECT(button), 
                                                               "tool_registry");

    /* Prevent recursion */
    if (g_updating_tools) {
        return;
    }

    if (registry) {
        if (tool_manager_activate(registry, tool_type)) {
            printf("Tool %d activated\n", tool_type);
            
            /* Set flag to prevent recursive calls */
            g_updating_tools = TRUE;
            
            /* Update toggle button states - only active tool is pressed */
            for (int i = 0; i < TOOL_COUNT; i++) {
                if (g_tool_buttons[i]) {
                    /* Set button active only if it's the selected tool */
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_tool_buttons[i]), 
                                                (i == tool_type));
                }
            }
            
            /* Clear recursion flag */
            g_updating_tools = FALSE;
            
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
 * Get the current foreground color from the color picker
 */
gboolean tools_panel_get_foreground_color(GdkRGBA *rgba)
{
    if (!g_fg_color_button || !rgba) {
        return FALSE;
    }
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_fg_color_button), rgba);
    return TRUE;
}

/**
 * Create the tools panel (loads from Glade file)
 */
GtkWidget* create_tools_panel(ToolRegistry *tool_registry)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *panel;
    
    /* Load the Glade file from resources */
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/tool_panel.glade", &error)) {
        g_warning("Failed to load tool_panel.glade: %s", error->message);
        g_error_free(error);
        g_object_unref(builder);
        
        /* Fallback: create empty panel */
        return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }
    
    /* Get the main panel from the builder */
    panel = GTK_WIDGET(gtk_builder_get_object(builder, "tool_panel"));
    if (!panel) {
        g_warning("Failed to get tool_panel object from builder");
        g_object_unref(builder);
        return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }
    
    /* Keep builder alive by storing it on the panel as object data */
    g_object_set_data_full(G_OBJECT(panel), "builder", builder, g_object_unref);
    
    /* Enforce 48px width constraint */
    gtk_widget_set_size_request(panel, 48, -1);
    gtk_widget_set_hexpand(panel, FALSE);
    gtk_widget_set_vexpand(panel, TRUE);
    
    /* Tool button IDs and types */
    const gchar *button_ids[] = {
        "tool_button_move",
        "tool_button_brush",
        "tool_button_eraser",
        "tool_button_fill",
    };
    
    const ToolType tool_types[] = {
        TOOL_MOVE,
        TOOL_BRUSH,
        TOOL_ERASER,
        TOOL_FILL,
    };
    
    /* Set up tool buttons with callbacks */
    for (int i = 0; i < TOOL_COUNT; i++) {
        GtkWidget *tool_button = GTK_WIDGET(gtk_builder_get_object(builder, button_ids[i]));
        if (tool_button) {
            /* Store tool registry and type in button */
            g_object_set_data(G_OBJECT(tool_button), "tool_registry", tool_registry);
            
            /* Store button reference for toggle state management */
            g_tool_buttons[i] = tool_button;
            
            /* Connect button click to tool activation */
            g_signal_connect(tool_button, "toggled",
                            G_CALLBACK(on_tool_button_clicked),
                            GINT_TO_POINTER(tool_types[i]));
        } else {
            g_warning("Failed to get tool button %d from builder", i);
        }
    }
    
    /* Set up color buttons */
    GtkWidget *fg_color = GTK_WIDGET(gtk_builder_get_object(builder, "fg_color_button"));
    GtkWidget *bg_color = GTK_WIDGET(gtk_builder_get_object(builder, "bg_color_button"));
    GtkWidget *swap_button = GTK_WIDGET(gtk_builder_get_object(builder, "swap_button"));
    
    if (fg_color) {
        GdkRGBA fg_rgba = {0.0, 0.0, 0.0, 1.0};  /* Black */
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(fg_color), &fg_rgba);
        g_fg_color_button = fg_color;  /* Store global reference */
    } else {
        g_warning("Failed to get foreground color button from builder");
    }
    
    if (bg_color) {
        GdkRGBA bg_rgba = {1.0, 1.0, 1.0, 1.0};  /* White */
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(bg_color), &bg_rgba);
        g_bg_color_button = bg_color;  /* Store global reference */
    } else {
        g_warning("Failed to get background color button from builder");
    }
    
    /* Connect swap button */
    if (swap_button) {
        g_signal_connect(swap_button, "clicked",
                        G_CALLBACK(on_swap_colors_clicked), NULL);
    } else {
        g_warning("Failed to get swap button from builder");
    }
    
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
 * Create the layers panel with tree view (loads from Glade file)
 */
LayersPanel* create_layers_panel(void)
{
    LayersPanel *layers_panel = (LayersPanel *)g_malloc(sizeof(LayersPanel));
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *scroll_window;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

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
        layers_panel->btn_duplicate = NULL;
        layers_panel->current_doc = NULL;
        layers_panel->app_context = NULL;
        return layers_panel;
    }
    
    /* Get the main Glade panel container */
    GtkWidget *glade_panel = GTK_WIDGET(gtk_builder_get_object(builder, "layers_panel"));
    if (!glade_panel) {
        g_warning("Failed to get layers_panel from builder");
        g_object_unref(builder);
        layers_panel->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        layers_panel->store = NULL;
        layers_panel->tree_view = NULL;
        layers_panel->btn_new = NULL;
        layers_panel->btn_delete = NULL;
        layers_panel->btn_duplicate = NULL;
        layers_panel->current_doc = NULL;
        layers_panel->app_context = NULL;
        return layers_panel;
    }
    
    /* Get scrolled window from builder for tree view population */
    scroll_window = GTK_WIDGET(gtk_builder_get_object(builder, "layers_scroll"));
    
    /* Get buttons from builder */
    layers_panel->btn_new = GTK_WIDGET(gtk_builder_get_object(builder, "btn_new_layer"));
    layers_panel->btn_delete = GTK_WIDGET(gtk_builder_get_object(builder, "btn_delete_layer"));
    layers_panel->btn_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "btn_duplicate_layer"));
    
    /* Create accordion widget - this becomes the main panel container */
    layers_panel->accordion = accordion_new();
    layers_panel->panel = accordion_get_widget(layers_panel->accordion);
    
    /* Keep builder alive by storing it on the panel as object data */
    g_object_set_data_full(G_OBJECT(layers_panel->panel), "builder", builder, g_object_unref);

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

    /* Store panel reference in buttons for callback access */
    if (layers_panel->btn_new) {
        g_object_set_data(G_OBJECT(layers_panel->btn_new), "layers_panel", layers_panel);
    }
    if (layers_panel->btn_delete) {
        g_object_set_data(G_OBJECT(layers_panel->btn_delete), "layers_panel", layers_panel);
    }
    if (layers_panel->btn_duplicate) {
        g_object_set_data(G_OBJECT(layers_panel->btn_duplicate), "layers_panel", layers_panel);
    }

    /* Add Glade panel as single accordion section */
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

    if (layers_panel->accordion) {
        accordion_free(layers_panel->accordion);
    }
    
    g_object_unref(layers_panel->store);
    g_free(layers_panel);
}

