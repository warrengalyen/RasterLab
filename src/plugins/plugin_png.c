#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBPNG
#include <png.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

/**
 * PNG color format options
 */
typedef enum {
    PNG_COLOR_FORMAT_AUTO = 0,
    PNG_COLOR_FORMAT_COLOR = 1,
    PNG_COLOR_FORMAT_GRAYSCALE = 2
} PNGColorFormat;

/**
 * PNG transparency format options
 */
typedef enum {
    PNG_TRANSPARENCY_AUTO = 0,
    PNG_TRANSPARENCY_FULL = 1,
    PNG_TRANSPARENCY_BINARY_CUTOFF = 2,
    PNG_TRANSPARENCY_BINARY_COLOR = 3,
    PNG_TRANSPARENCY_NONE = 4
} PNGTransparencyFormat;

/**
 * PNG depth options
 */
typedef enum {
    PNG_DEPTH_HDR = 0,
    PNG_DEPTH_STANDARD = 1,
    PNG_DEPTH_INDEXED = 2
} PNGDepth;

/**
 * PNG-specific save options
 */
typedef struct {
    /* Compression level (0-9, where 0 = no compression, 9 = maximum compression) */
    int32_t compression_level;

    /* Filter type (PNG_FILTER_NONE, PNG_FILTER_SUB, PNG_FILTER_UP, PNG_FILTER_AVG, PNG_FILTER_PAETH) */
    int32_t filter_type;

    /* Strategy (PNG_Z_DEFAULT_STRATEGY, PNG_Z_FILTERED, PNG_Z_HUFFMAN_ONLY, etc.) */
    int32_t compression_strategy;

    /* Automatic mode: test multiple strategies and select best compression */
    bool automatic_mode;

    /* Embed background color (bKGD chunk) */
    bool embed_background_color;

    /* Background color RGB values (0-255) when embed_background_color is true */
    uint8_t background_color_r;
    uint8_t background_color_g;
    uint8_t background_color_b;

    /* Color format: auto, color, or grayscale */
    PNGColorFormat color_format;

    /* Transparency format: auto, full, binary (by cutoff), binary (by color), or none */
    PNGTransparencyFormat transparency_format;

    /* Alpha cutoff value (0-255) for PNG_TRANSPARENCY_BINARY_CUTOFF */
    uint8_t transparency_cutoff;

    /* Color key RGB values (0-255) for PNG_TRANSPARENCY_BINARY_COLOR */
    uint8_t transparency_color_r;
    uint8_t transparency_color_g;
    uint8_t transparency_color_b;

    /* Depth: HDR, standard, or indexed (used when color_format = color or grayscale) */
    PNGDepth depth;

    /* Palette size (2-256, default 256, used when color_format = grayscale) */
    int palette_size;

    /* Compositing color RGB values (0-255, default rgb(255,255,255), used when transparency_format = binary color, binary cut-off, or none) */
    uint8_t compositing_color_r;
    uint8_t compositing_color_g;
    uint8_t compositing_color_b;

    /* Reserved for future use */
    uint32_t reserved[1];
} PNGSaveOptions;

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
 * Helper function to check if an image is grayscale by sampling pixels
 */
static bool is_image_grayscale(guchar* surface_data, int surface_stride, guint width, guint height) {
    /* Sample pixels to determine if image is grayscale */
    for (guint y = 0; y < height; y += 10) {
        for (guint x = 0; x < width; x += 10) {
            guchar* pixel = surface_data + y * surface_stride + x * 4;
            guchar b = pixel[0];
            guchar g = pixel[1];
            guchar r = pixel[2];
            /* If R, G, B are not approximately equal, it's not grayscale */
            if (abs((int)r - (int)g) > 2 || abs((int)g - (int)b) > 2) {
                return false;
            }
        }
    }
    return true;
}

/**
 * Structure to hold converted image data and metadata
 */
struct converted_image_data {
    png_bytep* row_pointers;
    png_bytep image_data;
    png_uint_32 rowbytes;
    int png_color_type;
    bool needs_trns;
    png_color_16 trns_color; /* For binary transparency by color */
};

/**
 * Convert image data based on PNG save options
 * Returns 0 on success, non-zero on error
 */
static int convert_image_data_for_png(guchar* surface_data,
                                      int surface_stride,
                                      guint width,
                                      guint height,
                                      PNGSaveOptions* png_opts,
                                      bool document_has_alpha,
                                      struct converted_image_data* out) {
    PNGColorFormat color_format;
    PNGTransparencyFormat transparency_format;
    bool is_gray = false;
    bool needs_transparency = false;
    png_uint_32 rowbytes;
    png_bytep* row_pointers = NULL;
    png_bytep image_data = NULL;

    /* Determine color format */
    if (png_opts && png_opts->color_format == PNG_COLOR_FORMAT_AUTO) {
        is_gray = is_image_grayscale(surface_data, surface_stride, width, height);
        color_format = is_gray ? PNG_COLOR_FORMAT_GRAYSCALE : PNG_COLOR_FORMAT_COLOR;
    } else if (png_opts && png_opts->color_format == PNG_COLOR_FORMAT_GRAYSCALE) {
        is_gray = true;
        color_format = PNG_COLOR_FORMAT_GRAYSCALE;
    } else {
        is_gray = false;
        color_format = PNG_COLOR_FORMAT_COLOR;
    }

    /* Determine transparency format */
    if (png_opts && png_opts->transparency_format == PNG_TRANSPARENCY_AUTO) {
        /* Use document's has_alpha flag to determine transparency */
        transparency_format = document_has_alpha ? PNG_TRANSPARENCY_FULL : PNG_TRANSPARENCY_NONE;
    } else if (png_opts) {
        transparency_format = png_opts->transparency_format;
    } else {
        transparency_format = PNG_TRANSPARENCY_NONE;
    }

    /* Determine if we need transparency */
    needs_transparency = (transparency_format == PNG_TRANSPARENCY_FULL ||
                          transparency_format == PNG_TRANSPARENCY_BINARY_CUTOFF ||
                          transparency_format == PNG_TRANSPARENCY_BINARY_COLOR);

    /* Allocate row pointers */
    row_pointers = g_malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        return -1;
    }

    /* Determine rowbytes and allocate image data */
    /* Binary by cutoff uses full alpha channel (RGBA/GrayA), binary by color uses tRNS (RGB/Gray) */
    bool use_alpha_channel = (transparency_format == PNG_TRANSPARENCY_FULL ||
                              transparency_format == PNG_TRANSPARENCY_BINARY_CUTOFF);
    if (is_gray) {
        rowbytes = width * (use_alpha_channel ? 2 : 1);
    } else {
        rowbytes = width * (use_alpha_channel ? 4 : 3);
    }

    image_data = g_malloc(rowbytes * height);
    if (!image_data) {
        g_free(row_pointers);
        return -1;
    }

    /* Convert image data */
    for (guint y = 0; y < height; y++) {
        guchar* src_row = surface_data + y * surface_stride;
        png_bytep dst_row = image_data + y * rowbytes;
        row_pointers[y] = dst_row;

        for (guint x = 0; x < width; x++) {
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];
            guchar original_a = a; /* Store original alpha for compositing */

            /* Un-premultiply alpha: Cairo uses premultiplied alpha, PNG uses straight alpha */
            /* This must be done before handling transparency format */
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

            /* Handle transparency format */
            if (transparency_format == PNG_TRANSPARENCY_BINARY_CUTOFF) {
                a = (a >= (png_opts ? png_opts->transparency_cutoff : 64)) ? 255 : 0;
                /* Composite with compositing color when transparent */
                if (a == 0 && png_opts) {
                    r = png_opts->compositing_color_r;
                    g = png_opts->compositing_color_g;
                    b = png_opts->compositing_color_b;
                }
            } else if (transparency_format == PNG_TRANSPARENCY_BINARY_COLOR) {
                if (png_opts && r == png_opts->transparency_color_r &&
                    g == png_opts->transparency_color_g &&
                    b == png_opts->transparency_color_b) {
                    a = 0;
                    /* Composite with compositing color when transparent */
                    if (png_opts) {
                        r = png_opts->compositing_color_r;
                        g = png_opts->compositing_color_g;
                        b = png_opts->compositing_color_b;
                    }
                } else {
                    a = 255;
                }
            } else if (transparency_format == PNG_TRANSPARENCY_NONE) {
                a = 255;
                /* Composite fully transparent areas with compositing color */
                if (png_opts && original_a == 0) {
                    r = png_opts->compositing_color_r;
                    g = png_opts->compositing_color_g;
                    b = png_opts->compositing_color_b;
                }
            }

            if (is_gray) {
                /* Convert to grayscale using ITU-R BT.601 luminance formula */
                guchar gray = (guchar)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
                if (use_alpha_channel) {
                    /* Grayscale with alpha (2 bytes per pixel) */
                    dst_row[x * 2 + 0] = gray;
                    dst_row[x * 2 + 1] = a;
                } else {
                    /* Grayscale (1 byte per pixel) */
                    dst_row[x] = gray;
                }
            } else {
                /* Color format */
                if (use_alpha_channel) {
                    /* RGB with alpha (4 bytes per pixel: RGBA) */
                    dst_row[x * 4 + 0] = r;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = b;
                    dst_row[x * 4 + 3] = a;
                } else {
                    /* RGB (3 bytes per pixel) */
                    dst_row[x * 3 + 0] = r;
                    dst_row[x * 3 + 1] = g;
                    dst_row[x * 3 + 2] = b;
                }
            }
        }
    }

    /* Determine PNG color type */
    /* Binary by cutoff uses full alpha channel, binary by color uses tRNS chunk */
    if (is_gray) {
        out->png_color_type = use_alpha_channel ? PNG_COLOR_TYPE_GRAY_ALPHA : PNG_COLOR_TYPE_GRAY;
    } else {
        out->png_color_type = use_alpha_channel ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB;
    }

    /* Set tRNS flag only for binary transparency by color (uses tRNS chunk, not alpha channel) */
    out->needs_trns = (transparency_format == PNG_TRANSPARENCY_BINARY_COLOR);

    if (out->needs_trns && transparency_format == PNG_TRANSPARENCY_BINARY_COLOR && png_opts) {
        /* Store color key for tRNS chunk */
        out->trns_color.red = (png_uint_16)(png_opts->transparency_color_r << 8);
        out->trns_color.green = (png_uint_16)(png_opts->transparency_color_g << 8);
        out->trns_color.blue = (png_uint_16)(png_opts->transparency_color_b << 8);
        out->trns_color.gray = 0;
    }

    out->row_pointers = row_pointers;
    out->image_data = image_data;
    out->rowbytes = rowbytes;

    return 0;
}

/**
 * Structure for memory buffer writing
 */
struct png_memory_buffer {
    GByteArray* buffer;
};

/**
 * Write callback for memory buffer
 */
static void png_write_memory_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
    struct png_memory_buffer* mem_buf = (struct png_memory_buffer*)png_get_io_ptr(png_ptr);
    if (mem_buf && mem_buf->buffer) {
        g_byte_array_append(mem_buf->buffer, data, length);
    }
}

/**
 * Flush callback for memory buffer (no-op)
 */
static void png_flush_memory_callback(png_structp png_ptr) {
    (void)png_ptr; /* No-op for memory buffers */
}

/**
 * Save PNG image to memory buffer with specified compression settings
 * Returns allocated GByteArray with PNG data, or NULL on error
 */
static GByteArray* save_png_to_memory(ImageDocument* doc,
                                      guchar* surface_data,
                                      int surface_stride,
                                      png_bytep* row_pointers,
                                      png_bytep image_data,
                                      int compression_level,
                                      int filter_type,
                                      int compression_strategy,
                                      int png_color_type) {
    png_structp png_ptr;
    png_infop info_ptr;
    struct png_error_info error_info = {0};
    struct png_memory_buffer mem_buf;
    GByteArray* result = NULL;
    png_uint_32 rowbytes;

    /* Initialize libpng structures */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, &error_info, png_error_handler, png_warning_handler);
    if (!png_ptr) {
        return NULL;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return NULL;
    }

    /* Set up error handling */
    if (setjmp(error_info.jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        if (result) {
            g_byte_array_free(result, TRUE);
        }
        return NULL;
    }

    /* Initialize memory buffer */
    mem_buf.buffer = g_byte_array_new();
    if (!mem_buf.buffer) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return NULL;
    }

    /* Set up custom write function */
    png_set_write_fn(png_ptr, &mem_buf, png_write_memory_callback, png_flush_memory_callback);

    /* Set compression options */
    png_set_compression_level(png_ptr, compression_level);
    png_set_compression_strategy(png_ptr, compression_strategy);
    png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, filter_type);

    /* Set PNG header with specified color type */
    png_set_IHDR(png_ptr, info_ptr, doc->width, doc->height, 8,
                 png_color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    /* Write info */
    png_write_info(png_ptr, info_ptr);

    /* Calculate rowbytes */
    rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    /* Convert from Cairo's ARGB32 (BGRA in memory) to PNG format */
    /* Note: row_pointers and image_data should already be set up by caller */
    /* This function just writes the data that's already in the correct format */

    /* Write image data */
    png_write_image(png_ptr, row_pointers);

    /* Finish writing */
    png_write_end(png_ptr, NULL);

    /* Get the result */
    result = mem_buf.buffer;

    /* Cleanup */
    png_destroy_write_struct(&png_ptr, &info_ptr);

    return result;
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
    PNGSaveOptions* png_opts = NULL;
    int compression_level = 9;
    int filter_type = PNG_FILTER_NONE;
    int compression_strategy = PNG_Z_DEFAULT_STRATEGY;

    /* Get PNG-specific options if provided */
    bool automatic_mode = false;
    if (opts && opts->plugin_data) {
        png_opts = (PNGSaveOptions*)opts->plugin_data;
        compression_level = (png_opts->compression_level >= 0 && png_opts->compression_level <= 12)
                                ? png_opts->compression_level
                                : 9;
        filter_type = png_opts->filter_type;
        compression_strategy = png_opts->compression_strategy;
        automatic_mode = png_opts->automatic_mode;
    } else if (opts && opts->compression_level >= 0 && opts->compression_level <= 9) {
        /* Fallback to base SaveOptions compression_level if plugin_data not provided */
        compression_level = opts->compression_level;
    }

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

    struct converted_image_data converted = {0};
    int png_color_type = PNG_COLOR_TYPE_RGB_ALPHA; /* Default to RGBA */
    bool use_converted_data = false;

    /* Use advanced conversion if PNG options are provided (for both manual and automatic mode) */
    /* This ensures transparency is properly handled when PNG_TRANSPARENCY_AUTO is set */
    if (png_opts) {
        /* Convert image data based on options */
        if (convert_image_data_for_png(surface_data, surface_stride, doc->width, doc->height,
                                       png_opts, doc->has_alpha, &converted) == 0) {
            row_pointers = converted.row_pointers;
            image_data = converted.image_data;
            png_color_type = converted.png_color_type;
            use_converted_data = true;
        }
    }

    /* For automatic mode or if conversion failed, use default format */
    if (!use_converted_data) {
        /* Determine color type based on document's has_alpha flag */
        if (doc->has_alpha) {
            png_color_type = PNG_COLOR_TYPE_RGB_ALPHA; /* RGBA = 4 bytes per pixel */
        } else {
            png_color_type = PNG_COLOR_TYPE_RGB; /* RGB = 3 bytes per pixel */
        }

        /* Pre-allocate image data buffers (needed for automatic mode or fallback) */
        png_uint_32 bytes_per_pixel = (png_color_type == PNG_COLOR_TYPE_RGB_ALPHA) ? 4 : 3;
        png_uint_32 rowbytes = (png_uint_32)(doc->width * bytes_per_pixel);
        row_pointers = g_malloc(sizeof(png_bytep) * doc->height);
        if (!row_pointers) {
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        image_data = g_malloc(rowbytes * doc->height);
        if (!image_data) {
            g_free(row_pointers);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Convert from Cairo's ARGB32 (BGRA in memory, premultiplied alpha) to PNG format (non-premultiplied) */
        for (guint y = 0; y < doc->height; y++) {
            guchar* src_row = surface_data + y * surface_stride;
            png_bytep dst_row = image_data + y * rowbytes;
            row_pointers[y] = dst_row;

            for (guint x = 0; x < doc->width; x++) {
                guchar b = src_row[x * 4 + 0];
                guchar g = src_row[x * 4 + 1];
                guchar r = src_row[x * 4 + 2];
                guchar a = src_row[x * 4 + 3];

                /* Un-premultiply alpha: Cairo uses premultiplied alpha, PNG uses straight alpha */
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

                if (png_color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                    /* PNG RGBA format */
                    dst_row[x * 4 + 0] = r;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = b;
                    dst_row[x * 4 + 3] = a;
                } else {
                    /* PNG RGB format (no alpha) */
                    dst_row[x * 3 + 0] = r;
                    dst_row[x * 3 + 1] = g;
                    dst_row[x * 3 + 2] = b;
                }
            }
        }
    }

    /* If automatic mode, test multiple strategies and pick the best */
    if (automatic_mode) {
        GByteArray* best_result = NULL;
        size_t best_size = SIZE_MAX;
        int best_filter = PNG_FILTER_NONE;
        int best_strategy = PNG_Z_DEFAULT_STRATEGY;

        /* Test a limited set of filter types and compression strategies for better performance */
        /* PNG_FILTER_SUB and PNG_FILTER_PAETH are typically the most effective */
        int filter_types[] = {
            PNG_FILTER_NONE,
            PNG_FILTER_SUB,
            PNG_FILTER_PAETH};
        int strategies[] = {
            PNG_Z_DEFAULT_STRATEGY
#ifdef HAVE_ZLIB
            ,
            Z_FILTERED
#endif
        };

        /* Use maximum compression level for automatic mode */
        int test_compression_level = 9;

        for (guint f = 0; f < G_N_ELEMENTS(filter_types); f++) {
            for (guint s = 0; s < G_N_ELEMENTS(strategies); s++) {
                GByteArray* test_result = save_png_to_memory(doc, surface_data, surface_stride,
                                                             row_pointers, image_data,
                                                             test_compression_level,
                                                             filter_types[f],
                                                             strategies[s],
                                                             png_color_type);
                if (test_result && test_result->len < best_size) {
                    /* Found a better compression */
                    if (best_result) {
                        g_byte_array_free(best_result, TRUE);
                    }
                    best_result = test_result;
                    best_size = test_result->len;
                    best_filter = filter_types[f];
                    best_strategy = strategies[s];
                } else if (test_result) {
                    g_byte_array_free(test_result, TRUE);
                }
            }
        }

        if (!best_result) {
            /* All tests failed, fall back to default */
            g_free(row_pointers);
            g_free(image_data);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }

        /* Use the best settings found */
        filter_type = best_filter;
        compression_strategy = best_strategy;
        compression_level = test_compression_level;

        /* Write best result to file */
        outfile = g_fopen(filename, "wb");
        if (!outfile) {
            g_byte_array_free(best_result, TRUE);
            g_free(row_pointers);
            g_free(image_data);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }

        if (fwrite(best_result->data, 1, best_result->len, outfile) != best_result->len) {
            g_byte_array_free(best_result, TRUE);
            g_free(row_pointers);
            g_free(image_data);
            fclose(outfile);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }

        g_byte_array_free(best_result, TRUE);
        fclose(outfile);
        g_free(row_pointers);
        g_free(image_data);
        cairo_surface_destroy(composite);

        return PLUGIN_ERROR_NONE;
    }

    /* Regular mode: use specified settings */
    /* Open output file */
    outfile = g_fopen(filename, "wb");
    if (!outfile) {
        g_free(row_pointers);
        g_free(image_data);
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
        if (use_converted_data) {
            /* Free converted data */
            if (converted.row_pointers) {
                g_free(converted.row_pointers);
            }
            if (converted.image_data) {
                g_free(converted.image_data);
            }
        } else {
            /* Free regular data */
            if (row_pointers) {
                g_free(row_pointers);
            }
            if (image_data) {
                g_free(image_data);
            }
        }
        fclose(outfile);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Initialize I/O */
    png_init_io(png_ptr, outfile);

    /* Set compression options */
    png_set_compression_level(png_ptr, compression_level);
    png_set_compression_strategy(png_ptr, compression_strategy);
    png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, filter_type);

    /* Set PNG header with the determined color type */
    png_set_IHDR(png_ptr, info_ptr, doc->width, doc->height, 8,
                 png_color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    /* Add bKGD chunk if requested */
    if (use_converted_data && png_opts && png_opts->embed_background_color) {
        png_color_16 background;
        if (png_color_type == PNG_COLOR_TYPE_GRAY || png_color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            /* For grayscale, use the grayscale value of the background color */
            guchar gray = (guchar)(0.299 * png_opts->background_color_r +
                                   0.587 * png_opts->background_color_g +
                                   0.114 * png_opts->background_color_b + 0.5);
            background.gray = (png_uint_16)(gray << 8);
            background.red = background.green = background.blue = 0;
        } else {
            /* For color formats, use RGB values */
            background.red = (png_uint_16)(png_opts->background_color_r << 8);
            background.green = (png_uint_16)(png_opts->background_color_g << 8);
            background.blue = (png_uint_16)(png_opts->background_color_b << 8);
            background.gray = 0;
        }
        background.index = 0;
        png_set_bKGD(png_ptr, info_ptr, &background);
    }

    /* Add tRNS chunk if needed (for binary transparency by color) */
    if (use_converted_data && converted.needs_trns) {
        png_set_tRNS(png_ptr, info_ptr, NULL, 0, &converted.trns_color);
    }

    /* Write info */
    png_write_info(png_ptr, info_ptr);

    /* Use rowbytes from converted data if available, otherwise calculate */
    png_uint_32 rowbytes;
    if (use_converted_data) {
        rowbytes = converted.rowbytes;
        /* Row pointers are already set in converted data */
    } else {
        rowbytes = png_get_rowbytes(png_ptr, info_ptr);
        /* Ensure row_pointers point to the correct locations */
        for (guint y = 0; y < doc->height; y++) {
            row_pointers[y] = image_data + y * rowbytes;
        }
    }

    /* Write image data */
    png_write_image(png_ptr, row_pointers);

    /* Finish writing */
    png_write_end(png_ptr, NULL);

    /* Cleanup */
    if (use_converted_data) {
        /* Free converted data */
        if (converted.row_pointers) {
            g_free(converted.row_pointers);
        }
        if (converted.image_data) {
            g_free(converted.image_data);
        }
    } else {
        /* Free regular data */
        g_free(row_pointers);
        g_free(image_data);
    }
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

#ifdef HAVE_LIBPNG
/**
 * Get size of PNG-specific save options structure
 */
static size_t get_png_save_options_size(void) {
    return sizeof(PNGSaveOptions);
}

/**
 * Initialize PNG-specific save options with defaults
 */
static void init_png_save_options(void* plugin_data) {
    PNGSaveOptions* opts = (PNGSaveOptions*)plugin_data;
    if (opts) {
        opts->compression_level = 9;
        opts->filter_type = PNG_FILTER_NONE;
        opts->compression_strategy = PNG_Z_DEFAULT_STRATEGY;
        opts->automatic_mode = true;
        opts->embed_background_color = false;
        opts->background_color_r = 255;
        opts->background_color_g = 255;
        opts->background_color_b = 255;
        opts->color_format = PNG_COLOR_FORMAT_AUTO;
        opts->transparency_format = PNG_TRANSPARENCY_AUTO;
        opts->transparency_cutoff = 64;
        opts->transparency_color_r = 255;
        opts->transparency_color_g = 0;
        opts->transparency_color_b = 255;
        opts->depth = PNG_DEPTH_STANDARD;
        opts->palette_size = 256;
        opts->compositing_color_r = 255;
        opts->compositing_color_g = 255;
        opts->compositing_color_b = 255;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}
#else
/**
 * Get size of PNG-specific save options structure (stub when libpng not available)
 */
static size_t get_png_save_options_size(void) {
    return 0;
}

/**
 * Initialize PNG-specific save options with defaults (stub when libpng not available)
 */
static void init_png_save_options(void* plugin_data) {
    (void)plugin_data;
}
#endif

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
    out_plugin->format_info.name = "PNG - Portable Network Graphic";
    out_plugin->format_info.extensions = "png";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_png;
    out_plugin->callbacks.load = load_png;
    out_plugin->callbacks.can_save = can_save_png;
    out_plugin->callbacks.save = save_png;
    out_plugin->callbacks.get_save_options_size = get_png_save_options_size;
    out_plugin->callbacks.init_save_options = init_png_save_options;

    return true;
#else
    (void)host;
    (void)out_plugin;
    /* Plugin not available without libpng */
    return false;
#endif
}
