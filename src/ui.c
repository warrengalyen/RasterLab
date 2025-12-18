#include "ui.h"
#include "app/autosave.h"
#include "app/recent_files.h"
#include "command.h"
#include "document.h"
#include "panels.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "tool_manager.h"
#include "ui/dialogs/recovery_dialog.h"
#include "ui/layers_panel.h"
#include "ui/tool_options_panel.h"
#include "ui/tools_panel.h"
#include "ui/ui_filter.h"
#include "ui/ui_filter_adjust.h"
#include "ui/ui_filter_effects.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Log handler to suppress harmless GTK Builder menu warnings */
static gboolean gtk_builder_menu_warning_handler(const gchar* log_domain,
                                                 GLogLevelFlags log_level,
                                                 const gchar* message,
                                                 gpointer user_data) {
    (void)log_domain;
    (void)log_level;
    (void)user_data;

    /* Suppress warnings about adding GtkMenu to GtkMenuItem */
    if (message && strstr(message, "Cannot add an object of type GtkMenu")) {
        return TRUE; /* Suppress this warning */
    }

    return FALSE; /* Let other warnings through */
}

/* Forward declarations */
static void on_file_open(GtkWidget* widget, gpointer data);
static void on_file_open_response(GtkDialog* dialog, gint response_id, gpointer user_data);
static void on_recent_file_activate(GtkMenuItem* menu_item, gpointer user_data);
static void on_clear_recent_files(GtkMenuItem* menu_item, gpointer user_data);
void ui_update_recent_files_menu(AppContext* ctx);
static void on_file_save_as(GtkWidget* widget, gpointer data);
static void on_file_save_as_response(GtkDialog* dialog, gint response_id, gpointer user_data);
static void on_file_close(GtkWidget* widget, gpointer data);
static void on_file_exit(GtkWidget* widget, gpointer data);
static void on_edit_undo(GtkWidget* widget, gpointer data);
static void on_edit_redo(GtkWidget* widget, gpointer data);
static void on_view_zoom_in(GtkWidget* widget, gpointer data);
static void on_view_zoom_out(GtkWidget* widget, gpointer data);
static void on_view_zoom_reset(GtkWidget* widget, gpointer data);
static void on_view_zoom_fit(GtkWidget* widget, gpointer data);
static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data);
static void on_layer_new(GtkWidget* widget, gpointer data);
static void on_layer_delete(GtkWidget* widget, gpointer data);
static void on_layer_duplicate(GtkWidget* widget, gpointer data);
static void on_layer_move_up(GtkWidget* widget, gpointer data);
static void on_layer_move_down(GtkWidget* widget, gpointer data);
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page,
                                    guint page_num, gpointer user_data);
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data);
static void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data);
static void setup_file_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);
static void setup_edit_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);
static void setup_view_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);
static void setup_layer_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx);

/**
 * Layer selection changed callback - proper signal handler signature
 */
static void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    (void)selection; /* Unused - we get it from the tree view */

    AppContext* ctx = (AppContext*)user_data;

    if (!ctx) {
        // printf("ERROR: on_layer_selection_changed called with NULL ctx\n");
        return;
    }

    // printf("Layer selection changed in tree view\n");

    /* Get the currently active document */
    ImageDocument* active_doc = ui_get_active_document(ctx);
    if (active_doc && ctx->layers_panel) {
        /* Set the selected layer in the document so tools use the right layer */
        ImageLayer* selected_layer = layers_panel_get_selected_layer(ctx->layers_panel);
        document_set_selected_layer(active_doc, selected_layer);

        /* Update opacity controls to reflect selected layer */
        layers_panel_update_opacity_controls(ctx->layers_panel);
    }

    /* Update menu and button states */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Create the File menu
 */
static GtkWidget* create_file_menu(AppContext* ctx) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* menu_item;
    GtkAccelGroup* accel_group;

    accel_group = gtk_accel_group_new();

    /* File > Open */
    menu_item = gtk_menu_item_new_with_mnemonic("_Open");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_file_open), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    /* File > Save As */
    menu_item = gtk_menu_item_new_with_mnemonic("_Save As");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_file_save_as), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* File > Close */
    menu_item = gtk_menu_item_new_with_mnemonic("_Close");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_file_close), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* File > Exit */
    menu_item = gtk_menu_item_new_with_mnemonic("E_xit");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_file_exit), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    gtk_widget_show_all(menu);

    return menu;
}

/**
 * Create the Edit menu
 */
static GtkWidget* create_edit_menu(AppContext* ctx) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* menu_item;
    GtkAccelGroup* accel_group = gtk_accel_group_new();

    /* Edit > Undo */
    menu_item = gtk_menu_item_new_with_mnemonic("_Undo");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_edit_undo), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_z, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_set_sensitive(menu_item, FALSE);
    ctx->edit_menu_undo = menu_item;

    /* Edit > Redo */
    menu_item = gtk_menu_item_new_with_mnemonic("_Redo");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_edit_redo), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_y, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_set_sensitive(menu_item, FALSE);
    ctx->edit_menu_redo = menu_item;

    gtk_widget_show_all(menu);

    return menu;
}

/**
 * Create the View menu
 */
static GtkWidget* create_view_menu(AppContext* ctx) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* menu_item;
    GtkAccelGroup* accel_group = gtk_accel_group_new();

    /* View > Zoom In */
    menu_item = gtk_menu_item_new_with_mnemonic("Zoom _In");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_view_zoom_in), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_plus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_equal, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    /* View > Zoom Out */
    menu_item = gtk_menu_item_new_with_mnemonic("Zoom _Out");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_view_zoom_out), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_minus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    /* View > Zoom Reset */
    menu_item = gtk_menu_item_new_with_mnemonic("_Reset Zoom");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_view_zoom_reset), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_0, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    /* View > Fit to Window */
    menu_item = gtk_menu_item_new_with_mnemonic("_Fit to Window");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_view_zoom_fit), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_add_accelerator(menu_item, "activate", accel_group,
                               GDK_KEY_1, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    gtk_widget_show_all(menu);

    return menu;
}

/**
 * Create the Layer menu
 */
static GtkWidget* create_layer_menu(AppContext* ctx) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* menu_item;

    /* Layer > New Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_New Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_new), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_new = menu_item;

    /* Layer > Duplicate Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_Duplicate Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_duplicate = menu_item;

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Layer > Delete Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_Delete Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_delete), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_delete = menu_item;

    gtk_widget_show_all(menu);

    return menu;
}

/**
 * Create the menu bar
 */
static GtkWidget* create_menu_bar(AppContext* ctx) {
    GtkWidget* menu_bar = gtk_menu_bar_new();
    GtkWidget* file_menu_item;
    GtkWidget* file_menu;
    GtkWidget* edit_menu_item;
    GtkWidget* edit_menu;
    GtkWidget* view_menu_item;
    GtkWidget* view_menu;
    GtkWidget* layer_menu_item;
    GtkWidget* layer_menu;

    /* File menu */
    file_menu_item = gtk_menu_item_new_with_mnemonic("_File");
    file_menu = create_file_menu(ctx);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_menu_item);

    /* Edit menu */
    edit_menu_item = gtk_menu_item_new_with_mnemonic("_Edit");
    edit_menu = create_edit_menu(ctx);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_menu_item), edit_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), edit_menu_item);

    /* View menu */
    view_menu_item = gtk_menu_item_new_with_mnemonic("_View");
    view_menu = create_view_menu(ctx);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_menu_item), view_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), view_menu_item);

    /* Layer menu */
    layer_menu_item = gtk_menu_item_new_with_mnemonic("_Layer");
    layer_menu = create_layer_menu(ctx);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(layer_menu_item), layer_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), layer_menu_item);

    gtk_widget_show_all(menu_bar);

    return menu_bar;
}

/**
 * Create the main application UI
 */
AppContext* ui_create_main_window(void) {
    AppContext* ctx = (AppContext*)g_malloc(sizeof(AppContext));
    GtkWidget* tools_panel;
    GtkWidget* layers_panel_widget;

    ctx->documents = NULL;
    ctx->layer_menu_new = NULL;
    ctx->layer_menu_delete = NULL;
    ctx->layer_menu_duplicate = NULL;
    ctx->edit_menu_undo = NULL;
    ctx->edit_menu_redo = NULL;
    ctx->layers_panel = NULL; /* Initialize layers_panel early */

    /* Initialize canvas background color to rgb(160, 160, 160) */
    ctx->canvas_bg_r = 160.0 / 255.0;
    ctx->canvas_bg_g = 160.0 / 255.0;
    ctx->canvas_bg_b = 160.0 / 255.0;

    /* Create and initialize tool manager */
    ctx->tool_registry = tool_manager_new();
    if (!tool_manager_init_defaults(ctx->tool_registry)) {
        g_warning("Failed to initialize tool manager");
        g_free(ctx);
        return NULL;
    }

    /* Set AppContext reference on all tools */
    if (ctx->tool_registry) {
        for (int i = 0; i < TOOL_COUNT; i++) {
            Tool* tool = tool_manager_get(ctx->tool_registry, i);
            if (tool) {
                tool->app_context = (gpointer)ctx;
            }
        }
    }

    /* Suppress GTK Builder warnings about menus (these are harmless when using standalone menus) */
    static gboolean menu_warning_suppressed = FALSE;
    if (!menu_warning_suppressed) {
        g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING,
                          (GLogFunc)gtk_builder_menu_warning_handler, NULL);
        menu_warning_suppressed = TRUE;
    }

    /* Load main window from Glade */
    GtkBuilder* builder = gtk_builder_new_from_resource("/ui/main_window.glade");
    if (!builder) {
        g_warning("Failed to load main window from Glade");
        g_free(ctx);
        return NULL;
    }

    /* Get main window */
    ctx->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!ctx->window) {
        g_warning("Failed to get main_window from Glade");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    gtk_window_set_icon_name(GTK_WINDOW(ctx->window), "image-editor");

    /* Get main containers */
    GtkWidget* main_vbox = GTK_WIDGET(gtk_builder_get_object(builder, "main_vbox"));
    GtkWidget* tool_options_container = GTK_WIDGET(gtk_builder_get_object(builder, "tool_options_container"));
    GtkWidget* main_hbox = GTK_WIDGET(gtk_builder_get_object(builder, "main_hbox"));
    GtkWidget* center_right_hpaned = GTK_WIDGET(gtk_builder_get_object(builder, "center_right_hpaned"));
    ctx->notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
    GtkWidget* layers_panel_container = GTK_WIDGET(gtk_builder_get_object(builder, "layers_panel_container"));
    ctx->status_bar = GTK_WIDGET(gtk_builder_get_object(builder, "status_bar"));

    if (!main_vbox || !tool_options_container || !main_hbox ||
        !center_right_hpaned || !ctx->notebook ||
        !layers_panel_container || !ctx->status_bar) {
        g_warning("Failed to get required widgets from Glade");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }

    /* Store builder reference in window for later use (e.g., status bar labels) */
    g_object_ref(builder);
    g_object_set_data_full(G_OBJECT(ctx->window), "main_builder", builder, g_object_unref);

    /* Connect notebook signal */
    g_signal_connect(ctx->notebook, "switch-page",
                     G_CALLBACK(on_notebook_switch_page), ctx);

    /* Get menu bar and menu items from Glade */
    ctx->menu_bar = GTK_WIDGET(gtk_builder_get_object(builder, "menu_bar"));

    /* Create accelerator group for keyboard shortcuts */
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(ctx->window), accel_group);

    /* Setup each menu separately */
    setup_file_menu(builder, ctx, accel_group);
    setup_edit_menu(builder, ctx, accel_group);
    setup_view_menu(builder, ctx, accel_group);
    setup_layer_menu(builder, ctx);
    setup_adjust_menu(builder, ctx);
    setup_effects_menu(builder, ctx);

    /* ==== TOP PANEL: Tool Options ==== */
    ctx->tool_options_panel = create_tool_options_panel();
    if (!ctx->tool_options_panel || !ctx->tool_options_panel->panel) {
        g_warning("Failed to create tool options panel");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    /* Set tool registry in panel for cursor updates */
    tool_options_panel_set_tool_registry(ctx->tool_options_panel, ctx->tool_registry);
    /* Store panel reference in panel widget */
    g_object_set_data(G_OBJECT(ctx->tool_options_panel->panel), "tool_options_panel", ctx->tool_options_panel);
    gtk_container_add(GTK_CONTAINER(tool_options_container), ctx->tool_options_panel->panel);

    /* ==== LEFT PANEL: Tools (fixed width) ==== */
    /* Tool panel is now loaded from main window Glade file */
    tools_panel = tools_panel_initialize_from_builder(builder, ctx->tool_registry);

    /* Connect tools panel to tool options panel for title updates */
    tools_panel_set_options_panel(ctx->tool_options_panel);

    /* ==== RIGHT PANEL: Layers ==== */
    ctx->layers_panel = create_layers_panel();
    layers_panel_widget = ctx->layers_panel->panel;
    gtk_container_add(GTK_CONTAINER(layers_panel_container), layers_panel_widget);

    /* Store layers panel reference for later updates */
    g_object_set_data(G_OBJECT(ctx->window), "layers_panel", ctx->layers_panel);

    /* Connect layers panel buttons to callbacks */
    layers_panel_connect_buttons(ctx->layers_panel,
                                 G_CALLBACK(on_layer_new),
                                 G_CALLBACK(on_layer_delete),
                                 G_CALLBACK(on_layer_duplicate),
                                 ctx);

    /* Connect move layer buttons */
    if (ctx->layers_panel->btn_up) {
        g_signal_connect(ctx->layers_panel->btn_up, "clicked",
                         G_CALLBACK(on_layer_move_up), ctx);
    }
    if (ctx->layers_panel->btn_down) {
        g_signal_connect(ctx->layers_panel->btn_down, "clicked",
                         G_CALLBACK(on_layer_move_down), ctx);
    }

    /* Connect layer tree view selection changes to update UI state */
    GtkTreeSelection* layer_selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(ctx->layers_panel->tree_view));
    g_signal_connect(layer_selection, "changed",
                     G_CALLBACK(on_layer_selection_changed), ctx);

    /* Status bar is already in Glade file, no need to add it */

    /* Connect window signals */
    g_signal_connect(ctx->window, "delete-event",
                     G_CALLBACK(on_window_delete), ctx);

    gtk_widget_show_all(ctx->window);

    /* Update status bar with initial information */
    ui_update_status_bar(ctx, NULL);

    /* Initialize menu and button states */
    ui_update_menu_and_button_states(ctx);

    /* Initialize recent files menu (must be before g_object_unref(builder)) */
    ui_update_recent_files_menu(ctx);

    /* Clean up builder - widgets are now owned by their containers */
    g_object_unref(builder);

    /* Check for recovery files and show dialog if found */
    recovery_dialog_show(ctx);

    // printf("Main window created with dockable panels and status bar\n");

    return ctx;
}

/**
 * Create and attach a new document tab
 */
ImageDocument* ui_create_document_tab(AppContext* ctx, const gchar* filename) {
    ImageDocument* doc;
    GtkWidget* page_content;
    GtkWidget* tab_hbox;
    GtkWidget* tab_label;
    GtkWidget* close_button;
    gint page_num;

    /* Create the document */
    doc = document_new(filename);

    /* Create the drawing area (scrolled window with drawing area) */
    page_content = document_create_drawing_area(doc);

    /* Create tab label with close button */
    tab_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(tab_hbox), 0);

    tab_label = gtk_label_new(filename);
    gtk_box_pack_start(GTK_BOX(tab_hbox), tab_label, FALSE, FALSE, 0);

    close_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close_button, FALSE);
    gtk_widget_set_size_request(close_button, 20, 20);
    GtkWidget* close_image = gtk_image_new_from_icon_name("window-close-symbolic",
                                                          GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(close_button), close_image);
    g_signal_connect(close_button, "clicked",
                     G_CALLBACK(on_tab_close_button_clicked), doc);
    gtk_box_pack_start(GTK_BOX(tab_hbox), close_button, FALSE, FALSE, 0);

    gtk_widget_show_all(tab_hbox);
    gtk_widget_show_all(page_content);

    /* Add page to notebook */
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook),
                                        page_content, tab_hbox);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    /* Store label in close button's data for later reference */
    g_object_set_data(G_OBJECT(close_button), "tab_label", tab_label);
    g_object_set_data(G_OBJECT(close_button), "app_context", ctx);

    /* Store AppContext and tool registry in drawing area for tool event handlers */
    if (doc && doc->drawing_area) {
        g_object_set_data(G_OBJECT(doc->drawing_area), "app_context", ctx);
        if (ctx->tool_registry) {
            g_object_set_data(G_OBJECT(doc->drawing_area), "tool_registry", ctx->tool_registry);
        }
        /* Store layers panel reference for thumbnail updates */
        if (ctx->layers_panel) {
            g_object_set_data(G_OBJECT(doc->drawing_area), "layers_panel", ctx->layers_panel);
        }
    }

    /* Update viewport background color with current canvas background color using CSS */
    if (doc && doc->viewport) {
        guint r = (guint)(ctx->canvas_bg_r * 255.0);
        guint g = (guint)(ctx->canvas_bg_g * 255.0);
        guint b = (guint)(ctx->canvas_bg_b * 255.0);

        /* Get or create CSS provider for this viewport */
        GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(G_OBJECT(doc->viewport), "canvas_bg_provider");
        if (!provider) {
            provider = gtk_css_provider_new();
            g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider", provider, g_object_unref);
            GtkStyleContext* style_context = gtk_widget_get_style_context(doc->viewport);
            gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        gchar* css = g_strdup_printf("#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        g_free(css);
        gtk_widget_queue_draw(doc->viewport);
    }

    /* Add document to list */
    ctx->documents = g_list_append(ctx->documents, doc);

    /* Register document for autosave */
    autosave_register_document(doc);

    /* Update window title */
    ui_update_window_title(ctx);

    return doc;
}

/**
 * Close a document tab
 */
/**
 * Internal: Actually close the document without prompting
 */
static void ui_close_document_tab_internal(AppContext* ctx, ImageDocument* doc) {
    gint page_num;
    GtkWidget* scrolled_window;

    if (!doc || !ctx) {
        return;
    }

    /* Store scrolled window pointer before we modify anything */
    scrolled_window = doc->scrolled_window;

    /* Find the page containing this document */
    if (!scrolled_window) {
        /* Document already has no scrolled window, just free it */
        ctx->documents = g_list_remove(ctx->documents, doc);
        document_free(doc);
        return;
    }

    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), scrolled_window);

    if (page_num >= 0) {
        /* Disconnect all signals from drawing area to prevent callbacks
         * from accessing the document after it's freed */
        if (doc->drawing_area) {
            g_signal_handlers_disconnect_by_data(doc->drawing_area, doc);
        }

        /* NULL out widget pointers BEFORE removing page to prevent
         * access to destroyed widgets during cleanup and signal handlers */
        doc->scrolled_window = NULL;
        doc->drawing_area = NULL;

        /* Remove from document list BEFORE removing page to prevent
         * switch-page callback from accessing the document */
        ctx->documents = g_list_remove(ctx->documents, doc);

        /* Unregister from autosave before freeing */
        autosave_unregister_document(doc);

        /* Remove the notebook page (this destroys the scrolled window
         * and may trigger switch-page signal, but doc is already removed) */
        gtk_notebook_remove_page(GTK_NOTEBOOK(ctx->notebook), page_num);

        /* Free the document */
        document_free(doc);
        doc = NULL; /* Ensure doc pointer is NULL after freeing */

        /* Update window title (handles empty notebook) */
        ui_update_window_title(ctx);

        /* Update status bar and menu/button states */
        ui_update_status_bar(ctx, NULL);

        /* Clear layers panel if no documents remain */
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                    "layers_panel");
        if (layers_panel) {
            layers_panel_update(layers_panel, NULL);
        }

        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Public: Close document with unsaved changes prompt
 */
void ui_close_document_tab(AppContext* ctx, ImageDocument* doc) {
    if (!doc || !ctx) {
        return;
    }

    /* Check if document has unsaved changes */
    if (document_is_dirty(doc)) {
        GtkWidget* dialog;
        gint response;
        const gchar* filename = document_get_filename(doc);

        /* Create confirmation dialog */
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_NONE,
            "Save changes to \"%s\" before closing?",
            filename);

        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "If you don't save, changes will be lost.");

        /* Add buttons */
        gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                               "_Discard", GTK_RESPONSE_REJECT,
                               "_Cancel", GTK_RESPONSE_CANCEL,
                               "_Save", GTK_RESPONSE_ACCEPT,
                               NULL);

        /* Set default button to Save */
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

        /* Show dialog and get response */
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        switch (response) {
            case GTK_RESPONSE_ACCEPT:
                /* User clicked Save */
                // printf("User wants to save before closing\n");
                ui_save_document_as(ctx);
                /* Note: We don't actually close here - let user complete save */
                /* In a full implementation, we'd detect when save completes */
                break;

            case GTK_RESPONSE_REJECT:
                /* User clicked Discard */
                // printf("User discarding changes\n");
                ui_close_document_tab_internal(ctx, doc);
                break;

            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
                /* User clicked Cancel or closed dialog */
                // printf("User cancelled close operation\n");
                break;

            default:
                break;
        }
    } else {
        /* No unsaved changes, close directly */
        ui_close_document_tab_internal(ctx, doc);
    }
}

/**
 * Get the current active document
 */
ImageDocument* ui_get_active_document(AppContext* ctx) {
    gint page_num;
    GtkWidget* page;

    if (!ctx) {
        return NULL;
    }

    page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(ctx->notebook));

    if (page_num < 0) {
        return NULL;
    }

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    if (!page) {
        return NULL;
    }

    /* Find the document by scrolled window */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument* doc = (ImageDocument*)iter->data;
        if (doc && doc->scrolled_window == page) {
            return doc;
        }
    }

    return NULL;
}

/**
 * Update the window title based on active document
 */
void ui_update_window_title(AppContext* ctx) {
    ImageDocument* active_doc;
    gchar* title;

    if (!ctx) {
        return;
    }

    active_doc = ui_get_active_document(ctx);

    if (active_doc) {
        const gchar* filename = document_get_filename(active_doc);
        const gchar* modified = active_doc->modified ? "*" : "";
        title = g_strdup_printf("Image Editor - %s%s", filename, modified);
    } else {
        title = g_strdup("Image Editor");
    }

    gtk_window_set_title(GTK_WINDOW(ctx->window), title);
    g_free(title);
}

/**
 * Free the application context
 */
/**
 * Callback for opening a recent file
 */
static void on_recent_file_activate(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;
    gchar* file_path = (gchar*)g_object_get_data(G_OBJECT(menu_item), "recent_file_path");

    if (!file_path) {
        return;
    }

    /* Check if file still exists */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        /* Show warning dialog */
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "File not found: %s\n\nThe file has been removed from the recent files list.",
            file_path);

        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        /* Remove from recent files */
        recent_files_remove(file_path);
        recent_files_save();
        ui_update_recent_files_menu(ctx);

        return;
    }

    /* Open the file */
    gchar* basename = g_path_get_basename(file_path);
    ImageDocument* doc = ui_create_document_tab(ctx, basename);

    if (doc) {
        /* Load the image into the document */
        if (!document_load_image_from_file(doc, file_path)) {
            g_warning("Failed to load image: %s", file_path);
        } else {
            /* Update recent files (move to top) */
            recent_files_add(file_path);
            recent_files_save();

            /* Update status bar after successful load */
            ui_update_status_bar(ctx, NULL);

            /* Update layers panel with loaded document */
            LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(
                G_OBJECT(ctx->window), "layers_panel");
            if (layers_panel) {
                layers_panel_update(layers_panel, doc);
            }

            /* Update menu and button states */
            ui_update_menu_and_button_states(ctx);

            /* Update recent files menu */
            ui_update_recent_files_menu(ctx);
        }
    }

    g_free(basename);
}

/**
 * Callback for clearing recent files
 */
static void on_clear_recent_files(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;

    recent_files_clear();
    recent_files_save();
    ui_update_recent_files_menu(ctx);
}

/**
 * Update the "Open Recent" submenu with current recent files
 */
void ui_update_recent_files_menu(AppContext* ctx) {
    if (!ctx || !ctx->window) {
        return;
    }

    /* Get the file menu from the window */
    GtkBuilder* builder = (GtkBuilder*)g_object_get_data(G_OBJECT(ctx->window), "main_builder");
    if (!builder) {
        return;
    }

    GtkWidget* file_menu = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu"));
    if (!file_menu) {
        return;
    }

    GtkWidget* recent_submenu = (GtkWidget*)g_object_get_data(G_OBJECT(file_menu), "recent_files_submenu");
    if (!recent_submenu) {
        /* Submenu not created yet - this is okay, just return */
        return;
    }

    if (!GTK_IS_MENU(recent_submenu)) {
        g_warning("recent_files_submenu is not a GtkMenu");
        return;
    }

    /* Clear existing menu items and free stored file paths */
    if (GTK_IS_CONTAINER(recent_submenu)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(recent_submenu));
        for (GList* iter = children; iter; iter = iter->next) {
            GtkWidget* widget = GTK_WIDGET(iter->data);
            if (widget) {
                gchar* stored_path = (gchar*)g_object_get_data(G_OBJECT(widget), "recent_file_path");
                if (stored_path) {
                    g_free(stored_path);
                }
                gtk_widget_destroy(widget);
            }
        }
        g_list_free(children);
    }

    /* Get recent files list */
    const GList* recent_files = recent_files_get();

    if (!recent_files || g_list_length((GList*)recent_files) == 0) {
        /* No recent files - disable the menu item */
        GtkWidget* file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
        if (file_menu_open_recent) {
            gtk_widget_set_sensitive(file_menu_open_recent, FALSE);
        }
        return;
    }

    /* Enable the menu item */
    GtkWidget* file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
    if (file_menu_open_recent) {
        gtk_widget_set_sensitive(file_menu_open_recent, TRUE);
    }

    /* Add menu items for each recent file */
    for (const GList* iter = recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        if (!rf || !rf->path) {
            continue;
        }

        /* Get filename for display */
        gchar* basename = g_path_get_basename(rf->path);
        GtkWidget* menu_item = gtk_menu_item_new_with_label(basename);
        g_free(basename);

        /* Set tooltip to full path */
        gtk_widget_set_tooltip_text(menu_item, rf->path);

        /* Store file path in menu item data */
        g_object_set_data(G_OBJECT(menu_item), "recent_file_path", g_strdup(rf->path));

        /* Connect activate signal */
        g_signal_connect(menu_item, "activate", G_CALLBACK(on_recent_file_activate), ctx);

        gtk_menu_shell_append(GTK_MENU_SHELL(recent_submenu), menu_item);
        gtk_widget_show(menu_item);
    }

    /* Add separator */
    GtkWidget* separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(recent_submenu), separator);
    gtk_widget_show(separator);

    /* Add "Clear Recent Files" item */
    GtkWidget* clear_item = gtk_menu_item_new_with_label("Clear Recent Files");
    g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear_recent_files), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(recent_submenu), clear_item);
    gtk_widget_show(clear_item);
}

void ui_context_free(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    /* Free all documents */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        document_free((ImageDocument*)iter->data);
    }

    g_list_free(ctx->documents);

    /* Free tool manager */
    if (ctx->tool_registry) {
        tool_manager_free(ctx->tool_registry);
    }

    g_free(ctx);
}

/**
 * Setup File menu from Glade builder
 */
static void setup_file_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* file_menu = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu"));
    GtkWidget* file_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_item"));

    if (file_menu && file_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
    }

    /* Connect File menu signals */
    GtkWidget* file_menu_open = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open"));
    GtkWidget* file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
    GtkWidget* file_menu_save_as = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_save_as"));
    GtkWidget* file_menu_close = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_close"));
    GtkWidget* file_menu_exit = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_exit"));

    if (file_menu_open) {
        g_signal_connect(file_menu_open, "activate", G_CALLBACK(on_file_open), ctx);
        gtk_widget_add_accelerator(file_menu_open, "activate", accel_group,
                                   GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    /* Setup "Open Recent" submenu */
    if (file_menu_open_recent) {
        if (!file_menu) {
            g_warning("file_menu is NULL, cannot setup Open Recent submenu");
        } else {
            GtkWidget* recent_submenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_open_recent), recent_submenu);
            g_object_set_data(G_OBJECT(file_menu), "recent_files_submenu", recent_submenu);
            /* Don't update menu here - it will be updated after window is fully created */
        }
    }

    if (file_menu_save_as) {
        g_signal_connect(file_menu_save_as, "activate", G_CALLBACK(on_file_save_as), ctx);
        gtk_widget_add_accelerator(file_menu_save_as, "activate", accel_group,
                                   GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    }
    if (file_menu_close) {
        g_signal_connect(file_menu_close, "activate", G_CALLBACK(on_file_close), ctx);
    }
    if (file_menu_exit) {
        g_signal_connect(file_menu_exit, "activate", G_CALLBACK(on_file_exit), ctx);
    }
}

/**
 * Setup Edit menu from Glade builder
 */
static void setup_edit_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
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
}

/**
 * Setup View menu from Glade builder
 */
static void setup_view_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    (void)accel_group; /* Not used for View menu yet */

    GtkWidget* view_menu = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu"));
    GtkWidget* view_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_item"));

    if (view_menu && view_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_menu_item), view_menu);
    }

    /* Connect View menu signals */
    GtkWidget* view_menu_zoom_in = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_in"));
    GtkWidget* view_menu_zoom_out = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_out"));
    GtkWidget* view_menu_zoom_reset = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_reset"));
    GtkWidget* view_menu_zoom_fit = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_fit"));

    if (view_menu_zoom_in) {
        g_signal_connect(view_menu_zoom_in, "activate", G_CALLBACK(on_view_zoom_in), ctx);
    }
    if (view_menu_zoom_out) {
        g_signal_connect(view_menu_zoom_out, "activate", G_CALLBACK(on_view_zoom_out), ctx);
    }
    if (view_menu_zoom_reset) {
        g_signal_connect(view_menu_zoom_reset, "activate", G_CALLBACK(on_view_zoom_reset), ctx);
    }
    if (view_menu_zoom_fit) {
        g_signal_connect(view_menu_zoom_fit, "activate", G_CALLBACK(on_view_zoom_fit), ctx);
    }
}

/**
 * Setup Layer menu from Glade builder
 */
static void setup_layer_menu(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* layer_menu = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu"));
    GtkWidget* layer_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_item"));

    if (layer_menu && layer_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(layer_menu_item), layer_menu);
    }

    /* Get menu items that need to be updated programmatically */
    ctx->layer_menu_new = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_new"));
    ctx->layer_menu_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_duplicate"));
    ctx->layer_menu_delete = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_delete"));

    /* Connect Layer menu signals */
    if (ctx->layer_menu_new) {
        g_signal_connect(ctx->layer_menu_new, "activate", G_CALLBACK(on_layer_new), ctx);
    }
    if (ctx->layer_menu_duplicate) {
        g_signal_connect(ctx->layer_menu_duplicate, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    }
    if (ctx->layer_menu_delete) {
        g_signal_connect(ctx->layer_menu_delete, "activate", G_CALLBACK(on_layer_delete), ctx);
    }
}

/**
 * Setup Adjustments menu from Glade builder
 */
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx) {
    ui_filter_adjust_setup_menu(builder, ctx);
}

/**
 * Setup Effects menu from Glade builder
 */
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx) {
    ui_filter_effects_setup_menu(builder, ctx);
}

/**
 * File Open dialog response callback
 */
static void on_file_open_response(GtkDialog* dialog, gint response_id, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar* file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (file_path) {
            /* Create new document with filename */
            gchar* basename = g_path_get_basename(file_path);
            ImageDocument* doc = ui_create_document_tab(ctx, basename);

            if (doc) {
                /* Load the image into the document */
                if (!document_load_image_from_file(doc, file_path)) {
                    g_warning("Failed to load image: %s", file_path);
                } else {
                    /* Add to recent files after successful load */
                    recent_files_add(file_path);
                    recent_files_save();

                    /* Update status bar after successful load */
                    ui_update_status_bar(ctx, NULL);

                    /* Update layers panel with loaded document */
                    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(
                        G_OBJECT(ctx->window), "layers_panel");
                    if (layers_panel) {
                        layers_panel_update(layers_panel, doc);
                    }

                    /* Update menu and button states */
                    ui_update_menu_and_button_states(ctx);

                    /* Update recent files menu */
                    ui_update_recent_files_menu(ctx);
                }
            }

            g_free(basename);
            g_free(file_path);
        }
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/**
 * File > Open callback
 */
static void on_file_open(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    if (!ctx) {
        g_warning("Invalid context in on_file_open");
        return;
    }
    if (!ctx->window || !GTK_IS_WINDOW(ctx->window)) {
        g_warning("Invalid window in on_file_open");
        return;
    }
    GtkWidget* dialog;
    GtkFileFilter* filter;

    /* Create file chooser dialog */
    dialog = gtk_file_chooser_dialog_new(
        "Open Image",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    /* Add file filters */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PNG Images");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.PNG");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "JPEG Images");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.JPG");
    gtk_file_filter_add_pattern(filter, "*.JPEG");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* Connect response signal */
    g_signal_connect(dialog, "response", G_CALLBACK(on_file_open_response), ctx);

    /* Show dialog */
    gtk_widget_show(dialog);
}

/**
 * File > Save As response callback
 */
static void on_file_save_as_response(GtkDialog* dialog, gint response_id, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar* file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (file_path) {
            ImageDocument* doc = ui_get_active_document(ctx);

            if (doc) {
                /* Save the document */
                if (document_save_as(doc, file_path)) {
                    /* Update window title to reflect new filename */
                    ui_update_window_title(ctx);
                    ui_update_status_bar(ctx, NULL);
                    // printf("Document saved: %s\n", file_path);
                } else {
                    g_warning("Failed to save document");
                }
            }

            g_free(file_path);
        }
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/**
 * File > Save As callback
 */
static void on_file_save_as(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    GtkWidget* dialog;
    GtkFileFilter* filter;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Create file chooser dialog */
    dialog = gtk_file_chooser_dialog_new(
        "Save Image As",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    /* Add file filters */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PNG Images");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.PNG");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "JPEG Images");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.JPG");
    gtk_file_filter_add_pattern(filter, "*.JPEG");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* Set current filename if document has a path */
    if (doc->file_path) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), doc->file_path);
    } else if (doc->filename) {
        /* Suggest a filename based on document name */
        gchar* suggested = g_strdup_printf("%s.png", doc->filename);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested);
        g_free(suggested);
    }

    /* Connect response signal */
    g_signal_connect(dialog, "response", G_CALLBACK(on_file_save_as_response), ctx);

    /* Show dialog */
    gtk_widget_show(dialog);
}

/**
 * File > Close callback
 */
static void on_file_close(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* active_doc = ui_get_active_document(ctx);

    if (active_doc) {
        ui_close_document_tab(ctx, active_doc);
    }
}

/**
 * Window delete event callback
 */
static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data) {
    (void)widget; /* Unused */
    (void)event;  /* Unused */

    AppContext* ctx = (AppContext*)data;

    // printf("Window delete event triggered - shutting down\n");

    /* Save and shutdown recent files */
    recent_files_shutdown();

    /* Shutdown autosave system */
    autosave_shutdown();

    /* Free the context (which frees all documents) */
    ui_context_free(ctx);

    /* Exit GTK main loop */
    gtk_main_quit();

    return FALSE; /* Allow window to close */
}

/**
 * File > Exit callback
 */
static void on_file_exit(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;

    /* Save and shutdown recent files */
    recent_files_shutdown();

    /* Free the context (which frees all documents) */
    ui_context_free(ctx);

    /* Exit GTK main loop */
    gtk_main_quit();
}

/**
 * Edit > Undo callback
 */
static void on_edit_undo(GtkWidget* widget, gpointer data) {
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
    ui_update_window_title(ctx);
}

/**
 * Edit > Redo callback
 */
static void on_edit_redo(GtkWidget* widget, gpointer data) {
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
    ui_update_window_title(ctx);
}

/**
 * View > Zoom In callback
 */
static void on_view_zoom_in(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_in(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Zoom Out callback
 */
static void on_view_zoom_out(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_out(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Reset Zoom callback
 */
static void on_view_zoom_reset(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_reset(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Zoom Fit callback
 */
static void on_view_zoom_fit(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_fit(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * Notebook page switch callback
 */
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page,
                                    guint page_num, gpointer user_data) {
    (void)notebook; /* Unused */
    (void)page_num; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = NULL;
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");

    /* Find the document that matches the page widget (scrolled window) */
    if (page) {
        for (GList* iter = ctx->documents; iter; iter = iter->next) {
            ImageDocument* d = (ImageDocument*)iter->data;
            if (d && d->scrolled_window == page) {
                doc = d;
                break;
            }
        }
    }

    /* Fallback to ui_get_active_document if page matching fails */
    if (!doc) {
        doc = ui_get_active_document(ctx);
    }

    ui_update_window_title(ctx);
    ui_update_status_bar(ctx, doc);

    /* Update layers panel with current document's layers */
    if (layers_panel && doc) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update menu and button sensitivity */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Tab close button clicked callback
 */
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(button),
                                                     "app_context");

    if (ctx && doc) {
        ui_close_document_tab(ctx, doc);
    } else {
        printf("  ERROR: ctx=%p or doc=%p is NULL\n", ctx, doc);
    }
}

/**
 * Layer > New Layer callback
 */
static void on_layer_new(GtkWidget* widget, gpointer data) {
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

    /* Create new layer with auto-generated name */
    static int layer_count = 1;
    gchar* layer_name = g_strdup_printf("Layer %d", layer_count++);

    /* Use default values: transparent background, above current layer */
    ImageLayer* new_layer = document_add_layer(doc, layer_name,
                                               LAYER_BACKGROUND_TRANSPARENT,
                                               LAYER_POSITION_ABOVE_CURRENT,
                                               NULL);
    g_free(layer_name);

    if (new_layer) {
        /* Create undo command */
        cmd = command_create_layer_add(doc, new_layer);
        if (cmd && doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);

            /* Clear redo stack */
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }
        } else if (cmd) {
            command_free(cmd);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx);
        doc->modified = TRUE;
    }
}

/**
 * Layer > Delete Layer callback
 */
static void on_layer_delete(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Delete the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create undo command before deleting */
    cmd = command_create_layer_delete(doc, selected_layer);
    if (!cmd) {
        g_warning("Failed to create delete layer command");
        return;
    }

    /* Execute the delete (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
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
    ui_update_window_title(ctx);
    doc->modified = TRUE;
}

/**
 * Layer > Duplicate Layer callback
 */
static void on_layer_duplicate(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Duplicate the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    static int dup_count = 1;
    gchar* layer_name = g_strdup_printf("%s copy %d", selected_layer->name, dup_count++);

    ImageLayer* dup_layer = document_duplicate_layer(doc, selected_layer, layer_name);
    g_free(layer_name);

    if (dup_layer) {
        /* Create undo command */
        cmd = command_create_layer_duplicate(doc, selected_layer, dup_layer);
        if (cmd && doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);

            /* Clear redo stack */
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }
        } else if (cmd) {
            command_free(cmd);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx);
        doc->modified = TRUE;
    }
}

/**
 * Layer > Move Up callback
 */
static void on_layer_move_up(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Get the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create undo command before moving */
    cmd = command_create_layer_move_up(doc, selected_layer);
    if (!cmd) {
        return; /* Can't move up */
    }

    /* Execute the move (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
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
    ui_update_window_title(ctx);
    doc->modified = TRUE;
}

/**
 * Layer > Move Down callback
 */
static void on_layer_move_down(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Get the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create undo command before moving */
    cmd = command_create_layer_move_down(doc, selected_layer);
    if (!cmd) {
        return; /* Can't move down */
    }

    /* Execute the move (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
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
    ui_update_window_title(ctx);
    doc->modified = TRUE;
}

/**
 * Update the status bar with document information
 */
void ui_update_status_bar(AppContext* ctx, ImageDocument* doc) {
    GtkWidget *size_label, *bitdepth_label, *zoom_label;
    GtkBuilder* builder;
    gchar *size_text, *bitdepth_text, *zoom_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get active document if not provided */
    if (!doc) {
        doc = ui_get_active_document(ctx);
    }

    /* Get builder from window to retrieve status bar labels */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get status bar labels */
    size_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_size"));
    bitdepth_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_bitdepth"));
    zoom_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_zoom"));

    if (!size_label || !bitdepth_label || !zoom_label) {
        return;
    }

    if (!doc || doc->width == 0) {
        /* No document or empty document */
        size_text = g_strdup("—");
        bitdepth_text = g_strdup("—");
        zoom_text = g_strdup("—");
    } else {
        /* Size label: WIDTH x HEIGHT */
        size_text = g_strdup_printf("%u × %u", doc->width, doc->height);

        /* Bit depth label: BITDEPTH-bit CHANNELS */
        gchar channels_str[32];
        if (doc->channels == 3) {
            snprintf(channels_str, sizeof(channels_str), "RGB");
        } else if (doc->channels == 4) {
            snprintf(channels_str, sizeof(channels_str), "RGBA");
        } else {
            snprintf(channels_str, sizeof(channels_str), "%u-channel", doc->channels);
        }
        bitdepth_text = g_strdup_printf("%u-bit %s", doc->bit_depth, channels_str);

        /* Zoom label: ZOOM% */
        zoom_text = g_strdup_printf("%.0f%%", doc->zoom_factor * 100.0);
    }

    /* Update labels */
    gtk_label_set_text(GTK_LABEL(size_label), size_text);
    gtk_label_set_text(GTK_LABEL(bitdepth_label), bitdepth_text);
    gtk_label_set_text(GTK_LABEL(zoom_label), zoom_text);

    g_free(size_text);
    g_free(bitdepth_text);
    g_free(zoom_text);
}

/**
 * Update the status bar time label with processing time
 * @param ctx The application context
 * @param time_seconds Processing time in seconds
 */
void ui_update_status_bar_time(AppContext* ctx, gdouble time_seconds) {
    GtkWidget* time_label;
    GtkBuilder* builder;
    gchar* time_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get builder from window to retrieve status bar labels */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get time label */
    time_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_time"));
    if (!time_label) {
        return;
    }

    /* Format time: show seconds with appropriate precision */
    if (time_seconds < 0.001) {
        time_text = g_strdup_printf("Time taken: %.3f ms", time_seconds * 1000.0);
    } else if (time_seconds < 1.0) {
        time_text = g_strdup_printf("Time taken: %.3f s", time_seconds);
    } else {
        time_text = g_strdup_printf("Time taken: %.2f s", time_seconds);
    }

    /* Update label */
    gtk_label_set_text(GTK_LABEL(time_label), time_text);
    g_free(time_text);
}

/**
 * Update menu and button sensitivity based on document and layer state
 */
void ui_update_menu_and_button_states(AppContext* ctx) {
    ImageDocument* doc;
    LayersPanel* layers_panel;
    gboolean has_document;
    gboolean has_selection;

    if (!ctx || !ctx->window) {
        return;
    }

    /* Get current document */
    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);

    /* Get layers panel and check for selection */
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                   "layers_panel");

    has_selection = FALSE;
    ImageLayer* selected_layer = NULL;
    if (layers_panel && doc && layers_panel->tree_view) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
        has_selection = (selected_layer != NULL);
    }

    /* Update layers panel button states */
    if (layers_panel) {
        layers_panel_update_button_sensitivity(layers_panel, has_document, has_selection, doc, selected_layer);
        layers_panel_update_opacity_controls(layers_panel);
    }

    /* Update Edit menu item states (undo/redo) */
    if (ctx->edit_menu_undo && GTK_IS_WIDGET(ctx->edit_menu_undo)) {
        gboolean can_undo = has_document && document_can_undo(doc);
        gtk_widget_set_sensitive(ctx->edit_menu_undo, can_undo);

        /* Update label with command name if available */
        if (can_undo && doc && doc->undo_stack) {
            Command* cmd = command_stack_peek(doc->undo_stack);
            if (cmd && cmd->name) {
                gchar* label = g_strdup_printf("_Undo: %s", cmd->name);
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_undo), label);
                g_free(label);
            } else {
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_undo), "_Undo");
            }
        } else {
            gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_undo), "_Undo");
        }
    }
    if (ctx->edit_menu_redo && GTK_IS_WIDGET(ctx->edit_menu_redo)) {
        gboolean can_redo = has_document && document_can_redo(doc);
        gtk_widget_set_sensitive(ctx->edit_menu_redo, can_redo);

        /* Update label with command name if available */
        if (can_redo && doc && doc->redo_stack) {
            Command* cmd = command_stack_peek(doc->redo_stack);
            if (cmd && cmd->name) {
                gchar* label = g_strdup_printf("_Redo: %s", cmd->name);
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_redo), label);
                g_free(label);
            } else {
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_redo), "_Redo");
            }
        } else {
            gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_redo), "_Redo");
        }
    }

    /* Update Layer menu item states */
    if (ctx->layer_menu_new && GTK_IS_WIDGET(ctx->layer_menu_new)) {
        gtk_widget_set_sensitive(ctx->layer_menu_new, has_document);
    }
    if (ctx->layer_menu_delete && GTK_IS_WIDGET(ctx->layer_menu_delete)) {
        gtk_widget_set_sensitive(ctx->layer_menu_delete, has_document && has_selection);
    }
    if (ctx->layer_menu_duplicate && GTK_IS_WIDGET(ctx->layer_menu_duplicate)) {
        gtk_widget_set_sensitive(ctx->layer_menu_duplicate, has_document && has_selection);
    }
}

/**
 * Get canvas background color
 */
void ui_get_canvas_background_color(AppContext* ctx, gdouble* r, gdouble* g, gdouble* b) {
    if (!ctx) {
        /* Return default if context is NULL */
        if (r)
            *r = 160.0 / 255.0;
        if (g)
            *g = 160.0 / 255.0;
        if (b)
            *b = 160.0 / 255.0;
        return;
    }

    if (r)
        *r = ctx->canvas_bg_r;
    if (g)
        *g = ctx->canvas_bg_g;
    if (b)
        *b = ctx->canvas_bg_b;
}

/**
 * Set canvas background color
 */
void ui_set_canvas_background_color(AppContext* ctx, gdouble r, gdouble g, gdouble b) {
    if (!ctx) {
        return;
    }

    /* Clamp values to 0.0-1.0 */
    ctx->canvas_bg_r = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    ctx->canvas_bg_g = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    ctx->canvas_bg_b = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);

    /* Update all open documents */
    ui_update_canvas_background_color(ctx);
}

/**
 * Update canvas background color for all open documents
 */
void ui_update_canvas_background_color(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    guint r = (guint)(ctx->canvas_bg_r * 255.0);
    guint g = (guint)(ctx->canvas_bg_g * 255.0);
    guint b = (guint)(ctx->canvas_bg_b * 255.0);

    /* Update viewport background for all documents */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument* doc = (ImageDocument*)iter->data;
        if (doc && doc->viewport) {
            /* Get or create CSS provider for this viewport */
            GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(G_OBJECT(doc->viewport), "canvas_bg_provider");
            if (!provider) {
                provider = gtk_css_provider_new();
                g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider", provider, g_object_unref);
                GtkStyleContext* style_context = gtk_widget_get_style_context(doc->viewport);
                gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            }

            gchar* css = g_strdup_printf("#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
            gtk_css_provider_load_from_data(provider, css, -1, NULL);
            g_free(css);
            gtk_widget_queue_draw(doc->viewport);
        }
    }
}

/**
 * Save active document with file dialog
 */
void ui_save_document_as(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    on_file_save_as(NULL, ctx);
}
