#include "ui.h"
#include "document.h"
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
static void on_file_open(GtkWidget *widget, gpointer data);
static void on_file_open_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_file_close(GtkWidget *widget, gpointer data);
static void on_file_exit(GtkWidget *widget, gpointer data);
static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data);
static void on_layer_new(GtkWidget *widget, gpointer data);
static void on_layer_delete(GtkWidget *widget, gpointer data);
static void on_layer_duplicate(GtkWidget *widget, gpointer data);
static void on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page,
                                     guint page_num, gpointer user_data);
static void on_tab_close_button_clicked(GtkButton *button, gpointer user_data);

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

    /* Layer > Duplicate Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_Duplicate Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Layer > Delete Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_Delete Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_delete), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

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
    GtkWidget *layer_menu_item;
    GtkWidget *layer_menu;

    /* File menu */
    file_menu_item = gtk_menu_item_new_with_mnemonic("_File");
    file_menu = create_file_menu(ctx);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_menu_item);

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
                     G_CALLBACK(on_window_delete), ctx);

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

        printf("Document closed successfully\n");
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
    
    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Delete the last (top) layer */
    ImageLayer *top_layer = (ImageLayer *)g_list_last(doc->layers)->data;
    
    if (document_delete_layer(doc, top_layer)) {
        printf("Layer deleted\n");
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
    
    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Duplicate the last (top) layer */
    ImageLayer *top_layer = (ImageLayer *)g_list_last(doc->layers)->data;
    
    static int dup_count = 1;
    gchar *layer_name = g_strdup_printf("%s copy %d", top_layer->name, dup_count++);
    
    ImageLayer *dup_layer = document_duplicate_layer(doc, top_layer, layer_name);
    g_free(layer_name);
    
    if (dup_layer) {
        printf("Layer duplicated\n");
    }
}

