#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <gtk/gtk.h>
#include <cairo.h>

/**
 * Structure to represent an image document
 */
typedef struct {
    gchar *filename;              /* File path/name */
    cairo_surface_t *surface;     /* Cairo surface for rendering */
    gboolean modified;            /* Has document been modified? */
    GtkWidget *drawing_area;      /* Associated drawing area widget */
    GtkWidget *scrolled_window;   /* Parent scrolled window */
} ImageDocument;

/**
 * Create a new image document
 * @param filename The filename for the document
 * @return Newly allocated ImageDocument
 */
ImageDocument* document_new(const gchar *filename);

/**
 * Free an image document
 * @param doc The document to free
 */
void document_free(ImageDocument *doc);

/**
 * Create a drawing area widget for the document
 * @param doc The document
 * @return The drawing area widget
 */
GtkWidget* document_create_drawing_area(ImageDocument *doc);

/**
 * Set the document as modified
 * @param doc The document
 * @param modified Whether the document has been modified
 */
void document_set_modified(ImageDocument *doc, gboolean modified);

/**
 * Get the document filename
 * @param doc The document
 * @return The filename
 */
const gchar* document_get_filename(ImageDocument *doc);

#endif /* DOCUMENT_H */

