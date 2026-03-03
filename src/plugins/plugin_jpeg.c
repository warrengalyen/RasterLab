#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(HAVE_LIBJPEG) && defined(HAVE_LCMS2)
#include "color_manager/icc_utils.h"
#endif

#ifdef HAVE_LIBJPEG
#include <jerror.h>
#include <jpeglib.h>

/**
 * JPEG compression method
 */
typedef enum {
    JPEG_COMPRESSION_BASELINE = 0,   /* Standard baseline JPEG */
    JPEG_COMPRESSION_OPTIMIZED = 1,  /* Optimized baseline (Huffman optimization) */
    JPEG_COMPRESSION_PROGRESSIVE = 2 /* Progressive JPEG */
} JPEGCompressionMethod;

/**
 * JPEG chroma subsampling
 */
typedef enum {
    JPEG_SUBSAMPLING_NONE = 0,   /* 4:4:4 - No subsampling (highest quality) */
    JPEG_SUBSAMPLING_LOW = 1,    /* 4:2:0 - Default (good quality/size balance) */
    JPEG_SUBSAMPLING_MEDIUM = 2, /* 4:2:2 - Medium subsampling */
    JPEG_SUBSAMPLING_HIGH = 3    /* 4:1:1 - High subsampling (smallest file) */
} JPEGChromaSubsampling;

/**
 * JPEG color depth
 */
typedef enum {
    JPEG_COLOR_AUTO = 0,     /* Auto-detect (use grayscale if image is grayscale) */
    JPEG_COLOR_RGB = 1,      /* 24-bit color (RGB) */
    JPEG_COLOR_GRAYSCALE = 2 /* 8-bit grayscale */
} JPEGColorDepth;

/**
 * JPEG-specific save options
 */
typedef struct {
    /* Quality setting (0-100, where 0 = lowest quality, 100 = highest quality) */
    int32_t quality;

    /* Compression method */
    JPEGCompressionMethod compression_method;

    /* Chroma subsampling */
    JPEGChromaSubsampling chroma_subsampling;

    /* Color depth */
    JPEGColorDepth color_depth;

    /* Embed thumbnail image (true = embed, false = don't embed) */
    bool embed_thumbnail;

    /* Reserved for future use */
    uint32_t reserved[2];
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
    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF); /* APP2 for ICC profile */
    jpeg_read_header(&cinfo, TRUE);

#if defined(HAVE_LIBJPEG) && defined(HAVE_LCMS2)
    /* JPEG: libjpeg marker parsing. jpeg_save_markers(APP2) + jpeg_read_icc_profile() collects
     * APP2 segments labeled "ICC_PROFILE" and reassembles multi-part ICC in correct sequence.
     * Pass reassembled buffer to icc_profile_from_memory(). Fall back to NULL profile (sRGB) on any failure. */
    {
        JOCTET* icc_data = NULL;
        unsigned int icc_len = 0;
        if (jpeg_read_icc_profile(&cinfo, &icc_data, &icc_len) && icc_data != NULL && icc_len > 0) {
            cmsHPROFILE profile = icc_profile_from_memory(icc_data, (size_t)icc_len);
            free(icc_data);
            icc_data = NULL;
            if (profile) {
                ImageFormatHostAPI* api = plugin_host_api_get();
                if (api && api->document_set_load_icc_profile) {
                    if (api->get_use_embedded_icc && !api->get_use_embedded_icc())
                        icc_destroy(profile);
                    else {
                        g_message("JPEG: embedded ICC profile found, will convert to sRGB");
                        api->document_set_load_icc_profile(doc, profile);
                    }
                } else {
                    icc_destroy(profile);
                }
            } else {
                g_warning("JPEG plugin: Invalid or non-RGB embedded ICC profile; assuming sRGB");
            }
        }
        /* No APP2 ICC or malformed */
    }
#endif

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
        opts->compression_method = JPEG_COMPRESSION_BASELINE;
        opts->chroma_subsampling = JPEG_SUBSAMPLING_LOW; /* Default: 4:2:0 */
        opts->color_depth = JPEG_COLOR_AUTO;
        opts->embed_thumbnail = false;
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
    JPEGCompressionMethod compression_method = JPEG_COMPRESSION_BASELINE;
    JPEGChromaSubsampling chroma_subsampling = JPEG_SUBSAMPLING_LOW;
    JPEGColorDepth color_depth = JPEG_COLOR_AUTO;
    bool embed_thumbnail = false;
    bool is_grayscale = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get JPEG-specific options if provided */
    if (opts && opts->plugin_data) {
        jpeg_opts = (JPEGSaveOptions*)opts->plugin_data;
        quality = (jpeg_opts->quality >= 0 && jpeg_opts->quality <= 100)
                      ? jpeg_opts->quality
                      : 85;
        compression_method = jpeg_opts->compression_method;
        chroma_subsampling = jpeg_opts->chroma_subsampling;
        color_depth = jpeg_opts->color_depth;
        embed_thumbnail = jpeg_opts->embed_thumbnail;
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

    /* Determine if image is grayscale (for auto color depth) */
    if (color_depth == JPEG_COLOR_AUTO) {
        /* Check if image is grayscale by sampling pixels */
        is_grayscale = TRUE;
        for (guint y = 0; y < doc->height && is_grayscale; y += 10) {
            for (guint x = 0; x < doc->width && is_grayscale; x += 10) {
                guchar* pixel = surface_data + y * surface_stride + x * 4;
                guchar b = pixel[0];
                guchar g = pixel[1];
                guchar r = pixel[2];
                /* If R, G, B are not approximately equal, it's not grayscale */
                if (abs((int)r - (int)g) > 2 || abs((int)g - (int)b) > 2) {
                    is_grayscale = FALSE;
                }
            }
        }
    } else if (color_depth == JPEG_COLOR_GRAYSCALE) {
        is_grayscale = TRUE;
    }

    /* Set compression parameters */
    cinfo.image_width = doc->width;
    cinfo.image_height = doc->height;

    if (is_grayscale) {
        cinfo.input_components = 1;
        cinfo.in_color_space = JCS_GRAYSCALE;
    } else {
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;
    }

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    /* Set compression method */
    if (compression_method == JPEG_COMPRESSION_PROGRESSIVE) {
        jpeg_simple_progression(&cinfo);
    } else if (compression_method == JPEG_COMPRESSION_OPTIMIZED) {
        cinfo.optimize_coding = TRUE;
    } else {
        cinfo.optimize_coding = FALSE;
    }

    /* Set chroma subsampling (only for color images) */
    if (!is_grayscale) {
        /* Set subsampling factors based on chroma_subsampling setting */
        switch (chroma_subsampling) {
            case JPEG_SUBSAMPLING_NONE: /* 4:4:4 - No subsampling */
                cinfo.comp_info[0].h_samp_factor = 1;
                cinfo.comp_info[0].v_samp_factor = 1;
                cinfo.comp_info[1].h_samp_factor = 1;
                cinfo.comp_info[1].v_samp_factor = 1;
                cinfo.comp_info[2].h_samp_factor = 1;
                cinfo.comp_info[2].v_samp_factor = 1;
                break;
            case JPEG_SUBSAMPLING_LOW: /* 4:2:0 - Default */
                /* Default is already 4:2:0, but set explicitly */
                cinfo.comp_info[0].h_samp_factor = 2;
                cinfo.comp_info[0].v_samp_factor = 2;
                cinfo.comp_info[1].h_samp_factor = 1;
                cinfo.comp_info[1].v_samp_factor = 1;
                cinfo.comp_info[2].h_samp_factor = 1;
                cinfo.comp_info[2].v_samp_factor = 1;
                break;
            case JPEG_SUBSAMPLING_MEDIUM: /* 4:2:2 */
                cinfo.comp_info[0].h_samp_factor = 2;
                cinfo.comp_info[0].v_samp_factor = 1;
                cinfo.comp_info[1].h_samp_factor = 1;
                cinfo.comp_info[1].v_samp_factor = 1;
                cinfo.comp_info[2].h_samp_factor = 1;
                cinfo.comp_info[2].v_samp_factor = 1;
                break;
            case JPEG_SUBSAMPLING_HIGH: /* 4:1:1 */
                cinfo.comp_info[0].h_samp_factor = 4;
                cinfo.comp_info[0].v_samp_factor = 1;
                cinfo.comp_info[1].h_samp_factor = 1;
                cinfo.comp_info[1].v_samp_factor = 1;
                cinfo.comp_info[2].h_samp_factor = 1;
                cinfo.comp_info[2].v_samp_factor = 1;
                break;
        }
    }

    /* Disable automatic JFIF header if we're embedding a thumbnail */
    /* We'll write our own APP0 marker with thumbnail */
    if (embed_thumbnail && !is_grayscale) {
        cinfo.write_JFIF_header = FALSE;
    }

    /* Start compression */
    jpeg_start_compress(&cinfo, TRUE);

#if defined(HAVE_LIBJPEG) && defined(HAVE_LCMS2)
    /* Embed fresh sRGB ICC on every save (color only); do not preserve original profile */
    if (!is_grayscale) {
        void* icc_data = NULL;
        size_t icc_size = 0;
        cmsHPROFILE srgb = icc_create_srgb_profile();
        if (srgb && icc_profile_to_memory(srgb, &icc_data, &icc_size) && icc_data && icc_size > 0) {
            icc_destroy(srgb);
            jpeg_write_icc_profile(&cinfo, (const JOCTET*)icc_data, (unsigned int)icc_size);
            free(icc_data);
        } else {
            if (srgb)
                icc_destroy(srgb);
            if (icc_data)
                free(icc_data);
        }
    }
#endif

    /* Write JFIF APP0 marker with thumbnail if requested */
    if (embed_thumbnail && !is_grayscale) {
        /* Create thumbnail (typically 1/8th size, max 255x255) */
        guint thumb_width = (doc->width + 7) / 8;
        guint thumb_height = (doc->height + 7) / 8;

        /* Clamp to maximum JFIF thumbnail size */
        if (thumb_width > 255)
            thumb_width = 255;
        if (thumb_height > 255)
            thumb_height = 255;

        if (thumb_width > 0 && thumb_height > 0) {
            /* Allocate thumbnail buffer (RGB, 3 bytes per pixel) */
            guchar* thumb_data = g_malloc(thumb_width * thumb_height * 3);
            if (thumb_data) {
                /* Downsample image to thumbnail size */
                for (guint ty = 0; ty < thumb_height; ty++) {
                    guint sy = (ty * doc->height) / thumb_height;
                    guchar* src_row = surface_data + sy * surface_stride;
                    guchar* dst_row = thumb_data + ty * thumb_width * 3;

                    for (guint tx = 0; tx < thumb_width; tx++) {
                        guint sx = (tx * doc->width) / thumb_width;
                        guchar b = src_row[sx * 4 + 0];
                        guchar g = src_row[sx * 4 + 1];
                        guchar r = src_row[sx * 4 + 2];

                        /* Store as RGB (not BGRA) */
                        dst_row[tx * 3 + 0] = r;
                        dst_row[tx * 3 + 1] = g;
                        dst_row[tx * 3 + 2] = b;
                    }
                }

                /* Build JFIF APP0 marker data */
                /* Structure: "JFIF\0"(5) + version(2) + units(1) +
                 *            xdensity(2) + ydensity(2) + xthumbnail(1) + ythumbnail(1) + data */
                /* Note: jpeg_write_marker adds the marker code and length field automatically */
                guint16 data_length = 5 + 2 + 1 + 2 + 2 + 1 + 1 + (thumb_width * thumb_height * 3);
                guchar* marker_data = g_malloc(data_length);

                if (marker_data) {
                    guchar* p = marker_data;

                    /* Identifier: "JFIF\0" */
                    memcpy(p, "JFIF\0", 5);
                    p += 5;

                    /* Version: 1.1 (major=1, minor=1) */
                    *p++ = 1;
                    *p++ = 1;

                    /* Units: 0 = no units */
                    *p++ = 0;

                    /* X density: 1 (no units) */
                    *p++ = 0;
                    *p++ = 1;

                    /* Y density: 1 (no units) */
                    *p++ = 0;
                    *p++ = 1;

                    /* X thumbnail */
                    *p++ = (guchar)thumb_width;

                    /* Y thumbnail */
                    *p++ = (guchar)thumb_height;

                    /* Thumbnail data (RGB) */
                    memcpy(p, thumb_data, thumb_width * thumb_height * 3);

                    /* Write APP0 marker (libjpeg adds marker code and length automatically) */
                    jpeg_write_marker(&cinfo, JPEG_APP0, marker_data, data_length);

                    g_free(marker_data);
                }

                g_free(thumb_data);
            }
        }
    }

    /* Write scanlines */
    if (is_grayscale) {
        row_stride = cinfo.image_width * 1;
        buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

        for (guint y = 0; y < doc->height; y++) {
            guchar* src_row = surface_data + y * surface_stride;
            guchar* dst_row = buffer[0];

            /* Convert ARGB32 (BGRA in memory) to grayscale using luminance formula */
            for (guint x = 0; x < doc->width; x++) {
                guchar b = src_row[x * 4 + 0];
                guchar g = src_row[x * 4 + 1];
                guchar r = src_row[x * 4 + 2];
                /* ITU-R BT.601 luminance formula: Y = 0.299*R + 0.587*G + 0.114*B */
                guchar gray = (guchar)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
                dst_row[x] = gray;
            }

            jpeg_write_scanlines(&cinfo, buffer, 1);
        }
    } else {
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
