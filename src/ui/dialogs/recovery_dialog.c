#include "ui/dialogs/recovery_dialog.h"
#include "app/autosave.h"
#include "document.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <time.h>

/* Forward declarations */
static void on_recovery_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data);
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data);

/**
 * Show recovery dialog for autosave files
 */
void recovery_dialog_show(AppContext* ctx) {
    GList* recovery_files = autosave_scan_recovery_files();

    if (!recovery_files) {
        return; /* No recovery files found */
    }

    /* Create dialog */
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Recovered Files Found",
        GTK_WINDOW(ctx->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Recover Selected",
        GTK_RESPONSE_ACCEPT,
        "_Discard All",
        GTK_RESPONSE_REJECT,
        NULL);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    /* Create content area */
    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    /* Label */
    GtkWidget* label = gtk_label_new(
        "Recovered files were found from a previous session.\n"
        "Select files to recover or discard all autosaves.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    /* Create tree view for recovery list */
    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 200);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled), 500);

    GtkListStore* store = gtk_list_store_new(5, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_POINTER);
    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    /* Columns */
    GtkCellRenderer* renderer;
    GtkTreeViewColumn* column;

    /* Checkbox column */
    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "active", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    /* Filename column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("File", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    /* Dimensions column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    /* Timestamp column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Last Saved", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    /* Populate list */
    for (GList* iter = recovery_files; iter; iter = iter->next) {
        AutosaveRecoveryEntry* entry = (AutosaveRecoveryEntry*)iter->data;
        GtkTreeIter tree_iter;

        gchar* filename = g_path_get_basename(entry->original_path);
        if (!filename || strlen(filename) == 0) {
            g_free(filename);
            filename = g_strdup("Untitled");
        }

        gchar* size_str = g_strdup_printf("%u × %u", entry->width, entry->height);

        /* Format timestamp */
        struct tm* tm_info = localtime(&entry->timestamp);
        gchar time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        gtk_list_store_append(store, &tree_iter);
        gtk_list_store_set(store, &tree_iter,
                           0, TRUE, /* Checked by default */
                           1, filename,
                           2, size_str,
                           3, time_str,
                           4, entry, /* Store entry pointer */
                           -1);

        g_free(filename);
        g_free(size_str);
    }

    gtk_container_add(GTK_CONTAINER(scrolled), tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    /* Store recovery list in dialog data */
    g_object_set_data(G_OBJECT(dialog), "recovery_files", recovery_files);
    g_object_set_data(G_OBJECT(dialog), "tree_view", tree_view);
    g_object_set_data(G_OBJECT(dialog), "app_context", ctx);

    /* Connect response signal */
    g_signal_connect(dialog, "response", G_CALLBACK(on_recovery_dialog_response), NULL);

    gtk_widget_show_all(dialog);
}

/**
 * Recovery dialog response callback
 */
static void on_recovery_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data) {
    (void)user_data; /* Unused */

    GList* recovery_files = (GList*)g_object_get_data(G_OBJECT(dialog), "recovery_files");
    GtkWidget* tree_view = (GtkWidget*)g_object_get_data(G_OBJECT(dialog), "tree_view");
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(dialog), "app_context");

    if (response_id == GTK_RESPONSE_ACCEPT) {
        /* Recover selected files */
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view));
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

        while (valid) {
            gboolean checked;
            AutosaveRecoveryEntry* entry;

            gtk_tree_model_get(model, &iter,
                               0, &checked,
                               4, &entry,
                               -1);

            if (checked && entry) {
                /* Load document from autosave */
                ImageDocument* doc = autosave_load_document(entry->autosave_path);
                if (doc) {
                    /* Create UI tab for recovered document */
                    gchar* filename = g_path_get_basename(entry->original_path);
                    if (!filename || strlen(filename) == 0) {
                        g_free(filename);
                        filename = g_strdup_printf("Recovered %ld", (long)entry->timestamp);
                    }

                    /* Set filename in document */
                    g_free(doc->filename);
                    doc->filename = g_strdup(filename);

                    /* Create drawing area and tab */
                    GtkWidget* page_content = document_create_drawing_area(doc);
                    if (page_content) {
                        /* Create tab label */
                        GtkWidget* tab_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
                        gtk_container_set_border_width(GTK_CONTAINER(tab_hbox), 0);

                        GtkWidget* tab_label = gtk_label_new(filename);
                        gtk_box_pack_start(GTK_BOX(tab_hbox), tab_label, FALSE, FALSE, 0);

                        GtkWidget* close_button = gtk_button_new();
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
                        gint page_num = gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook),
                                                                 page_content, tab_hbox);
                        gtk_notebook_set_current_page(GTK_NOTEBOOK(ctx->notebook), page_num);

                        /* Store references */
                        g_object_set_data(G_OBJECT(close_button), "tab_label", tab_label);
                        g_object_set_data(G_OBJECT(close_button), "app_context", ctx);

                        if (doc && doc->drawing_area) {
                            g_object_set_data(G_OBJECT(doc->drawing_area), "app_context", ctx);
                            if (ctx->tool_registry) {
                                g_object_set_data(G_OBJECT(doc->drawing_area), "tool_registry", ctx->tool_registry);
                            }
                            if (ctx->layers_panel) {
                                g_object_set_data(G_OBJECT(doc->drawing_area), "layers_panel", ctx->layers_panel);
                            }
                        }

                        /* Add document to list */
                        ctx->documents = g_list_append(ctx->documents, doc);

                        /* Register for autosave */
                        autosave_register_document(doc);

                        /* Update UI */
                        ui_update_window_title(ctx, NULL);
                        ui_update_status_bar(ctx, doc);

                        /* Update layers panel */
                        if (ctx->layers_panel) {
                            layers_panel_update(ctx->layers_panel, doc);
                        }
                    }

                    g_free(filename);
                }
            }

            valid = gtk_tree_model_iter_next(model, &iter);
        }
    }

    /* Delete all autosave files (user chose to recover or discard) */
    for (GList* iter = recovery_files; iter; iter = iter->next) {
        AutosaveRecoveryEntry* entry = (AutosaveRecoveryEntry*)iter->data;
        autosave_delete_file(entry->autosave_path);
    }

    /* Free recovery list */
    autosave_free_recovery_list(recovery_files);

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/**
 * Tab close button click handler (forward declaration needed for signal connection)
 */
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(button), "app_context");

    if (ctx && doc) {
        /* Use the UI function to close the document tab */
        ui_close_document_tab(ctx, doc);
    }
}
