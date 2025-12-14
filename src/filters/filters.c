#include "filters.h"
#include "ocular.h"
#include "render/layer.h"
#include <glib.h>

/**
 * Validate surface format and get dimensions
 */
gboolean adjustments_validate_surface(cairo_surface_t* surface, gint* width, gint* height) {
    cairo_format_t format;

    if (!surface || !width || !height) {
        return FALSE;
    }

    format = cairo_image_surface_get_format(surface);
    if (format != CAIRO_FORMAT_ARGB32) {
        g_warning("Adjustment filter: Unsupported surface format");
        return FALSE;
    }

    *width = cairo_image_surface_get_width(surface);
    *height = cairo_image_surface_get_height(surface);

    if (*width <= 0 || *height <= 0) {
        g_warning("Adjustment filter: Invalid surface dimensions");
        return FALSE;
    }

    return TRUE;
}

/**
 * Convert Cairo ARGB32 surface to RGB buffer for Ocular library
 */
gboolean adjustments_cairo_to_rgb(cairo_surface_t* surface, guchar* rgb_output) {
    gint width, height, stride;
    guchar* surface_data;
    gint x, y;

    if (!surface || !rgb_output) {
        return FALSE;
    }

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = cairo_image_surface_get_stride(surface);

    /* Flush surface to ensure all drawing operations are complete */
    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Convert from Cairo ARGB32 (BGRA in memory on little-endian) to RGB */
    for (y = 0; y < height; y++) {
        guchar* src_row = surface_data + y * stride;
        guchar* dst = rgb_output + y * width * 3;

        for (x = 0; x < width; x++) {
            /* Read BGRA bytes from Cairo surface */
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];

            /* Un-premultiply alpha if needed */
            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            }

            /* Write RGB (3 channels) in RGB byte-order */
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst += 3;
        }
    }

    return TRUE;
}

/**
 * Convert RGB buffer to Cairo ARGB32 surface
 */
gboolean adjustments_rgb_to_cairo(cairo_surface_t* surface, const guchar* rgb_input) {
    gint width, height, stride;
    guchar* surface_data;
    gint x, y;

    if (!surface || !rgb_input) {
        return FALSE;
    }

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = cairo_image_surface_get_stride(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Convert from RGB to Cairo ARGB32 (BGRA in memory) */
    for (y = 0; y < height; y++) {
        const guchar* src = rgb_input + y * width * 3;
        guchar* dst_row = surface_data + y * stride;

        for (x = 0; x < width; x++) {
            guchar r = src[0];
            guchar g = src[1];
            guchar b = src[2];
            guchar a = dst_row[x * 4 + 3]; /* Preserve original alpha */

            /* Pre-multiply alpha for Cairo */
            if (a < 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }

            /* Write BGRA bytes to Cairo surface */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
            src += 3;
        }
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);

    return TRUE;
}

/**
 * Convert Cairo ARGB32 surface to RGBA buffer for Ocular library
 */
gboolean adjustments_cairo_to_rgba(cairo_surface_t* surface, guchar* rgba_output) {
    gint width, height, stride;
    guchar* surface_data;
    gint x, y;

    if (!surface || !rgba_output) {
        return FALSE;
    }

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = cairo_image_surface_get_stride(surface);

    /* Flush surface to ensure all drawing operations are complete */
    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Convert from Cairo ARGB32 (BGRA in memory on little-endian) to RGBA */
    for (y = 0; y < height; y++) {
        guchar* src_row = surface_data + y * stride;
        guchar* dst = rgba_output + y * width * 4;

        for (x = 0; x < width; x++) {
            /* Read BGRA bytes from Cairo surface */
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];

            /* Un-premultiply alpha if needed */
            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            }

            /* Write RGBA (4 channels) in RGBA byte-order */
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = a;
            dst += 4;
        }
    }

    return TRUE;
}

/**
 * Convert RGBA buffer to Cairo ARGB32 surface
 */
gboolean adjustments_rgba_to_cairo(cairo_surface_t* surface, const guchar* rgba_input) {
    gint width, height, stride;
    guchar* surface_data;
    gint x, y;

    if (!surface || !rgba_input) {
        return FALSE;
    }

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = cairo_image_surface_get_stride(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Convert from RGBA to Cairo ARGB32 (BGRA in memory) */
    for (y = 0; y < height; y++) {
        const guchar* src = rgba_input + y * width * 4;
        guchar* dst_row = surface_data + y * stride;

        for (x = 0; x < width; x++) {
            guchar r = src[0];
            guchar g = src[1];
            guchar b = src[2];
            guchar a = src[3];

            /* Pre-multiply alpha for Cairo */
            if (a < 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }

            /* Write BGRA bytes to Cairo surface */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
            src += 4;
        }
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);

    return TRUE;
}

/**
 * Convert single-channel grayscale buffer to Cairo ARGB32 surface
 */
gboolean adjustments_grayscale_to_cairo(cairo_surface_t* surface, const guchar* grayscale_input) {
    gint width, height, stride;
    guchar* surface_data;
    gint x, y;

    if (!surface || !grayscale_input) {
        return FALSE;
    }

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = cairo_image_surface_get_stride(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Convert from single channel grayscale to Cairo ARGB32 (BGRA in memory) */
    for (y = 0; y < height; y++) {
        const guchar* src = grayscale_input + y * width;
        guchar* dst_row = surface_data + y * stride;

        for (x = 0; x < width; x++) {
            guchar gray = src[x];
            guchar a = dst_row[x * 4 + 3]; /* Preserve original alpha */

            /* Duplicate grayscale value to R, G, B channels */
            guchar r = gray;
            guchar g = gray;
            guchar b = gray;

            /* Pre-multiply alpha for Cairo */
            if (a < 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }

            /* Write BGRA bytes to Cairo surface */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
        }
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);

    return TRUE;
}

/**
 * Scale a value from UI range to filter range
 */
gdouble adjustments_scale_value(gdouble ui_value,
                                gdouble ui_min,
                                gdouble ui_max,
                                gdouble filter_min,
                                gdouble filter_max) {
    gdouble normalized;
    gdouble range_ui, range_filter;

    /* Handle edge cases */
    if (ui_max == ui_min) {
        return filter_min; /* Avoid division by zero */
    }

    /* Normalize UI value to 0.0-1.0 range */
    normalized = (ui_value - ui_min) / (ui_max - ui_min);

    /* Clamp to [0.0, 1.0] */
    if (normalized < 0.0) {
        normalized = 0.0;
    } else if (normalized > 1.0) {
        normalized = 1.0;
    }

    /* Scale to filter range */
    range_filter = filter_max - filter_min;
    return filter_min + (normalized * range_filter);
}
