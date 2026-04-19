/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/*
 * Tools menu: Language, Settings...
 */
#include "ui/ui_tools_menu.h"
#include "app/settings.h"
#include "debug_logger.h"
#include "document.h"
#include "i18n.h"
#include "ui.h"
#include "ui/dialogs/gradient_editor_dialog.h"
#include "ui/dialogs/settings_dialog.h"
#include <glib.h>
#include <gtk/gtk.h>

#define RL_LOCALE_KEY "rl-locale"

static void on_tools_menu_settings_activate(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    AppContext* ctx = (AppContext*)user_data;
    if (ctx) {
        settings_dialog_show(ctx);
    }
}

static void on_tools_menu_show_debug_log_activate(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    AppContext* ctx = (AppContext*)user_data;
    char path[4608];

    if (!ctx || !ctx->window) {
        return;
    }

    if (!debug_get_current_log_path(path, sizeof(path))) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "%s",
            _("Debug logging is not active for this session."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    debug_flush();

    GError* err = NULL;
    gchar* uri = g_filename_to_uri(path, NULL, &err);
    if (!uri) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "%s",
            err ? err->message : _("Could not build a file URI for the debug log."));
        if (err) {
            g_error_free(err);
            err = NULL;
        }
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    if (!gtk_show_uri_on_window(GTK_WINDOW(ctx->window), uri, GDK_CURRENT_TIME, &err)) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "%s\n\n%s",
            err ? err->message : _("Could not open the debug log."),
            path);
        if (err) {
            g_error_free(err);
        }
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    g_free(uri);
}

static void on_tools_menu_gradient_editor_activate(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    AppContext* ctx = (AppContext*)user_data;
    if (ctx) {
        gradient_editor_dialog_show(ctx);
    }
}

static void on_tools_menu_gpu_debug_toggled(GtkCheckMenuItem* check_menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (!ctx || !ctx->settings) {
        return;
    }

    gboolean active = gtk_check_menu_item_get_active(check_menu_item);
    settings_set_show_gpu_stats(ctx->settings, active);

    if (ctx->documents) {
        for (GList* iter = ctx->documents; iter; iter = iter->next) {
            ImageDocument* doc = (ImageDocument*)iter->data;
            if (doc && doc->viewport) {
                gtk_widget_queue_draw(doc->viewport);
            }
        }
    }
}

static void on_language_activate(GtkMenuItem* item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (!ctx || !ctx->settings || !ctx->app_dir) {
        return;
    }
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        return;
    }

    const gchar* new_loc = (const gchar*)g_object_get_data(G_OBJECT(item), RL_LOCALE_KEY);
    const gchar* old = settings_get_interface_locale(ctx->settings);

    if (!new_loc || !new_loc[0] || g_strcmp0(new_loc, old) == 0) {
        return;
    }

    settings_set_interface_locale(ctx->settings, new_loc);
    if (!settings_save(ctx->settings, ctx->app_dir)) {
        debug_log("WRN", "i18n: failed to save settings after language change");
    } else {
        debug_log("DBG", "i18n: settings saved with interface locale \"%s\"", new_loc);
    }
    i18n_apply_locale(ctx->app_dir, new_loc);
    debug_log("DBG", "i18n: gettext rebound for locale \"%s\"", new_loc);

    {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "%s",
            _("Restart the application for all interface text to use the new language."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void sync_language_radio(AppContext* ctx, GtkWidget* submenu, const gchar* current) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(submenu));

    for (GList* l = children; l; l = l->next) {
        GtkWidget* w = GTK_WIDGET(l->data);
        const gchar* loc = (const gchar*)g_object_get_data(G_OBJECT(w), RL_LOCALE_KEY);
        gboolean match;

        if (current == NULL || !current[0]) {
            match = (loc == NULL);
        } else {
            match = (loc != NULL && strcmp(loc, current) == 0);
        }

        if (match) {
            g_signal_handlers_block_by_func(w, G_CALLBACK(on_language_activate), ctx);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), TRUE);
            g_signal_handlers_unblock_by_func(w, G_CALLBACK(on_language_activate), ctx);
            break;
        }
    }
    g_list_free(children);
}

void ui_tools_menu_populate_language(AppContext* ctx) {
    GtkBuilder* builder;
    GtkWidget* submenu;

    if (!ctx || !ctx->window) {
        debug_log("DBG", "i18n: language menu not populated (no window)");
        return;
    }

    builder = g_object_get_data(G_OBJECT(ctx->window), "main_builder");
    if (!builder) {
        debug_log("DBG", "i18n: language menu not populated (no main_builder on window)");
        return;
    }

    /* Prefer the submenu actually attached to the Language item; Glade sometimes leaves
     * the named GtkMenu unattached, so populating by id alone can fill an orphan menu. */
    {
        GtkWidget* lang_mi = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_language"));

        submenu = NULL;
        if (lang_mi) {
            submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(lang_mi));
        }
        if (!submenu) {
            submenu = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_language_submenu"));
            if (submenu && lang_mi) {
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(lang_mi), submenu);
                debug_log("DBG", "i18n: attached tools_menu_language_submenu to Language menu item (was detached)");
            }
        }
    }

    if (!submenu) {
        debug_log("DBG", "i18n: language menu not populated (no Language submenu widget)");
        return;
    }

    debug_log("DBG", "i18n: populating language menu (app_dir=%s)", ctx->app_dir ? ctx->app_dir : "(null)");

    {
        GtkContainer* container = GTK_CONTAINER(submenu);
        GList* children = gtk_container_get_children(container);
        GList* l;

        for (l = children; l; l = l->next) {
            gtk_widget_destroy(GTK_WIDGET(l->data));
        }
        g_list_free(children);
    }

    {
        GSList* group = NULL;
        GPtrArray* locales = NULL;
        guint i;

        if (ctx->app_dir) {
            locales = i18n_collect_mo_locales(ctx->app_dir);
        } else {
            locales = g_ptr_array_new_with_free_func(g_free);
        }
        if (locales->len == 0) {
            g_ptr_array_add(locales, g_strdup("en_US"));
        }

        for (i = 0; i < locales->len; i++) {
            const gchar* code = g_ptr_array_index(locales, i);
            gchar* label = i18n_locale_menu_label(code);
            GtkWidget* item = gtk_radio_menu_item_new_with_label(group, label);

            g_free(label);
            if (i == 0) {
                group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
            }
            g_object_set_data_full(G_OBJECT(item), RL_LOCALE_KEY, g_strdup(code), g_free);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
            g_signal_connect(item, "activate", G_CALLBACK(on_language_activate), ctx);
        }
        g_ptr_array_unref(locales);
    }

    gtk_widget_show_all(submenu);

    {
        GList* ch = gtk_container_get_children(GTK_CONTAINER(submenu));
        guint n = (guint)g_list_length(ch);

        g_list_free(ch);
        debug_log("DBG", "i18n: language submenu now has %u item(s) (visible after show_all)", n);
    }

    {
        const gchar* cur = ctx->settings ? settings_get_interface_locale(ctx->settings) : "en_US";

        sync_language_radio(ctx, submenu, cur);
    }
}

void ui_tools_menu_setup(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* tools_menu = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu"));
    GtkWidget* tools_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_item"));

    if (tools_menu && tools_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_menu_item), tools_menu);
    }

    GtkWidget* tools_menu_settings = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_settings"));
    if (tools_menu_settings) {
        g_signal_connect(tools_menu_settings, "activate", G_CALLBACK(on_tools_menu_settings_activate), ctx);
    }

    GtkWidget* tools_menu_show_debug_log = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_show_debug_log"));
    if (tools_menu_show_debug_log) {
        g_signal_connect(tools_menu_show_debug_log, "activate", G_CALLBACK(on_tools_menu_show_debug_log_activate), ctx);
    }

    GtkWidget* tools_menu_gpu_debug = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_gpu_debug"));
    if (tools_menu_gpu_debug) {
        g_signal_connect(tools_menu_gpu_debug, "toggled", G_CALLBACK(on_tools_menu_gpu_debug_toggled), ctx);
        if (ctx->settings) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(tools_menu_gpu_debug),
                                           settings_get_show_gpu_stats(ctx->settings));
        }
    }

    GtkWidget* tools_menu_gradient_editor = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_gradient_editor"));
    if (tools_menu_gradient_editor) {
        g_signal_connect(tools_menu_gradient_editor, "activate",
                         G_CALLBACK(on_tools_menu_gradient_editor_activate), ctx);
    }
}
