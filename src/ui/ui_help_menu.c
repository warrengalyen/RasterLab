/*
 * Help menu: User Guide (local HTML or online mirror), About.
 */
#include "ui/ui_help_menu.h"
#include "app/settings.h"
#include "build_version.h"
#include "debug_logger.h"
#include "i18n.h"
#include "ui/ui_utils.h"
#include "version.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <gtk/gtk.h>

/** Append /index.php to base URL, stripping trailing slashes from base. */
static gchar* build_help_index_http_url(const gchar* base) {
    if (!base || !base[0]) {
        return NULL;
    }
    gsize len = strlen(base);
    while (len > 0 && base[len - 1] == '/') {
        len--;
    }
    return g_strdup_printf("%.*s/index.php", (int)len, base);
}

static void show_uri_error(GtkWindow* parent, const gchar* message, const gchar* detail) {
    GtkWidget* dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "%s",
        message);
    if (detail && detail[0]) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
    }
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/** Open URI; on failure show a warning dialog. Returns TRUE if opened. */
static gboolean open_uri_with_feedback(GtkWindow* parent, const gchar* uri) {
    GError* err = NULL;
    gboolean ok = gtk_show_uri_on_window(parent, uri, GDK_CURRENT_TIME, &err);
    if (!ok && err) {
        show_uri_error(parent, _("Could not open the help page."), err->message);
        g_error_free(err);
    } else if (!ok) {
        show_uri_error(parent, _("Could not open the help page."), "");
    }
    return ok;
}

static void on_help_user_guide_activate(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->window || !ctx->app_dir) {
        return;
    }

    GtkWindow* win = GTK_WINDOW(ctx->window);
    gchar* local_path = g_build_filename(ctx->app_dir, "help", "index.php", NULL);
    gboolean have_local = g_file_test(local_path, G_FILE_TEST_IS_REGULAR);

    if (have_local) {
        GError* conv_err = NULL;
        gchar* file_uri = g_filename_to_uri(local_path, NULL, &conv_err);
        if (file_uri) {
            GError* show_err = NULL;
            gboolean opened = gtk_show_uri_on_window(win, file_uri, GDK_CURRENT_TIME, &show_err);
            g_free(file_uri);
            if (opened) {
                g_free(local_path);
                if (show_err) {
                    g_error_free(show_err);
                }
                return;
            }
            if (show_err) {
                g_error_free(show_err);
            }
        } else {
            show_uri_error(win, _("Could not open the help page."),
                           conv_err ? conv_err->message : local_path);
            if (conv_err) {
                g_error_free(conv_err);
            }
        }
    }

    const gchar* base = (ctx->settings) ? settings_get_help_online_base_url(ctx->settings) : NULL;
    gchar* http_url = build_help_index_http_url(base);
    if (http_url) {
        if (open_uri_with_feedback(win, http_url)) {
            g_free(http_url);
            g_free(local_path);
            return;
        }
        g_free(http_url);
    }

    if (!have_local && !base) {
        show_uri_error(
            win,
            _("The user guide was not found."),
            _("Install the help folder next to the application, or set an online help base URL in settings.xml (help_online_base_url)."));
    } else if (have_local && !base) {
        show_uri_error(
            win,
            _("Could not open the user guide."),
            _("No online help URL is configured. Add help_online_base_url under the ui section in settings.xml if you use a web mirror."));
    }

    g_free(local_path);
}

static void on_help_about_activate(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->window) {
        return;
    }

    GtkBuilder* builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    GError* error = NULL;
    if (!gtk_builder_add_from_resource(builder, "/ui/about_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load about_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return;
    }

    GtkWidget* dialog = GTK_WIDGET(gtk_builder_get_object(builder, "help_about_dialog"));
    if (!dialog) {
        debug_log("WRN", "about_dialog.glade: missing help_about_dialog");
        g_object_unref(builder);
        return;
    }

    /* Version from version.h; build number from generated build_version.h. */
#if RASTERLAB_BUILD_NUMBER != 0
    gchar* version_line = g_strdup_printf(_("%s (Build %d)"), RASTERLAB_VERSION_FOR_DISPLAY, RASTERLAB_BUILD_NUMBER);
#else
    gchar* version_line = g_strdup(RASTERLAB_VERSION_FOR_DISPLAY);
#endif
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), version_line);
    g_free(version_line);

    GError* pix_err = NULL;
    GdkPixbuf* logo = gdk_pixbuf_new_from_resource("/icons/rasterlab-logo.png", &pix_err);
    if (logo) {
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), logo);
        g_object_unref(logo);
    } else {
        debug_log("WRN", "Could not load about dialog logo: %s", pix_err ? pix_err->message : "?");
        if (pix_err) {
            g_error_free(pix_err);
        }
    }

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(ctx->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(builder);
}

void ui_help_menu_setup(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* help_menu = GTK_WIDGET(gtk_builder_get_object(builder, "help_menu"));
    GtkWidget* help_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "help_menu_item"));

    if (help_menu && help_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_menu_item), help_menu);
    }

    GtkWidget* help_menu_user_guide = GTK_WIDGET(gtk_builder_get_object(builder, "help_menu_user_guide"));
    if (help_menu_user_guide) {
        g_signal_connect(help_menu_user_guide, "activate", G_CALLBACK(on_help_user_guide_activate), ctx);
    }

    GtkWidget* help_menu_about = GTK_WIDGET(gtk_builder_get_object(builder, "help_menu_about"));
    if (help_menu_about) {
        g_signal_connect(help_menu_about, "activate", G_CALLBACK(on_help_about_activate), ctx);
    }
}
