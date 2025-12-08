#ifndef RENDER_UTILS_H
#define RENDER_UTILS_H

#include <gtk/gtk.h>
#include <cairo/cairo.h>

/**
 * Convert GdkPixbuf to Cairo image surface
 * @param pixbuf The GdkPixbuf to convert
 * @return A new Cairo surface, or NULL on error. Caller must call cairo_surface_destroy().
 */
cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf *pixbuf);

/**
 * Convert Cairo image surface to GdkPixbuf
 * @param surface The Cairo surface to convert
 * @param keep_alpha Whether to preserve the alpha channel
 * @return A new GdkPixbuf, or NULL on error. Caller must call g_object_unref().
 */
GdkPixbuf* cairo_surface_to_pixbuf(cairo_surface_t *surface, gboolean keep_alpha);

/**
 * Draw a checkered background pattern for transparency visualization
 * @param cr The Cairo context to draw on
 * @param image_width Width of the area to fill
 * @param image_height Height of the area to fill
 */
void draw_checkered_background(cairo_t *cr, gint image_width, gint image_height);

#endif /* RENDER_UTILS_H */

