#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo.h>

/**
 * Structure to represent a single layer in an image
 */
typedef struct {
    gchar *name;                  /* Layer name */
    cairo_surface_t *surface;     /* Layer content */
    gdouble opacity;              /* Layer opacity (0.0 - 1.0) */
    gboolean visible;             /* Is layer visible? */
    guint width;                  /* Layer width in pixels */
    guint height;                 /* Layer height in pixels */
} ImageLayer;

/**
 * Structure to represent an image document
 */
typedef struct {
    gchar *filename;              /* File path/name */
    gchar *file_path;             /* Full file path for loading/saving */
    gboolean modified;            /* Has document been modified? */
    GtkWidget *drawing_area;      /* Associated drawing area widget */
    GtkWidget *scrolled_window;   /* Parent scrolled window */
    
    /* Image metadata */
    guint width;                  /* Image width in pixels */
    guint height;                 /* Image height in pixels */
    guint channels;               /* Number of color channels (3 or 4) */
    guint bit_depth;              /* Bits per channel (usually 8) */
    gboolean has_alpha;           /* Whether image has alpha channel */
    
    /* Rendering pipeline */
    GList *layers;                /* List of ImageLayer objects */
    cairo_surface_t *composite_surface;  /* Cached composite surface */
    gboolean composite_dirty;     /* Does composite need re-rendering? */
    
    /* Viewport and zoom */
    gdouble zoom_factor;          /* Current zoom level (1.0 = 100%) */
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

/**
 * Set zoom factor for the document
 * @param doc The document
 * @param zoom_factor Zoom level (1.0 = 100%, 2.0 = 200%, 0.5 = 50%)
 */
void document_set_zoom(ImageDocument *doc, gdouble zoom_factor);

/**
 * Get current zoom factor
 * @param doc The document
 * @return Current zoom level
 */
gdouble document_get_zoom(ImageDocument *doc);

/**
 * Render all layers to composite surface
 * @param doc The document
 * @return TRUE if successful
 */
gboolean document_render_composite(ImageDocument *doc);

/**
 * Get the composite rendered surface
 * @param doc The document
 * @return Cairo surface or NULL
 */
cairo_surface_t* document_get_composite_surface(ImageDocument *doc);

/**
 * Mark composite surface as needing re-render
 * @param doc The document
 */
void document_invalidate_composite(ImageDocument *doc);

#endif /* DOCUMENT_H */

