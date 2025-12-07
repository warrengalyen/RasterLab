#include "panels.h"
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
 * Create the tools panel (vertical icon list)
 */
GtkWidget* create_tools_panel(void)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(panel, 5);
    gtk_widget_set_margin_start(panel, 5);
    gtk_widget_set_margin_end(panel, 5);
    gtk_widget_set_margin_bottom(panel, 5);

    /* Tool buttons with icons */
    const gchar *tools[] = {
        "Edit Tool",
        "Selection Tool",
        "Brush Tool",
        "Eraser Tool",
        "Fill Tool",
        "Text Tool",
        NULL
    };

    for (int i = 0; tools[i] != NULL; i++) {
        GtkWidget *tool_button = gtk_button_new_with_label(tools[i]);
        gtk_widget_set_size_request(tool_button, 100, 40);
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
GtkWidget* create_tool_options_panel(void)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(panel, 5);
    gtk_widget_set_margin_start(panel, 5);
    gtk_widget_set_margin_end(panel, 5);
    gtk_widget_set_margin_bottom(panel, 5);

    /* Placeholder content */
    GtkWidget *label = gtk_label_new("Tool Options");
    gtk_label_set_markup(GTK_LABEL(label), "<i>No tool selected</i>");
    gtk_box_pack_start(GTK_BOX(panel), label, TRUE, TRUE, 0);

    gtk_widget_show_all(panel);

    return panel;
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
    gtk_tree_view_append_column(GTK_TREE_VIEW(layers_panel->tree_view), column);

    /* Name column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Layer",
                                                      renderer,
                                                      "text", 2,
                                                      NULL);
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

    GtkWidget *btn_new = gtk_button_new_with_label("New");
    GtkWidget *btn_delete = gtk_button_new_with_label("Delete");
    GtkWidget *btn_duplicate = gtk_button_new_with_label("Duplicate");

    gtk_box_pack_start(GTK_BOX(controls), btn_new, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), btn_delete, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), btn_duplicate, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(layers_panel->panel), controls, FALSE, FALSE, 0);

    layers_panel->current_doc = NULL;

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

