#include "plugins/plugin_ras.h"
#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RAS file format constants */
#define RAS_MAGIC 0x59A66A95U
#define RAS_HEADER_SIZE 32

/* RAS type constants */
#define RT_STANDARD 0x0001     /* Raw/uncompressed */
#define RT_BYTE_ENCODED 0x0002 /* RLE compressed */
#define RT_FORMAT_RGB 0x0003   /* RGB format */
#define RT_FORMAT_TIFF 0x0004  /* TIFF format */
#define RT_FORMAT_IFF 0x0005   /* IFF format */
#define RT_EXPERIMENTAL 0xFFFF /* Experimental format */

/* RAS map type constants */
#define RMT_NONE 0x0000      /* No colormap */
#define RMT_EQUAL_RGB 0x0001 /* RGB colormap (three planes: R, G, B) */
#define RMT_RAW 0x0002       /* Raw colormap */

/* RAS RLE constants */
#define RAS_RLE_FLAG 0x80 /* RLE flag byte */

/* RAS header structure (big-endian) */
typedef struct {
    uint32_t magic;     /* 0x59A66A95 */
    uint32_t width;     /* Image width in pixels */
    uint32_t height;    /* Image height in pixels */
    uint32_t depth;     /* Bits per pixel */
    uint32_t length;    /* Length of image data in bytes */
    uint32_t type;      /* Encoding type */
    uint32_t maptype;   /* Colormap type */
    uint32_t maplength; /* Colormap length in bytes */
} RASHeader;

/**
 * Read a 32-bit big-endian integer from file
 */
static uint32_t read_be32(FILE* f) {
    uint8_t bytes[4];
    if (fread(bytes, 1, 4, f) != 4) {
        return 0;
    }
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

/**
 * Read a 32-bit big-endian integer from buffer
 */
static uint32_t read_be32_from_buffer(const uint8_t* buffer) {
    return ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) | ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
}

/**
 * Check if file is RAS format
 */
static bool can_load_ras(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 4) {
        return false;
    }

    /* Check for RAS magic number (0x59A66A95 in big-endian) */
    uint32_t magic = read_be32_from_buffer(header);
    return magic == RAS_MAGIC;
}

/**
 * Check if plugin can save to RAS format
 * This plugin is read-only for now
 */
static bool can_save_ras(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Decompress RLE-encoded RAS data
 * Returns allocated buffer with decompressed data, or NULL on error
 */
static uint8_t* decompress_rle(const uint8_t* compressed_data, size_t compressed_size, size_t decompressed_size) {
    uint8_t* output = g_malloc(decompressed_size);
    if (!output) {
        return NULL;
    }

    size_t out_pos = 0;
    size_t in_pos = 0;

    while (in_pos < compressed_size && out_pos < decompressed_size) {
        uint8_t c = compressed_data[in_pos++];

        if (c == RAS_RLE_FLAG) {
            /* RLE sequence */
            if (in_pos >= compressed_size) {
                break;
            }
            uint8_t count = compressed_data[in_pos++];

            if (count == 0) {
                /* Literal 0x80 */
                if (out_pos < decompressed_size) {
                    output[out_pos++] = RAS_RLE_FLAG;
                }
            } else {
                /* Repeat next byte (count + 1) times */
                if (in_pos >= compressed_size) {
                    break;
                }
                uint8_t value = compressed_data[in_pos++];

                int repeat = count + 1;
                for (int i = 0; i < repeat && out_pos < decompressed_size; i++) {
                    output[out_pos++] = value;
                }
            }
        } else {
            /* Literal byte */
            if (out_pos < decompressed_size) {
                output[out_pos++] = c;
            }
        }
    }

    if (out_pos != decompressed_size) {
        g_warning("RAS plugin: RLE decompression incomplete (expected %zu bytes, got %zu)", decompressed_size, out_pos);
        g_free(output);
        return NULL;
    }

    return output;
}

/**
 * Load RAS image
 */
static PluginError load_ras(ImageDocument* doc, const char* filename) {
    FILE* infile;
    RASHeader header;
    uint8_t* colormap = NULL;
    uint8_t* image_data = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint32_t bytes_per_pixel;
    uint32_t row_stride;
    uint32_t colormap_size = 0;

    if (!doc || !filename) {
        g_warning("RAS plugin: Invalid parameters (doc=%p, filename=%p)", doc, filename);
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open RAS file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        g_warning("RAS plugin: Failed to open file: %s", filename);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read header */
    header.magic = read_be32(infile);
    header.width = read_be32(infile);
    header.height = read_be32(infile);
    header.depth = read_be32(infile);
    header.length = read_be32(infile);
    header.type = read_be32(infile);
    header.maptype = read_be32(infile);
    header.maplength = read_be32(infile);

    /* Validate magic number */
    if (header.magic != RAS_MAGIC) {
        g_warning("RAS plugin: Invalid magic number: 0x%08X (expected 0x%08X)", header.magic, RAS_MAGIC);
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Validate dimensions */
    if (header.width == 0 || header.height == 0) {
        g_warning("RAS plugin: Invalid dimensions: %ux%u", header.width, header.height);
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Validate depth */
    if (header.depth != 1 && header.depth != 8 && header.depth != 24 && header.depth != 32) {
        g_warning("RAS plugin: Unsupported depth: %u bits per pixel", header.depth);
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Calculate bytes per pixel and row stride */
    bytes_per_pixel = (header.depth + 7) / 8; /* Round up to nearest byte */
    /* RAS scanlines are padded to even bytes (16-bit boundary) */
    /* Calculate bytes per row first, then round up to even */
    uint32_t bytes_per_row = (header.width * header.depth + 7) / 8; /* Round up to nearest byte */
    row_stride = (bytes_per_row + 1) & ~1;                          /* Round up to even bytes */

    /* Read colormap if present */
    if (header.maptype != RMT_NONE && header.maplength > 0) {
        colormap_size = header.maplength;
        colormap = g_malloc(colormap_size);
        if (!colormap) {
            g_warning("RAS plugin: Failed to allocate colormap (%u bytes)", colormap_size);
            fclose(infile);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        if (fread(colormap, 1, colormap_size, infile) != colormap_size) {
            g_warning("RAS plugin: Failed to read colormap");
            g_free(colormap);
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
    }

    /* Read image data */
    size_t expected_data_size;
    if (header.type == RT_BYTE_ENCODED) {
        /* RLE compressed - use length from header */
        expected_data_size = header.length;
    } else {
        /* Raw/uncompressed - calculate from dimensions */
        expected_data_size = row_stride * header.height;
    }

    /* Allocate buffer for compressed data (if RLE) or raw data */
    uint8_t* raw_data = g_malloc(expected_data_size);
    if (!raw_data) {
        g_warning("RAS plugin: Failed to allocate image data buffer (%zu bytes)", expected_data_size);
        g_free(colormap);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    if (fread(raw_data, 1, expected_data_size, infile) != expected_data_size) {
        g_warning("RAS plugin: Failed to read image data");
        g_free(raw_data);
        g_free(colormap);
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    fclose(infile);

    /* Decompress if RLE encoded */
    if (header.type == RT_BYTE_ENCODED) {
        size_t decompressed_size = row_stride * header.height;
        image_data = decompress_rle(raw_data, expected_data_size, decompressed_size);
        g_free(raw_data);
        if (!image_data) {
            g_warning("RAS plugin: RLE decompression failed");
            g_free(colormap);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
    } else {
        /* Use raw data directly */
        image_data = raw_data;
    }

    /* Set document metadata */
    doc->width = header.width;
    doc->height = header.height;
    doc->channels = 4; /* RGBA */
    doc->bit_depth = 8;
    doc->has_alpha = (header.depth == 32); /* 32-bit depth may have alpha */

    /* Free old layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Create base layer */
    base_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        g_warning("RAS plugin: layer_new returned NULL for %ux%u layer", doc->width, doc->height);
        g_free(image_data);
        g_free(colormap);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        g_warning("RAS plugin: base_layer->surface is NULL");
        g_free(image_data);
        g_free(colormap);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        g_warning("RAS plugin: cairo_image_surface_get_data returned NULL");
        g_free(image_data);
        g_free(colormap);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert RAS image data to Cairo ARGB32 format */
    uint32_t palette_size = 0;
    uint8_t* palette_r = NULL;
    uint8_t* palette_g = NULL;
    uint8_t* palette_b = NULL;

    /* Setup palette if colormap is present */
    if (colormap && header.maptype == RMT_EQUAL_RGB) {
        /* RGB colormap: three consecutive arrays (R, G, B) */
        palette_size = colormap_size / 3;
        if (palette_size > 256) {
            palette_size = 256; /* Limit to 256 colors */
        }
        palette_r = colormap;
        palette_g = colormap + palette_size;
        palette_b = colormap + palette_size * 2;
    }

    /* Convert pixel data */
    for (uint32_t y = 0; y < header.height; y++) {
        uint8_t* src_row = image_data + y * row_stride;
        guchar* dst_row = surface_data + y * surface_stride;

        for (uint32_t x = 0; x < header.width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            switch (header.depth) {
                case 1: {
                    /* 1-bit monochrome */
                    uint32_t byte_offset = x / 8;
                    uint32_t bit_offset = 7 - (x % 8);
                    uint8_t bit = (src_row[byte_offset] >> bit_offset) & 1;
                    r = g = b = bit ? 255 : 0;
                    break;
                }
                case 8: {
                    /* 8-bit indexed or grayscale */
                    uint8_t index = src_row[x];
                    if (palette_r && index < palette_size) {
                        r = palette_r[index];
                        g = palette_g[index];
                        b = palette_b[index];
                    } else {
                        /* Grayscale */
                        r = g = b = index;
                    }
                    break;
                }
                case 24: {
                    /* 24-bit RGB */
                    uint32_t pixel_offset = x * 3;
                    if (header.type == RT_FORMAT_RGB) {
                        /* RGB order */
                        r = src_row[pixel_offset + 0];
                        g = src_row[pixel_offset + 1];
                        b = src_row[pixel_offset + 2];
                    } else {
                        /* Standard RAS: BGR order */
                        b = src_row[pixel_offset + 0];
                        g = src_row[pixel_offset + 1];
                        r = src_row[pixel_offset + 2];
                    }
                    break;
                }
                case 32: {
                    /* 32-bit RGBA or ARGB */
                    uint32_t pixel_offset = x * 4;
                    if (header.type == RT_FORMAT_RGB) {
                        /* RGBA order */
                        r = src_row[pixel_offset + 0];
                        g = src_row[pixel_offset + 1];
                        b = src_row[pixel_offset + 2];
                        a = src_row[pixel_offset + 3];
                    } else {
                        /* Standard RAS: may be ARGB or BGRA, try ARGB first */
                        a = src_row[pixel_offset + 0];
                        r = src_row[pixel_offset + 1];
                        g = src_row[pixel_offset + 2];
                        b = src_row[pixel_offset + 3];
                    }
                    break;
                }
            }

            /* Premultiply alpha for Cairo */
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
    g_free(colormap);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    return PLUGIN_ERROR_NONE;
}

/**
 * RAS plugin initialization
 */
bool plugin_init_ras(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "RAS - Sun Raster Image";
    out_plugin->format_info.extensions = "ras,sun";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_ras;
    out_plugin->callbacks.load = load_ras;
    out_plugin->callbacks.can_save = can_save_ras;
    out_plugin->callbacks.save = NULL; /* Read-only for now */

    return true;
}
