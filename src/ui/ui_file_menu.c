#include "ui/ui_file_menu.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "command.h"
#include "document.h"
#include "io/image_io.h"
#include "plugins/format_registry.h"
#include "ui.h"
#include "ui/dialogs/save_options_dialog.h"
#include "ui/layers_panel.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

/**
 * Callback for activating a recent file
 */
void on_recent_file_activate(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;
    gchar* file_path = (gchar*)g_object_get_data(G_OBJECT(menu_item), "recent_file_path");
    FormatHandler* handler;
    uint8_t header[64];
    size_t header_size = 0;
    FILE* file;

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

    /* Check if a plugin can handle this file format */
    file = g_fopen(file_path, "rb");
    if (file) {
        header_size = fread(header, 1, sizeof(header), file);
        fclose(file);
    }

    handler = format_registry_find_loader(file_path, header, header_size);
    if (!handler) {
        /* No plugin can handle this file format */
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Unsupported file format: %s\n\nNo plugin is available to load this file type.\n\nThe file has been removed from the recent files list.",
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
        /* Load the image into the document using plugin system */
        if (!document_load_image_from_file(doc, file_path)) {
            /* Show error dialog */
            GtkWidget* dialog = gtk_message_dialog_new(
                GTK_WINDOW(ctx->window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "Failed to load image: %s\n\nThe file may be corrupted or in an unsupported format.",
                file_path);

            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
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
void on_clear_recent_files(GtkMenuItem* menu_item, gpointer user_data) {
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
        FormatHandler* handler;
        gchar* basename;
        uint8_t header[64];
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
 * File Open dialog response callback
 */
void on_file_open_response(GtkDialog* dialog, gint response_id, gpointer user_data) {
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
    GtkWidget* dialog;
    GtkFileFilter* filter;
    GList* handlers;
    GHashTable* seen_formats;
    gchar* all_patterns;

    /* Create file chooser dialog */
    dialog = gtk_file_chooser_dialog_new(
        "Open Image",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

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
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
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

            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        }
    }

    g_hash_table_destroy(seen_formats);

    /* Add fallback "All Files" filter if no formats registered */
    if (!handlers || g_list_length(handlers) == 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "All Files");
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }

    /* Connect response signal */
    g_signal_connect(dialog, "response", G_CALLBACK(on_file_open_response), ctx);

    /* Show dialog */
    gtk_widget_show(dialog);
}

/**
 * File > Save As response callback
 */
void on_file_save_as_response(GtkDialog* dialog, gint response_id, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar* file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        /* Close file chooser dialog first */
        gtk_widget_destroy(GTK_WIDGET(dialog));

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
                dialog_result = save_options_dialog_show(GTK_WINDOW(ctx->window), file_path, &opts);

                if (dialog_result) {
                    /* User clicked OK, proceed with save */
                    if (document_save_as(doc, file_path, &opts)) {
                        /* Add to recent files */
                        recent_files_add(file_path);
                        recent_files_save(); /* This syncs to settings if connected */

                        /* Update window title to reflect new filename */
                        ui_update_window_title(ctx, NULL);
                        ui_update_status_bar(ctx, NULL);
                        ui_update_recent_files_menu(ctx);
                    } else {
                        g_warning("Failed to save document");
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
    } else {
        /* User cancelled file chooser, just close it */
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

/**
 * File > Save As callback
 */
void on_file_save_as(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    GtkWidget* dialog;
    GtkFileFilter* filter;
    GList* handlers;
    GHashTable* seen_formats;

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

    /* Get all registered format handlers from plugin system */
    handlers = format_registry_get_all_handlers();
    seen_formats = g_hash_table_new(g_str_hash, g_str_equal);

    /* Create filters for each format */
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

            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        }
    }

    g_hash_table_destroy(seen_formats);

    /* Add fallback "All Files" filter if no formats registered */
    if (!handlers || g_list_length(handlers) == 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "All Files");
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }

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
void on_file_close(GtkWidget* widget, gpointer data) {
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

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
    gtk_main_quit();
}
