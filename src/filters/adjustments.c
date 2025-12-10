#include "adjustments.h"
#include "render/layer.h"
#include "ocular.h"
#include <glib.h>

/**
 * Apply grayscale filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_apply_grayscale(ImageLayer *layer)
{
    cairo_surface_t *surface;
    gint width, height, stride;
    guchar *surface_data;
    guchar *rgb_input, *grayscale_output;
    gint x, y;
    OC_STATUS status;
    cairo_format_t format;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    format = cairo_image_surface_get_format(surface);
    stride = cairo_image_surface_get_stride(surface);

    /* Only support ARGB32 format for now */
    if (format != CAIRO_FORMAT_ARGB32) {
        g_warning("Grayscale filter: Unsupported surface format");
        return FALSE;
    }

    /* Flush surface to ensure all drawing operations are complete */
    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Allocate buffers:
       - rgb_input: RGB format (3 channels) for Ocular input
       - grayscale_output: Single channel grayscale output from Ocular */
    rgb_input = (guchar *)g_malloc(width * height * 3);
    grayscale_output = (guchar *)g_malloc(width * height);
    
    if (!rgb_input || !grayscale_output) {
        g_warning("Grayscale filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(grayscale_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 (BGRA in memory on little-endian) to RGB */
    for (y = 0; y < height; y++) {
        guchar *src_row = surface_data + y * stride;
        guchar *dst = rgb_input + y * width * 3;

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
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }

            /* Write RGB (3 channels) in RGB byte-order */
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst += 3;
        }
    }

    /* Apply grayscale filter using Ocular library
       Input: RGB format (stride = width * 3)
       Output: Single channel grayscale (stride = width) */
    status = ocularGrayscaleFilter(rgb_input, grayscale_output, width, height, width * 3);
    
    if (status != OC_STATUS_OK) {
        g_warning("Grayscale filter: Ocular filter returned error %d", status);
        g_free(rgb_input);
        g_free(grayscale_output);
        return FALSE;
    }

    /* Convert back from single channel grayscale to Cairo ARGB32 (BGRA in memory) */
    for (y = 0; y < height; y++) {
        guchar *src = grayscale_output + y * width;
        guchar *dst_row = surface_data + y * stride;

        for (x = 0; x < width; x++) {
            guchar gray = src[x];
            guchar a = dst_row[x * 4 + 3];  /* Preserve original alpha */

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

    /* Free temporary buffers */
    g_free(rgb_input);
    g_free(grayscale_output);

    return TRUE;
}

