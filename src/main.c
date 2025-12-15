#include "app/recent_files.h"
#include "render/layer.h"
#include "test_widgets.h"
#include "ui.h"
#include <cairo.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>

/**
 * Application initialization
 */
int main(int argc, char* argv[]) {
    AppContext* app;

    /* Print GTK version information */
    printf("GTK Version: %d.%d.%d\n",
           gtk_get_major_version(),
           gtk_get_minor_version(),
           gtk_get_micro_version());

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    /* Initialize recent files system */
    recent_files_init();

    /* Test filter preview widget */
    // test_filter_preview_dialog();

    /* Test filter dialog */
    // test_filter_dialog();

    /* Create the main application UI */
    app = ui_create_main_window();

    /* Start GTK main event loop (no initial document) */
    gtk_main();

    return 0;
}
