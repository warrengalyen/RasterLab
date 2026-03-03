#include "app/autosave.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "plugins/builtin_plugins.h"
#include "plugins/format_registry.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_loader.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "test_widgets.h"
#include "ui.h"
#include "ui/swatches.h"
#include <cairo.h>
#include <gtk/gtk.h>
#include <stdio.h>

static void load_global_css(void) {
    GtkCssProvider* provider = gtk_css_provider_new();

    gtk_css_provider_load_from_resource(provider, "/css/app.css");
    GdkScreen* screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
/**
 * Set GDK_PIXBUF_MODULE_FILE to loaders.cache next to the executable so pixbuf loaders
 * are found when running from Windows cmd regardless of cwd.
 */
static void setup_gdk_pixbuf_loaders(void) {
#ifdef _WIN32
    gchar* exe_dir = settings_get_executable_dir();
    if (exe_dir) {
        gchar* loaders_dir = g_build_filename(
            exe_dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders", NULL);
        gchar* cache_path = g_build_filename(
            exe_dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache", NULL);
        if (g_file_test(loaders_dir, G_FILE_TEST_IS_DIR) && g_file_test(cache_path, G_FILE_TEST_EXISTS)) {
            /* Absolute paths so resolution works from any cwd */
            gchar* abs_loaders = g_canonicalize_filename(loaders_dir, NULL);
            gchar* abs_cache = g_canonicalize_filename(cache_path, NULL);
            if (abs_loaders && abs_cache) {
                g_setenv("GDK_PIXBUF_MODULEDIR", abs_loaders, TRUE);
                g_setenv("GDK_PIXBUF_MODULE_FILE", abs_cache, TRUE);
            }
            g_free(abs_loaders);
            g_free(abs_cache);
        }
        g_free(loaders_dir);
        g_free(cache_path);
        g_free(exe_dir);
    }
#endif
}

/**
 * Application initialization
 */
int main(int argc, char* argv[]) {
    AppContext* app;
    gchar* app_dir;

    /* Set pixbuf loaders path before gtk_init so loaders work from any cwd (Windows cmd) */
    setup_gdk_pixbuf_loaders();

    /* Print GTK version information */
    printf("GTK Version: %d.%d.%d\n",
           gtk_get_major_version(),
           gtk_get_minor_version(),
           gtk_get_micro_version());

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    load_global_css();

    GBytes* bytes = g_resources_lookup_data(
        "/css/app.css",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        NULL);
    g_assert(bytes != NULL);

    /* Initialize plugin system */
    plugin_loader_init();
    format_registry_init();

    /* Register built-in plugins (PNG, JPEG) */
    builtin_plugins_register();

    /* TODO: Scan and load plugins from ./plugins/ and ./plugins/formats/ directories */
    /* For now, we only use built-in plugins */

    app_dir = settings_get_executable_dir();

    autosave_init();

    // test_color_chooser();

    // test_filter_preview_dialog();

    // test_filter_dialog();

    // test_curves_widget();
    // test_anchor_position_widget();

    /* Create the main application UI (will load settings) */
    app = ui_create_main_window();
    if (app) {
        /* Store app directory in context */
        app->app_dir = app_dir;

        /* Load swatches from file (widgets are created in ui_create_main_window, 
         * and they sync when created, but we also sync here to ensure they're updated) */
        if (app->app_dir) {
            swatches_load(&app->swatches, app->app_dir);
            /* Sync to widgets if they already exist (they should, since layers panel is created in ui_create_main_window) */
            GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(app->window), "main_swatches_widget");
            GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(app->window), "recent_colors_widget");
            if (main_widget || recent_widget) {
                swatches_sync_to_widgets(&app->swatches, main_widget, recent_widget);
            }
        }

        /* Load settings */
        app->settings = settings_load(app_dir);
        if (app->settings) {
            /* Connect recent_files system to settings */
            recent_files_set_settings(app->settings);
            /* Initialize and load recent files from settings XML */
            recent_files_init();

            /* Set file recovery (autosave) interval from settings */
            autosave_set_interval((guint)settings_get_file_recovery_interval_seconds(app->settings));

            /* Update recent files menu after loading */
            ui_update_recent_files_menu(app);

            /* Apply canvas background color from settings */
            gdouble r, g, b;
            settings_get_canvas_background(app->settings, &r, &g, &b);
            ui_set_canvas_background_color(app, r, g, b);

            /* Apply alpha (transparency) checkerboard options from settings */
            render_utils_set_alpha_check_from_settings(app->settings);

            /* Load tool options from settings */
            ui_load_tool_options_from_settings(app);

            /* Pass settings to plugin host for color management (use embedded ICC, etc.) */
            plugin_host_api_set_cm_settings(app->settings);
        }
    } else {
        g_free(app_dir);
    }

    /* Start GTK main event loop (no initial document) */
    /* Settings are saved in on_window_delete/on_file_exit handlers before cleanup */
    gtk_main();

    /* Cleanup - settings should already be saved by window delete/exit handlers */
    /* But if we get here without going through those handlers, try to save */
    if (app && app->settings && app->app_dir) {
        /* Sync recent files to settings before saving */
        recent_files_save();

        /* Save all current tool options to settings */
        if (app->tool_registry) {
            ui_save_all_tool_options_to_settings(app);
        }

        /* Sync widgets to swatches data before saving */
        GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(app->window), "main_swatches_widget");
        GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(app->window), "recent_colors_widget");
        swatches_sync_from_widgets(&app->swatches, main_widget, recent_widget);
        /* Save swatches to file */
        swatches_save(&app->swatches, app->app_dir);

        /* Save all settings to file */
        settings_save(app->settings, app->app_dir);
    }

    /* Shutdown recent files system */
    recent_files_shutdown();

    /* Shutdown autosave system */
    autosave_shutdown();

    /* Shutdown plugin system */
    format_registry_shutdown();
    plugin_loader_shutdown();

    /* Cleanup - free context and all resources */
    if (app) {
        ui_context_free(app);
    }

    return 0;
}
