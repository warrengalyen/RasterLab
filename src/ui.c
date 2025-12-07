#include "ui.h"
#include "document.h"
#include "panels.h"
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
static void on_file_open(GtkWidget *widget, gpointer data);
static void on_file_open_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_file_save_as(GtkWidget *widget, gpointer data);
static void on_file_save_as_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_file_close(GtkWidget *widget, gpointer data);
static void on_file_exit(GtkWidget *widget, gpointer data);
static void on_edit_undo(GtkWidget *widget, gpointer data);
static void on_edit_redo(GtkWidget *widget, gpointer data);
static void on_view_zoom_in(GtkWidget *widget, gpointer data);
static void on_view_zoom_out(GtkWidget *widget, gpointer data);
static void on_view_zoom_reset(GtkWidget *widget, gpointer data);
static void on_view_zoom_fit(GtkWidget *widget, gpointer data);
static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data);
static void on_layer_new(GtkWidget *widget, gpointer data);
static void on_layer_delete(GtkWidget *widget, gpointer data);
static void on_layer_duplicate(GtkWidget *widget, gpointer data);
static void on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page,
                                     guint page_num, gpointer user_data);
static void on_tab_close_button_clicked(GtkButton *button, gpointer user_data);
static void on_layer_selection_changed(GtkTreeSelection *selection, gpointer user_data);

/**
 * Layer selection changed callback - proper signal handler signature
 */
static void on_layer_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    (void)selection;  /* Unused - we get it from the tree view */

    AppContext *ctx = (AppContext *)user_data;

    if (!ctx) {
        printf("ERROR: on_layer_selection_changed called with NULL ctx\n");
        return;
    }

    printf("Layer selection changed in tree view\n");
    
    /* Update menu and button states */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Create the File menu
 */
static GtkWidget* create_file_menu(AppContext *ctx)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *menu_item;
    GtkAccelGroup *accel_group;

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
static GtkWidget* create_edit_menu(AppContext *ctx)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *menu_item;
    GtkAccelGroup *accel_group = gtk_accel_group_new();

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
static GtkWidget* create_view_menu(AppContext *ctx)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *menu_item;
    GtkAccelGroup *accel_group = gtk_accel_group_new();

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
static GtkWidget* create_layer_menu(AppContext *ctx)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *menu_item;

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
static GtkWidget* create_menu_bar(AppContext *ctx)
{
    GtkWidget *menu_bar = gtk_menu_bar_new();
    GtkWidget *file_menu_item;
    GtkWidget *file_menu;
    GtkWidget *edit_menu_item;
    GtkWidget *edit_menu;
    GtkWidget *view_menu_item;
    GtkWidget *view_menu;
    GtkWidget *layer_menu_item;
    GtkWidget *layer_menu;

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
AppContext* ui_create_main_window(void)
{
    AppContext *ctx = (AppContext *)g_malloc(sizeof(AppContext));
    GtkWidget *main_vbox;
    GtkWidget *tool_options_panel;
    GtkWidget *tools_panel;
    GtkWidget *layers_panel_widget;
    GtkWidget *main_hpaned;
    PanelHeader *tools_header;
    PanelHeader *layers_header;
    LayersPanel *layers_panel;

    ctx->documents = NULL;
    ctx->layer_menu_new = NULL;
    ctx->layer_menu_delete = NULL;
    ctx->layer_menu_duplicate = NULL;
    ctx->edit_menu_undo = NULL;
    ctx->edit_menu_redo = NULL;

    /* Create and initialize tool registry */
    ctx->tool_registry = tool_registry_new();
    if (!tool_registry_init_defaults(ctx->tool_registry)) {
        g_warning("Failed to initialize tool registry");
        g_free(ctx);
        return NULL;
    }

    /* Set AppContext reference on all tools */
    if (ctx->tool_registry) {
        for (int i = 0; i < TOOL_COUNT; i++) {
            Tool *tool = tool_registry_get(ctx->tool_registry, i);
            if (tool) {
                tool->app_context = (gpointer)ctx;
            }
        }
    }

    /* Create main window */
    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "Image Editor");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 1600, 1000);
    gtk_window_set_position(GTK_WINDOW(ctx->window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(ctx->window), "image-editor");

    /* Main vertical box (menu + toolbar + content) */
    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ctx->window), main_vbox);

    /* Menu bar */
    ctx->menu_bar = create_menu_bar(ctx);
    gtk_box_pack_start(GTK_BOX(main_vbox), ctx->menu_bar, FALSE, FALSE, 0);

    /* ==== TOP PANEL: Tool Options ==== */
    tool_options_panel = create_tool_options_panel();
    PanelHeader *tool_options_header = panel_header_new("Tool Options", tool_options_panel);
    gtk_box_pack_start(GTK_BOX(main_vbox), tool_options_header->container, FALSE, FALSE, 0);

    /* ==== MAIN HORIZONTAL LAYOUT: Left | Center+Right ==== */
    main_hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(main_hpaned), 150);
    gtk_box_pack_start(GTK_BOX(main_vbox), main_hpaned, TRUE, TRUE, 0);

    /* ==== LEFT PANEL: Tools ==== */
    tools_panel = create_tools_panel(ctx->tool_registry);
    tools_header = panel_header_new("Tools", tools_panel);
    gtk_paned_pack1(GTK_PANED(main_hpaned), tools_header->container, FALSE, TRUE);

    /* ==== CENTER-RIGHT HORIZONTAL PANED: Center (notebook) | Right (layers) ==== */
    GtkWidget *center_right_hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(center_right_hpaned), 800);
    gtk_paned_pack2(GTK_PANED(main_hpaned), center_right_hpaned, TRUE, TRUE);

    /* CENTER: Notebook for document tabs */
    ctx->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(ctx->notebook), TRUE);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(ctx->notebook), GTK_POS_TOP);
    g_signal_connect(ctx->notebook, "switch-page", 
                     G_CALLBACK(on_notebook_switch_page), ctx);
    gtk_paned_pack1(GTK_PANED(center_right_hpaned), ctx->notebook, TRUE, TRUE);

    /* ==== RIGHT PANEL: Layers ==== */
    layers_panel = create_layers_panel();
    layers_panel_widget = layers_panel->panel;
    layers_header = panel_header_new("Layers", layers_panel_widget);
    gtk_paned_pack2(GTK_PANED(center_right_hpaned), layers_header->container, FALSE, TRUE);

    /* Store layers panel reference for later updates */
    g_object_set_data(G_OBJECT(ctx->window), "layers_panel", layers_panel);

    /* Connect layers panel buttons to callbacks */
    layers_panel_connect_buttons(layers_panel,
                                G_CALLBACK(on_layer_new),
                                G_CALLBACK(on_layer_delete),
                                G_CALLBACK(on_layer_duplicate),
                                ctx);

    /* Connect layer tree view selection changes to update UI state */
    GtkTreeSelection *layer_selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(layers_panel->tree_view));
    g_signal_connect(layer_selection, "changed",
                     G_CALLBACK(on_layer_selection_changed), ctx);

    /* ==== STATUS BAR ==== */
    ctx->status_bar = gtk_statusbar_new();
    gtk_box_pack_end(GTK_BOX(main_vbox), ctx->status_bar, FALSE, FALSE, 0);

    /* Connect window signals */
    g_signal_connect(ctx->window, "delete-event", 
                     G_CALLBACK(on_window_delete), ctx);

    gtk_widget_show_all(ctx->window);

    /* Update status bar with initial information */
    ui_update_status_bar(ctx);

    /* Initialize menu and button states */
    ui_update_menu_and_button_states(ctx);

    printf("Main window created with dockable panels and status bar\n");

    return ctx;
}

/**
 * Create and attach a new document tab
 */
ImageDocument* ui_create_document_tab(AppContext *ctx, const gchar *filename)
{
    ImageDocument *doc;
    GtkWidget *page_content;
    GtkWidget *tab_hbox;
    GtkWidget *tab_label;
    GtkWidget *close_button;
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
    GtkWidget *close_image = gtk_image_new_from_icon_name("window-close-symbolic",
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
    }

    /* Add document to list */
    ctx->documents = g_list_append(ctx->documents, doc);

    /* Update window title */
    ui_update_window_title(ctx);

    printf("Opened document: %s\n", filename);

    return doc;
}

/**
 * Close a document tab
 */
/**
 * Internal: Actually close the document without prompting
 */
static void ui_close_document_tab_internal(AppContext *ctx, ImageDocument *doc)
{
    gint page_num;
    gint n_pages;

    if (!doc || !ctx) {
        return;
    }

    /* Get number of pages before removal */
    n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(ctx->notebook));

    /* Find the page containing this document */
    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), 
                                     doc->scrolled_window);

    if (page_num >= 0) {
        printf("Closing document: %s (page %d of %d)\n", 
               document_get_filename(doc), page_num + 1, n_pages);

        /* Remove from document list first */
        ctx->documents = g_list_remove(ctx->documents, doc);

        /* Remove the notebook page */
        gtk_notebook_remove_page(GTK_NOTEBOOK(ctx->notebook), page_num);

        /* Free the document */
        document_free(doc);

        /* Check if any pages left */
        gint remaining = gtk_notebook_get_n_pages(GTK_NOTEBOOK(ctx->notebook));
        printf("Remaining pages after close: %d\n", remaining);

         /* Update window title (handles empty notebook) */
         ui_update_window_title(ctx);

         /* Update status bar and menu/button states */
         ui_update_status_bar(ctx);
         ui_update_menu_and_button_states(ctx);

         printf("Document closed successfully\n");
     }
}

/**
 * Public: Close document with unsaved changes prompt
 */
void ui_close_document_tab(AppContext *ctx, ImageDocument *doc)
{
    if (!doc || !ctx) {
        return;
    }

    /* Check if document has unsaved changes */
    if (document_is_dirty(doc)) {
        GtkWidget *dialog;
        gint response;
        const gchar *filename = document_get_filename(doc);

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
                printf("User wants to save before closing\n");
                ui_save_document_as(ctx);
                /* Note: We don't actually close here - let user complete save */
                /* In a full implementation, we'd detect when save completes */
                break;

            case GTK_RESPONSE_REJECT:
                /* User clicked Discard */
                printf("User discarding changes\n");
                ui_close_document_tab_internal(ctx, doc);
                break;

            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
                /* User clicked Cancel or closed dialog */
                printf("User cancelled close operation\n");
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
ImageDocument* ui_get_active_document(AppContext *ctx)
{
    gint page_num;
    GtkWidget *page;

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
    for (GList *iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument *doc = (ImageDocument *)iter->data;
        if (doc->scrolled_window == page) {
            return doc;
        }
    }

    return NULL;
}

/**
 * Update the window title based on active document
 */
void ui_update_window_title(AppContext *ctx)
{
    ImageDocument *active_doc;
    gchar *title;

    if (!ctx) {
        return;
    }

    active_doc = ui_get_active_document(ctx);

    if (active_doc) {
        const gchar *filename = document_get_filename(active_doc);
        const gchar *modified = active_doc->modified ? "*" : "";
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
void ui_context_free(AppContext *ctx)
{
    if (!ctx) {
        return;
    }

    /* Free all documents */
    for (GList *iter = ctx->documents; iter; iter = iter->next) {
        document_free((ImageDocument *)iter->data);
    }

    g_list_free(ctx->documents);

    /* Free tool registry */
    if (ctx->tool_registry) {
        tool_registry_free(ctx->tool_registry);
    }

    g_free(ctx);
}

/**
 * File Open dialog response callback
 */
static void on_file_open_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    AppContext *ctx = (AppContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar *file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (file_path) {
            /* Create new document with filename */
            gchar *basename = g_path_get_basename(file_path);
            ImageDocument *doc = ui_create_document_tab(ctx, basename);

            if (doc) {
                /* Load the image into the document */
                if (!document_load_image_from_file(doc, file_path)) {
                    g_warning("Failed to load image: %s", file_path);
                } else {
                    /* Update status bar after successful load */
                    ui_update_status_bar(ctx);

                    /* Update layers panel with loaded document */
                    LayersPanel *layers_panel = (LayersPanel *)g_object_get_data(
                        G_OBJECT(ctx->window), "layers_panel");
                    if (layers_panel) {
                        layers_panel_update(layers_panel, doc);
                    }

                    /* Update menu and button states */
                    ui_update_menu_and_button_states(ctx);
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
static void on_file_open(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    GtkWidget *dialog;
    GtkFileFilter *filter;

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
static void on_file_save_as_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    AppContext *ctx = (AppContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar *file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (file_path) {
            ImageDocument *doc = ui_get_active_document(ctx);

            if (doc) {
                /* Save the document */
                if (document_save_as(doc, file_path)) {
                    /* Update window title to reflect new filename */
                    ui_update_window_title(ctx);
                    ui_update_status_bar(ctx);
                    printf("Document saved: %s\n", file_path);
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
static void on_file_save_as(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);
    GtkWidget *dialog;
    GtkFileFilter *filter;

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
        gchar *suggested = g_strdup_printf("%s.png", doc->filename);
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
static void on_file_close(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *active_doc = ui_get_active_document(ctx);

    if (active_doc) {
        ui_close_document_tab(ctx, active_doc);
    }
}

/**
 * Window delete event callback
 */
static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget;  /* Unused */
    (void)event;   /* Unused */

    AppContext *ctx = (AppContext *)data;

    printf("Window delete event triggered - shutting down\n");

    /* Free the context (which frees all documents) */
    ui_context_free(ctx);

    /* Exit GTK main loop */
    gtk_main_quit();

    return FALSE;  /* Allow window to close */
}

/**
 * File > Exit callback
 */
static void on_file_exit(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;

    /* Destroy the main window, which triggers delete-event */
    gtk_widget_destroy(ctx->window);
}

/**
 * Edit > Undo callback
 */
static void on_edit_undo(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc;
    gint page_num;

    if (!ctx || !ctx->notebook) {
        return;
    }

    /* Get current document */
    page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(ctx->notebook));
    if (page_num < 0) {
        return;
    }

    doc = (ImageDocument *)g_list_nth_data(ctx->documents, page_num);
    if (!doc) {
        return;
    }

    /* Perform undo */
    document_undo(doc);

    /* Update menu state and window title */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx);
}

/**
 * Edit > Redo callback
 */
static void on_edit_redo(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc;
    gint page_num;

    if (!ctx || !ctx->notebook) {
        return;
    }

    /* Get current document */
    page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(ctx->notebook));
    if (page_num < 0) {
        return;
    }

    doc = (ImageDocument *)g_list_nth_data(ctx->documents, page_num);
    if (!doc) {
        return;
    }

    /* Perform redo */
    document_redo(doc);

    /* Update menu state and window title */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx);
}

/**
 * View > Zoom In callback
 */
static void on_view_zoom_in(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_in(doc);
        ui_update_status_bar(ctx);
    }
}

/**
 * View > Zoom Out callback
 */
static void on_view_zoom_out(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_out(doc);
        ui_update_status_bar(ctx);
    }
}

/**
 * View > Reset Zoom callback
 */
static void on_view_zoom_reset(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_reset(doc);
        ui_update_status_bar(ctx);
    }
}

/**
 * View > Zoom Fit callback
 */
static void on_view_zoom_fit(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_fit(doc);
        ui_update_status_bar(ctx);
    }
}

/**
 * Notebook page switch callback
 */
static void on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page,
                                     guint page_num, gpointer user_data)
{
    (void)notebook;   /* Unused */
    (void)page;       /* Unused */
    (void)page_num;   /* Unused */

    AppContext *ctx = (AppContext *)user_data;
    ImageDocument *doc = ui_get_active_document(ctx);
    LayersPanel *layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(ctx->window), 
                                                                  "layers_panel");

    ui_update_window_title(ctx);
    ui_update_status_bar(ctx);

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
static void on_tab_close_button_clicked(GtkButton *button, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    AppContext *ctx = (AppContext *)g_object_get_data(G_OBJECT(button), 
                                                       "app_context");

    printf("Tab close button clicked for document: %s\n", 
           doc ? document_get_filename(doc) : "unknown");
    printf("  button=%p, ctx=%p, doc=%p\n", button, ctx, doc);

    if (ctx && doc) {
        printf("  Calling ui_close_document_tab...\n");
        ui_close_document_tab(ctx, doc);
    } else {
        printf("  ERROR: ctx=%p or doc=%p is NULL\n", ctx, doc);
    }
}

/**
 * Layer > New Layer callback
 */
static void on_layer_new(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);
    LayersPanel *layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(ctx->window), 
                                                                  "layers_panel");
    
    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Create new layer with auto-generated name */
    static int layer_count = 1;
    gchar *layer_name = g_strdup_printf("Layer %d", layer_count++);
    
    ImageLayer *new_layer = document_add_layer(doc, layer_name);
    g_free(layer_name);
    
    if (new_layer) {
        printf("New layer created\n");

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }
    }
}

/**
 * Layer > Delete Layer callback
 */
static void on_layer_delete(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);
    LayersPanel *layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(ctx->window), 
                                                                  "layers_panel");
    
    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Delete the currently selected layer */
    ImageLayer *selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }
    
    if (document_delete_layer(doc, selected_layer)) {
        printf("Layer deleted\n");

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }
    }
}

/**
 * Layer > Duplicate Layer callback
 */
static void on_layer_duplicate(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc = ui_get_active_document(ctx);
    LayersPanel *layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(ctx->window), 
                                                                  "layers_panel");
    
    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Duplicate the currently selected layer */
    ImageLayer *selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }
    
    static int dup_count = 1;
    gchar *layer_name = g_strdup_printf("%s copy %d", selected_layer->name, dup_count++);
    
    ImageLayer *dup_layer = document_duplicate_layer(doc, selected_layer, layer_name);
    g_free(layer_name);
    
    if (dup_layer) {
        printf("Layer duplicated\n");

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }
    }
}

/**
 * Update the status bar with document information
 */
void ui_update_status_bar(AppContext *ctx)
{
    ImageDocument *doc;
    gchar *status_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get active document */
    doc = ui_get_active_document(ctx);

    /* Clear previous context */
    gtk_statusbar_pop(GTK_STATUSBAR(ctx->status_bar), 0);

    if (!doc || doc->width == 0) {
        /* No document or empty document */
        status_text = g_strdup("Ready");
    } else {
        /* Format: WIDTHxHEIGHT | BitDepth-bit CHANNELS | ZOOMx% */
        gchar channels_str[32];
        
        if (doc->channels == 3) {
            snprintf(channels_str, sizeof(channels_str), "RGB");
        } else if (doc->channels == 4) {
            snprintf(channels_str, sizeof(channels_str), "RGBA");
        } else {
            snprintf(channels_str, sizeof(channels_str), "%u-channel", doc->channels);
        }

        status_text = g_strdup_printf("%ux%u | %u-bit %s | %.0f%%",
                                     doc->width, doc->height,
                                     doc->bit_depth,
                                     channels_str,
                                     doc->zoom_factor * 100.0);
    }

    gtk_statusbar_push(GTK_STATUSBAR(ctx->status_bar), 0, status_text);
    g_free(status_text);
}

/**
 * Update menu and button sensitivity based on document and layer state
 */
void ui_update_menu_and_button_states(AppContext *ctx)
{
    ImageDocument *doc;
    LayersPanel *layers_panel;
    gboolean has_document;
    gboolean has_selection;

    if (!ctx || !ctx->window) {
        printf("DEBUG: ui_update_menu_and_button_states called with NULL ctx or ctx->window\n");
        return;
    }

    /* Get current document */
    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);

    /* Get layers panel and check for selection */
    layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(ctx->window), 
                                                    "layers_panel");
    
    has_selection = FALSE;
    if (layers_panel && doc && layers_panel->tree_view) {
        has_selection = (layers_panel_get_selected_layer(layers_panel) != NULL);
    }

    /* Update layers panel button states */
    if (layers_panel) {
        layers_panel_update_button_sensitivity(layers_panel, has_document, has_selection);
    }

    /* Update Edit menu item states (undo/redo) */
    if (ctx->edit_menu_undo) {
        gtk_widget_set_sensitive(ctx->edit_menu_undo, 
                                has_document && document_can_undo(doc));
    }
    if (ctx->edit_menu_redo) {
        gtk_widget_set_sensitive(ctx->edit_menu_redo, 
                                has_document && document_can_redo(doc));
    }

    /* Update Layer menu item states */
    if (ctx->layer_menu_new) {
        gtk_widget_set_sensitive(ctx->layer_menu_new, has_document);
    }
    if (ctx->layer_menu_delete) {
        gtk_widget_set_sensitive(ctx->layer_menu_delete, has_document && has_selection);
    }
    if (ctx->layer_menu_duplicate) {
        gtk_widget_set_sensitive(ctx->layer_menu_duplicate, has_document && has_selection);
    }

    printf("UI State: document=%s, selection=%s\n",
           has_document ? "yes" : "no",
           has_selection ? "yes" : "no");
}

/**
 * Save active document with file dialog
 */
void ui_save_document_as(AppContext *ctx)
{
    if (!ctx) {
        return;
    }

    on_file_save_as(NULL, ctx);
}

