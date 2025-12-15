#include "ui/widgets/filter_dialog.h"
#include "ui/widgets/filter_preview.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * Create a test dialog for filter preview widget
 */
static void test_filter_preview_dialog(void) {
    GtkWidget* dialog;
    GtkWidget* content_area;
    FilterPreview* preview;
    cairo_surface_t* before_surface;
    cairo_surface_t* after_surface;
    cairo_t* cr;
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
 * Create a test dialog for filter dialog widget
 */
static void test_filter_dialog(void) {
    FilterDialog* dialog;
    FilterControlParam controls[3];
    ImageLayer* before_layer;
    ImageLayer* after_layer;
    cairo_t* cr;
    gint width = 800;
    gint height = 600;
    gint x, y;
    gdouble values[3];
    gint response;
    gint i;

    /* Define control parameters */
    controls[0].label = "vibrance";
    controls[0].min_value = -100.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;

    controls[1].label = "saturation";
    controls[1].min_value = -100.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;

    controls[2].label = "brightness";
    controls[2].min_value = -100.0;
    controls[2].max_value = 100.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;

    /* Create filter dialog */
    dialog = filter_dialog_new("Filter Test", controls, 3);
    if (!dialog) {
        g_warning("Failed to create filter dialog");
        return;
    }

    /* Create test layers */
    before_layer = layer_new("Before", width, height, TRUE);
    after_layer = layer_new("After", width, height, TRUE);

    if (!before_layer || !after_layer) {
        g_warning("Failed to create test layers");
        filter_dialog_free(dialog);
        if (before_layer)
            layer_free(before_layer);
        if (after_layer)
            layer_free(after_layer);
        return;
    }

    /* Create "before" test image - colorful gradient pattern */
    cr = cairo_create(before_layer->surface);

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
    cairo_surface_flush(before_layer->surface);

    /* Create "after" test image - slightly modified version */
    cr = cairo_create(after_layer->surface);

    /* Draw similar pattern but with slight modification */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble r = (gdouble)x / (gdouble)width;
            gdouble g = (gdouble)y / (gdouble)height;
            gdouble b = 0.6; /* Slightly different */
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }

    /* Add same shapes */
    cairo_set_source_rgba(cr, 0.95, 0.95, 0.95, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(after_layer->surface);

    /* Set layers in dialog */
    filter_dialog_set_layers(dialog, before_layer, after_layer);

    /* Run dialog */
    response = filter_dialog_run(dialog, NULL, values, 3);

    if (response == GTK_RESPONSE_OK) {
        printf("Filter dialog values:\n");
        for (i = 0; i < 3; i++) {
            printf("  %s: %.0f\n", controls[i].label, values[i]);
        }
    } else {
        printf("Filter dialog cancelled\n");
    }

    /* Clean up */
    filter_dialog_free(dialog);
    layer_free(before_layer);
    layer_free(after_layer);
}
