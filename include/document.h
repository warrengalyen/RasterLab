#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo.h>

/**
 * Structure to represent an image document
 */
typedef struct {
    gchar *filename;              /* File path/name */
    gchar *file_path;             /* Full file path for loading/saving */
    cairo_surface_t *surface;     /* Cairo surface for rendering */
    gboolean modified;            /* Has document been modified? */
    GtkWidget *drawing_area;      /* Associated drawing area widget */
    GtkWidget *scrolled_window;   /* Parent scrolled window */
    
    /* Image metadata */
    guint width;                  /* Image width in pixels */
    guint height;                 /* Image height in pixels */
    guint channels;               /* Number of color channels (3 or 4) */
    guint bit_depth;              /* Bits per channel (usually 8) */
    gboolean has_alpha;           /* Whether image has alpha channel */
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

/**
 * Load an image from file into the document
 * @param doc The document
 * @param file_path The path to the image file
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_load_image_from_file(ImageDocument *doc, const gchar *file_path);

/**
 * Get image width
 * @param doc The document
 * @return Width in pixels
 */
guint document_get_width(ImageDocument *doc);

/**
 * Get image height
 * @param doc The document
 * @return Height in pixels
 */
guint document_get_height(ImageDocument *doc);

/**
 * Get image metadata string
 * @param doc The document
 * @return Allocated string with image info (must be freed)
 */
gchar* document_get_image_info(ImageDocument *doc);

#endif /* DOCUMENT_H */

