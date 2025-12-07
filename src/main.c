#include <gtk/gtk.h>
#include <stdio.h>
#include "ui.h"

/**
 * Application initialization
 */
int main(int argc, char *argv[])
{
    AppContext *app;

    /* Print GTK version information */
    printf("GTK Version: %d.%d.%d\n",
           gtk_get_major_version(),
           gtk_get_minor_version(),
           gtk_get_micro_version());

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    /* Create the main application UI */
    app = ui_create_main_window();

    /* Start GTK main event loop (no initial document) */
    gtk_main();

    return 0;
}

