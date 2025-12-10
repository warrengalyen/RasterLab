#include <gtk/gtk.h>
#include <stdio.h>
#include <math.h>
#include "ui.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * Create a test dialog for filter preview widget
 */
static void test_filter_preview_dialog(void)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    FilterPreview *preview;
    cairo_surface_t *before_surface;
    cairo_surface_t *after_surface;
    cairo_t *cr;
    gint width = 800;
    gint height = 600;
    gint x, y;

    /* Create dialog window */
    dialog = gtk_dialog_new_with_buttons("Filter Preview Test",
                                         NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close",
                                         GTK_RESPONSE_CLOSE,
                                         NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 900, 700);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Create filter preview widget */
    preview = FILTER_PREVIEW(filter_preview_new());
    gtk_container_add(GTK_CONTAINER(content_area), GTK_WIDGET(preview));

    /* Create "before" test image - colorful gradient pattern */
    before_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(before_surface);
    
    /* Draw a colorful gradient pattern */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble r = (gdouble)x / (gdouble)width;
            gdouble g = (gdouble)y / (gdouble)height;
            gdouble b = 0.5;
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }
    
    /* Add some shapes for visual interest */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);
    
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);
    
    cairo_destroy(cr);
    cairo_surface_flush(before_surface);

    /* Create "after" test image - grayscale version */
    after_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(after_surface);
    
    /* Draw grayscale gradient pattern */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble gray = ((gdouble)x / (gdouble)width + (gdouble)y / (gdouble)height) / 2.0;
            cairo_set_source_rgb(cr, gray, gray, gray);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }
    
    /* Add same shapes but in grayscale */
    cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);
    
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);
    
    cairo_destroy(cr);
    cairo_surface_flush(after_surface);

    /* Set surfaces in preview widget */
    filter_preview_set_before_surface(preview, before_surface);
    filter_preview_set_after_surface(preview, after_surface);

    /* Connect response handler to clean up surfaces */
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);

    /* Show dialog */
    gtk_widget_show_all(dialog);
    
    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dialog));
    
    /* Clean up surfaces */
    if (before_surface) {
        cairo_surface_destroy(before_surface);
    }
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
    
    gtk_widget_destroy(dialog);
}

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

    /* Test filter preview widget */
    test_filter_preview_dialog();

    /* Create the main application UI */
    app = ui_create_main_window();

    /* Start GTK main event loop (no initial document) */
    gtk_main();

    return 0;
}

