#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBTIFF
#include <tiff.h>
#include <tiffio.h>

/**
 * TIFF color compression options
 */
typedef enum {
    TIFF_COLOR_COMPRESSION_AUTO = 0,
    TIFF_COLOR_COMPRESSION_LZW = 1,
    TIFF_COLOR_COMPRESSION_ZIP = 2,
    TIFF_COLOR_COMPRESSION_NONE = 3
} TIFFColorCompression;

/**
 * TIFF monochrome compression options
 */
typedef enum {
    TIFF_MONO_COMPRESSION_AUTO = 0,
    TIFF_MONO_COMPRESSION_CCITT_FAX4 = 1,
    TIFF_MONO_COMPRESSION_CCITT_FAX3 = 2,
    TIFF_MONO_COMPRESSION_LZW = 3,
    TIFF_MONO_COMPRESSION_NONE = 4
} TIFFMonochromeCompression;

/**
 * TIFF color format options
 */
typedef enum {
    TIFF_COLOR_FORMAT_AUTO = 0,
    TIFF_COLOR_FORMAT_COLOR = 1,
    TIFF_COLOR_FORMAT_GRAYSCALE = 2
} TIFFColorFormat;

/**
 * TIFF transparency format options
 */
typedef enum {
    TIFF_TRANSPARENCY_AUTO = 0,
    TIFF_TRANSPARENCY_FULL = 1,
    TIFF_TRANSPARENCY_BINARY_CUTOFF = 2,
    TIFF_TRANSPARENCY_BINARY_COLOR = 3,
    TIFF_TRANSPARENCY_NONE = 4
} TIFFTransparencyFormat;

/**
 * TIFF-specific save options
 */
typedef struct {
    /* Color compression: auto, LZW, ZIP, none */
    TIFFColorCompression color_compression;

    /* Monochrome compression: auto, CCITT Fax 4, CCITT Fax 3, LZW, none */
    TIFFMonochromeCompression monochrome_compression;

    /* Color format: auto, color, grayscale */
    TIFFColorFormat color_format;

    /* Transparency format: auto, full, binary (cut-off), binary (color), none */
    TIFFTransparencyFormat transparency_format;

    /* Alpha cutoff value (0-255) for TIFF_TRANSPARENCY_BINARY_CUTOFF */
    uint8_t transparency_cutoff;

    /* Color key RGB values (0-255) for TIFF_TRANSPARENCY_BINARY_COLOR */
    uint8_t transparency_color_r;
    uint8_t transparency_color_g;
    uint8_t transparency_color_b;

    /* Compositing color RGB values (0-255, default rgb(255,255,255), used when transparency_format = binary color, binary cut-off, or none) */
    uint8_t compositing_color_r;
    uint8_t compositing_color_g;
    uint8_t compositing_color_b;

    /* Reserved for future use */
    uint32_t reserved[3];
} TIFFSaveOptions;

/* TIFF file signature (II for Intel byte order, MM for Motorola) */
static const uint8_t TIFF_SIGNATURE_II[4] = {0x49, 0x49, 0x2A, 0x00}; /* "II*\0" */
static const uint8_t TIFF_SIGNATURE_MM[4] = {0x4D, 0x4D, 0x00, 0x2A}; /* "MM\0*" */

#else
/* TIFF file signature (for can_load even when libtiff not available) */
static const uint8_t TIFF_SIGNATURE_II[4] = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t TIFF_SIGNATURE_MM[4] = {0x4D, 0x4D, 0x00, 0x2A};
#endif

/**
 */
static bool can_load_tiff(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 4) {
        return false;
    }

    return memcmp(header, TIFF_SIGNATURE_II, 4) == 0 ||
           memcmp(header, TIFF_SIGNATURE_MM, 4) == 0;
}

#ifdef HAVE_LIBTIFF
/**
 * Error handler for libtiff
 */
static void tiff_error_handler(const char* module, const char* fmt, va_list ap) {
    (void)module;
    (void)fmt;
    (void)ap;
    /* Errors are handled via return codes */
}

/**
 * Warning handler for libtiff
 */
static void tiff_warning_handler(const char* module, const char* fmt, va_list ap) {
    (void)module;
    (void)fmt;
    (void)ap;
    /* Warnings are ignored for now */
}

/**
 * Load TIFF image using libtiff
 */
static PluginError load_tiff(ImageDocument* doc, const char* filename) {
    TIFF* tif;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint32_t width, height;
    uint16_t photometric, samples_per_pixel, bits_per_sample, planar_config;
    uint16_t *red = NULL, *green = NULL, *blue = NULL;
    uint32_t* raster = NULL;
    guchar* image_data = NULL;

    if (!doc || !filename) {
        g_warning("TIFF plugin: Invalid parameters");
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Set error handlers */
    TIFFSetErrorHandler(tiff_error_handler);
    TIFFSetWarningHandler(tiff_warning_handler);

    /* Open TIFF file */
    /* Note: TIFFOpen automatically reads the first directory, so we don't need to call TIFFReadDirectory */
    tif = TIFFOpen(filename, "r");
    if (!tif) {
        g_warning("TIFF plugin: Failed to open file: %s", filename);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Get image dimensions */
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
        g_warning("TIFF plugin: Failed to get image dimensions");
        TIFFClose(tif);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    if (width == 0 || height == 0) {
        g_warning("TIFF plugin: Invalid dimensions: %ux%u", width, height);
        TIFFClose(tif);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Get image properties */
    if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric)) {
        photometric = PHOTOMETRIC_RGB;
    }
    if (!TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel)) {
        samples_per_pixel = 1;
    }
    if (!TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample)) {
        bits_per_sample = 8;
    }
    if (!TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &planar_config)) {
        planar_config = PLANARCONFIG_CONTIG;
    }

    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = samples_per_pixel;
    doc->bit_depth = bits_per_sample;
    doc->has_alpha = (samples_per_pixel == 4 || samples_per_pixel == 2);

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
        g_warning("TIFF plugin: layer_new returned NULL");
        TIFFClose(tif);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        g_warning("TIFF plugin: base_layer->surface is NULL");
        TIFFClose(tif);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        g_warning("TIFF plugin: cairo_image_surface_get_data returned NULL");
        TIFFClose(tif);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Read image using TIFFReadRGBAImage which handles most formats (8-bit and 16-bit) */
    /* TIFFReadRGBAImage always returns 32-bit RGBA values regardless of input bit depth */
    uint32_t npixels = width * height;
    raster = (uint32_t*)_TIFFmalloc(npixels * sizeof(uint32_t));
    if (!raster) {
        g_warning("TIFF plugin: Failed to allocate raster");
        TIFFClose(tif);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Read image - TIFFReadRGBAImage handles 8-bit and 16-bit automatically */
    if (!TIFFReadRGBAImage(tif, width, height, raster, 0)) {
        g_warning("TIFF plugin: Failed to read RGBA image");
        _TIFFfree(raster);
        TIFFClose(tif);
        layer_free(base_layer);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Allocate image data buffer */
    image_data = g_malloc(width * height * 4);
    if (!image_data) {
        g_warning("TIFF plugin: Failed to allocate image data");
        _TIFFfree(raster);
        TIFFClose(tif);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert from TIFF RGBA (ABGR in memory) to our RGBA format */
    /* TIFFReadRGBAImage returns pixels in bottom-to-top order, so we flip vertically */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t pixel = raster[(height - 1 - y) * width + x]; /* Flip vertically */
            guchar r = TIFFGetR(pixel);
            guchar g = TIFFGetG(pixel);
            guchar b = TIFFGetB(pixel);
            guchar a = TIFFGetA(pixel);

            image_data[(y * width + x) * 4 + 0] = r;
            image_data[(y * width + x) * 4 + 1] = g;
            image_data[(y * width + x) * 4 + 2] = b;
            image_data[(y * width + x) * 4 + 3] = a;
        }
    }

    _TIFFfree(raster);

    if (!image_data) {
        TIFFClose(tif);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert RGBA (straight alpha) to Cairo's ARGB32 (BGRA in memory, premultiplied alpha) */
    for (uint32_t y = 0; y < height; y++) {
        guchar* src_row = image_data + y * width * 4;
        guchar* dst_row = surface_data + y * surface_stride;

        for (uint32_t x = 0; x < width; x++) {
            guchar r = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar b = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];

            /* Premultiply alpha: TIFF uses straight alpha, Cairo uses premultiplied alpha */
            if (a == 0) {
                r = g = b = 0;
            } else if (a < 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }

            /* Cairo ARGB32: BGRA in memory (little-endian) */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
        }
    }

    cairo_surface_mark_dirty(temp_surface);

    /* Cleanup */
    g_free(image_data);
    TIFFClose(tif);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    return PLUGIN_ERROR_NONE;
}

/**
 * Helper function to check if an image is grayscale by sampling pixels
 */
static bool is_image_grayscale(guchar* surface_data, int surface_stride, guint width, guint height) {
    for (guint y = 0; y < height; y += 10) {
        for (guint x = 0; x < width; x += 10) {
            guchar* pixel = surface_data + y * surface_stride + x * 4;
            guchar b = pixel[0];
            guchar g = pixel[1];
            guchar r = pixel[2];
            if (abs((int)r - (int)g) > 2 || abs((int)g - (int)b) > 2) {
                return false;
            }
        }
    }
    return true;
}

/**
 * Save TIFF image using libtiff
 */
static PluginError save_tiff(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    TIFF* tif;
    cairo_surface_t* composite;
    guchar* surface_data;
    int surface_stride;
    TIFFSaveOptions* tiff_opts = NULL;
    TIFFColorCompression color_compression = TIFF_COLOR_COMPRESSION_AUTO;
    TIFFMonochromeCompression mono_compression = TIFF_MONO_COMPRESSION_AUTO;
    TIFFColorFormat color_format = TIFF_COLOR_FORMAT_AUTO;
    TIFFTransparencyFormat transparency_format = TIFF_TRANSPARENCY_AUTO;
    bool is_grayscale = false;
    bool has_alpha = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get TIFF-specific options if provided */
    if (opts && opts->plugin_data) {
        tiff_opts = (TIFFSaveOptions*)opts->plugin_data;
        color_compression = tiff_opts->color_compression;
        mono_compression = tiff_opts->monochrome_compression;
        color_format = tiff_opts->color_format;
        transparency_format = tiff_opts->transparency_format;
    }

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    cairo_surface_flush(composite);
    surface_data = cairo_image_surface_get_data(composite);
    surface_stride = cairo_image_surface_get_stride(composite);

    if (!surface_data) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Check composite surface for transparency */
    for (guint y = 0; y < doc->height && !has_alpha; y++) {
        guchar* row = surface_data + y * surface_stride;
        for (guint x = 0; x < doc->width; x++) {
            guchar a = row[x * 4 + 3];
            if (a < 255) {
                has_alpha = true;
                break;
            }
        }
    }

    /* Determine color format */
    if (color_format == TIFF_COLOR_FORMAT_AUTO) {
        is_grayscale = is_image_grayscale(surface_data, surface_stride, doc->width, doc->height);
    } else if (color_format == TIFF_COLOR_FORMAT_GRAYSCALE) {
        is_grayscale = true;
    } else {
        is_grayscale = false;
    }

    /* Determine transparency format */
    if (transparency_format == TIFF_TRANSPARENCY_AUTO) {
        transparency_format = has_alpha ? TIFF_TRANSPARENCY_FULL : TIFF_TRANSPARENCY_NONE;
    }

    /* Open output file */
    tif = TIFFOpen(filename, "w");
    if (!tif) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Set basic TIFF tags */
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, doc->width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, doc->height);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

    /* Determine compression */
    uint16_t compression = COMPRESSION_NONE;
    if (is_grayscale) {
        /* Monochrome compression */
        if (mono_compression == TIFF_MONO_COMPRESSION_AUTO) {
            /* Auto: use LZW for grayscale */
            compression = COMPRESSION_LZW;
        } else {
            switch (mono_compression) {
                case TIFF_MONO_COMPRESSION_CCITT_FAX4:
                    compression = COMPRESSION_CCITTFAX4;
                    break;
                case TIFF_MONO_COMPRESSION_CCITT_FAX3:
                    compression = COMPRESSION_CCITTFAX3;
                    break;
                case TIFF_MONO_COMPRESSION_LZW:
                    compression = COMPRESSION_LZW;
                    break;
                case TIFF_MONO_COMPRESSION_NONE:
                    compression = COMPRESSION_NONE;
                    break;
                default:
                    compression = COMPRESSION_LZW;
                    break;
            }
        }
    } else {
        /* Color compression */
        if (color_compression == TIFF_COLOR_COMPRESSION_AUTO) {
            /* Auto: use LZW for color */
            compression = COMPRESSION_LZW;
        } else {
            switch (color_compression) {
                case TIFF_COLOR_COMPRESSION_LZW:
                    compression = COMPRESSION_LZW;
                    break;
                case TIFF_COLOR_COMPRESSION_ZIP:
                    compression = COMPRESSION_DEFLATE;
                    break;
                case TIFF_COLOR_COMPRESSION_NONE:
                    compression = COMPRESSION_NONE;
                    break;
                default:
                    compression = COMPRESSION_LZW;
                    break;
            }
        }
    }
    TIFFSetField(tif, TIFFTAG_COMPRESSION, compression);

    /* Set photometric and samples per pixel */
    /* Binary by cutoff uses full alpha channel, binary by color also uses alpha channel */
    bool use_alpha = (transparency_format == TIFF_TRANSPARENCY_FULL ||
                      transparency_format == TIFF_TRANSPARENCY_BINARY_CUTOFF ||
                      transparency_format == TIFF_TRANSPARENCY_BINARY_COLOR);
    if (is_grayscale) {
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, use_alpha ? 2 : 1);
    } else {
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, use_alpha ? 4 : 3);
    }

    /* Allocate scanline buffer */
    uint32_t bytes_per_pixel = is_grayscale ? (use_alpha ? 2 : 1) : (use_alpha ? 4 : 3);
    tdata_t scanline = _TIFFmalloc(doc->width * bytes_per_pixel);
    if (!scanline) {
        TIFFClose(tif);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Write scanlines */
    for (guint y = 0; y < doc->height; y++) {
        guchar* src_row = surface_data + y * surface_stride;
        guchar* dst_row = (guchar*)scanline;

        for (guint x = 0; x < doc->width; x++) {
            guchar b = src_row[x * 4 + 0];
            guchar g = src_row[x * 4 + 1];
            guchar r = src_row[x * 4 + 2];
            guchar a = src_row[x * 4 + 3];
            guchar original_a = a;

            /* Un-premultiply alpha: Cairo uses premultiplied alpha, TIFF uses straight alpha */
            if (a == 0) {
                r = g = b = 0;
            } else if (a < 255) {
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

            /* Handle transparency format */
            /* Note: TIFF_TRANSPARENCY_FULL preserves alpha and RGB as-is (no modification needed) */
            if (transparency_format == TIFF_TRANSPARENCY_BINARY_CUTOFF) {
                a = (a >= (tiff_opts ? tiff_opts->transparency_cutoff : 64)) ? 255 : 0;
                /* Composite with compositing color when transparent */
                if (a == 0 && tiff_opts) {
                    r = tiff_opts->compositing_color_r;
                    g = tiff_opts->compositing_color_g;
                    b = tiff_opts->compositing_color_b;
                }
            } else if (transparency_format == TIFF_TRANSPARENCY_BINARY_COLOR) {
                if (tiff_opts && r == tiff_opts->transparency_color_r &&
                    g == tiff_opts->transparency_color_g &&
                    b == tiff_opts->transparency_color_b) {
                    a = 0;
                    /* Composite with compositing color when transparent */
                    if (tiff_opts) {
                        r = tiff_opts->compositing_color_r;
                        g = tiff_opts->compositing_color_g;
                        b = tiff_opts->compositing_color_b;
                    }
                } else {
                    a = 255;
                }
            } else if (transparency_format == TIFF_TRANSPARENCY_NONE) {
                a = 255;
                /* Composite fully transparent areas with compositing color */
                if (tiff_opts && original_a == 0) {
                    r = tiff_opts->compositing_color_r;
                    g = tiff_opts->compositing_color_g;
                    b = tiff_opts->compositing_color_b;
                }
            }
            /* TIFF_TRANSPARENCY_FULL: alpha and RGB are preserved as-is (no modification) */

            if (is_grayscale) {
                /* Convert to grayscale using ITU-R BT.601 luminance formula */
                guchar gray = (guchar)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
                if (use_alpha) {
                    dst_row[x * 2 + 0] = gray;
                    dst_row[x * 2 + 1] = a;
                } else {
                    dst_row[x] = gray;
                }
            } else {
                if (use_alpha) {
                    dst_row[x * 4 + 0] = r;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = b;
                    dst_row[x * 4 + 3] = a;
                } else {
                    dst_row[x * 3 + 0] = r;
                    dst_row[x * 3 + 1] = g;
                    dst_row[x * 3 + 2] = b;
                }
            }
        }

        if (TIFFWriteScanline(tif, scanline, y, 0) < 0) {
            _TIFFfree(scanline);
            TIFFClose(tif);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    }

    /* Cleanup */
    _TIFFfree(scanline);
    TIFFClose(tif);
    cairo_surface_destroy(composite);

    return PLUGIN_ERROR_NONE;
}
#else
/**
 * Load TIFF image (stub when libtiff is not available)
 */
static PluginError load_tiff(ImageDocument* doc, const char* filename) {
    (void)doc;
    (void)filename;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}

/**
 * Save TIFF image (stub when libtiff is not available)
 */
static PluginError save_tiff(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}
#endif

/**
 * Check if we can save as TIFF
 */
static bool can_save_tiff(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "tif") == 0 ||
           g_ascii_strcasecmp(ext + 1, "tiff") == 0;
}

#ifdef HAVE_LIBTIFF
/**
 * Get size of TIFF-specific save options structure
 */
static size_t get_tiff_save_options_size(void) {
    return sizeof(TIFFSaveOptions);
}

/**
 * Initialize TIFF-specific save options with defaults
 */
static void init_tiff_save_options(void* plugin_data) {
    TIFFSaveOptions* opts = (TIFFSaveOptions*)plugin_data;
    if (opts) {
        opts->color_compression = TIFF_COLOR_COMPRESSION_AUTO;
        opts->monochrome_compression = TIFF_MONO_COMPRESSION_AUTO;
        opts->color_format = TIFF_COLOR_FORMAT_AUTO;
        opts->transparency_format = TIFF_TRANSPARENCY_AUTO;
        opts->transparency_cutoff = 64;
        opts->transparency_color_r = 255;
        opts->transparency_color_g = 0;
        opts->transparency_color_b = 255;
        opts->compositing_color_r = 255;
        opts->compositing_color_g = 255;
        opts->compositing_color_b = 255;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}
#else
/**
 * Get size of TIFF-specific save options structure (stub when libtiff not available)
 */
static size_t get_tiff_save_options_size(void) {
    return 0;
}

/**
 * Initialize TIFF-specific save options with defaults (stub when libtiff not available)
 */
static void init_tiff_save_options(void* plugin_data) {
    (void)plugin_data;
}
#endif

/**
 * TIFF plugin initialization
 */
bool plugin_init_tiff(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
#ifdef HAVE_LIBTIFF
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "TIFF - Tagged Image File Format";
    out_plugin->format_info.extensions = "tif,tiff";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_tiff;
    out_plugin->callbacks.load = load_tiff;
    out_plugin->callbacks.can_save = can_save_tiff;
    out_plugin->callbacks.save = save_tiff;
    out_plugin->callbacks.get_save_options_size = get_tiff_save_options_size;
    out_plugin->callbacks.init_save_options = init_tiff_save_options;

    return true;
#else
    (void)host;
    (void)out_plugin;
    /* Plugin not available without libtiff */
    return false;
#endif
}
