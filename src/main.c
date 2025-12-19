#include "app/autosave.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "render/layer.h"
#include "test_widgets.h"
#include "ui.h"
#include <cairo.h>
#include <gtk/gtk.h>
#include <stdio.h>

/**
 * Application initialization
 */
int main(int argc, char* argv[]) {
    AppContext* app;
    gchar* app_dir;

    /* Print GTK version information */
    printf("GTK Version: %d.%d.%d\n",
           gtk_get_major_version(),
           gtk_get_minor_version(),
           gtk_get_micro_version());

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    app_dir = settings_get_executable_dir();

    // autosave_init();

    // test_filter_preview_dialog();

    // test_filter_dialog();

    // test_curves_widget();
    // test_anchor_position_widget();

    /* Create the main application UI (will load settings) */
    app = ui_create_main_window();
    if (app) {
        /* Store app directory in context */
        app->app_dir = app_dir;

        /* Load settings */
        app->settings = settings_load(app_dir);
        if (app->settings) {
            /* Connect recent_files system to settings */
            recent_files_set_settings(app->settings);
            /* Initialize and load recent files from settings XML */
            recent_files_init();

            /* Update recent files menu after loading */
            ui_update_recent_files_menu(app);

            /* Apply canvas background color from settings */
            gdouble r, g, b;
            settings_get_canvas_background(app->settings, &r, &g, &b);
            ui_set_canvas_background_color(app, r, g, b);

            /* Load tool options from settings */
            ui_load_tool_options_from_settings(app);
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

        /* Save all settings to file */
        settings_save(app->settings, app->app_dir);
    }

    /* Shutdown recent files system */
    recent_files_shutdown();

    /* Shutdown autosave system */
    autosave_shutdown();

    /* Cleanup - free context and all resources */
    if (app) {
        ui_context_free(app);
    }

    return 0;
}
