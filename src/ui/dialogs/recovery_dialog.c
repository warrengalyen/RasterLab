/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/recovery_dialog.h"
#include "app/autosave.h"
#include "document.h"
#include "i18n.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include "ui/ui_file_menu.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <time.h>

/* Forward declarations */
static void on_recovery_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data);
static void on_recovery_checkbox_toggled(GtkCellRendererToggle* renderer, gchar* path_str,
                                         gpointer user_data);

/**
 * Toggle the checked state of a recovery entry row.
 * GTK's GtkCellRendererToggle emits "toggled" but does NOT update the model;
 * the application must do it manually.
 */
static void on_recovery_checkbox_toggled(GtkCellRendererToggle* renderer, gchar* path_str,
                                         gpointer user_data) {
    (void)renderer;
    GtkListStore* store = GTK_LIST_STORE(user_data);
    GtkTreeIter iter;
    GtkTreePath* path = gtk_tree_path_new_from_string(path_str);

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, path)) {
        gboolean current = FALSE;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &current, -1);
        gtk_list_store_set(store, &iter, 0, !current, -1);
    }

    gtk_tree_path_free(path);
}

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

    /* Replace default titlebar with header bar */
    ui_utils_set_header_bar(GTK_WINDOW(dialog), "Recovered Files Found");

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

    /* Checkbox column — must connect "toggled" to update the model; GTK cell
     * renderer toggles do NOT flip the model value automatically. */
    renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect(renderer, "toggled", G_CALLBACK(on_recovery_checkbox_toggled), store);
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
                    /* Determine display name */
                    gchar* filename = g_path_get_basename(entry->original_path);
                    if (!filename || strlen(filename) == 0) {
                        g_free(filename);
                        filename = g_strdup_printf("Recovered %ld", (long)entry->timestamp);
                    }

                    g_free(doc->filename);
                    doc->filename = g_strdup(filename);
                    g_free(filename);

                    /* Create the drawing area hierarchy (sets doc->scrolled_window,
                     * doc->canvas_container, doc->viewport, doc->drawing_area, etc.) */
                    document_create_drawing_area(doc);

                    /* Wire up per-widget context references used by tool and draw handlers */
                    if (doc->drawing_area) {
                        g_object_set_data(G_OBJECT(doc->drawing_area), "app_context", ctx);
                        if (ctx->tool_registry) {
                            g_object_set_data(G_OBJECT(doc->drawing_area), "tool_registry",
                                              ctx->tool_registry);
                        }
                        if (ctx->layers_panel) {
                            g_object_set_data(G_OBJECT(doc->drawing_area), "layers_panel",
                                              ctx->layers_panel);
                        }

                        /* Update drawing area size to match the actual document dimensions.
                         * document_create_drawing_area() starts at a hardcoded 800×600;
                         * without this the canvas clips incorrectly and can paint over
                         * adjacent UI panels. */
                        gdouble zoom = (doc->zoom_factor > 0.0) ? doc->zoom_factor : 1.0;
                        gint display_w = (gint)(doc->width * zoom);
                        gint display_h = (gint)(doc->height * zoom);
                        if (display_w < 1)
                            display_w = (gint)doc->width;
                        if (display_h < 1)
                            display_h = (gint)doc->height;
                        gtk_widget_set_size_request(doc->drawing_area, display_w, display_h);
                    }

                    /* Apply canvas background colour to the viewport via CSS */
                    if (doc->viewport) {
                        gdouble r_val, g_val, b_val;
                        ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
                        guint r = (guint)(r_val * 255.0);
                        guint g = (guint)(g_val * 255.0);
                        guint b = (guint)(b_val * 255.0);

                        GtkCssProvider* provider = gtk_css_provider_new();
                        g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider",
                                               provider, g_object_unref);
                        GtkStyleContext* sc = gtk_widget_get_style_context(doc->viewport);
                        gtk_style_context_add_provider(sc, GTK_STYLE_PROVIDER(provider),
                                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                        gchar* css = g_strdup_printf(
                            "#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
                        gtk_css_provider_load_from_data(provider, css, -1, NULL);
                        g_free(css);
                    }

                    /* Set up drag-and-drop file opening on the viewport */
                    ui_file_menu_setup_viewport_drag_drop(doc, ctx);

                    /* Add the tab using the standard path (handles rulers, label, close
                     * button, and adds to ctx->documents BEFORE the notebook append so
                     * the switch-page signal fires with the document already registered) */
                    ui_add_document_to_notebook(ctx, doc);

                    /* Point the active tool at the newly recovered document */
                    if (ctx->tool_registry) {
                        ctx->tool_registry->current_doc = doc;
                    }

                    /* Register for future autosave */
                    autosave_register_document(doc);

                    /* Refresh UI */
                    ui_update_window_title(ctx, NULL);
                    ui_update_status_bar(ctx, doc);

                    if (ctx->layers_panel) {
                        layers_panel_update(ctx->layers_panel, doc);
                    }
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
