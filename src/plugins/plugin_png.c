#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBPNG
#include <png.h>

/* PNG file signature */
static const uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

/**
 * Error handling structure for libpng
 */
struct png_error_info {
    jmp_buf jmpbuf;
    const char* message;
};

/**
 * Error handler for libpng
 */
static void png_error_handler(png_structp png_ptr, png_const_charp error_msg) {
    struct png_error_info* error_info = (struct png_error_info*)png_get_error_ptr(png_ptr);
    if (error_info) {
        error_info->message = error_msg;
    }
    longjmp(error_info->jmpbuf, 1);
}

/**
 * Warning handler for libpng (can be NULL, but we provide one for completeness)
 */
static void png_warning_handler(png_structp png_ptr, png_const_charp warning_msg) {
    (void)png_ptr;
    (void)warning_msg;
    /* Ignore warnings for now */
}

#else
/* PNG file signature (for can_load even when libpng not available) */
static const uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
#endif

/**
 * Check if file is PNG format
 */
static bool can_load_png(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 8) {
        return false;
    }

    return memcmp(header, PNG_SIGNATURE, 8) == 0;
}

#ifdef HAVE_LIBPNG
/**
 * Load PNG image using libpng
 */
static PluginError load_png(ImageDocument* doc, const char* filename) {
    FILE* infile;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep* row_pointers = NULL;
    struct png_error_info error_info = {0};
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type;
    png_bytep image_data = NULL;

    if (!doc || !filename) {
        g_warning("PNG plugin: Invalid parameters (doc=%p, filename=%p)", doc, filename);
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open PNG file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        g_warning("PNG plugin: Failed to open file: %s", filename);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Check PNG signature */
    png_byte header[8];
    if (fread(header, 1, 8, infile) != 8) {
        g_warning("PNG plugin: Failed to read PNG signature");
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }
    if (png_sig_cmp(header, 0, 8)) {
        g_warning("PNG plugin: Invalid PNG signature");
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Set up error handling FIRST - jmp_buf must be initialized before any libpng calls
     * that might use the error handler */
    if (setjmp(error_info.jmpbuf)) {
        /* Error occurred - clean up if structures were created */
        if (png_ptr) {
            png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, NULL);
        }
        if (row_pointers) {
            g_free(row_pointers);
        }
        if (image_data) {
            g_free(image_data);
        }
        if (base_layer) {
            layer_free(base_layer);
        }
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Initialize libpng structures - now that jmp_buf is initialized */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, &error_info, png_error_handler, png_warning_handler);
    if (!png_ptr) {
        g_warning("PNG plugin: png_create_read_struct failed");
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        g_warning("PNG plugin: Failed to create png_info_struct");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize I/O */
    png_init_io(png_ptr, infile);
    png_set_sig_bytes(png_ptr, 8);

    /* Read PNG info */
    png_read_info(png_ptr, info_ptr);

    /* Get image properties */
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

    /* Convert to 8-bit per channel if needed */
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (bit_depth < 8) {
        png_set_packing(png_ptr);
    }

    /* Convert palette/grayscale to RGB if needed */
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    /* Add alpha channel if not present */
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    /* Update info after transformations */
    png_read_update_info(png_ptr, info_ptr);

    /* Get updated properties */
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = 4; /* RGBA */
    doc->bit_depth = 8;
    doc->has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) != 0;

    /* Free old layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Validate dimensions before creating layer */
    if (width == 0 || height == 0) {
        g_warning("PNG plugin: Invalid dimensions: %ux%u", width, height);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Create base layer */
    base_layer = layer_new("Background", doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        g_warning("PNG plugin: layer_new returned NULL for %ux%u layer", doc->width, doc->height);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        g_warning("PNG plugin: base_layer->surface is NULL");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_status_t surface_status = cairo_surface_status(temp_surface);
    if (surface_status != CAIRO_STATUS_SUCCESS) {
        g_warning("PNG plugin: Cairo surface status error: %s", cairo_status_to_string(surface_status));
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        g_warning("PNG plugin: cairo_image_surface_get_data returned NULL");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate row pointers */
    row_pointers = g_malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        g_warning("PNG plugin: Failed to allocate row_pointers array (%u pointers)", height);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate image data buffer */
    png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    image_data = g_malloc(rowbytes * height);
    if (!image_data) {
        g_warning("PNG plugin: Failed to allocate image_data buffer (%u bytes)", rowbytes * height);
        g_free(row_pointers);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Set up row pointers */
    for (png_uint_32 y = 0; y < height; y++) {
        row_pointers[y] = image_data + y * rowbytes;
    }

    /* Read image data */
    png_read_image(png_ptr, row_pointers);

    /* Convert RGBA to Cairo's ARGB32 (BGRA in memory) */
    for (png_uint_32 y = 0; y < height; y++) {
        png_bytep src_row = row_pointers[y];
        guchar* dst_row = surface_data + y * surface_stride;

        for (png_uint_32 x = 0; x < width; x++) {
            guchar r = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar b = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];

            /* Cairo ARGB32: BGRA in memory (little-endian) */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
        }
    }

    cairo_surface_mark_dirty(temp_surface);

    /* Finish reading */
    png_read_end(png_ptr, NULL);

    /* Cleanup */
    g_free(row_pointers);
    g_free(image_data);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(infile);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    return PLUGIN_ERROR_NONE;
}

/**
 * Save PNG image using libpng
 */
static PluginError save_png(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite;
    FILE* outfile;
    png_structp png_ptr;
    png_infop info_ptr;
    struct png_error_info error_info;
    guchar* surface_data;
    int surface_stride;
    png_bytep* row_pointers = NULL;
    png_bytep image_data = NULL;

    (void)opts; /* Options not used for PNG yet */

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Flush surface to ensure data is available */
    cairo_surface_flush(composite);
    surface_data = cairo_image_surface_get_data(composite);
    surface_stride = cairo_image_surface_get_stride(composite);

    if (!surface_data) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Open output file */
    outfile = g_fopen(filename, "wb");
    if (!outfile) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Initialize libpng structures */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, &error_info, png_error_handler, png_warning_handler);
    if (!png_ptr) {
        fclose(outfile);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(outfile);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Set up error handling */
    if (setjmp(error_info.jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        if (row_pointers) {
            g_free(row_pointers);
        }
        if (image_data) {
            g_free(image_data);
        }
        fclose(outfile);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Initialize I/O */
    png_init_io(png_ptr, outfile);

    /* Set PNG header */
    png_set_IHDR(png_ptr, info_ptr, doc->width, doc->height, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    /* Write info */
    png_write_info(png_ptr, info_ptr);

    /* Allocate row pointers */
    row_pointers = g_malloc(sizeof(png_bytep) * doc->height);
    if (!row_pointers) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(outfile);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate image data buffer */
    png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    image_data = g_malloc(rowbytes * doc->height);
    if (!image_data) {
        g_free(row_pointers);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(outfile);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert from Cairo's ARGB32 (BGRA in memory) to RGBA */
    for (guint y = 0; y < doc->height; y++) {
        guchar* src_row = surface_data + y * surface_stride;
        png_bytep dst_row = image_data + y * rowbytes;
        row_pointers[y] = dst_row;

        for (guint x = 0; x < doc->width; x++) {
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];

            /* PNG RGBA format */
            dst_row[x * 4 + 0] = r;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = b;
            dst_row[x * 4 + 3] = a;
        }
    }

    /* Write image data */
    png_write_image(png_ptr, row_pointers);

    /* Finish writing */
    png_write_end(png_ptr, NULL);

    /* Cleanup */
    g_free(row_pointers);
    g_free(image_data);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(outfile);
    cairo_surface_destroy(composite);

    return PLUGIN_ERROR_NONE;
}
#else
/**
 * Load PNG image (stub when libpng is not available)
 */
static PluginError load_png(ImageDocument* doc, const char* filename) {
    (void)doc;
    (void)filename;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}

/**
 * Save PNG image (stub when libpng is not available)
 */
static PluginError save_png(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}
#endif

/**
 * Check if we can save as PNG
 */
static bool can_save_png(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "png") == 0;
}

/**
 * PNG plugin initialization
 */
bool plugin_init_png(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
#ifdef HAVE_LIBPNG
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "PNG";
    out_plugin->format_info.extensions = "png";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_png;
    out_plugin->callbacks.load = load_png;
    out_plugin->callbacks.can_save = can_save_png;
    out_plugin->callbacks.save = save_png;

    return true;
#else
    (void)host;
    (void)out_plugin;
    /* Plugin not available without libpng */
    return false;
#endif
}
