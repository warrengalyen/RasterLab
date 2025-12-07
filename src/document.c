#include "document.h"
#include <stdlib.h>
#include <string.h>

/**
 * Drawing area draw callback
 */
static gboolean on_drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;

    /* Set white background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Draw the document surface if available */
    if (doc->surface) {
        cairo_set_source_surface(cr, doc->surface, 0, 0);
        cairo_paint(cr);
    } else {
        /* Draw a placeholder grid */
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_set_line_width(cr, 0.5);
        
        for (int x = 0; x < 800; x += 50) {
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, 600);
        }
        
        for (int y = 0; y < 600; y += 50) {
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, 800, y);
        }
        
        cairo_stroke(cr);
    }

    return FALSE;
}

/**
 * Create a new image document
 */
ImageDocument* document_new(const gchar *filename)
{
    ImageDocument *doc = (ImageDocument *)g_malloc(sizeof(ImageDocument));

    doc->filename = g_strdup(filename);
    doc->surface = NULL;
    doc->modified = FALSE;
    doc->drawing_area = NULL;
    doc->scrolled_window = NULL;

    return doc;
}

/**
 * Free an image document
 */
void document_free(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    if (doc->filename) {
        g_free(doc->filename);
    }

    if (doc->surface) {
        cairo_surface_destroy(doc->surface);
    }

    g_free(doc);
}

/**
 * Create a drawing area widget for the document
 */
GtkWidget* document_create_drawing_area(ImageDocument *doc)
{
    GtkWidget *scrolled_window;
    GtkWidget *drawing_area;
    GtkAdjustment *h_adjustment;
    GtkAdjustment *v_adjustment;

    /* Create scrolled window */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    /* Create drawing area */
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 800, 600);
    gtk_container_add(GTK_CONTAINER(scrolled_window), drawing_area);

    /* Connect draw signal */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), doc);

    /* Store references in document */
    doc->drawing_area = drawing_area;
    doc->scrolled_window = scrolled_window;

    return scrolled_window;
}

/**
 * Set the document as modified
 */
void document_set_modified(ImageDocument *doc, gboolean modified)
{
    if (!doc) {
        return;
    }

    doc->modified = modified;
}

/**
 * Get the document filename
 */
const gchar* document_get_filename(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    return doc->filename;
}

