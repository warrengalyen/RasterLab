#include "ui.h"
#include "document.h"
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
static void on_file_open(GtkWidget *widget, gpointer data);
static void on_file_close(GtkWidget *widget, gpointer data);
static void on_file_exit(GtkWidget *widget, gpointer data);
static void on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page,
                                     guint page_num, gpointer user_data);
static gboolean on_tab_close_button_clicked(GtkWidget *button, GdkEvent *event,
                                            gpointer user_data);

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
 * Create the menu bar
 */
static GtkWidget* create_menu_bar(AppContext *ctx)
{
    GtkWidget *menu_bar = gtk_menu_bar_new();
    GtkWidget *file_menu_item;
    GtkWidget *file_menu;

    /* File menu */
    file_menu_item = gtk_menu_item_new_with_mnemonic("_File");
    file_menu = create_file_menu(ctx);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_menu_item);

    gtk_widget_show_all(menu_bar);

    return menu_bar;
}

/**
 * Create the main application UI
 */
AppContext* ui_create_main_window(void)
{
    AppContext *ctx = (AppContext *)g_malloc(sizeof(AppContext));

    ctx->documents = NULL;

    /* Create main window */
    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "Image Editor");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 1024, 768);
    gtk_window_set_position(GTK_WINDOW(ctx->window), GTK_WIN_POS_CENTER);

    /* Create main container */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ctx->window), vbox);

    /* Create menu bar */
    ctx->menu_bar = create_menu_bar(ctx);
    gtk_box_pack_start(GTK_BOX(vbox), ctx->menu_bar, FALSE, FALSE, 0);

    /* Create notebook for document tabs */
    ctx->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(ctx->notebook), TRUE);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(ctx->notebook), GTK_POS_TOP);
    g_signal_connect(ctx->notebook, "switch-page", 
                     G_CALLBACK(on_notebook_switch_page), ctx);
    gtk_box_pack_start(GTK_BOX(vbox), ctx->notebook, TRUE, TRUE, 0);

    /* Connect window signals */
    g_signal_connect(ctx->window, "delete-event", 
                     G_CALLBACK(on_file_exit), ctx);

    gtk_widget_show_all(ctx->window);

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
    gtk_button_set_focus_on_click(GTK_BUTTON(close_button), FALSE);
    gtk_widget_set_size_request(close_button, 20, 20);
    GtkWidget *close_image = gtk_image_new_from_icon_name("window-close-symbolic",
                                                          GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(close_button), close_image);
    g_signal_connect(close_button, "clicked", 
                     G_CALLBACK(on_tab_close_button_clicked), doc);
    gtk_box_pack_start(GTK_BOX(tab_hbox), close_button, FALSE, FALSE, 0);

    gtk_widget_show_all(tab_hbox);

    /* Add page to notebook */
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), 
                                        page_content, tab_hbox);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    /* Store label in close button's data for later reference */
    g_object_set_data(G_OBJECT(close_button), "tab_label", tab_label);
    g_object_set_data(G_OBJECT(close_button), "app_context", ctx);

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
void ui_close_document_tab(AppContext *ctx, ImageDocument *doc)
{
    gint page_num;
    GtkWidget *page;

    if (!doc || !ctx) {
        return;
    }

    /* Find the page containing this document */
    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), 
                                     doc->scrolled_window);

    if (page_num >= 0) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(ctx->notebook), page_num);
    }

    /* Remove from document list */
    ctx->documents = g_list_remove(ctx->documents, doc);

    /* Free the document */
    document_free(doc);

    /* Update window title */
    ui_update_window_title(ctx);

    printf("Closed document\n");
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
    g_free(ctx);
}

/**
 * File > Open callback
 */
static void on_file_open(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;

    /* Generate a default filename */
    static int document_count = 1;
    gchar *filename = g_strdup_printf("Untitled-%d", document_count++);

    /* Create a new document tab */
    ui_create_document_tab(ctx, filename);

    g_free(filename);
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
 * File > Exit callback
 */
static void on_file_exit(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;

    /* Free the context (which frees all documents) */
    ui_context_free(ctx);

    /* Exit GTK main loop */
    gtk_main_quit();
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
    ui_update_window_title(ctx);
}

/**
 * Tab close button clicked callback
 */
static gboolean on_tab_close_button_clicked(GtkWidget *button, GdkEvent *event,
                                            gpointer user_data)
{
    (void)button;  /* Unused */
    (void)event;   /* Unused */

    ImageDocument *doc = (ImageDocument *)user_data;
    AppContext *ctx = (AppContext *)g_object_get_data(G_OBJECT(button), 
                                                       "app_context");

    if (ctx && doc) {
        ui_close_document_tab(ctx, doc);
    }

    return TRUE;
}

