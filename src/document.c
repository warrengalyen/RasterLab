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
 * Draw a checkered background pattern for transparency
 */
static void draw_checkered_background(cairo_t *cr, gint x, gint y, gint width, gint height)
{
    const gint square_size = 10;    /* Size of each check square */
    const double color1 = 0.85;    /* Light gray */
    const double color2 = 0.95;    /* Lighter gray */
    gint end_x = x + width;
    gint end_y = y + height;

    /* Draw checkerboard pattern */
    for (gint row = y; row < end_y; row += square_size) {
        for (gint col = x; col < end_x; col += square_size) {
            /* Calculate which "cell" we're in */
            gint cell_x = col / square_size;
            gint cell_y = row / square_size;
            
            /* Alternate colors in a checkerboard pattern */
            double color = ((cell_x + cell_y) % 2 == 0) ? color1 : color2;

            cairo_set_source_rgb(cr, color, color, color);
            cairo_rectangle(cr, col, row, square_size, square_size);
            cairo_fill(cr);
        }
    }
}

/**
 * Drawing area draw callback
 */
static gboolean on_drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    cairo_surface_t *composite;
    double x1, y1, x2, y2;
    gint clip_width, clip_height;

    /* Get the clip region to determine what needs to be drawn */
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    clip_width = (gint)(x2 - x1);
    clip_height = (gint)(y2 - y1);

    /* Draw the document if image is loaded */
    if (doc->layers && g_list_length(doc->layers) > 0) {
        /* Get the composite rendered surface */
        composite = document_get_composite_surface(doc);

        if (composite) {
            /* Draw checkered background for transparency */
            if (doc->has_alpha) {
                draw_checkered_background(cr, (gint)x1, (gint)y1, clip_width, clip_height);
            } else {
                /* Draw white background for opaque images */
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                cairo_paint(cr);
            }

            /* Apply zoom and draw composite surface */
            if (doc->zoom_factor != 1.0) {
                cairo_scale(cr, doc->zoom_factor, doc->zoom_factor);
            }

            cairo_set_source_surface(cr, composite, 0, 0);
            cairo_paint(cr);
        }
    } else {
        /* Draw white background for empty canvas */
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);

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
 * Create a new image layer
 */
static ImageLayer* layer_new(const gchar *name, guint width, guint height, gboolean has_alpha)
{
    ImageLayer *layer = (ImageLayer *)g_malloc(sizeof(ImageLayer));

    layer->name = g_strdup(name);
    
    /* Create layer surface */
    cairo_format_t format = has_alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    layer->surface = cairo_image_surface_create(format, width, height);
    
    /* Clear with transparent black if has alpha, white otherwise */
    cairo_t *cr = cairo_create(layer->surface);
    if (has_alpha) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    }
    cairo_paint(cr);
    cairo_destroy(cr);

    layer->opacity = 1.0;
    layer->visible = TRUE;
    layer->width = width;
    layer->height = height;

    return layer;
}

/**
 * Free an image layer
 */
static void layer_free(ImageLayer *layer)
{
    if (!layer) {
        return;
    }

    if (layer->name) {
        g_free(layer->name);
    }

    if (layer->surface) {
        cairo_surface_destroy(layer->surface);
    }

    g_free(layer);
}

/**
 * Create a new image document
 */
ImageDocument* document_new(const gchar *filename)
{
    ImageDocument *doc = (ImageDocument *)g_malloc(sizeof(ImageDocument));

    doc->filename = g_strdup(filename);
    doc->file_path = NULL;
    doc->modified = FALSE;
    doc->drawing_area = NULL;
    doc->scrolled_window = NULL;

    /* Initialize image metadata */
    doc->width = 0;
    doc->height = 0;
    doc->channels = 0;
    doc->bit_depth = 0;
    doc->has_alpha = FALSE;

    /* Initialize rendering pipeline */
    doc->layers = NULL;
    doc->composite_surface = NULL;
    doc->composite_dirty = TRUE;
    doc->zoom_factor = 1.0;

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

    /* Free all layers */
    for (GList *iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer *)iter->data);
    }
    g_list_free(doc->layers);

    /* Free composite surface */
    if (doc->composite_surface) {
        cairo_surface_destroy(doc->composite_surface);
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

    /* Store metadata first */
    doc->width = gdk_pixbuf_get_width(pixbuf);
    doc->height = gdk_pixbuf_get_height(pixbuf);
    doc->channels = gdk_pixbuf_get_n_channels(pixbuf);
    doc->bit_depth = 8;  /* GdkPixbuf always uses 8 bits per channel */
    doc->has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    /* Free old layers if exists */
    for (GList *iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer *)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Create base layer from loaded image */
    ImageLayer *base_layer = layer_new("Base", doc->width, doc->height, doc->has_alpha);
    
    /* Convert pixbuf to Cairo surface and copy to layer */
    cairo_surface_t *temp_surface = pixbuf_to_cairo_surface(pixbuf);
    if (!temp_surface) {
        g_object_unref(pixbuf);
        layer_free(base_layer);
        return FALSE;
    }

    /* Copy temp surface to layer surface */
    cairo_t *cr = cairo_create(base_layer->surface);
    cairo_set_source_surface(cr, temp_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(temp_surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

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

    return g_strdup_printf("%ux%u, %d-bit %s%s (zoom: %.0f%%)",
                          doc->width, doc->height,
                          doc->bit_depth,
                          doc->channels == 3 ? "RGB" : "RGBA",
                          doc->has_alpha ? " (with alpha)" : "",
                          doc->zoom_factor * 100.0);
}

/**
 * Set zoom factor for the document
 */
void document_set_zoom(ImageDocument *doc, gdouble zoom_factor)
{
    if (!doc) {
        return;
    }

    /* Clamp zoom to reasonable range (10% - 400%) */
    if (zoom_factor < 0.1) {
        zoom_factor = 0.1;
    } else if (zoom_factor > 4.0) {
        zoom_factor = 4.0;
    }

    doc->zoom_factor = zoom_factor;

    /* Update drawing area and trigger redraw */
    if (doc->drawing_area) {
        gint scaled_width = (gint)(doc->width * zoom_factor);
        gint scaled_height = (gint)(doc->height * zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, scaled_width, scaled_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Get current zoom factor
 */
gdouble document_get_zoom(ImageDocument *doc)
{
    if (!doc) {
        return 1.0;
    }

    return doc->zoom_factor;
}

/**
 * Render all layers to composite surface
 */
gboolean document_render_composite(ImageDocument *doc)
{
    cairo_t *cr;
    GList *iter;
    ImageLayer *layer;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    /* Create composite surface if needed */
    if (!doc->composite_surface) {
        doc->composite_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    }

    if (cairo_surface_status(doc->composite_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create composite surface");
        return FALSE;
    }

    /* Clear composite surface */
    cr = cairo_create(doc->composite_surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    /* Composite each visible layer */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer *)iter->data;

        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Draw layer with opacity */
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        if (layer->opacity < 1.0) {
            cairo_paint_with_alpha(cr, layer->opacity);
        } else {
            cairo_paint(cr);
        }
    }

    cairo_destroy(cr);
    doc->composite_dirty = FALSE;

    return TRUE;
}

/**
 * Get the composite rendered surface
 */
cairo_surface_t* document_get_composite_surface(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    /* Re-render if composite is dirty */
    if (doc->composite_dirty) {
        document_render_composite(doc);
    }

    return doc->composite_surface;
}

/**
 * Mark composite surface as needing re-render
 */
void document_invalidate_composite(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    doc->composite_dirty = TRUE;

    /* Trigger redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

