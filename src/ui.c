#include "ui.h"
#include "app/autosave.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "ocular.h"
#include "panels.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "render/tile_worker.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "selection/selection_undo.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui/dialogs/canvas_size_dialog.h"
#include "ui/dialogs/new_layer_dialog.h"
#include "ui/dialogs/recovery_dialog.h"
#include "ui/dialogs/selection_radius_dialog.h"
#include "ui/layers_panel.h"
#include "ui/tool_options_panel.h"
#include "ui/tools_panel.h"
#include "ui/ui_filter.h"
#include "ui/ui_filter_adjust.h"
#include "ui/ui_filter_effects.h"
#include "undo/undo_disk.h"
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
static void on_image_canvas_size(GtkWidget* widget, gpointer data);
static void on_image_duplicate(GtkWidget* widget, gpointer data);
static void on_image_fit_active_layer(GtkWidget* widget, gpointer data);
static void on_image_fit_all_layers(GtkWidget* widget, gpointer data);
static void on_image_flip_horizontal(GtkWidget* widget, gpointer data);
static void on_image_flip_vertical(GtkWidget* widget, gpointer data);
static void on_image_transpose(GtkWidget* widget, gpointer data);
static void on_image_merge_visible(GtkWidget* widget, gpointer data);
static void on_image_flatten(GtkWidget* widget, gpointer data);
static void on_edit_undo(GtkWidget* widget, gpointer data);
static void on_edit_redo(GtkWidget* widget, gpointer data);
static void on_edit_copy(GtkWidget* widget, gpointer data);
static void on_edit_cut(GtkWidget* widget, gpointer data);
static void on_edit_paste(GtkWidget* widget, gpointer data);
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
static void setup_image_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_layer_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_select_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx);

/**
 * Layer selection changed callback - proper signal handler signature
 */
static void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    (void)selection; /* Unused - we get it from the tree view */

    AppContext* ctx = (AppContext*)user_data;

    if (!ctx) {
        return;
    }

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
    ctx->settings = NULL;     /* Will be set in main.c */
    ctx->app_dir = NULL;      /* Will be set in main.c */

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
    setup_image_menu(builder, ctx);
    setup_layer_menu(builder, ctx);
    setup_select_menu(builder, ctx, accel_group);
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

    /* Create the document with worker pool for on-screen rendering */
    doc = document_new(filename, TRUE);

    /* Create disk-backed undo journal if settings are available */
    if (ctx && ctx->settings && doc) {
        gint compression_level = settings_get_undo_compression_level(ctx->settings);
        const gchar* temp_dir = settings_get_undo_temp_directory(ctx->settings);
        doc->undo_journal = undo_journal_create((struct ImageDocument*)doc, temp_dir, compression_level);
        if (!doc->undo_journal) {
            g_warning("Failed to create undo journal, falling back to in-memory undo");
        }
    }

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
        gdouble r_val, g_val, b_val;
        ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
        guint r = (guint)(r_val * 255.0);
        guint g = (guint)(g_val * 255.0);
        guint b = (guint)(b_val * 255.0);

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

    /* Set the current document in tool registry so tools can access it */
    if (ctx->tool_registry) {
        ctx->tool_registry->current_doc = doc;
    }

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

        /* Update layers panel - show active document if one exists, otherwise clear */
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                    "layers_panel");
        if (layers_panel) {
            ImageDocument* active_doc = ui_get_active_document(ctx);
            layers_panel_update(layers_panel, active_doc);
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
        recent_files_save(); /* This syncs to settings if connected */
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
            if (ctx->settings && ctx->app_dir) {
                /* Sync recent files to settings and save */
                recent_files_save();
                settings_save(ctx->settings, ctx->app_dir);
            }

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

    /* Clear recent files */
    recent_files_clear();
    recent_files_save(); /* This syncs to settings if connected */

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

    /* Get recent files list from recent_files system */
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

    /* Free settings (will be saved in main.c before this is called) */
    if (ctx->settings) {
        settings_free(ctx->settings);
    }

    /* Free app directory */
    if (ctx->app_dir) {
        g_free(ctx->app_dir);
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

    /* Connect Copy, Cut, Paste menu items */
    GtkWidget* edit_menu_copy = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_copy"));
    GtkWidget* edit_menu_cut = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_cut"));
    GtkWidget* edit_menu_paste = GTK_WIDGET(gtk_builder_get_object(builder, "edit_menu_paste"));

    if (edit_menu_copy) {
        g_signal_connect(edit_menu_copy, "activate", G_CALLBACK(on_edit_copy), ctx);
    }
    if (edit_menu_cut) {
        g_signal_connect(edit_menu_cut, "activate", G_CALLBACK(on_edit_cut), ctx);
    }
    if (edit_menu_paste) {
        g_signal_connect(edit_menu_paste, "activate", G_CALLBACK(on_edit_paste), ctx);
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
 * Setup Image menu from Glade builder
 */
static void setup_image_menu(GtkBuilder* builder, AppContext* ctx) {
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
 * Callback for Select All menu item
 */
static void on_select_all(GtkMenuItem* menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = ui_get_active_document(ctx);

    (void)menu_item; /* Unused parameter */

    if (!doc || !doc->selection_mask) {
        return;
    }

    /* Begin transaction, perform operation, commit */
    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, command_get_name_string(CMD_NAME_SELECT_ALL));
    if (!transaction) {
        return;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0,
                                               doc->selection_mask->width,
                                               doc->selection_mask->height);

    /* Perform select all operation - fill entire mask using direct_modify flag */
    selection_mask_fill_rect(doc->selection_mask, 0, 0,
                             doc->selection_mask->width,
                             doc->selection_mask->height,
                             SELECTION_COMBINE_NEW,
                             SELECTION_SMOOTH_NONE,
                             0.0f,
                             TRUE); /* TRUE = direct modify (no Selection objects, no tool commands) */

    /* Ensure mask->data is set correctly after the operation */
    if (doc->selection_mask) {
        if (!doc->selection_mask->data && doc->selection_mask->base_mask) {
            doc->selection_mask->data = doc->selection_mask->base_mask;
        }
    }

    /* Commit transaction and get command */
    Command* cmd = selection_undo_transaction_commit(transaction);
    if (cmd && doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
        command_execute(cmd, doc);
        document_invalidate_composite(doc);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        /* Update menu to show the new undo command */
        ui_update_menu_and_button_states(ctx);
    } else if (cmd) {
        command_free(cmd);
    }
}

/**
 * Callback for Deselect All (Select None) menu item
 */
static void on_select_none(GtkMenuItem* menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = ui_get_active_document(ctx);

    (void)menu_item; /* Unused parameter */

    if (!doc || !doc->selection_mask) {
        return;
    }

    /* Begin transaction, perform operation, commit */
    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, command_get_name_string(CMD_NAME_DESELECT_ALL));
    if (!transaction) {
        return;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0,
                                               doc->selection_mask->width,
                                               doc->selection_mask->height);

    /* Perform deselect all operation - directly clear base_mask */
    int stride = doc->selection_mask->stride;
    int width = doc->selection_mask->width;
    int height = doc->selection_mask->height;
    uint8_t* base_mask = doc->selection_mask->base_mask;

    /* Clear entire base_mask */
    for (int y = 0; y < height; y++) {
        uint8_t* row = base_mask + y * stride;
        for (int x = 0; x < width; x++) {
            row[x] = 0;
        }
    }

    /* Clear selections list since we're modifying base_mask directly */
    if (doc->selection_mask->selections) {
        GList* iter;
        for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(doc->selection_mask->selections);
        doc->selection_mask->selections = NULL;
    }

    /* Set data pointer to base_mask (no feathering) */
    doc->selection_mask->data = doc->selection_mask->base_mask;

    /* Mark mask as dirty */
    selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
    doc->selection_mask->feather_dirty = TRUE;

    /* Commit transaction and get command */
    Command* cmd = selection_undo_transaction_commit(transaction);
    if (cmd && doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
        command_execute(cmd, doc);
        document_invalidate_composite(doc);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        /* Update menu to show the new undo command */
        ui_update_menu_and_button_states(ctx);
    } else if (cmd) {
        command_free(cmd);
    }
}

/**
 * Callback for Invert Selection menu item
 */
static void on_select_invert(GtkMenuItem* menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = ui_get_active_document(ctx);

    (void)menu_item; /* Unused parameter */

    if (!doc || !doc->selection_mask || !doc->selection_mask->base_mask) {
        return;
    }

    /* Begin transaction, perform operation, commit */
    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, command_get_name_string(CMD_NAME_INVERT_SELECTION));
    if (!transaction) {
        return;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0,
                                               doc->selection_mask->width,
                                               doc->selection_mask->height);

    /* Check if there's any feathering in existing selections before clearing them */
    gboolean has_feathering = FALSE;
    SelectionSmoothingMode preserved_feather_mode = SELECTION_SMOOTH_NONE;
    float preserved_feather_radius = 0.0f;
    if (doc->selection_mask->selections) {
        GList* iter;
        for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel && sel->feather_mode == SELECTION_SMOOTH_FEATHERED && sel->feather_radius > 0.0f) {
                has_feathering = TRUE;
                preserved_feather_mode = sel->feather_mode;
                preserved_feather_radius = sel->feather_radius;
                break; /* Use first feathered selection's parameters */
            }
        }
    }

    int stride = doc->selection_mask->stride;
    int width = doc->selection_mask->width;
    int height = doc->selection_mask->height;

    /* For Select Invert with feathered selections, we need to invert the base_mask first,
     * then regenerate feathering from the inverted base_mask. This ensures the feathering
     * gradient is calculated from the correct direction (outside edge inward, not inside edge outward). */

    /* Invert the base_mask */
    uint8_t* inverted_base_mask = g_malloc0(stride * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            inverted_base_mask[y * stride + x] = 255 - doc->selection_mask->base_mask[y * stride + x];
        }
    }

    /* Clear existing selections list */
    if (doc->selection_mask->selections) {
        GList* iter;
        for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(doc->selection_mask->selections);
        doc->selection_mask->selections = NULL;
    }

    /* Create a Selection object to represent the inverted selection */
    /* Preserve feathering parameters if they existed */
    Selection* sel = selection_new(0, 0, width, height,
                                   SELECTION_COMBINE_NEW,
                                   preserved_feather_mode,
                                   preserved_feather_radius);
    if (sel) {
        /* Allocate mask for this selection (use same stride as main mask) */
        sel->mask = g_malloc0(stride * height);

        /* Copy the inverted base_mask to the selection's mask */
        /* Note: base_mask will be rebuilt by selection_mask_add_selection -> selection_mask_rebuild_from_selections */
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                sel->mask[y * stride + x] = inverted_base_mask[y * stride + x];
            }
        }

        /* If feathering is active, mark the selection as dirty so feathering will be
         * regenerated from the inverted base_mask. This ensures the feathering gradient
         * is calculated from the correct direction (outside edge inward for the inverted selection). */
        if (has_feathering) {
            /* Clear any existing feathered_preview to force regeneration */
            if (sel->feathered_preview) {
                g_free(sel->feathered_preview);
                sel->feathered_preview = NULL;
            }
            sel->feather_dirty = TRUE; /* Force regeneration from inverted base_mask */
        }

        g_free(inverted_base_mask);
        inverted_base_mask = NULL;

        /* Add selection to list */
        selection_mask_add_selection(doc->selection_mask, sel);
        selection_unref(sel); /* Release our reference (list now owns it) */
    } else {
        /* Failed to create selection, free the inverted base mask */
        if (inverted_base_mask) {
            g_free(inverted_base_mask);
        }
    }

    /* Regenerate combined feathered preview if feathering is active */
    /* For inverted selections with feathering, this will regenerate feathering from the
     * inverted base_mask, ensuring the gradient is calculated from the correct direction
     * (outside edge inward for the inverted selection). */
    if (has_feathering) {
        selection_mask_regenerate_combined_feather_preview(doc->selection_mask);
    } else {
        doc->selection_mask->data = doc->selection_mask->base_mask;
    }

    /* Mark mask as dirty */
    selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
    doc->selection_mask->feather_dirty = TRUE;

    /* Commit transaction and get command */
    Command* cmd = selection_undo_transaction_commit(transaction);
    if (cmd && doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
        command_execute(cmd, doc);
        document_invalidate_composite(doc);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        /* Update menu to show the new undo command */
        ui_update_menu_and_button_states(ctx);
    } else if (cmd) {
        command_free(cmd);
    }
}

/**
 * Helper function to execute a selection radius operation
 */
/**
 * Progress dialog structure for selection operations
 */
typedef struct {
    GtkWidget* dialog;
    GtkWidget* label;
    GtkWidget* progress_bar;
    guint pulse_timeout_id;
} SelectionProgressDialog;

/* Forward declaration */
static gboolean pulse_selection_progress_bar(gpointer user_data);

/**
 * Create and show a progress dialog for selection operations
 */
static SelectionProgressDialog* show_selection_progress_dialog(GtkWindow* parent, const gchar* operation_name) {
    SelectionProgressDialog* progress;
    GtkWidget* content_area;
    GtkWidget* vbox;
    GtkWidget* label;
    GtkWidget* progress_bar;

    progress = (SelectionProgressDialog*)g_malloc(sizeof(SelectionProgressDialog));
    if (!progress) {
        return NULL;
    }

    /* Initialize */
    progress->pulse_timeout_id = 0;

    /* Create dialog */
    progress->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(progress->dialog), "Processing Selection");
    gtk_window_set_modal(GTK_WINDOW(progress->dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(progress->dialog), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(progress->dialog), FALSE);

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(progress->dialog), parent);
    }

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(progress->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);

    /* Create vertical box */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    /* Create label showing operation name */
    label = gtk_label_new(NULL);
    gchar* label_text = g_strdup_printf("Processing %s...", operation_name ? operation_name : "selection");
    gtk_label_set_text(GTK_LABEL(label), label_text);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    g_free(label_text);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    progress->label = label;

    /* Create progress bar in activity mode (indeterminate) */
    progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(progress_bar), 0.1);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), FALSE);
    gtk_widget_set_size_request(progress_bar, 300, -1);
    gtk_box_pack_start(GTK_BOX(vbox), progress_bar, FALSE, FALSE, 0);
    progress->progress_bar = progress_bar;

    /* Show all widgets */
    gtk_widget_show_all(progress->dialog);

    /* Start pulsing the progress bar periodically (every 50ms) */
    progress->pulse_timeout_id = g_timeout_add(50, pulse_selection_progress_bar, progress);
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));

    /* Process pending events to show the dialog */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return progress;
}

/**
 * Timeout callback to pulse progress bar
 */
static gboolean pulse_selection_progress_bar(gpointer user_data) {
    SelectionProgressDialog* progress = (SelectionProgressDialog*)user_data;
    if (progress && progress->progress_bar) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress->progress_bar));
    }
    return G_SOURCE_CONTINUE;
}

/**
 * Hide and destroy progress dialog
 */
static void hide_selection_progress_dialog(SelectionProgressDialog* progress) {
    if (!progress) {
        return;
    }

    /* Remove timeout if it exists */
    if (progress->pulse_timeout_id > 0) {
        g_source_remove(progress->pulse_timeout_id);
        progress->pulse_timeout_id = 0;
    }

    if (progress->dialog) {
        gtk_widget_destroy(progress->dialog);
    }

    g_free(progress);
}

/**
 * Progress callback for selection operations
 */
static gboolean selection_operation_progress_callback(gint current, gint total, gpointer user_data) {
    SelectionProgressDialog* progress = (SelectionProgressDialog*)user_data;

    if (!progress || !progress->progress_bar) {
        return TRUE; /* Continue */
    }

    /* Update progress bar fraction */
    if (total > 0) {
        gdouble fraction = (gdouble)(current + 1) / (gdouble)total;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress->progress_bar), fraction);

        /* Update label text */
        if (progress->label) {
            gchar* label_text = g_strdup_printf("Processing selection %d of %d...", current + 1, total);
            gtk_label_set_text(GTK_LABEL(progress->label), label_text);
            g_free(label_text);
        }
    }

    /* Process pending events to update UI */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return TRUE; /* Continue */
}

/**
 * Execute a selection radius operation (grow, shrink, border, feather, sharpen)
 */
static void execute_select_radius_operation(AppContext* ctx,
                                            const gchar* operation_name,
                                            gboolean (*operation_func)(SelectionMask*, gint,
                                                                       SelectionOperationProgressCallback,
                                                                       gpointer)) {
    ImageDocument* doc = ui_get_active_document(ctx);
    SelectionRadiusDialog* dialog;
    gint radius;
    gint response;

    if (!doc || !doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        g_warning("No selection to modify");
        return;
    }

    /* Create dialog */
    dialog = selection_radius_dialog_new(operation_name);
    if (!dialog) {
        g_warning("Failed to create %s dialog", operation_name);
        return;
    }

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(selection_radius_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = selection_radius_dialog_run(dialog, GTK_WINDOW(ctx->window), &radius);

    if (response == GTK_RESPONSE_OK) {
        /* Begin transaction for undo/redo */
        SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
            doc->selection_mask, doc, operation_name);
        if (transaction) {
            /* Register entire mask as modified */
            selection_undo_transaction_register_region(transaction, 0, 0,
                                                       doc->selection_mask->width,
                                                       doc->selection_mask->height);

            /* Show progress dialog */
            SelectionProgressDialog* progress = NULL;
            if (ctx->window) {
                progress = show_selection_progress_dialog(GTK_WINDOW(ctx->window), operation_name);
            }

            /* Apply operation with progress callback */
            gboolean success = operation_func(doc->selection_mask, radius,
                                              selection_operation_progress_callback,
                                              progress);

            /* Hide progress dialog */
            if (progress) {
                hide_selection_progress_dialog(progress);
            }

            if (success) {
                /* Commit transaction and get command */
                Command* cmd = selection_undo_transaction_commit(transaction);
                if (cmd && doc->undo_stack) {
                    command_stack_push(doc->undo_stack, cmd);
                    if (doc->redo_stack) {
                        command_stack_clear(doc->redo_stack);
                    }
                } else if (cmd) {
                    command_free(cmd);
                }

                /* Mark document as modified */
                doc->modified = TRUE;

                /* Invalidate document for redraw */
                document_invalidate_composite(doc);

                /* Update window title and menu states */
                ui_update_window_title(ctx);
                ui_update_menu_and_button_states(ctx);
            } else {
                /* Operation failed, cancel transaction */
                selection_undo_transaction_cancel(transaction);
            }
        }
    }

    /* Clean up */
    selection_radius_dialog_free(dialog);
}

/**
 * Callback for Grow Selection menu item
 */
static void on_select_grow(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, "Grow Selection", selection_mask_grow);
}

/**
 * Callback for Shrink Selection menu item
 */
static void on_select_shrink(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, "Shrink Selection", selection_mask_shrink);
}

/**
 * Callback for Border Selection menu item
 */
static void on_select_border(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, "Border Selection", selection_mask_border);
}

/**
 * Callback for Feather Selection menu item
 */
static void on_select_feather(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, "Feather Selection", selection_mask_feather);
}

/**
 * Callback for Sharpen Selection menu item
 */
static void on_select_sharpen(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, "Sharpen Selection", selection_mask_sharpen);
}

/**
 * Setup Select menu from Glade builder
 */
static void setup_select_menu(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* select_menu = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu"));
    GtkWidget* select_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_item"));

    if (select_menu && select_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(select_menu_item), select_menu);
    }

    /* Get menu items */
    GtkWidget* select_menu_all = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_all"));
    GtkWidget* select_menu_none = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_none"));
    GtkWidget* select_menu_invert = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_invert"));
    GtkWidget* select_menu_grow = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_grow"));
    GtkWidget* select_menu_shrink = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_shrink"));
    GtkWidget* select_menu_border = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_border"));
    GtkWidget* select_menu_feather = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_feather"));
    GtkWidget* select_menu_sharpen = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_sharpen"));

    /* Connect Select menu signals */
    if (select_menu_all) {
        g_signal_connect(select_menu_all, "activate", G_CALLBACK(on_select_all), ctx);
        /* Add accelerator (Ctrl+A) */
        gtk_widget_add_accelerator(select_menu_all, "activate", accel_group,
                                   GDK_KEY_a, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    if (select_menu_none) {
        g_signal_connect(select_menu_none, "activate", G_CALLBACK(on_select_none), ctx);
        /* Add accelerator (Ctrl+D) */
        gtk_widget_add_accelerator(select_menu_none, "activate", accel_group,
                                   GDK_KEY_d, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    if (select_menu_invert) {
        g_signal_connect(select_menu_invert, "activate", G_CALLBACK(on_select_invert), ctx);
        /* Add accelerator (Ctrl+I) if not already set in Glade */
        gtk_widget_add_accelerator(select_menu_invert, "activate", accel_group,
                                   GDK_KEY_i, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    }
    if (select_menu_grow) {
        g_signal_connect(select_menu_grow, "activate", G_CALLBACK(on_select_grow), ctx);
    }
    if (select_menu_shrink) {
        g_signal_connect(select_menu_shrink, "activate", G_CALLBACK(on_select_shrink), ctx);
    }
    if (select_menu_border) {
        g_signal_connect(select_menu_border, "activate", G_CALLBACK(on_select_border), ctx);
    }
    if (select_menu_feather) {
        g_signal_connect(select_menu_feather, "activate", G_CALLBACK(on_select_feather), ctx);
    }
    if (select_menu_sharpen) {
        g_signal_connect(select_menu_sharpen, "activate", G_CALLBACK(on_select_sharpen), ctx);
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
                    recent_files_save(); /* This syncs to settings if connected */

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
                    /* Add to recent files */
                    recent_files_add(file_path);
                    recent_files_save(); /* This syncs to settings if connected */

                    /* Update window title to reflect new filename */
                    ui_update_window_title(ctx);
                    ui_update_status_bar(ctx, NULL);
                    ui_update_recent_files_menu(ctx);
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

    /* SAVE SETTINGS FIRST - before any cleanup */
    if (ctx && ctx->settings && ctx->app_dir) {
        /* Sync recent files to settings before saving */
        recent_files_save(); /* This will sync to settings if connected */

        /* Save all current tool options to settings before final save */
        if (ctx->tool_registry) {
            ui_save_all_tool_options_to_settings(ctx);
        }

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
    gtk_main_quit();

    return FALSE; /* Allow window to close */
}

/**
 * File > Exit callback
 */
static void on_file_exit(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;

    /* SAVE SETTINGS FIRST - before any cleanup */
    if (ctx && ctx->settings && ctx->app_dir) {
        /* Sync recent files to settings before saving */
        recent_files_save(); /* This will sync to settings if connected */

        /* Save all current tool options to settings before final save */
        if (ctx->tool_registry) {
            ui_save_all_tool_options_to_settings(ctx);
        }

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
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
 * Edit > Copy callback
 */
static void on_edit_copy(GtkWidget* widget, gpointer data) {
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
static void on_edit_cut(GtkWidget* widget, gpointer data) {
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
    ui_update_window_title(ctx);

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Edit > Paste callback
 */
static void on_edit_paste(GtkWidget* widget, gpointer data) {
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
    ui_update_window_title(ctx);

    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
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

    /* Set the current document in the tool registry so tools can access it */
    if (ctx->tool_registry) {
        ctx->tool_registry->current_doc = doc;
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
    NewLayerDialog* dialog;
    NewLayerDialogResult* result;
    gint response;
    ImageLayer* new_layer;
    const gdouble* custom_color = NULL;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Create and show new layer dialog */
    dialog = new_layer_dialog_new();
    if (!dialog) {
        g_warning("Failed to create new layer dialog");
        return;
    }

    response = new_layer_dialog_run(dialog, GTK_WINDOW(ctx->window), &result);

    if (response == GTK_RESPONSE_OK && result) {
        /* Get custom color if needed */
        if (result->background == LAYER_BACKGROUND_CUSTOM) {
            custom_color = result->custom_color;
        }

        /* Create new layer with dialog parameters */
        new_layer = document_add_layer(doc, result->name,
                                       result->background,
                                       result->position,
                                       custom_color);

        if (new_layer) {
            /* Set as active layer if requested */
            if (result->set_active) {
                document_set_selected_layer(doc, new_layer);
            }

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

                /* Select the new layer if requested */
                if (result->set_active) {
                    layers_panel_select_layer(layers_panel, doc, new_layer);
                }
            }

            /* Update UI state */
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx);
            doc->modified = TRUE;
        }

        /* Free dialog result */
        new_layer_dialog_result_free(result);
    }

    /* Free dialog */
    new_layer_dialog_free(dialog);
}

/**
 * Image > Canvas Size callback
 */
static void on_image_canvas_size(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
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

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Create and show dialog */
    dialog = canvas_size_dialog_new(doc);
    if (!dialog) {
        g_warning("Failed to create canvas size dialog");
        return;
    }

    response = canvas_size_dialog_run(dialog, GTK_WINDOW(ctx->window), &result);

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
            ui_update_window_title(ctx);
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
static void on_image_duplicate(GtkWidget* widget, gpointer data) {
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
    ui_update_window_title(ctx);
    ui_update_status_bar(ctx, NULL);

    /* Mark new document as modified */
    new_doc->modified = TRUE;
}

/**
 * Image > Fit canvas to active layer callback
 */
static void on_image_fit_active_layer(GtkWidget* widget, gpointer data) {
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
        ui_update_window_title(ctx);
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
static void on_image_fit_all_layers(GtkWidget* widget, gpointer data) {
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
        ui_update_window_title(ctx);
        ui_update_status_bar(ctx, NULL);
        doc->modified = TRUE;
    } else {
        g_warning("Failed to resize canvas to fit all layers");
        command_free(cmd);
    }
}

/**
 * Helper function to flip a layer using Ocular library
 */
static gboolean flip_layer(ImageLayer* layer, OcDirection direction) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    if (width == 0 || height == 0) {
        return FALSE;
    }

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Flip layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        g_warning("Flip layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply flip using Ocular library (4 channels for RGBA) */
    status = ocularFlipImage(rgba_input, rgba_output, width, height, 4, direction);

    if (status != OC_STATUS_OK) {
        g_warning("Flip layer: Ocular flip returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        g_warning("Flip layer: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper function to transpose a layer using Ocular library
 */
static gboolean transpose_layer(ImageLayer* layer) {
    cairo_surface_t* old_surface;
    cairo_surface_t* new_surface;
    gint old_width, old_height;
    gint new_width, new_height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    old_surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(old_surface, &old_width, &old_height)) {
        return FALSE;
    }

    if (old_width == 0 || old_height == 0) {
        return FALSE;
    }

    /* Transpose swaps width and height */
    new_width = old_height;
    new_height = old_width;

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(old_width * old_height * 4);
    rgba_output = (guchar*)g_malloc(new_width * new_height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Transpose layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(old_surface, rgba_input)) {
        g_warning("Transpose layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply transpose using Ocular library
       Stride is width * 4 for RGBA format */
    status = ocularTransposeImage(rgba_input, rgba_output, old_width, old_height, old_width * 4);

    if (status != OC_STATUS_OK) {
        g_warning("Transpose layer: Ocular transpose returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Create new surface with swapped dimensions */
    new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!new_surface) {
        g_warning("Transpose layer: Failed to create new surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert RGBA output to new Cairo surface */
    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        g_warning("Transpose layer: Failed to convert RGBA to new surface");
        cairo_surface_destroy(new_surface);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Replace old surface with new one */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;

    /* Update layer dimensions */
    layer->width = new_width;
    layer->height = new_height;

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Image > Flip horizontal callback
 */
static void on_image_flip_horizontal(GtkWidget* widget, gpointer data) {
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
static void on_image_flip_vertical(GtkWidget* widget, gpointer data) {
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
static void on_image_transpose(GtkWidget* widget, gpointer data) {
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
    ui_update_window_title(ctx);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Image > Merge visible layers callback
 */
static void on_image_merge_visible(GtkWidget* widget, gpointer data) {
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
static void on_image_flatten(GtkWidget* widget, gpointer data) {
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

    /* Get from settings if available, otherwise use defaults */
    if (ctx->settings) {
        settings_get_canvas_background(ctx->settings, r, g, b);
    } else {
        /* Return defaults if settings not loaded yet */
        if (r)
            *r = 160.0 / 255.0;
        if (g)
            *g = 160.0 / 255.0;
        if (b)
            *b = 160.0 / 255.0;
    }
}

/**
 * Set canvas background color
 */
void ui_set_canvas_background_color(AppContext* ctx, gdouble r, gdouble g, gdouble b) {
    if (!ctx) {
        return;
    }

    /* Clamp values to 0.0-1.0 */
    gdouble r_clamped = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    gdouble g_clamped = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    gdouble b_clamped = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);

    /* Update settings if available */
    if (ctx->settings) {
        settings_set_canvas_background(ctx->settings, r_clamped, g_clamped, b_clamped);
        /* Save settings immediately */
        if (ctx->app_dir) {
            settings_save(ctx->settings, ctx->app_dir);
        }
    }

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

    gdouble r_val, g_val, b_val;
    ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
    guint r = (guint)(r_val * 255.0);
    guint g = (guint)(g_val * 255.0);
    guint b = (guint)(b_val * 255.0);

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

/* Forward declaration */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately);

/**
 * Get tool name from tool type (for settings storage)
 */
static const char* tool_type_to_name(ToolType tool_type) {
    switch (tool_type) {
        case TOOL_BRUSH:
            return "brush";
        case TOOL_ERASER:
            return "eraser";
        case TOOL_PAINT_BUCKET:
            return "paintbucket";
        case TOOL_HAND:
            return "hand";
        case TOOL_ZOOM:
            return "zoom";
        case TOOL_MOVE:
            return "move";
        case TOOL_RECT_SELECT:
            return "rect_select";
        default:
            return NULL;
    }
}

/**
 * Load tool options from settings
 */
void ui_load_tool_options_from_settings(AppContext* ctx) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    /* Load options for each tool type */
    for (int i = 0; i < TOOL_COUNT; i++) {
        ToolType tool_type = (ToolType)i;
        const char* tool_name = tool_type_to_name(tool_type);
        if (!tool_name) {
            continue;
        }

        ToolOptions* opts = tool_options_get_for_tool(tool_type);
        if (!opts) {
            continue;
        }

        /* Load size (use default if not found) */
        const char* size_str = settings_get_tool_option(ctx->settings, tool_name, "size");
        if (size_str) {
            gfloat size = (gfloat)g_ascii_strtod(size_str, NULL);
            if (size > 0.0f) {
                tool_options_set_size(opts, size);
            } else {
                tool_options_set_size(opts, settings_get_default_tool_size());
            }
        } else {
            tool_options_set_size(opts, settings_get_default_tool_size());
        }

        /* Load opacity (use default if not found) */
        const char* opacity_str = settings_get_tool_option(ctx->settings, tool_name, "opacity");
        if (opacity_str) {
            gfloat opacity = (gfloat)g_ascii_strtod(opacity_str, NULL);
            tool_options_set_opacity(opts, opacity);
        } else {
            tool_options_set_opacity(opts, settings_get_default_tool_opacity());
        }

        /* Load hardness (use default if not found) */
        const char* hardness_str = settings_get_tool_option(ctx->settings, tool_name, "hardness");
        if (hardness_str) {
            gfloat hardness = (gfloat)g_ascii_strtod(hardness_str, NULL);
            tool_options_set_hardness(opts, hardness);
        } else {
            tool_options_set_hardness(opts, settings_get_default_tool_hardness());
        }

        /* Load flow (use default if not found) */
        const char* flow_str = settings_get_tool_option(ctx->settings, tool_name, "flow");
        if (flow_str) {
            gfloat flow = (gfloat)g_ascii_strtod(flow_str, NULL);
            tool_options_set_flow(opts, flow);
        } else {
            tool_options_set_flow(opts, settings_get_default_tool_flow());
        }

        /* Load spacing (use default if not found) */
        const char* spacing_str = settings_get_tool_option(ctx->settings, tool_name, "spacing");
        if (spacing_str) {
            gfloat spacing = (gfloat)g_ascii_strtod(spacing_str, NULL);
            tool_options_set_spacing(opts, spacing);
        } else {
            tool_options_set_spacing(opts, settings_get_default_tool_spacing());
        }

        /* Load tolerance (use default if not found) */
        const char* tolerance_str = settings_get_tool_option(ctx->settings, tool_name, "tolerance");
        if (tolerance_str) {
            gfloat tolerance = (gfloat)g_ascii_strtod(tolerance_str, NULL);
            tool_options_set_tolerance(opts, tolerance);
        } else {
            tool_options_set_tolerance(opts, settings_get_default_tool_tolerance());
        }

        /* Load fill_contiguous (use default if not found) */
        const char* contiguous_str = settings_get_tool_option(ctx->settings, tool_name, "fill_contiguous");
        if (contiguous_str) {
            gboolean contiguous = (g_strcmp0(contiguous_str, "true") == 0 || g_strcmp0(contiguous_str, "1") == 0);
            tool_options_set_fill_contiguous(opts, contiguous);
        } else {
            tool_options_set_fill_contiguous(opts, settings_get_default_tool_fill_contiguous());
        }

        /* Load fill_antialiased (use default if not found) */
        const char* antialiased_str = settings_get_tool_option(ctx->settings, tool_name, "fill_antialiased");
        if (antialiased_str) {
            gboolean antialiased = (g_strcmp0(antialiased_str, "true") == 0 || g_strcmp0(antialiased_str, "1") == 0);
            tool_options_set_fill_antialiased(opts, antialiased);
        } else {
            tool_options_set_fill_antialiased(opts, settings_get_default_tool_fill_antialiased());
        }

        /* Rectangular select tool specific options */
        if (tool_type == TOOL_RECT_SELECT) {
            /* Load combine mode (0=NEW, 1=ADD, 2=SUBTRACT, 3=INTERSECT) */
            const char* combine_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_combine");
            if (combine_str) {
                gint combine_val = (gint)g_ascii_strtoll(combine_str, NULL, 10);
                if (combine_val >= 0 && combine_val < 4) {
                    tool_options_set_rect_select_combine(opts, (SelectionCombineMode)combine_val);
                } else {
                    tool_options_set_rect_select_combine(opts, SELECTION_COMBINE_NEW);
                }
            } else {
                tool_options_set_rect_select_combine(opts, SELECTION_COMBINE_NEW);
            }

            /* Load smoothing mode (0=NONE, 1=ANTIALIASED, 2=FEATHERED) */
            const char* smooth_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_smooth");
            if (smooth_str) {
                gint smooth_val = (gint)g_ascii_strtoll(smooth_str, NULL, 10);
                if (smooth_val >= 0 && smooth_val < 3) {
                    tool_options_set_rect_select_smooth(opts, (SelectionSmoothingMode)smooth_val);
                } else {
                    tool_options_set_rect_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
                }
            } else {
                tool_options_set_rect_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
            }

            /* Load feather radius */
            const char* feather_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_feather");
            if (feather_str) {
                gfloat feather_val = (gfloat)g_ascii_strtod(feather_str, NULL);
                if (feather_val >= 0.0f && feather_val <= 200.0f) {
                    tool_options_set_rect_select_feather(opts, feather_val);
                } else {
                    tool_options_set_rect_select_feather(opts, 0.0f);
                }
            } else {
                tool_options_set_rect_select_feather(opts, 0.0f);
            }

            /* Load animation toggle */
            const char* animate_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_animate");
            if (animate_str) {
                gboolean animate = (g_strcmp0(animate_str, "true") == 0 || g_strcmp0(animate_str, "1") == 0);
                tool_options_set_rect_select_animate(opts, animate);
            } else {
                tool_options_set_rect_select_animate(opts, TRUE);
            }
        }
    }
}

/* Forward declaration */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately);

/**
 * Save tool options to settings (in-memory only, not to file)
 * Tool options are saved to file only on app shutdown
 */
void ui_save_tool_options_to_settings(AppContext* ctx, ToolType tool_type) {
    ui_save_tool_options_to_settings_internal(ctx, tool_type, FALSE);
}

/**
 * Internal function to save tool options to settings
 * @param save_immediately If TRUE, saves settings to file immediately. If FALSE, only updates in-memory settings.
 */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    const char* tool_name = tool_type_to_name(tool_type);
    if (!tool_name) {
        return;
    }

    /* Get the tool to check which options it supports */
    Tool* tool = tool_manager_get(ctx->tool_registry, tool_type);
    if (!tool) {
        return;
    }

    /* Read tool options flags immediately to avoid accessing freed memory */
    ToolOptionFlags tool_options_flags = tool->options;

    ToolOptions* opts = tool_options_get_for_tool(tool_type);
    if (!opts) {
        return;
    }

    /* Read all values from opts structure immediately to avoid accessing freed memory */
    /* Store values in local variables before any operations that might free memory */
    gfloat size_val = opts->size;
    gfloat opacity_val = opts->opacity;
    gfloat hardness_val = opts->hardness;
    gfloat flow_val = opts->flow;
    gfloat spacing_val = opts->spacing;
    gfloat tolerance_val = opts->tolerance;
    gboolean fill_contiguous_val = opts->fill_contiguous;
    gboolean fill_antialiased_val = opts->fill_antialiased;
    SelectionCombineMode rect_select_combine_val = opts->rect_select_combine;
    SelectionSmoothingMode rect_select_smooth_val = opts->rect_select_smooth;
    gfloat rect_select_feather_val = opts->rect_select_feather;
    gboolean rect_select_animate_val = opts->rect_select_animate;

    /* Save only the options that this tool supports */
    /* Check tool_options_flags to determine which options to save */

    if (tool_options_flags & TOOL_OPT_SIZE) {
        gchar size_str[32];
        gfloat safe_size = (size_val >= 1.0f && size_val <= 10000.0f) ? size_val : 5.0f;
        g_snprintf(size_str, sizeof(size_str), "%.2f", safe_size);
        settings_set_tool_option(ctx->settings, tool_name, "size", size_str);
    }

    if (tool_options_flags & TOOL_OPT_OPACITY) {
        gchar opacity_str[32];
        gfloat safe_opacity = (opacity_val >= 0.0f && opacity_val <= 1.0f) ? opacity_val : 1.0f;
        g_snprintf(opacity_str, sizeof(opacity_str), "%.3f", safe_opacity);
        settings_set_tool_option(ctx->settings, tool_name, "opacity", opacity_str);
    }

    if (tool_options_flags & TOOL_OPT_HARDNESS) {
        gchar hardness_str[32];
        gfloat safe_hardness = (hardness_val >= 0.0f && hardness_val <= 1.0f) ? hardness_val : 1.0f;
        g_snprintf(hardness_str, sizeof(hardness_str), "%.3f", safe_hardness);
        settings_set_tool_option(ctx->settings, tool_name, "hardness", hardness_str);
    }

    if (tool_options_flags & TOOL_OPT_FLOW) {
        gchar flow_str[32];
        gfloat safe_flow = (flow_val >= 0.0f && flow_val <= 1.0f) ? flow_val : 1.0f;
        g_snprintf(flow_str, sizeof(flow_str), "%.3f", safe_flow);
        settings_set_tool_option(ctx->settings, tool_name, "flow", flow_str);
    }

    if (tool_options_flags & TOOL_OPT_SPACING) {
        gchar spacing_str[32];
        gfloat safe_spacing = (spacing_val >= 0.0f && spacing_val <= 1.0f) ? spacing_val : 0.25f;
        g_snprintf(spacing_str, sizeof(spacing_str), "%.3f", safe_spacing);
        settings_set_tool_option(ctx->settings, tool_name, "spacing", spacing_str);
    }

    /* Paint bucket tool uses tolerance, fill_contiguous, and fill_antialiased */
    /* These are not in ToolOptionFlags, so check tool type directly */
    if (tool_type == TOOL_PAINT_BUCKET) {
        gchar tolerance_str[32];
        gfloat safe_tolerance = (tolerance_val >= 0.0f && tolerance_val <= 100.0f) ? tolerance_val : 15.0f;
        g_snprintf(tolerance_str, sizeof(tolerance_str), "%.2f", safe_tolerance);
        settings_set_tool_option(ctx->settings, tool_name, "tolerance", tolerance_str);
        settings_set_tool_option(ctx->settings, tool_name, "fill_contiguous", fill_contiguous_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "fill_antialiased", fill_antialiased_val ? "true" : "false");
    }

    /* Rectangular select tool uses combine mode, smoothing, feather, and animate options */
    if (tool_type == TOOL_RECT_SELECT) {
        gchar combine_str[16];
        gint combine_val = (gint)rect_select_combine_val;
        g_snprintf(combine_str, sizeof(combine_str), "%d", combine_val);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_combine", combine_str);

        gchar smooth_str[16];
        gint smooth_val = (gint)rect_select_smooth_val;
        g_snprintf(smooth_str, sizeof(smooth_str), "%d", smooth_val);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_smooth", smooth_str);

        gchar feather_str[32];
        gfloat safe_feather = (rect_select_feather_val >= 0.0f && rect_select_feather_val <= 200.0f) ? rect_select_feather_val : 0.0f;
        g_snprintf(feather_str, sizeof(feather_str), "%.2f", safe_feather);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_feather", feather_str);

        settings_set_tool_option(ctx->settings, tool_name, "rect_select_animate", rect_select_animate_val ? "true" : "false");
    }

    /* Save settings immediately if requested */
    if (save_immediately && ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Save settings immediately if requested */
    if (save_immediately && ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }
}

/**
 * Save all tool options to settings (saves all tools at once)
 */
void ui_save_all_tool_options_to_settings(AppContext* ctx) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    /* Save options for each tool type (without saving to file after each one) */
    /* settings_set_tool_option will create the hash table if needed */
    /* Tool options should always be saved on app close, even if unchanged */
    for (int i = 0; i < TOOL_COUNT; i++) {
        ToolType tool_type = (ToolType)i;
        ui_save_tool_options_to_settings_internal(ctx, tool_type, FALSE);
    }
}
