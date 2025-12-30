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

#ifdef HAVE_LIBJPEG
#include <jerror.h>
#include <jpeglib.h>

/**
 * JPEG-specific save options
 */
typedef struct {
    /* Quality setting (0-100, where 0 = lowest quality, 100 = highest quality) */
    int32_t quality;

    /* Progressive JPEG encoding (true = progressive, false = baseline) */
    bool progressive;

    /* Optimize Huffman tables (slower encoding, smaller file) */
    bool optimize_huffman;

    /* Reserved for future use */
    uint32_t reserved[4];
} JPEGSaveOptions;

/* JPEG file signature */
static const uint8_t JPEG_SIGNATURE_SOI[2] = {0xFF, 0xD8};

/**
 * Error handling structure for libjpeg
 */
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr* my_error_ptr;

/**
 * Error handler for libjpeg
 */
static void my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}
#else
/* JPEG file signature (for can_load even when libjpeg not available) */
static const uint8_t JPEG_SIGNATURE_SOI[2] = {0xFF, 0xD8};
#endif

/**
 * Check if file is JPEG format
 */
static bool can_load_jpeg(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 2) {
        return false;
    }

    return memcmp(header, JPEG_SIGNATURE_SOI, 2) == 0;
}

#ifdef HAVE_LIBJPEG
/**
 * Load JPEG image using libjpeg
 */
static PluginError load_jpeg(ImageDocument* doc, const char* filename) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    FILE* infile;
    JSAMPARRAY buffer;
    int row_stride;
    ImageLayer* base_layer;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open JPEG file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Set up error handling */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Initialize JPEG decompression */
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);

    /* Start decompression */
    jpeg_start_decompress(&cinfo);

    /* Set document metadata */
    doc->width = cinfo.output_width;
    doc->height = cinfo.output_height;
    doc->channels = cinfo.output_components; /* 3 for RGB */
    doc->bit_depth = 8;
    doc->has_alpha = FALSE; /* JPEG doesn't support alpha */

    /* Free old layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Create base layer */
    base_layer = layer_new("Background", doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    /* Read scanlines */
    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    int y = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);

        /* Convert RGB to ARGB32 (BGRA in memory for Cairo) */
        guchar* src_row = buffer[0];
        guchar* dst_row = surface_data + y * surface_stride;

        for (guint x = 0; x < doc->width; x++) {
            guchar r = src_row[x * 3 + 0];
            guchar g = src_row[x * 3 + 1];
            guchar b = src_row[x * 3 + 2];

            /* Cairo ARGB32: BGRA in memory (little-endian) */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = 0xFF; /* Alpha = opaque */
        }
        y++;
    }

    cairo_surface_mark_dirty(temp_surface);

    /* Finish decompression */
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    return PLUGIN_ERROR_NONE;
}
#else
/**
 * Load JPEG image (stub when libjpeg is not available)
 */
static PluginError load_jpeg(ImageDocument* doc, const char* filename) {
    (void)doc;
    (void)filename;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}
#endif

/**
 * Check if we can save as JPEG
 */
static bool can_save_jpeg(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "jpg") == 0 ||
           g_ascii_strcasecmp(ext + 1, "jpeg") == 0;
}

#ifdef HAVE_LIBJPEG
/**
 * Get size of JPEG-specific save options structure
 */
static size_t get_jpeg_save_options_size(void) {
    return sizeof(JPEGSaveOptions);
}

/**
 * Initialize JPEG-specific save options with defaults
 */
static void init_jpeg_save_options(void* plugin_data) {
    JPEGSaveOptions* opts = (JPEGSaveOptions*)plugin_data;
    if (opts) {
        opts->quality = 85; /* Default quality */
        opts->progressive = false;
        opts->optimize_huffman = false;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}

/**
 * Save JPEG image using libjpeg
 */
static PluginError save_jpeg(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite;
    cairo_surface_t* flattened;
    struct jpeg_compress_struct cinfo;
    struct my_error_mgr jerr;
    FILE* outfile;
    JSAMPARRAY buffer;
    int row_stride;
    guchar* surface_data;
    int surface_stride;
    gint quality;
    JPEGSaveOptions* jpeg_opts = NULL;
    bool progressive = false;
    bool optimize_huffman = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get JPEG-specific options if provided */
    if (opts && opts->plugin_data) {
        jpeg_opts = (JPEGSaveOptions*)opts->plugin_data;
        quality = (jpeg_opts->quality >= 0 && jpeg_opts->quality <= 100)
                      ? jpeg_opts->quality
                      : 85;
        progressive = jpeg_opts->progressive;
        optimize_huffman = jpeg_opts->optimize_huffman;
    } else if (opts && opts->quality >= 0) {
        /* Fallback to base SaveOptions quality if plugin_data not provided */
        quality = (opts->quality > 100) ? 100 : opts->quality;
    } else {
        quality = 85; /* Default quality */
    }

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Flatten to white background */
    flattened = compositor_flatten_to_white_background(composite, doc->width, doc->height);
    cairo_surface_destroy(composite);

    if (!flattened) {
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Open output file */
    outfile = g_fopen(filename, "wb");
    if (!outfile) {
        cairo_surface_destroy(flattened);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Set up error handling */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_compress(&cinfo);
        fclose(outfile);
        cairo_surface_destroy(flattened);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Initialize JPEG compression */
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);

    /* Get surface data */
    cairo_surface_flush(flattened);
    surface_data = cairo_image_surface_get_data(flattened);
    surface_stride = cairo_image_surface_get_stride(flattened);

    /* Set compression parameters */
    cinfo.image_width = doc->width;
    cinfo.image_height = doc->height;
    cinfo.input_components = 3; /* RGB */
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    /* Set progressive encoding if requested */
    if (progressive) {
        jpeg_simple_progression(&cinfo);
    }

    /* Set optimization if requested */
    cinfo.optimize_coding = optimize_huffman ? TRUE : FALSE;

    /* Start compression */
    jpeg_start_compress(&cinfo, TRUE);

    /* Write scanlines */
    row_stride = cinfo.image_width * 3;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    for (guint y = 0; y < doc->height; y++) {
        guchar* src_row = surface_data + y * surface_stride;
        guchar* dst_row = buffer[0];

        /* Convert ARGB32 (BGRA in memory) to RGB */
        for (guint x = 0; x < doc->width; x++) {
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];

            dst_row[x * 3 + 0] = r;
            dst_row[x * 3 + 1] = g;
            dst_row[x * 3 + 2] = b;
        }

        jpeg_write_scanlines(&cinfo, buffer, 1);
    }

    /* Finish compression */
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);
    cairo_surface_destroy(flattened);

    return PLUGIN_ERROR_NONE;
}
#else
/**
 * Save JPEG image (stub when libjpeg is not available)
 */
static PluginError save_jpeg(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}

/**
 * Get size of JPEG-specific save options structure (stub when libjpeg not available)
 */
static size_t get_jpeg_save_options_size(void) {
    return 0;
}

/**
 * Initialize JPEG-specific save options with defaults (stub when libjpeg not available)
 */
static void init_jpeg_save_options(void* plugin_data) {
    (void)plugin_data;
}
#endif

/**
 * JPEG plugin initialization
 */
bool plugin_init_jpeg(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
#ifdef HAVE_LIBJPEG
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "JPEG - Joint Photographic Experts Group";
    out_plugin->format_info.extensions = "jpg,jpeg";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_jpeg;
    out_plugin->callbacks.load = load_jpeg;
    out_plugin->callbacks.can_save = can_save_jpeg;
    out_plugin->callbacks.save = save_jpeg;
    out_plugin->callbacks.get_save_options_size = get_jpeg_save_options_size;
    out_plugin->callbacks.init_save_options = init_jpeg_save_options;

    return true;
#else
    (void)host;
    (void)out_plugin;
    /* Plugin not available without libjpeg */
    return false;
#endif
}
