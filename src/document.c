#include "document.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Convert GdkPixbuf to Cairo image surface
 */
static cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf *pixbuf)
{
    cairo_surface_t *surface;
    cairo_t *cr;
    gint width, height;
    gint rowstride;
    guchar *pixels;
    guint n_channels;
    gint y;

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    n_channels = gdk_pixbuf_get_n_channels(pixbuf);

    /* Create RGB or ARGB surface depending on alpha channel */
    cairo_format_t format = gdk_pixbuf_get_has_alpha(pixbuf) 
                            ? CAIRO_FORMAT_ARGB32 
                            : CAIRO_FORMAT_RGB24;
    surface = cairo_image_surface_create(format, width, height);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create Cairo surface");
        return NULL;
    }

    /* Copy pixel data row by row */
    guchar *surface_data = cairo_image_surface_get_data(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);

    for (y = 0; y < height; y++) {
        guchar *src_row = pixels + y * rowstride;
        guchar *dst_row = surface_data + y * surface_stride;

        if (n_channels == 3) {
            /* RGB to BGRX (Cairo RGB24) */
            for (int x = 0; x < width; x++) {
                dst_row[4 * x + 0] = src_row[3 * x + 2];  /* B */
                dst_row[4 * x + 1] = src_row[3 * x + 1];  /* G */
                dst_row[4 * x + 2] = src_row[3 * x + 0];  /* R */
                dst_row[4 * x + 3] = 0xFF;                /* X (opaque) */
            }
        } else if (n_channels == 4) {
            /* RGBA to BGRA (Cairo ARGB32) */
            for (int x = 0; x < width; x++) {
                dst_row[4 * x + 0] = src_row[4 * x + 2];  /* B */
                dst_row[4 * x + 1] = src_row[4 * x + 1];  /* G */
                dst_row[4 * x + 2] = src_row[4 * x + 0];  /* R */
                dst_row[4 * x + 3] = src_row[4 * x + 3];  /* A */
            }
        }
    }

    cairo_surface_mark_dirty(surface);

    return surface;
}

/**
 * Drawing area draw callback
 */
static gboolean on_drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    double scale_x = 1.0;
    double scale_y = 1.0;
    GtkAllocation allocation;

    /* Set white background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Draw the document surface if available */
    if (doc->surface) {
        /* Get widget allocation to check for scaling */
        gtk_widget_get_allocation(widget, &allocation);

        /* Calculate scaling to fit in visible area (optional) */
        if (doc->width > 0 && doc->height > 0) {
            if (allocation.width > 0 && allocation.height > 0) {
                double w_scale = (double)allocation.width / doc->width;
                double h_scale = (double)allocation.height / doc->height;
                scale_x = (w_scale < h_scale) ? w_scale : 1.0;
                scale_y = (h_scale < w_scale) ? h_scale : 1.0;
            }
        }

        /* Draw image at natural size (scrollbars handle panning) */
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
    doc->file_path = NULL;
    doc->surface = NULL;
    doc->modified = FALSE;
    doc->drawing_area = NULL;
    doc->scrolled_window = NULL;

    /* Initialize image metadata */
    doc->width = 0;
    doc->height = 0;
    doc->channels = 0;
    doc->bit_depth = 0;
    doc->has_alpha = FALSE;

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

    if (doc->file_path) {
        g_free(doc->file_path);
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
    gtk_widget_show(drawing_area);

    /* Connect draw signal */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), doc);

    /* Store references in document */
    doc->drawing_area = drawing_area;
    doc->scrolled_window = scrolled_window;

    gtk_widget_show(scrolled_window);

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

/**
 * Load an image from file into the document
 */
gboolean document_load_image_from_file(ImageDocument *doc, const gchar *file_path)
{
    GdkPixbuf *pixbuf;
    GError *error = NULL;
    gchar *basename;

    if (!doc || !file_path) {
        return FALSE;
    }

    /* Load image file with GdkPixbuf */
    pixbuf = gdk_pixbuf_new_from_file(file_path, &error);

    if (!pixbuf) {
        g_warning("Failed to load image: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    /* Free old surface if exists */
    if (doc->surface) {
        cairo_surface_destroy(doc->surface);
    }

    /* Convert pixbuf to Cairo surface */
    doc->surface = pixbuf_to_cairo_surface(pixbuf);

    if (!doc->surface) {
        g_object_unref(pixbuf);
        return FALSE;
    }

    /* Store metadata */
    doc->width = gdk_pixbuf_get_width(pixbuf);
    doc->height = gdk_pixbuf_get_height(pixbuf);
    doc->channels = gdk_pixbuf_get_n_channels(pixbuf);
    doc->bit_depth = 8;  /* GdkPixbuf always uses 8 bits per channel */
    doc->has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    /* Update filename to basename */
    basename = g_path_get_basename(file_path);
    g_free(doc->filename);
    doc->filename = basename;

    /* Store full file path */
    if (doc->file_path) {
        g_free(doc->file_path);
    }
    doc->file_path = g_strdup(file_path);

    /* Update drawing area size */
    if (doc->drawing_area) {
        gtk_widget_set_size_request(doc->drawing_area, doc->width, doc->height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    g_object_unref(pixbuf);

    printf("Loaded image: %s (%ux%u, %u channels, alpha=%s)\n",
           doc->filename, doc->width, doc->height, doc->channels,
           doc->has_alpha ? "yes" : "no");

    return TRUE;
}

/**
 * Get image width
 */
guint document_get_width(ImageDocument *doc)
{
    if (!doc) {
        return 0;
    }

    return doc->width;
}

/**
 * Get image height
 */
guint document_get_height(ImageDocument *doc)
{
    if (!doc) {
        return 0;
    }

    return doc->height;
}

/**
 * Get image metadata string
 */
gchar* document_get_image_info(ImageDocument *doc)
{
    if (!doc || doc->width == 0) {
        return g_strdup("No image loaded");
    }

    return g_strdup_printf("%ux%u, %d-bit %s%s",
                          doc->width, doc->height,
                          doc->bit_depth,
                          doc->channels == 3 ? "RGB" : "RGBA",
                          doc->has_alpha ? " (with alpha)" : "");
}

