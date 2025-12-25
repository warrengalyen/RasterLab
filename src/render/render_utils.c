#include "render/render_utils.h"
#include "selection/selection_mask.h"
#include <stdint.h>
#include <stdio.h>

/**
 * Clip Cairo context to dirty rectangle
 * @param ctx Render context
 */
void render_clip_to_dirty(RenderContext* ctx) {
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
cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf* pixbuf) {
    cairo_surface_t* surface;
    gint width, height;
    gint rowstride;
    guchar* pixels;
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
    guchar* surface_data = cairo_image_surface_get_data(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);

    for (y = 0; y < height; y++) {
        guchar* src_row = pixels + y * rowstride;
        guchar* dst_row = surface_data + y * surface_stride;

        if (n_channels == 3) {
            /* RGB to BGRX (Cairo RGB24) */
            for (int x = 0; x < width; x++) {
                dst_row[4 * x + 0] = src_row[3 * x + 2]; /* B */
                dst_row[4 * x + 1] = src_row[3 * x + 1]; /* G */
                dst_row[4 * x + 2] = src_row[3 * x + 0]; /* R */
                dst_row[4 * x + 3] = 0xFF;               /* X (opaque) */
            }
        } else if (n_channels == 4) {
            /* RGBA to BGRA (Cairo ARGB32) */
            for (int x = 0; x < width; x++) {
                dst_row[4 * x + 0] = src_row[4 * x + 2]; /* B */
                dst_row[4 * x + 1] = src_row[4 * x + 1]; /* G */
                dst_row[4 * x + 2] = src_row[4 * x + 0]; /* R */
                dst_row[4 * x + 3] = src_row[4 * x + 3]; /* A */
            }
        }
    }

    cairo_surface_mark_dirty(surface);

    return surface;
}

/**
 * Convert Cairo image surface to GdkPixbuf
 */
GdkPixbuf* cairo_surface_to_pixbuf(cairo_surface_t* surface, gboolean keep_alpha) {
    GdkPixbuf* pixbuf;
    gint width, height;
    guchar* pixels;
    guchar* surface_data;
    gint rowstride;
    gint x, y;
    cairo_format_t format;

    if (!surface) {
        return NULL;
    }

    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    format = cairo_image_surface_get_format(surface);

    /* Verify surface has alpha if we need to keep it */
    if (keep_alpha && format != CAIRO_FORMAT_ARGB32) {
        g_warning("Surface format is not ARGB32, alpha may not be preserved");
    }

    /* Create pixbuf with or without alpha channel */
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, keep_alpha ? TRUE : FALSE, 8, width, height);
    if (!pixbuf) {
        g_warning("Failed to create pixbuf");
        return NULL;
    }

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    /* Flush surface to ensure all drawing operations are complete before reading */
    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);

    /* Copy pixel data from Cairo surface to pixbuf
       Cairo ARGB32 format: on little-endian, bytes in memory are BGRA
       When read as 32-bit int: 0xAARRGGBB (A=MSB, B=LSB) */
    for (y = 0; y < height; y++) {
        guchar* src_row = surface_data + y * surface_stride;
        guchar* dst = pixels + y * rowstride;

        for (x = 0; x < width; x++) {
            /* Read bytes directly to avoid endianness issues
               Cairo ARGB32: bytes are BGRA in memory on little-endian */
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];

            /* Cairo uses pre-multiplied alpha, we need to un-premultiply */
            if (a > 0 && a < 255) {
                /* Un-premultiply: convert from pre-multiplied to straight alpha */
                r = (r * 255 + a / 2) / a; /* Add rounding */
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                /* Clamp to valid range */
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            } else if (a == 0) {
                /* Fully transparent pixel - set RGB to 0 to ensure transparency */
                r = g = b = 0;
            }
            /* If a == 255, no un-premultiplication needed (already straight alpha) */

            /* Write to pixbuf (RGBA format) */
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
void draw_checkered_background(cairo_t* cr, gint image_width, gint image_height) {
    const gint square_size = 10; /* Size of each check square */
    const double color1 = 0.85;  /* Light gray */
    const double color2 = 0.95;  /* Lighter gray */

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

/**
 * Draw a checkered background pattern starting from a specific offset
 * This is useful when drawing only a portion of the canvas (e.g., when zoomed/scrolled)
 */
void draw_checkered_background_offset(cairo_t* cr, gint offset_x, gint offset_y, gint image_width, gint image_height) {
    const gint square_size = 10; /* Size of each check square */
    const double color1 = 0.85;  /* Light gray */
    const double color2 = 0.95;  /* Lighter gray */

    /* Calculate starting cell position */
    gint start_cell_x = offset_x / square_size;
    gint start_cell_y = offset_y / square_size;

    /* Calculate starting pixel position (aligned to grid) */
    gint start_x = start_cell_x * square_size;
    gint start_y = start_cell_y * square_size;

    /* Calculate how many squares we need to draw */
    gint end_x = offset_x + image_width;
    gint end_y = offset_y + image_height;

    /* Draw checkerboard pattern aligned to document origin */
    for (gint y = start_y; y < end_y; y += square_size) {
        for (gint x = start_x; x < end_x; x += square_size) {
            /* Calculate which cell we're in (relative to document origin) */
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

/**
 * Apply a selection mask to a Cairo surface by multiplying alpha values
 */
void render_utils_apply_selection_mask(
    cairo_surface_t* surface,
    const uint8_t* mask,
    gint mask_x,
    gint mask_y,
    gint mask_width,
    gint mask_height,
    gint mask_stride) {

    if (!surface || !mask) {
        return;
    }

    /* Flush surface to ensure all drawing is complete */
    cairo_surface_flush(surface);

    /* Verify surface format */
    cairo_format_t format = cairo_image_surface_get_format(surface);
    if (format != CAIRO_FORMAT_ARGB32) {
        /* Only support ARGB32 for now */
        return;
    }

    gint surface_width = cairo_image_surface_get_width(surface);
    gint surface_height = cairo_image_surface_get_height(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);
    uint8_t* surface_data = cairo_image_surface_get_data(surface);

    if (!surface_data) {
        return;
    }

    /* Calculate intersection region */
    gint start_x = (mask_x < 0) ? 0 : mask_x;
    gint start_y = (mask_y < 0) ? 0 : mask_y;
    gint end_x = (mask_x + mask_width > surface_width) ? surface_width : (mask_x + mask_width);
    gint end_y = (mask_y + mask_height > surface_height) ? surface_height : (mask_y + mask_height);

    if (start_x >= end_x || start_y >= end_y) {
        return; /* No intersection */
    }

    /* Apply mask to each pixel */
    for (gint y = start_y; y < end_y; y++) {
        gint mask_row_y = y - mask_y;
        if (mask_row_y < 0 || mask_row_y >= mask_height) {
            continue;
        }

        uint8_t* surface_row = surface_data + y * surface_stride;
        const uint8_t* mask_row = mask + mask_row_y * mask_stride;

        for (gint x = start_x; x < end_x; x++) {
            gint mask_col_x = x - mask_x;
            if (mask_col_x < 0 || mask_col_x >= mask_width) {
                continue;
            }

            /* Get mask alpha value (0-255) */
            uint8_t mask_alpha = mask_row[mask_col_x];

            /* Get pixel (stored as BGRA in memory for ARGB32) */
            uint8_t* pixel = surface_row + x * 4;
            uint8_t pixel_alpha = pixel[3];

            /* Only apply mask to pixels that were actually painted (non-zero alpha) */
            /* If pixel is transparent, it wasn't painted by this stroke, so leave it alone */
            if (pixel_alpha == 0) {
                continue; /* Skip transparent pixels - they weren't painted */
            }

            /* If mask is 0, completely zero out the pixel (outside selection) */
            if (mask_alpha == 0) {
                pixel[0] = 0; /* B */
                pixel[1] = 0; /* G */
                pixel[2] = 0; /* R */
                pixel[3] = 0; /* A */
            } else if (mask_alpha == 255) {
                /* Fully inside selection: keep painted pixel as-is */
                /* No change needed */
            } else {
                /* Feather zone: apply mask to both RGB and alpha for smooth fade */
                /* For proper alpha compositing, we need to premultiply RGB by mask factor */
                float mask_factor = (float)mask_alpha / 255.0f;

                /* Multiply alpha by mask factor */
                uint16_t new_alpha = ((uint16_t)pixel_alpha * (uint16_t)mask_alpha) / 255;
                pixel[3] = (uint8_t)new_alpha;

                /* Premultiply RGB by mask factor for proper alpha compositing */
                /* This ensures smooth fade to transparency at edges */
                pixel[0] = (uint8_t)((float)pixel[0] * mask_factor);
                pixel[1] = (uint8_t)((float)pixel[1] * mask_factor);
                pixel[2] = (uint8_t)((float)pixel[2] * mask_factor);
            }
        }
    }

    /* Mark surface as dirty so changes are visible */
    cairo_surface_mark_dirty(surface);
}

/**
 * Apply selection mask to eraser result: restore original pixels outside selection
 */
void render_utils_apply_selection_mask_to_eraser(
    cairo_surface_t* erased_surface,
    cairo_surface_t* original_surface,
    const uint8_t* mask,
    gint mask_x,
    gint mask_y,
    gint mask_width,
    gint mask_height,
    gint mask_stride,
    gint original_x,
    gint original_y) {

    if (!erased_surface || !original_surface || !mask) {
        return;
    }

    /* Flush surfaces to ensure all drawing is complete */
    cairo_surface_flush(erased_surface);
    cairo_surface_flush(original_surface);

    /* Verify surface formats */
    if (cairo_image_surface_get_format(erased_surface) != CAIRO_FORMAT_ARGB32 ||
        cairo_image_surface_get_format(original_surface) != CAIRO_FORMAT_ARGB32) {
        return;
    }

    gint erased_width = cairo_image_surface_get_width(erased_surface);
    gint erased_height = cairo_image_surface_get_height(erased_surface);
    gint erased_stride = cairo_image_surface_get_stride(erased_surface);
    uint8_t* erased_data = cairo_image_surface_get_data(erased_surface);

    gint original_width = cairo_image_surface_get_width(original_surface);
    gint original_height = cairo_image_surface_get_height(original_surface);
    gint original_stride = cairo_image_surface_get_stride(original_surface);
    uint8_t* original_data = cairo_image_surface_get_data(original_surface);

    if (!erased_data || !original_data) {
        return;
    }

    /* For each pixel in erased surface, check if it's outside selection */
    for (gint y = 0; y < erased_height; y++) {
        gint mask_row_y = y - mask_y;
        uint8_t* erased_row = erased_data + y * erased_stride;
        gint orig_y = original_y + y;

        for (gint x = 0; x < erased_width; x++) {
            gint mask_col_x = x - mask_x;
            uint8_t* erased_pixel = erased_row + x * 4;

            /* Get mask value for this pixel */
            uint8_t mask_alpha = 0;
            if (mask_col_x >= 0 && mask_col_x < mask_width &&
                mask_row_y >= 0 && mask_row_y < mask_height) {
                mask_alpha = mask[mask_row_y * mask_stride + mask_col_x];
            }

            gint orig_x = original_x + x;
            if (orig_x >= 0 && orig_x < original_width &&
                orig_y >= 0 && orig_y < original_height) {
                uint8_t* orig_pixel = original_data + orig_y * original_stride + orig_x * 4;

                if (mask_alpha == 0) {
                    /* Outside selection: fully restore original pixel */
                    erased_pixel[0] = orig_pixel[0]; /* B */
                    erased_pixel[1] = orig_pixel[1]; /* G */
                    erased_pixel[2] = orig_pixel[2]; /* R */
                    erased_pixel[3] = orig_pixel[3]; /* A */
                } else if (mask_alpha == 255) {
                    /* Inside selection: keep fully erased result (already in erased_pixel) */
                    /* No change needed */
                } else {
                    /* Feather zone: blend between erased and original with proper alpha compositing */
                    /* mask_alpha / 255.0 = how much erasing is active (how much to use erased result) */
                    /* (255 - mask_alpha) / 255.0 = how much to restore from original */
                    float mask_factor = (float)mask_alpha / 255.0f;
                    float orig_factor = 1.0f - mask_factor;

                    /* For proper alpha compositing, we need to blend premultiplied alpha values */
                    /* Convert to premultiplied alpha, blend, then convert back */
                    float erased_a = (float)erased_pixel[3] / 255.0f;
                    float orig_a = (float)orig_pixel[3] / 255.0f;

                    /* Premultiplied RGB values */
                    float erased_r_pre = ((float)erased_pixel[2] / 255.0f) * erased_a;
                    float erased_g_pre = ((float)erased_pixel[1] / 255.0f) * erased_a;
                    float erased_b_pre = ((float)erased_pixel[0] / 255.0f) * erased_a;

                    float orig_r_pre = ((float)orig_pixel[2] / 255.0f) * orig_a;
                    float orig_g_pre = ((float)orig_pixel[1] / 255.0f) * orig_a;
                    float orig_b_pre = ((float)orig_pixel[0] / 255.0f) * orig_a;

                    /* Blend premultiplied values */
                    float result_r_pre = erased_r_pre * mask_factor + orig_r_pre * orig_factor;
                    float result_g_pre = erased_g_pre * mask_factor + orig_g_pre * orig_factor;
                    float result_b_pre = erased_b_pre * mask_factor + orig_b_pre * orig_factor;
                    float result_a = erased_a * mask_factor + orig_a * orig_factor;

                    /* Convert back from premultiplied alpha */
                    if (result_a > 0.0f) {
                        erased_pixel[2] = (uint8_t)((result_r_pre / result_a) * 255.0f + 0.5f);
                        erased_pixel[1] = (uint8_t)((result_g_pre / result_a) * 255.0f + 0.5f);
                        erased_pixel[0] = (uint8_t)((result_b_pre / result_a) * 255.0f + 0.5f);
                    } else {
                        erased_pixel[2] = 0;
                        erased_pixel[1] = 0;
                        erased_pixel[0] = 0;
                    }
                    erased_pixel[3] = (uint8_t)(result_a * 255.0f + 0.5f);
                }
            }
        }
    }

    /* Mark surface as dirty so changes are visible */
    cairo_surface_mark_dirty(erased_surface);
}

/**
 * TEMPORARY: Visualize selection mask as a semi-transparent overlay
 * Draws the mask as a red overlay (white = fully selected, transparent = not selected)
 * This is for debugging feathered selection masks
 */
void render_utils_visualize_selection_mask(cairo_t* cr, SelectionMask* mask) {
    if (!cr || !mask || !mask->data) {
        return;
    }

    /* Create a temporary ARGB32 surface from the mask data */
    /* Convert 8-bit alpha mask to ARGB32: white (fully selected) = red overlay, transparent = no overlay */
    cairo_surface_t* mask_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, mask->width, mask->height);

    if (!mask_surface) {
        return;
    }

    uint8_t* surface_data = cairo_image_surface_get_data(mask_surface);
    gint surface_stride = cairo_image_surface_get_stride(mask_surface);
    const uint8_t* mask_data = mask->data;

    /* Convert mask to ARGB32: use mask value as alpha, set RGB to red (255, 0, 0) */
    for (gint y = 0; y < mask->height; y++) {
        uint8_t* surface_row = surface_data + y * surface_stride;
        const uint8_t* mask_row = mask_data + y * mask->stride;

        for (gint x = 0; x < mask->width; x++) {
            uint8_t mask_alpha = mask_row[x];
            uint8_t* pixel = surface_row + x * 4;

            /* BGRA format: B, G, R, A */
            pixel[0] = 0;          /* B */
            pixel[1] = 0;          /* G */
            pixel[2] = 255;        /* R (red) */
            pixel[3] = mask_alpha; /* A (use mask value as alpha) */
        }
    }

    cairo_surface_mark_dirty(mask_surface);
    cairo_surface_flush(mask_surface);

    /* Draw the mask as a semi-transparent overlay */
    cairo_save(cr);
    cairo_set_source_surface(cr, mask_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint_with_alpha(cr, 0.5); /* 50% opacity overlay */
    cairo_restore(cr);

    cairo_surface_destroy(mask_surface);
}
