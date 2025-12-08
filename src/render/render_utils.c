#include "render/render_utils.h"
#include <stdio.h>

/**
 * Clip Cairo context to dirty rectangle
 * @param ctx Render context
 */
void render_clip_to_dirty(RenderContext *ctx)
{
    if (!ctx || !ctx->use_dirty_rect || dirty_rect_is_empty(&ctx->dirty_rect)) {
        return;
    }
    
    cairo_rectangle(ctx->cr, 
                   ctx->dirty_rect.x, 
                   ctx->dirty_rect.y, 
                   ctx->dirty_rect.width, 
                   ctx->dirty_rect.height);
    cairo_clip(ctx->cr);
}

/**
 * Convert GdkPixbuf to Cairo image surface
 */
cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf *pixbuf)
{
    cairo_surface_t *surface;
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
 * Convert Cairo image surface to GdkPixbuf
 */
GdkPixbuf* cairo_surface_to_pixbuf(cairo_surface_t *surface, gboolean keep_alpha)
{
    GdkPixbuf *pixbuf;
    gint width, height;
    guchar *pixels;
    guchar *surface_data;
    gint rowstride;
    gint x, y;

    if (!surface) {
        return NULL;
    }

    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);

    /* Create pixbuf with or without alpha channel */
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, keep_alpha ? TRUE : FALSE, 8, width, height);
    if (!pixbuf) {
        g_warning("Failed to create pixbuf");
        return NULL;
    }

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    surface_data = cairo_image_surface_get_data(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);

    /* Copy pixel data from Cairo surface to pixbuf */
    for (y = 0; y < height; y++) {
        guint32 *src = (guint32 *)(surface_data + y * surface_stride);
        guchar *dst = pixels + y * rowstride;

        for (x = 0; x < width; x++) {
            guint32 pixel = src[x];
            guchar a = (pixel >> 24) & 0xFF;
            guchar r = (pixel >> 16) & 0xFF;
            guchar g = (pixel >> 8) & 0xFF;
            guchar b = pixel & 0xFF;

            /* Cairo uses pre-multiplied alpha, we need to un-premultiply */
            if (a > 0) {
                r = (r * 255) / a;
                g = (g * 255) / a;
                b = (b * 255) / a;
            }

            dst[0] = r;
            dst[1] = g;
            dst[2] = b;

            if (keep_alpha) {
                dst[3] = a;
                dst += 4;
            } else {
                dst += 3;
            }
        }
    }

    return pixbuf;
}

/**
 * Draw a checkered background pattern for transparency
 */
void draw_checkered_background(cairo_t *cr, gint image_width, gint image_height)
{
    const gint square_size = 10;    /* Size of each check square */
    const double color1 = 0.85;    /* Light gray */
    const double color2 = 0.95;    /* Lighter gray */

    /* Draw checkerboard pattern aligned to image origin */
    for (gint y = 0; y < image_height; y += square_size) {
        for (gint x = 0; x < image_width; x += square_size) {
            /* Calculate which cell we're in (relative to origin) */
            gint cell_x = x / square_size;
            gint cell_y = y / square_size;
            
            /* Alternate colors in a checkerboard pattern */
            double color = ((cell_x + cell_y) % 2 == 0) ? color1 : color2;

            /* Draw this square */
            cairo_set_source_rgb(cr, color, color, color);
            cairo_rectangle(cr, x, y, square_size, square_size);
            cairo_fill(cr);
        }
    }
}

