#include "ui/ui_file_menu.h"
#include "app/autosave.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "command.h"
#include "commands/command_revert.h"
#include "document.h"
#include "document_revert_diff.h"
#include "io/image_io.h"
#include "plugins/format_registry.h"
#include "ui.h"
#include "ui/dialogs/new_image_dialog.h"
#include "ui/dialogs/save_options_dialog.h"
#include "ui/layers_panel.h"
#include "ui/swatches.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>


/* Header probe size for format detection (must be >= 132 for DICOM) */
#define FILE_HEADER_PROBE_SIZE 256

/**
 * Callback for activating a recent file
 */
void on_recent_file_activate(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;
    gchar* file_path = (gchar*)g_object_get_data(G_OBJECT(menu_item), "recent_file_path");
    FormatHandler* handler;
    uint8_t header[FILE_HEADER_PROBE_SIZE];
    size_t header_size = 0;
    FILE* file;

    if (!file_path) {
        return;
    }

    /* Check if file still exists */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        /* Show warning dialog */
        gchar* msg = g_strdup_printf(
            "File not found: %s\n\nThe file has been removed from the recent files list.",
            file_path);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_WARNING,
                                    msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        g_free(msg);

        /* Remove from recent files */
        recent_files_remove(file_path);
        recent_files_save(); /* This syncs to settings if connected */
        ui_update_recent_files_menu(ctx);

        return;
    }

    /* Check if a plugin can handle this file format */
    file = g_fopen(file_path, "rb");
    if (file) {
        header_size = fread(header, 1, sizeof(header), file);
        fclose(file);
    }

    handler = format_registry_find_loader(file_path, header, header_size);
    if (!handler) {
        /* No plugin can handle this file format */
        gchar* msg = g_strdup_printf(
            "Unsupported file format: %s\n\nNo plugin is available to load this file type.\n\nThe file has been removed from the recent files list.",
            file_path);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        g_free(msg);

        /* Remove from recent files */
        recent_files_remove(file_path);
        recent_files_save(); /* This syncs to settings if connected */
        ui_update_recent_files_menu(ctx);

        return;
    }

    /* Create document WITHOUT adding to notebook - we'll add it only after successful load */
    gchar* basename = g_path_get_basename(file_path);
    ImageDocument* doc = ui_create_document_without_tab(ctx, basename);

    if (doc) {
        /* Load the image into the document using plugin system */
        PluginError load_error = PLUGIN_ERROR_NONE;
        gboolean load_result = image_io_load(doc, file_path, &load_error, ctx->settings);

        if (!load_result) {
            if (load_error == PLUGIN_ERROR_USER_CANCELLED) {
                document_free(doc);
                g_free(basename);
                return;
            }
            /* Get user-friendly error message */
            const char* error_message = image_io_get_error_message(load_error, file_path);

            /* Show error dialog */
            gchar* msg = g_strdup_printf("Failed to load image: %s\n\n%s",
                                         file_path, error_message);
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
            g_free(msg);

            /* Destroy document since load failed (it was never added to notebook) */
            document_free(doc);
            g_free(basename);
            return;
        }

        /* Load succeeded - now add document to notebook */
        ui_add_document_to_notebook(ctx, doc);

        /* Register document for autosave */
        autosave_register_document(doc);

        /* Initialize rendering structures after image is loaded */
        /* Note: We don't call document_load_image_from_file() here because it would
         * call image_io_load() again, which would re-trigger the plugin and show
         * dialogs again (e.g., HDR tone mapping dialog). Instead, we call the
         * initialization parts directly since image_io_load() already loaded everything. */
        if (!document_init_rendering_structures(doc)) {
            gchar* msg = g_strdup_printf("Failed to initialize document rendering for: %s",
                                         file_path);
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
            g_free(msg);

            /* Remove from notebook if it was added, then destroy document */
            if (doc->scrolled_window && ctx->notebook) {
                gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), doc->scrolled_window);
                if (page_num >= 0) {
                    ui_close_document_tab(ctx, doc);
                } else {
                    document_free(doc);
                }
            } else {
                document_free(doc);
            }
            g_free(basename);
            return;
        }

        /* Ensure we have at least one layer selected */
        ImageLayer* layer_0 = document_get_layer(doc, 0);
        if (layer_0) {
            document_set_selected_layer(doc, layer_0);
        }

        /* Mark composite as needing re-render */
        document_invalidate_composite(doc);

        /* Update drawing area size to match image dimensions */
        if (doc->drawing_area) {
            /* Set exact size for the drawing area based on image dimensions */
            gint display_width = (gint)(doc->width * doc->zoom_factor);
            gint display_height = (gint)(doc->height * doc->zoom_factor);
            gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);

            /* Queue redraw to display the image */
            gtk_widget_queue_draw(doc->drawing_area);
        }

        {
            /* Update status bar with success message */
            ui_update_status_bar_message(ctx, "Image successfully loaded");

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
void on_clear_recent_files(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;

    /* Clear recent files */
    recent_files_clear();
    recent_files_save(); /* This syncs to settings if connected */

    ui_update_recent_files_menu(ctx);
}

/**
 * Reload document from disk and record as a single undo step.
 */
static void on_file_revert(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item;
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc;
    ImageDocument* temp;
    DocumentRevertDiff* diff;
    Command* cmd;
    gchar* basename;
    guint undo_levels = 10;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc || !doc->file_path || doc->file_path[0] == '\0') {
        return;
    }

    {
        gint confirm;
        confirm = ui_utils_message_dialog_run(
            GTK_WINDOW(ctx->window),
            GTK_MESSAGE_WARNING,
            "Revert to saved file?",
            "The document will be reloaded from disk. Undo history for this document "
            "will be cleared. You can undo the revert from the Edit menu.",
            GTK_RESPONSE_CANCEL,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Revert", GTK_RESPONSE_OK,
            NULL);
        if (confirm != GTK_RESPONSE_OK) {
            return;
        }
    }

    basename = g_path_get_basename(doc->file_path);
    if (ctx->settings) {
        undo_levels = (guint)settings_get_undo_levels(ctx->settings);
    }
    temp = document_new(basename ? basename : "revert", TRUE, undo_levels);
    g_free(basename);
    if (!temp) {
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    "Out of memory preparing revert.", NULL,
                                    GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        return;
    }

    {
        PluginError load_error = PLUGIN_ERROR_NONE;
        gboolean load_ok = image_io_load(temp, doc->file_path, &load_error, ctx->settings);

        if (!load_ok) {
            if (load_error == PLUGIN_ERROR_USER_CANCELLED) {
                document_free(temp);
                return;
            }
            {
                const char* err_msg = image_io_get_error_message(load_error, doc->file_path);
                gchar* msg = g_strdup_printf("Could not reload the file for revert:\n\n%s", err_msg);
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
                g_free(msg);
            }
            document_free(temp);
            return;
        }

        if (!document_init_rendering_structures(temp)) {
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        "Could not initialize rendering after reload.", NULL,
                                        GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
            document_free(temp);
            return;
        }

        {
            ImageLayer* l0 = document_get_layer(temp, 0);
            if (l0) {
                document_set_selected_layer(temp, l0);
            }
        }
        document_invalidate_composite(temp);
    }

    diff = document_revert_diff_build(doc, temp);
    if (!diff) {
        document_free(temp);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    "Could not prepare revert undo data.", NULL,
                                    GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        return;
    }

    /* Drop prior undo/redo before replacing document content. If we cleared the stacks after
     * applying new content, command destroy callbacks would see stale layer pointers
     * (already freed by the apply) and could double-free. */
    if (doc->undo_stack) {
        command_stack_clear(doc->undo_stack);
    }
    if (doc->redo_stack) {
        command_stack_clear(doc->redo_stack);
    }

    if (!document_revert_apply_loaded_document(doc, temp)) {
        document_revert_diff_free(diff);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    "Could not apply revert.", NULL,
                                    GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        return;
    }

    cmd = command_create_document_revert(doc, diff, doc->file_path, ctx->settings);
    if (!cmd) {
        if (!document_revert_diff_apply_undo(doc, diff)) {
            g_warning("Revert: failed to restore document after command creation failure");
        }
        document_revert_diff_free(diff);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    "Could not create revert undo step.", NULL,
                                    GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        return;
    }

    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
    } else {
        command_free(cmd);
    }

    document_mark_saved(doc);
    autosave_mark_saved(doc);

    ui_update_window_title(ctx, doc);
    ui_update_status_bar(ctx, doc);
    ui_update_status_bar_message(ctx, "Reverted to saved file");

    {
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }
    }
    ui_update_menu_and_button_states(ctx);
    ui_update_recent_files_menu(ctx);
}

void ui_file_menu_update_sensitivity(AppContext* ctx) {
    ImageDocument* doc;
    gboolean has_document;
    gboolean can_save;
    gboolean can_save_as;
    gboolean can_close;
    gboolean can_close_all;
    gboolean can_open_recent;
    const GList* recent_files;

    if (!ctx || !ctx->window) {
        return;
    }

    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);

    can_save = has_document && document_is_dirty(doc);
    can_save_as = has_document;
    can_close = has_document;
    can_close_all = (ctx->documents != NULL && g_list_length(ctx->documents) > 0);

    recent_files = recent_files_get();
    can_open_recent = (recent_files != NULL && g_list_length((GList*)recent_files) > 0);

    if (ctx->file_menu_new && GTK_IS_WIDGET(ctx->file_menu_new)) {
        gtk_widget_set_sensitive(ctx->file_menu_new, TRUE);
    }
    if (ctx->file_menu_open && GTK_IS_WIDGET(ctx->file_menu_open)) {
        gtk_widget_set_sensitive(ctx->file_menu_open, TRUE);
    }
    if (ctx->file_menu_open_recent && GTK_IS_WIDGET(ctx->file_menu_open_recent)) {
        gtk_widget_set_sensitive(ctx->file_menu_open_recent, can_open_recent);
    }
    if (ctx->file_menu_save && GTK_IS_WIDGET(ctx->file_menu_save)) {
        gtk_widget_set_sensitive(ctx->file_menu_save, can_save);
    }
    if (ctx->file_menu_save_as && GTK_IS_WIDGET(ctx->file_menu_save_as)) {
        gtk_widget_set_sensitive(ctx->file_menu_save_as, can_save_as);
    }
    if (ctx->file_menu_revert && GTK_IS_WIDGET(ctx->file_menu_revert)) {
        gboolean can_revert = has_document && doc->file_path && doc->file_path[0] != '\0';
        gtk_widget_set_sensitive(ctx->file_menu_revert, can_revert);
    }
    if (ctx->file_menu_close && GTK_IS_WIDGET(ctx->file_menu_close)) {
        gtk_widget_set_sensitive(ctx->file_menu_close, can_close);
    }
    if (ctx->file_menu_close_all && GTK_IS_WIDGET(ctx->file_menu_close_all)) {
        gtk_widget_set_sensitive(ctx->file_menu_close_all, can_close_all);
    }
    if (ctx->file_menu_exit && GTK_IS_WIDGET(ctx->file_menu_exit)) {
        gtk_widget_set_sensitive(ctx->file_menu_exit, TRUE);
    }
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
        GtkWidget* file_menu_open_recent = ctx->file_menu_open_recent;
        if (!file_menu_open_recent) {
            file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
        }
        if (file_menu_open_recent) {
            gtk_widget_set_sensitive(file_menu_open_recent, FALSE);
        }
        return;
    }

    /* Enable the menu item */
    {
        GtkWidget* file_menu_open_recent = ctx->file_menu_open_recent;
        if (!file_menu_open_recent) {
            file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
        }
        if (file_menu_open_recent) {
            gtk_widget_set_sensitive(file_menu_open_recent, TRUE);
        }
    }

    /* Add menu items for each recent file */
    for (const GList* iter = recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        FormatHandler* handler;
        gchar* basename;
        uint8_t header[FILE_HEADER_PROBE_SIZE];
        size_t header_size = 0;
        FILE* file;
        gboolean file_exists;

        if (!rf || !rf->path) {
            continue;
        }

        /* Check if file exists */
        file_exists = g_file_test(rf->path, G_FILE_TEST_EXISTS);

        /* Try to detect format if file exists */
        handler = NULL;
        if (file_exists) {
            file = g_fopen(rf->path, "rb");
            if (file) {
                header_size = fread(header, 1, sizeof(header), file);
                fclose(file);
            }

            /* Find plugin handler for this file */
            handler = format_registry_find_loader(rf->path, header, header_size);
        }

        /* Get filename for display */
        basename = g_path_get_basename(rf->path);
        GtkWidget* menu_item = gtk_menu_item_new_with_label(basename);
        g_free(basename);

        /* Set tooltip to full path */
        gtk_widget_set_tooltip_text(menu_item, rf->path);

        /* Disable menu item if file doesn't exist or no plugin can handle it */
        if (!file_exists || !handler) {
            gtk_widget_set_sensitive(menu_item, FALSE);
        }

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

/**
 * Setup File menu from Glade builder
 */
void ui_file_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* file_menu = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu"));
    GtkWidget* file_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_item"));

    if (file_menu && file_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
    }

    /* Connect File menu signals */
    ctx->file_menu_new = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_new"));
    ctx->file_menu_open = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open"));
    ctx->file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
    ctx->file_menu_save = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_save"));
    ctx->file_menu_save_as = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_save_as"));
    ctx->file_menu_revert = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_revert"));
    ctx->file_menu_close = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_close"));
    ctx->file_menu_close_all = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_close_all"));
    ctx->file_menu_exit = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_exit"));

    if (ctx->file_menu_new) {
        g_signal_connect(ctx->file_menu_new, "activate", G_CALLBACK(on_file_new), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_new, "activate", accel_group,
                                   GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    if (ctx->file_menu_open) {
        g_signal_connect(ctx->file_menu_open, "activate", G_CALLBACK(on_file_open), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_open, "activate", accel_group,
                                   GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    /* Setup "Open Recent" submenu */
    if (ctx->file_menu_open_recent) {
        if (!file_menu) {
            g_warning("file_menu is NULL, cannot setup Open Recent submenu");
        } else {
            GtkWidget* recent_submenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(ctx->file_menu_open_recent), recent_submenu);
            g_object_set_data(G_OBJECT(file_menu), "recent_files_submenu", recent_submenu);
            /* Don't update menu here - it will be updated after window is fully created */
        }
    }

    if (ctx->file_menu_save) {
        g_signal_connect(ctx->file_menu_save, "activate", G_CALLBACK(on_file_save), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_save, "activate", accel_group,
                                   GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    if (ctx->file_menu_save_as) {
        g_signal_connect(ctx->file_menu_save_as, "activate", G_CALLBACK(on_file_save_as), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_save_as, "activate", accel_group,
                                   GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    }
    if (ctx->file_menu_revert) {
        g_signal_connect(ctx->file_menu_revert, "activate", G_CALLBACK(on_file_revert), ctx);
    }
    if (ctx->file_menu_close) {
        g_signal_connect(ctx->file_menu_close, "activate", G_CALLBACK(on_file_close), ctx);
    }
    if (ctx->file_menu_close_all) {
        g_signal_connect(ctx->file_menu_close_all, "activate", G_CALLBACK(on_file_close_all), ctx);
    }
    if (ctx->file_menu_exit) {
        g_signal_connect(ctx->file_menu_exit, "activate", G_CALLBACK(on_file_exit), ctx);
    }
}

/**
 * File Open dialog response callback
 */
void on_file_open_response(GtkNativeDialog* dialog, gint response_id, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar* file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (file_path) {
            /* Check if a plugin can handle this file format */
            FormatHandler* handler = NULL;
            uint8_t header[FILE_HEADER_PROBE_SIZE];
            size_t header_size = 0;
            FILE* file = g_fopen(file_path, "rb");
            if (file) {
                header_size = fread(header, 1, sizeof(header), file);
                fclose(file);
            }

            handler = format_registry_find_loader(file_path, header, header_size);
            if (!handler) {
                /* No plugin can handle this file format */
                gchar* msg = g_strdup_printf("Unsupported file format: %s", file_path);
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
                g_free(msg);
                g_free(file_path);
                g_object_unref(dialog);
                return;
            }

            /* Create document WITHOUT adding to notebook - we'll add it only after successful load */
            gchar* basename = g_path_get_basename(file_path);
            ImageDocument* doc = ui_create_document_without_tab(ctx, basename);

            if (doc) {
                /* Load the image into the document */
                PluginError load_error = PLUGIN_ERROR_NONE;
                gboolean load_result = image_io_load(doc, file_path, &load_error, ctx->settings);

                if (!load_result) {
                    if (load_error == PLUGIN_ERROR_USER_CANCELLED) {
                        document_free(doc);
                        g_free(basename);
                        g_free(file_path);
                        g_object_unref(dialog);
                        return;
                    }
                    /* Get user-friendly error message */
                    const char* error_message = image_io_get_error_message(load_error, file_path);

                    /* Show error dialog */
                    gchar* msg = g_strdup_printf("Failed to load image: %s\n\n%s",
                                                 file_path, error_message);
                    ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                                msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
                    g_free(msg);

                    /* Destroy document since load failed (it was never added to notebook) */
                    document_free(doc);
                    g_free(basename);
                    g_free(file_path);
                    g_object_unref(dialog);
                    return;
                }

                /* Load succeeded - now add document to notebook */
                ui_add_document_to_notebook(ctx, doc);

                /* Register document for autosave */
                autosave_register_document(doc);

                /* Initialize rendering structures after image is loaded */
                /* Note: We don't call document_load_image_from_file() here because it would
                 * call image_io_load() again, which would re-trigger the plugin and show
                 * dialogs again (e.g., HDR tone mapping dialog). Instead, we call the
                 * initialization parts directly since image_io_load() already loaded everything. */
                if (!document_init_rendering_structures(doc)) {
                    gchar* msg = g_strdup_printf("Failed to initialize document rendering for: %s",
                                                 file_path);
                    ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                                msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
                    g_free(msg);

                    /* Remove from notebook if it was added, then destroy document */
                    if (doc->scrolled_window && ctx->notebook) {
                        gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), doc->scrolled_window);
                        if (page_num >= 0) {
                            ui_close_document_tab(ctx, doc);
                        } else {
                            document_free(doc);
                        }
                    } else {
                        document_free(doc);
                    }
                    g_free(basename);
                    g_free(file_path);
                    g_object_unref(dialog);
                    return;
                }

                /* Ensure we have at least one layer selected */
                ImageLayer* layer_0 = document_get_layer(doc, 0);
                if (layer_0) {
                    document_set_selected_layer(doc, layer_0);
                }

                /* Mark composite as needing re-render */
                document_invalidate_composite(doc);

                /* Update drawing area size to match image dimensions */
                if (doc->drawing_area) {
                    /* Set exact size for the drawing area based on image dimensions */
                    gint display_width = (gint)(doc->width * doc->zoom_factor);
                    gint display_height = (gint)(doc->height * doc->zoom_factor);
                    gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);

                    /* Queue redraw to display the image */
                    gtk_widget_queue_draw(doc->drawing_area);
                }

                {
                    /* Update status bar with success message */
                    ui_update_status_bar_message(ctx, "Image successfully loaded");

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

    g_object_unref(dialog);
}

/**
 * File > Open callback
 */
void on_file_open(GtkWidget* widget, gpointer data) {
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
    GtkFileChooserNative* native_dialog;
    GtkFileFilter* filter;
    GList* handlers;
    GHashTable* seen_formats;
    gchar* all_patterns;

    /* Create native file chooser dialog */
    native_dialog = gtk_file_chooser_native_new(
        "Open Image",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open",
        "_Cancel");

    /* Get all registered format handlers from plugin system */
    handlers = format_registry_get_all_handlers();
    seen_formats = g_hash_table_new(g_str_hash, g_str_equal);

    /* Create "All Supported Images" filter with all patterns */
    all_patterns = format_registry_get_file_filter_patterns();
    if (all_patterns && strlen(all_patterns) > 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "All Supported Images");
        /* Parse patterns (semicolon-separated) and add each */
        gchar** patterns = g_strsplit(all_patterns, ";", -1);
        for (gint i = 0; patterns[i]; i++) {
            g_strstrip(patterns[i]);
            if (strlen(patterns[i]) > 0) {
                gtk_file_filter_add_pattern(filter, patterns[i]);
            }
        }
        g_strfreev(patterns);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
        g_free(all_patterns);
    }

    /* Create individual filters for each format */
    if (handlers) {
        for (GList* iter = handlers; iter; iter = iter->next) {
            FormatHandler* handler = (FormatHandler*)iter->data;
            if (!handler || !handler->format_info.name || !handler->format_info.extensions) {
                continue;
            }

            /* Skip if we've already added a filter for this format name */
            if (g_hash_table_contains(seen_formats, handler->format_info.name)) {
                continue;
            }
            g_hash_table_insert(seen_formats, (gpointer)handler->format_info.name, GINT_TO_POINTER(1));

            /* Create filter for this format */
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, handler->format_info.name);

            /* Parse extensions (comma-separated) and add patterns */
            gchar** exts = g_strsplit(handler->format_info.extensions, ",", -1);
            for (gint i = 0; exts[i]; i++) {
                g_strstrip(exts[i]);
                if (strlen(exts[i]) > 0) {
                    gchar* pattern = g_strdup_printf("*.%s", exts[i]);
                    gtk_file_filter_add_pattern(filter, pattern);
                    g_free(pattern);
                }
            }
            g_strfreev(exts);

            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
        }
    }

    g_hash_table_destroy(seen_formats);

    /* Add fallback "All Files" filter if no formats registered */
    if (!handlers || g_list_length(handlers) == 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "All Files");
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
    }

    /* Connect response signal */
    g_signal_connect(native_dialog, "response", G_CALLBACK(on_file_open_response), ctx);

    /* Show dialog */
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native_dialog));
}

/**
 * File > Save callback
 */
void on_file_save(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Check if document is dirty - if not, nothing to save */
    if (!document_is_dirty(doc)) {
        return; /* Nothing to save */
    }

    /* If document doesn't have a filename, trigger Save As */
    if (!doc->file_path || strlen(doc->file_path) == 0) {
        on_file_save_as(NULL, ctx);
        return;
    }

    /* Save using existing filename with default options */
    SaveOptions default_opts;
    FormatHandler* handler;
    void* plugin_data = NULL;
    size_t plugin_options_size = 0;

    memset(&default_opts, 0, sizeof(SaveOptions));
    default_opts.quality = -1;           /* Use default */
    default_opts.compression_level = -1; /* Use default */
    default_opts.preserve_alpha = doc->has_alpha ? true : false;
    default_opts.flatten_layers = FALSE;

    /* Find handler to determine if we need plugin-specific options */
    handler = format_registry_find_saver(doc->file_path);
    if (handler && handler->plugin && handler->plugin->callbacks.get_save_options_size) {
        plugin_options_size = handler->plugin->callbacks.get_save_options_size();
        if (plugin_options_size > 0) {
            plugin_data = g_malloc0(plugin_options_size);
            if (plugin_data && handler->plugin->callbacks.init_save_options) {
                /* Initialize plugin-specific options with defaults */
                /* For PNG, init_png_save_options already sets transparency_format to PNG_TRANSPARENCY_AUTO */
                handler->plugin->callbacks.init_save_options(plugin_data);
            }
            default_opts.plugin_data = plugin_data;
        }
    } else {
        default_opts.plugin_data = NULL;
    }

    PluginError save_error = PLUGIN_ERROR_NONE;
    if (document_save_as_with_error(doc, doc->file_path, &default_opts, &save_error)) {
        /* Mark document as saved */
        document_mark_saved(doc);

        /* Update window title and status bar */
        ui_update_window_title(ctx, NULL);
        ui_update_status_bar(ctx, NULL);
        ui_update_status_bar_message(ctx, "Image successfully saved");

        /* Update menu states */
        ui_update_menu_and_button_states(ctx);
    } else {
        /* Get user-friendly error message */
        const char* error_message = image_io_get_error_message(save_error, doc->file_path);

        /* Show error dialog */
        gchar* msg = g_strdup_printf("Failed to save image: %s\n\n%s",
                                     doc->file_path, error_message);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
        g_free(msg);
    }

    /* Clean up plugin data */
    if (plugin_data) {
        g_free(plugin_data);
    }
}

/**
 * Process save as result after native file chooser dialog
 */
static void process_save_as_result(GtkNativeDialog* native_dialog, AppContext* ctx) {
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(native_dialog);
    gchar* file_path = gtk_file_chooser_get_filename(chooser);

    if (file_path) {
        ImageDocument* doc = ui_get_active_document(ctx);

        if (doc) {
            SaveOptions opts;
            FormatHandler* handler;
            void* plugin_data = NULL;
            size_t plugin_options_size = 0;
            gboolean dialog_result;

            /* Initialize save options */
            memset(&opts, 0, sizeof(SaveOptions));
            opts.quality = -1;
            opts.compression_level = -1;
            opts.preserve_alpha = doc->has_alpha ? true : false;
            opts.flatten_layers = FALSE;

            /* Find handler to determine if we need plugin-specific options */
            handler = format_registry_find_saver(file_path);
            if (handler && handler->plugin && handler->plugin->callbacks.get_save_options_size) {
                plugin_options_size = handler->plugin->callbacks.get_save_options_size();
                if (plugin_options_size > 0) {
                    plugin_data = g_malloc0(plugin_options_size);
                    if (plugin_data && handler->plugin->callbacks.init_save_options) {
                        handler->plugin->callbacks.init_save_options(plugin_data);
                    }
                    opts.plugin_data = plugin_data;
                }
            }

            /* Show save options dialog if needed (after file chooser is closed) */
            dialog_result = save_options_dialog_show(GTK_WINDOW(ctx->window), file_path, &opts, doc);

            if (dialog_result) {
                /* User clicked OK, proceed with save */
                PluginError save_error = PLUGIN_ERROR_NONE;
                if (document_save_as_with_error(doc, file_path, &opts, &save_error)) {
                    /* Update document filename and path */
                    if (doc->file_path) {
                        g_free(doc->file_path);
                    }
                    if (doc->filename) {
                        g_free(doc->filename);
                    }
                    doc->file_path = g_strdup(file_path);
                    doc->filename = g_path_get_basename(file_path);

                    /* Mark document as saved */
                    document_mark_saved(doc);

                    /* Add to recent files */
                    recent_files_add(file_path);
                    recent_files_save(); /* This syncs to settings if connected */

                    /* Update tab label to reflect new filename */
                    ui_update_document_tab_label(ctx, doc);

                    /* Update window title to reflect new filename */
                    ui_update_window_title(ctx, NULL);
                    ui_update_status_bar(ctx, NULL);
                    ui_update_status_bar_message(ctx, "Image successfully saved");
                    ui_update_recent_files_menu(ctx);

                    /* Update menu states */
                    ui_update_menu_and_button_states(ctx);
                } else {
                    /* Get user-friendly error message */
                    const char* error_message = image_io_get_error_message(save_error, file_path);

                    /* Show error dialog */
                    gchar* msg = g_strdup_printf("Failed to save image: %s\n\n%s",
                                                 file_path, error_message);
                    ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                                msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
                    g_free(msg);
                }
            }
            /* If dialog_result is FALSE, user cancelled, so don't save */

            /* Clean up plugin data */
            if (plugin_data) {
                g_free(plugin_data);
            }
        }

        g_free(file_path);
    }
}

/**
 * File > Save As callback
 */
void on_file_save_as(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    GtkFileChooserNative* native_dialog;
    GtkFileFilter* filter;
    GList* handlers;
    GHashTable* seen_formats;
    gint response;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Create native file chooser dialog */
    native_dialog = gtk_file_chooser_native_new(
        "Save Image As",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Save",
        "_Cancel");

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native_dialog), TRUE);

    /* Get all registered format handlers from plugin system */
    handlers = format_registry_get_all_handlers();
    seen_formats = g_hash_table_new(g_str_hash, g_str_equal);

    /* Track filters for default selection */
    GtkFileFilter* first_filter = NULL;
    GtkFileFilter* png_filter = NULL;

    /* Create filters for each format */
    if (handlers) {
        for (GList* iter = handlers; iter; iter = iter->next) {
            FormatHandler* handler = (FormatHandler*)iter->data;
            if (!handler || !handler->format_info.name || !handler->format_info.extensions) {
                continue;
            }

            /* Skip if plugin doesn't support saving */
            if (!handler->plugin || !handler->plugin->callbacks.save ||
                !handler->plugin->callbacks.can_save) {
                continue;
            }

            /* Skip if we've already added a filter for this format name */
            if (g_hash_table_contains(seen_formats, handler->format_info.name)) {
                continue;
            }
            g_hash_table_insert(seen_formats, (gpointer)handler->format_info.name, GINT_TO_POINTER(1));

            /* Create filter for this format */
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, handler->format_info.name);

            /* Parse extensions (comma-separated) and add patterns */
            gchar** exts = g_strsplit(handler->format_info.extensions, ",", -1);
            for (gint i = 0; exts[i]; i++) {
                g_strstrip(exts[i]);
                if (strlen(exts[i]) > 0) {
                    gchar* pattern = g_strdup_printf("*.%s", exts[i]);
                    gtk_file_filter_add_pattern(filter, pattern);
                    g_free(pattern);
                }
            }
            g_strfreev(exts);

            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

            /* Track first filter */
            if (!first_filter) {
                first_filter = filter;
            }

            /* Track PNG filter if found (prefer as default) */
            if (!png_filter && handler->format_info.name &&
                g_str_has_prefix(handler->format_info.name, "PNG")) {
                png_filter = filter;
            }
        }
    }

    g_hash_table_destroy(seen_formats);

    /* Add fallback "All Files" filter if no formats registered */
    if (!handlers || g_list_length(handlers) == 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "All Files");
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
        first_filter = filter;
    }

    /* Determine default filter (prefer PNG, otherwise use first filter) */
    GtkFileFilter* default_filter = png_filter ? png_filter : first_filter;

    /* Set default filter */
    if (default_filter) {
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(native_dialog), default_filter);
    }

    /* Set current filename if document has a path */
    if (doc->file_path) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(native_dialog), doc->file_path);
    } else if (doc->filename) {
        /* Suggest a filename based on document name */
        gchar* suggested = g_strdup_printf("%s.png", doc->filename);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native_dialog), suggested);
        g_free(suggested);
    }

    /* Show dialog and get response */
    response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native_dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        /* User clicked Save */
        process_save_as_result(GTK_NATIVE_DIALOG(native_dialog), ctx);
    }

    /* Clean up native dialog */
    g_object_unref(native_dialog);
}

/**
 * File > Close callback
 */
void on_file_close(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* active_doc = ui_get_active_document(ctx);

    if (active_doc) {
        ui_close_document_tab(ctx, active_doc);
    }
}

/**
 * File > Close All callback
 * Closes all document tabs. Stops if user cancels a save prompt on any document.
 */
void on_file_close_all(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;

    while (ctx->documents != NULL) {
        ImageDocument* doc = (ImageDocument*)ctx->documents->data;
        guint n_before = g_list_length(ctx->documents);

        ui_close_document_tab(ctx, doc);

        /* If list length unchanged, user cancelled save prompt - stop closing */
        if (g_list_length(ctx->documents) == n_before) {
            break;
        }
    }
}

/**
 * Window delete event callback
 */
gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data) {
    (void)widget; /* Unused */
    (void)event;  /* Unused */

    AppContext* ctx = (AppContext*)data;

    /* SAVE SETTINGS FIRST - before any cleanup */
    if (ctx && ctx->settings && ctx->app_dir) {
        /* Sync recent files to settings before saving */
        recent_files_save(); /* This will sync to settings if connected */

        /* Save all current tool options to settings before final save */
        if (ctx->tool_registry) {
            ui_save_all_tool_options_to_settings(ctx);
        }

        /* Sync widgets to swatches data before saving */
        GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
        GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
        swatches_sync_from_widgets(&ctx->swatches, main_widget, recent_widget);
        /* Save swatches to file */
        swatches_save(&ctx->swatches, ctx->app_dir);

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
    gtk_main_quit();

    return FALSE; /* Allow window to close */
}

/**
 * File > New callback
 */
void on_file_new(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    NewImageDialog* dialog;
    NewImageDialogResult* result;
    gint response;
    ImageDocument* doc;
    ImageLayer* background_layer;
    LayersPanel* layers_panel;
    const gdouble* custom_color = NULL;

    if (!ctx) {
        g_warning("Invalid application context");
        return;
    }

    if (!ctx->window || !GTK_IS_WINDOW(ctx->window)) {
        g_warning("Invalid main window");
        return;
    }

    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");

    /* Create and show new image dialog */
    dialog = new_image_dialog_new();
    if (!dialog) {
        g_warning("Failed to create new image dialog");
        return;
    }

    response = new_image_dialog_run(dialog, GTK_WINDOW(ctx->window), &result);

    if (response == GTK_RESPONSE_OK && result) {
        /* Validate dimensions */
        if (result->width == 0 || result->height == 0) {
            gchar* msg = g_strdup_printf(
                "Invalid image dimensions: %u x %u\n\nWidth and height must be greater than 0.",
                result->width, result->height);
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        msg, NULL, GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
            g_free(msg);
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Get custom color if needed */
        if (result->background == LAYER_BACKGROUND_CUSTOM) {
            custom_color = result->custom_color;
        }

        /* Create new document */
        doc = ui_create_document_without_tab(ctx, "Untitled");
        if (!doc) {
            g_warning("Failed to create document");
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Set document dimensions */
        doc->width = result->width;
        doc->height = result->height;
        doc->has_alpha = (result->background == LAYER_BACKGROUND_TRANSPARENT) ? TRUE : FALSE;
        doc->channels = doc->has_alpha ? 4 : 3;
        doc->bit_depth = 8;

        /* Initialize rendering structures */
        if (!document_init_rendering_structures(doc)) {
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        "Failed to initialize document rendering structures", NULL,
                                        GTK_RESPONSE_OK, "_OK", GTK_RESPONSE_OK, NULL);
            document_free(doc);
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Create background layer */
        background_layer = layer_new("Background", doc->width, doc->height, TRUE,
                                     result->background, LAYER_POSITION_ABOVE_CURRENT,
                                     custom_color, doc);
        if (!background_layer) {
            g_warning("Failed to create background layer");
            document_free(doc);
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Add layer to document */
        doc->layers = g_list_append(doc->layers, background_layer);
        document_set_selected_layer(doc, background_layer);

        /* Mark composite as needing re-render */
        document_invalidate_composite(doc);

        /* Add document to notebook */
        ui_add_document_to_notebook(ctx, doc);

        /* Register document for autosave */
        autosave_register_document(doc);

        /* Update drawing area size to match image dimensions */
        if (doc->drawing_area) {
            gint display_width = (gint)(doc->width * doc->zoom_factor);
            gint display_height = (gint)(doc->height * doc->zoom_factor);
            gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
            gtk_widget_queue_draw(doc->drawing_area);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        ui_update_status_bar(ctx, NULL);
        ui_update_status_bar_message(ctx, "New image created");

        /* Free dialog result */
        new_image_dialog_result_free(result);
    }

    /* Free dialog */
    new_image_dialog_free(dialog);
}

/**
 * File > Exit callback
 */
void on_file_exit(GtkWidget* widget, gpointer data) {
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

        /* Sync widgets to swatches data before saving */
        GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
        GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
        swatches_sync_from_widgets(&ctx->swatches, main_widget, recent_widget);
        /* Save swatches to file */
        swatches_save(&ctx->swatches, ctx->app_dir);

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
    gtk_main_quit();
}
