#include "render/render_utils.h"
#include "app/settings.h"
#include "selection/selection_mask.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "debug_logger.h"

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
        debug_log("WRN", "Failed to create Cairo surface");
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
        debug_log("WRN", "Surface format is not ARGB32, alpha may not be preserved");
    }

    /* Create pixbuf with or without alpha channel */
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, keep_alpha ? TRUE : FALSE, 8, width, height);
    if (!pixbuf) {
        debug_log("WRN", "Failed to create pixbuf");
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

/* Default: Medium = 8 px per check → 6×6 squares in 48px */
#define DEFAULT_ALPHA_CHECK_SQUARE_SIZE 8

#define DEFAULT_ALPHA_COLOR_ONE_R 1.0
#define DEFAULT_ALPHA_COLOR_ONE_G 1.0
#define DEFAULT_ALPHA_COLOR_ONE_B 1.0
#define DEFAULT_ALPHA_COLOR_TWO_R (204.0 / 255.0)
#define DEFAULT_ALPHA_COLOR_TWO_G (204.0 / 255.0)
#define DEFAULT_ALPHA_COLOR_TWO_B (204.0 / 255.0)

/* Pixels per check (one square): Small=4 (12×12 in 48px), Medium=8 (6×6), Large=16 (3×3). Tile = 2×2 checks. */
static gint current_alpha_check_square_size = DEFAULT_ALPHA_CHECK_SQUARE_SIZE;
static gdouble current_r1 = DEFAULT_ALPHA_COLOR_ONE_R, current_g1 = DEFAULT_ALPHA_COLOR_ONE_G, current_b1 = DEFAULT_ALPHA_COLOR_ONE_B;
static gdouble current_r2 = DEFAULT_ALPHA_COLOR_TWO_R, current_g2 = DEFAULT_ALPHA_COLOR_TWO_G, current_b2 = DEFAULT_ALPHA_COLOR_TWO_B;

/* Static pattern surface for checkered background - recreated when size/colors change */
static cairo_surface_t* checkered_pattern_surface = NULL;
static cairo_pattern_t* checkered_pattern = NULL;
static gint cached_square_size = -1;
static gdouble cached_r1 = -1.0, cached_g1 = -1.0, cached_b1 = -1.0;
static gdouble cached_r2 = -1.0, cached_g2 = -1.0, cached_b2 = -1.0;

/**
 * Set alpha checkerboard options from app settings. Call after loading settings.
 */
void render_utils_set_alpha_check_from_settings(const Settings* settings) {
    if (!settings) {
        return;
    }
    /* Small: 4 px/check (12×12 in 48px); Medium: 8 (6×6); Large: 16 (3×3). Tile = 2×2 of these squares. */
    switch (settings_get_alpha_check_size((Settings*)settings)) {
        case 0: current_alpha_check_square_size = 4;  break; /* Small */
        case 1: current_alpha_check_square_size = 8; break; /* Medium */
        case 2: current_alpha_check_square_size = 16; break; /* Large */
        default: current_alpha_check_square_size = DEFAULT_ALPHA_CHECK_SQUARE_SIZE; break;
    }
    settings_get_alpha_color_one((Settings*)settings, &current_r1, &current_g1, &current_b1);
    settings_get_alpha_color_two((Settings*)settings, &current_r2, &current_g2, &current_b2);
    if (checkered_pattern) {
        cairo_pattern_destroy(checkered_pattern);
        checkered_pattern = NULL;
    }
    if (checkered_pattern_surface) {
        cairo_surface_destroy(checkered_pattern_surface);
        checkered_pattern_surface = NULL;
    }
    cached_square_size = -1;
}

/**
 * Create tile: 2×2 checkerboard of squares (square_size × square_size each). Tile size = 2*square_size.
 */
static void ensure_checkered_pattern(void) {
    gint square_size = current_alpha_check_square_size;
    gdouble r1 = current_r1, g1 = current_g1, b1 = current_b1;
    gdouble r2 = current_r2, g2 = current_g2, b2 = current_b2;

    if (checkered_pattern != NULL &&
        cached_square_size == square_size &&
        cached_r1 == r1 && cached_g1 == g1 && cached_b1 == b1 &&
        cached_r2 == r2 && cached_g2 == g2 && cached_b2 == b2) {
        return;
    }

    if (checkered_pattern) {
        cairo_pattern_destroy(checkered_pattern);
        checkered_pattern = NULL;
    }
    if (checkered_pattern_surface) {
        cairo_surface_destroy(checkered_pattern_surface);
        checkered_pattern_surface = NULL;
    }

    cached_square_size = square_size;
    cached_r1 = r1; cached_g1 = g1; cached_b1 = b1;
    cached_r2 = r2; cached_g2 = g2; cached_b2 = b2;

    {
        const gint tile_size = square_size * 2; /* 2×2 block */
        cairo_t* pattern_cr;

        checkered_pattern_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, tile_size, tile_size);
        pattern_cr = cairo_create(checkered_pattern_surface);

        /* Top-left, bottom-right: color1. Top-right, bottom-left: color2. */
        cairo_set_source_rgb(pattern_cr, r1, g1, b1);
        cairo_rectangle(pattern_cr, 0, 0, square_size, square_size);
        cairo_fill(pattern_cr);
        cairo_rectangle(pattern_cr, square_size, square_size, square_size, square_size);
        cairo_fill(pattern_cr);
        cairo_set_source_rgb(pattern_cr, r2, g2, b2);
        cairo_rectangle(pattern_cr, square_size, 0, square_size, square_size);
        cairo_fill(pattern_cr);
        cairo_rectangle(pattern_cr, 0, square_size, square_size, square_size);
        cairo_fill(pattern_cr);

        cairo_destroy(pattern_cr);

        checkered_pattern = cairo_pattern_create_for_surface(checkered_pattern_surface);
        cairo_pattern_set_extend(checkered_pattern, CAIRO_EXTEND_REPEAT);
        cairo_pattern_set_filter(checkered_pattern, CAIRO_FILTER_NEAREST);
    }
}

/**
 * Draw a checkered background for transparency.
 * Tile = 2×2 squares; square size 4/8/16 px (Small/Medium/Large) → 12/6/3 squares per row in 48px.
 * Tile is repeated across the area (no scaling).
 */
void draw_checkered_background(cairo_t* cr, gint image_width, gint image_height) {
    if (image_width <= 0 || image_height <= 0) {
        return;
    }

    ensure_checkered_pattern();
    if (!checkered_pattern) {
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_rectangle(cr, 0, 0, image_width, image_height);
        cairo_fill(cr);
        return;
    }

    cairo_save(cr);
    cairo_set_source(cr, checkered_pattern);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, image_width, image_height);
    cairo_fill(cr);
    cairo_restore(cr);
}

/**
 * Draw a checkered background pattern starting from a specific offset
 * This is useful when drawing only a portion of the canvas (e.g., when zoomed/scrolled)
 */
void draw_checkered_background_offset(cairo_t* cr, gint offset_x, gint offset_y, gint image_width, gint image_height) {
    ensure_checkered_pattern();

    if (!checkered_pattern) {
        /* Fallback to solid gray if pattern creation failed */
        cairo_save(cr);
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_rectangle(cr, offset_x, offset_y, image_width, image_height);
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }

    cairo_save(cr);

    /* Use SOURCE operator to completely replace destination pixels. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    if (image_width > 0 && image_height > 0) {
        cairo_translate(cr, offset_x, offset_y);
        cairo_set_source(cr, checkered_pattern);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_rectangle(cr, 0, 0, image_width, image_height);
        cairo_fill(cr);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_restore(cr);
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
                /* Feather zone: apply mask to alpha with proper premultiplied alpha handling */
                /* Cairo uses premultiplied alpha, so we need to un-premultiply, change alpha, then re-premultiply */
                uint8_t new_alpha = (uint8_t)((pixel_alpha * mask_alpha) / 255);

                if (new_alpha == 0) {
                    /* Completely transparent */
                    pixel[0] = 0;
                    pixel[1] = 0;
                    pixel[2] = 0;
                    pixel[3] = 0;
                } else if (pixel_alpha > 0) {
                    /* Un-premultiply: convert from premultiplied to straight alpha */
                    uint16_t r = (pixel[2] * 255 + pixel_alpha / 2) / pixel_alpha;
                    uint16_t g = (pixel[1] * 255 + pixel_alpha / 2) / pixel_alpha;
                    uint16_t b = (pixel[0] * 255 + pixel_alpha / 2) / pixel_alpha;

                    /* Clamp to valid range */
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    /* Re-premultiply with new alpha */
                    pixel[0] = (b * new_alpha + 127) / 255; /* B */
                    pixel[1] = (g * new_alpha + 127) / 255; /* G */
                    pixel[2] = (r * new_alpha + 127) / 255; /* R */
                    pixel[3] = new_alpha;                   /* A */
                } else {
                    /* Source was transparent */
                    pixel[0] = 0;
                    pixel[1] = 0;
                    pixel[2] = 0;
                    pixel[3] = 0;
                }
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
                    /* Feather zone: blend between erased and original with proper premultiplied alpha handling */
                    /* mask_alpha / 255.0 = how much erasing is active (how much to use erased result) */
                    /* (255 - mask_alpha) / 255.0 = how much to restore from original */
                    float mask_factor = (float)mask_alpha / 255.0f;
                    float orig_factor = 1.0f - mask_factor;

                    /* Both pixels are in premultiplied alpha format (Cairo ARGB32) */
                    /* Un-premultiply both to get straight alpha values */
                    uint8_t erased_a = erased_pixel[3];
                    uint8_t orig_a = orig_pixel[3];

                    uint16_t erased_r, erased_g, erased_b;
                    uint16_t orig_r, orig_g, orig_b;

                    if (erased_a > 0) {
                        erased_r = (erased_pixel[2] * 255 + erased_a / 2) / erased_a;
                        erased_g = (erased_pixel[1] * 255 + erased_a / 2) / erased_a;
                        erased_b = (erased_pixel[0] * 255 + erased_a / 2) / erased_a;
                        if (erased_r > 255)
                            erased_r = 255;
                        if (erased_g > 255)
                            erased_g = 255;
                        if (erased_b > 255)
                            erased_b = 255;
                    } else {
                        erased_r = erased_g = erased_b = 0;
                    }

                    if (orig_a > 0) {
                        orig_r = (orig_pixel[2] * 255 + orig_a / 2) / orig_a;
                        orig_g = (orig_pixel[1] * 255 + orig_a / 2) / orig_a;
                        orig_b = (orig_pixel[0] * 255 + orig_a / 2) / orig_a;
                        if (orig_r > 255)
                            orig_r = 255;
                        if (orig_g > 255)
                            orig_g = 255;
                        if (orig_b > 255)
                            orig_b = 255;
                    } else {
                        orig_r = orig_g = orig_b = 0;
                    }

                    /* Blend in straight alpha space */
                    uint16_t result_r = (uint16_t)(erased_r * mask_factor + orig_r * orig_factor);
                    uint16_t result_g = (uint16_t)(erased_g * mask_factor + orig_g * orig_factor);
                    uint16_t result_b = (uint16_t)(erased_b * mask_factor + orig_b * orig_factor);
                    uint8_t result_a = (uint8_t)(erased_a * mask_factor + orig_a * orig_factor);

                    /* Re-premultiply with blended alpha */
                    if (result_a > 0) {
                        erased_pixel[0] = (result_b * result_a + 127) / 255; /* B */
                        erased_pixel[1] = (result_g * result_a + 127) / 255; /* G */
                        erased_pixel[2] = (result_r * result_a + 127) / 255; /* R */
                        erased_pixel[3] = result_a;                          /* A */
                    } else {
                        erased_pixel[0] = 0;
                        erased_pixel[1] = 0;
                        erased_pixel[2] = 0;
                        erased_pixel[3] = 0;
                    }
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
