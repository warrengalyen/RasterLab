#include <gtk/gtk.h>
#include <stdio.h>

/**
 * Callback for GTK window delete event
 */
static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget;  /* Unused parameter */
    (void)event;   /* Unused parameter */
    (void)data;    /* Unused parameter */
    gtk_main_quit();
    return FALSE;
}

/**
 * Application initialization
 */
int main(int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *vbox;
    GtkWidget *label;

    /* Print GTK version information */
    printf("GTK Version: %d.%d.%d\n",
           gtk_get_major_version(),
           gtk_get_minor_version(),
           gtk_get_micro_version());

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    /* Create main window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Image Editor");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    /* Create main container */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Create welcome label */
    label = gtk_label_new("Welcome to Image Editor\n\nReady for future expansion");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);

    /* Connect signals */
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete), NULL);

    /* Show all widgets */
    gtk_widget_show_all(window);

    /* Start GTK main event loop */
    gtk_main();

    return 0;
}

