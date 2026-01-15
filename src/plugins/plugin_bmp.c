#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * BMP color model options
 */
typedef enum {
    BMP_COLOR_MODEL_AUTO = 0,               /* Auto-calculate best model */
    BMP_COLOR_MODEL_COLOR_TRANSPARENCY = 1, /* Color with transparency (32-bpp ARGB) */
    BMP_COLOR_MODEL_COLOR_ONLY = 2,         /* Color only */
    BMP_COLOR_MODEL_GRAYSCALE = 3           /* Grayscale */
} BMPColorModel;

/**
 * BMP color depth for color-only mode
 */
typedef enum {
    BMP_COLOR_DEPTH_32BPP = 0, /* 32-bpp XRGB */
    BMP_COLOR_DEPTH_24BPP = 1, /* 24-bpp RGB */
    BMP_COLOR_DEPTH_16BPP = 2, /* 16-bpp RGB565 */
    BMP_COLOR_DEPTH_8BPP = 3   /* 8-bpp indexed */
} BMPColorDepth;

/**
 * BMP grayscale depth
 */
typedef enum {
    BMP_GRAYSCALE_DEPTH_8BPP = 0, /* 8-bpp grayscale */
    BMP_GRAYSCALE_DEPTH_4BPP = 1, /* 4-bpp grayscale */
    BMP_GRAYSCALE_DEPTH_1BPP = 2  /* 1-bpp grayscale */
} BMPGrayscaleDepth;

/**
 * BMP-specific save options
 */
typedef struct {
    /* Flip row order (BMP is typically bottom-up, but can be top-down) */
    bool flip_row_order;

    /* Color model to use */
    BMPColorModel color_model;

    /* Color depth (for color_only mode) */
    BMPColorDepth color_depth;

    /* Grayscale depth (for grayscale mode) */
    BMPGrayscaleDepth grayscale_depth;

    /* For 8-bpp color: use RLE compression */
    bool use_rle_compression;

    /* For 8-bpp color: restrict palette size */
    bool restrict_palette_size;

    /* Palette size (2-256, used when restrict_palette_size is true) */
    int32_t palette_size;

    /* For 16-bpp color: use legacy 15-bit encoding (RGB555) instead of RGB565 */
    bool use_legacy_15bit;

    /* Reserved for future use */
    uint32_t reserved[1];
} BMPSaveOptions;

/* BMP file signature */
static const uint8_t BMP_SIGNATURE[2] = {0x42, 0x4D}; /* "BM" */

/* BMP structures (packed for file I/O) */
#pragma pack(push, 1)
typedef struct {
    uint16_t type;      /* File type (must be 0x4D42 = "BM") */
    uint32_t size;      /* File size in bytes */
    uint16_t reserved1; /* Reserved (must be 0) */
    uint16_t reserved2; /* Reserved (must be 0) */
    uint32_t off_bits;  /* Offset to bitmap data */
} BITMAPFILEHEADER;

typedef struct {
    uint32_t size;            /* Size of this structure (40 bytes) */
    int32_t width;            /* Image width in pixels (negative = top-down) */
    int32_t height;           /* Image height in pixels (negative = top-down) */
    uint16_t planes;          /* Number of color planes (must be 1) */
    uint16_t bit_count;       /* Bits per pixel */
    uint32_t compression;     /* Compression type (0 = none, 1 = RLE8, 2 = RLE4) */
    uint32_t size_image;      /* Image size in bytes (0 if no compression) */
    int32_t x_pels_per_meter; /* Horizontal resolution */
    int32_t y_pels_per_meter; /* Vertical resolution */
    uint32_t clr_used;        /* Number of colors in palette (0 = 2^bit_count) */
    uint32_t clr_important;   /* Important colors (0 = all) */
} BITMAPINFOHEADER;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved; /* Must be 0 */
} RGBQUAD;
#pragma pack(pop)

/**
 * Check if file is BMP format
 */
static bool can_load_bmp(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 2) {
        return false;
    }

    return memcmp(header, BMP_SIGNATURE, 2) == 0;
}

/**
 * Check if plugin can save to BMP format
 */
static bool can_save_bmp(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    /* Check for .bmp extension (case-insensitive) */
    if (g_ascii_strcasecmp(ext, ".bmp") == 0) {
        return true;
    }

    return false;
}

/**
 * Load BMP image
 */
static PluginError load_bmp(ImageDocument* doc, const char* filename) {
    FILE* infile;
    BITMAPFILEHEADER file_header;
    BITMAPINFOHEADER info_header;
    RGBQUAD* palette = NULL;
    uint32_t palette_size = 0;
    uint8_t* image_data = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint32_t width, height;
    uint16_t bit_count;
    bool top_down = false;
    bool has_alpha = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open BMP file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read file header */
    if (fread(&file_header, sizeof(BITMAPFILEHEADER), 1, infile) != 1) {
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Verify BMP signature */
    if (file_header.type != 0x4D42) { /* "BM" */
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Read info header (first 40 bytes - common to all BMP header versions) */
    if (fread(&info_header, sizeof(BITMAPINFOHEADER), 1, infile) != 1) {
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Check header size and skip extra bytes for newer header versions */
    /* All BMP headers have at least 40 bytes, but V4/V5 have more */
    /* V1 = 40 bytes, V2 = 52 bytes (uncommon), V3 = 40 bytes, V4 = 108 bytes, V5 = 124 bytes */
    if (info_header.size < 40) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT; /* Invalid header size */
    }

    /* If header is larger than 40 bytes, skip the extra data */
    if (info_header.size > 40) {
        uint32_t extra_bytes = info_header.size - 40;
        if (fseek(infile, extra_bytes, SEEK_CUR) != 0) {
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
    }

    width = (uint32_t)abs(info_header.width);
    height = (uint32_t)abs(info_header.height);
    bit_count = info_header.bit_count;
    top_down = (info_header.height < 0);

    /* Determine if alpha channel exists (only for 32-bpp) */
    if (bit_count == 32) {
        has_alpha = true;
    }

    /* Validate dimensions */
    if (width == 0 || height == 0) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Read palette for indexed modes */
    if (bit_count <= 8) {
        palette_size = info_header.clr_used;
        if (palette_size == 0) {
            palette_size = (1 << bit_count); /* 2^bit_count */
        }
        if (palette_size > 256) {
            palette_size = 256;
        }

        palette = g_malloc(sizeof(RGBQUAD) * palette_size);
        if (!palette) {
            fclose(infile);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        if (fread(palette, sizeof(RGBQUAD), palette_size, infile) != palette_size) {
            g_free(palette);
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
    }

    /* Calculate row size and padding */
    uint32_t row_size = ((width * bit_count + 31) / 32) * 4; /* Align to 4 bytes */
    uint32_t data_size = row_size * height;

    /* Handle compressed formats */
    if (info_header.compression != 0) {
        /* RLE compression - use the compressed data size if available */
        if (info_header.size_image > 0) {
            data_size = info_header.size_image;
        }
    }

    /* Get actual file size to be tolerant of header mismatches */
    if (fseek(infile, 0, SEEK_END) != 0) {
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }
    long file_size = ftell(infile);
    long available_size = file_size - file_header.off_bits;

    if (available_size <= 0) {
        /* No data available - file is too short */
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Determine how many bytes to read */
    uint32_t bytes_to_read = data_size;

    if (info_header.compression == 0) {
        /* For uncompressed formats, we need at least the calculated size */
        if ((long)bytes_to_read > available_size) {
            /* Not enough data in file - corrupted */
            if (palette) {
                g_free(palette);
            }
            fclose(infile);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
        /* For uncompressed, read exactly what we need (ignore extra padding) */
        bytes_to_read = row_size * height;
    } else {
        /* For compressed formats, read up to available size or size_image, whichever is smaller */
        if (info_header.size_image > 0) {
            bytes_to_read = (uint32_t)info_header.size_image;
        }
        /* Limit to available data if header is wrong */
        if ((long)bytes_to_read > available_size) {
            bytes_to_read = (uint32_t)available_size;
        }
    }

    /* Seek to bitmap data */
    if (fseek(infile, file_header.off_bits, SEEK_SET) != 0) {
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Allocate image data buffer */
    image_data = g_malloc(bytes_to_read);
    if (!image_data) {
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Read image data - read what's available */
    size_t bytes_read = fread(image_data, 1, bytes_to_read, infile);
    if (bytes_read == 0 && bytes_to_read > 0) {
        /* Failed to read any data */
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* For uncompressed formats, verify we read enough for at least the image */
    if (info_header.compression == 0) {
        uint32_t min_required = row_size * height;
        if (bytes_read < min_required) {
            /* Not enough data for the image */
            g_free(image_data);
            if (palette) {
                g_free(palette);
            }
            fclose(infile);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
        /* Use exactly the calculated size for processing */
        data_size = row_size * height;
    } else {
        /* For compressed formats, use what we read */
        data_size = (uint32_t)bytes_read;
    }

    fclose(infile);

    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = has_alpha ? 4 : 3;
    doc->bit_depth = 8;
    doc->has_alpha = has_alpha;

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
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert BMP data to Cairo ARGB32 format */
    for (uint32_t y = 0; y < height; y++) {
        uint32_t src_y = top_down ? y : (height - 1 - y);
        const uint8_t* src_row = image_data + src_y * row_size;
        guchar* dst_row = surface_data + y * surface_stride;

        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            switch (bit_count) {
                case 32: {
                    /* BGRA format */
                    const uint8_t* pixel = src_row + x * 4;
                    b = pixel[0];
                    g = pixel[1];
                    r = pixel[2];
                    a = pixel[3];
                    break;
                }
                case 24: {
                    /* BGR format */
                    const uint8_t* pixel = src_row + x * 3;
                    b = pixel[0];
                    g = pixel[1];
                    r = pixel[2];
                    break;
                }
                case 16: {
                    /* Try to detect RGB565 vs RGB555 format */
                    /* RGB565: bits 11-15 for R, bits 5-10 for G, bits 0-4 for B */
                    /* RGB555: bits 10-14 for R, bits 5-9 for G, bits 0-4 for B */
                    const uint16_t* pixel = (const uint16_t*)(src_row + x * 2);
                    uint16_t pixel_val = *pixel;

                    /* Check if bit 15 is set - if so, likely RGB565, otherwise try RGB555 */
                    if (pixel_val & 0x8000) {
                        /* RGB565 format */
                        r = ((pixel_val >> 11) & 0x1F) << 3;
                        g = ((pixel_val >> 5) & 0x3F) << 2;
                        b = (pixel_val & 0x1F) << 3;
                    } else {
                        /* RGB555 format (legacy 15-bit) */
                        r = ((pixel_val >> 10) & 0x1F) << 3;
                        g = ((pixel_val >> 5) & 0x1F) << 3;
                        b = (pixel_val & 0x1F) << 3;
                    }
                    break;
                }
                case 8: {
                    /* Indexed 8-bit */
                    uint8_t index = src_row[x];
                    if (index < palette_size && palette) {
                        r = palette[index].red;
                        g = palette[index].green;
                        b = palette[index].blue;
                    }
                    break;
                }
                case 4: {
                    /* Indexed 4-bit */
                    uint8_t byte = src_row[x / 2];
                    uint8_t index = (x % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
                    if (index < palette_size && palette) {
                        r = palette[index].red;
                        g = palette[index].green;
                        b = palette[index].blue;
                    }
                    break;
                }
                case 1: {
                    /* Indexed 1-bit */
                    uint8_t byte = src_row[x / 8];
                    uint8_t bit_pos = 7 - (x % 8);
                    uint8_t index = (byte >> bit_pos) & 0x01;
                    if (index < palette_size && palette) {
                        r = palette[index].red;
                        g = palette[index].green;
                        b = palette[index].blue;
                    }
                    break;
                }
                default:
                    r = g = b = 0;
                    break;
            }

            /* Convert to Cairo ARGB32 (BGRA in memory, premultiplied alpha) */
            uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
            uint32_t* dst_pixel = (uint32_t*)(dst_row + x * 4);
            *dst_pixel = argb;
        }
    }

    /* Premultiply alpha */
    for (uint32_t y = 0; y < height; y++) {
        guchar* dst_row = surface_data + y * surface_stride;
        for (uint32_t x = 0; x < width; x++) {
            uint32_t* pixel = (uint32_t*)(dst_row + x * 4);
            uint8_t a = (*pixel >> 24) & 0xFF;
            uint8_t r = (*pixel >> 16) & 0xFF;
            uint8_t g = (*pixel >> 8) & 0xFF;
            uint8_t b = *pixel & 0xFF;

            if (a != 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }

            *pixel = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Cleanup */
    g_free(image_data);
    if (palette) {
        g_free(palette);
    }

    cairo_surface_mark_dirty(temp_surface);
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

/**
 * Simple color quantization using median cut algorithm
 * Returns palette and converts image to indexed
 */
static bool quantize_to_palette(const guchar* src_data, int src_stride,
                                uint32_t width, uint32_t height,
                                RGBQUAD* palette, uint32_t palette_size,
                                uint8_t* dst_data, uint32_t dst_row_size) {
    /* Simple implementation: use color histogram */
    typedef struct {
        uint32_t count;
        uint32_t r_sum, g_sum, b_sum;
        uint8_t r, g, b;
    } ColorBucket;

    ColorBucket* buckets = g_malloc0((256 * 256 * 256) * sizeof(ColorBucket));
    if (!buckets) {
        return false;
    }

    /* Build color histogram from source image */
    for (uint32_t y = 0; y < height; y++) {
        const guchar* src_row = src_data + y * src_stride;
        for (uint32_t x = 0; x < width; x++) {
            uint32_t* pixel = (uint32_t*)(src_row + x * 4);
            uint8_t a = (*pixel >> 24) & 0xFF;
            uint8_t r = (*pixel >> 16) & 0xFF;
            uint8_t g = (*pixel >> 8) & 0xFF;
            uint8_t b = *pixel & 0xFF;

            /* Unpremultiply if needed */
            if (a != 0 && a != 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
            }

            /* Quantize to 5 bits per channel for histogram */
            uint32_t idx = ((r >> 3) << 11) | ((g >> 3) << 6) | (b >> 3);
            buckets[idx].count++;
            buckets[idx].r_sum += r;
            buckets[idx].g_sum += g;
            buckets[idx].b_sum += b;
        }
    }

    /* Find most common colors */
    typedef struct {
        uint32_t count;
        uint8_t r, g, b;
    } ColorEntry;

    ColorEntry* colors = g_malloc(sizeof(ColorEntry) * (256 * 256 * 256));
    uint32_t color_count = 0;

    for (uint32_t i = 0; i < 256 * 256 * 256; i++) {
        if (buckets[i].count > 0) {
            colors[color_count].count = buckets[i].count;
            colors[color_count].r = buckets[i].r_sum / buckets[i].count;
            colors[color_count].g = buckets[i].g_sum / buckets[i].count;
            colors[color_count].b = buckets[i].b_sum / buckets[i].count;
            color_count++;
        }
    }

    /* Sort by count */
    for (uint32_t i = 0; i < color_count - 1; i++) {
        for (uint32_t j = i + 1; j < color_count; j++) {
            if (colors[j].count > colors[i].count) {
                ColorEntry temp = colors[i];
                colors[i] = colors[j];
                colors[j] = temp;
            }
        }
    }

    /* Fill palette with top colors */
    uint32_t palette_entries = (palette_size < color_count) ? palette_size : color_count;
    for (uint32_t i = 0; i < palette_entries; i++) {
        palette[i].red = colors[i].r;
        palette[i].green = colors[i].g;
        palette[i].blue = colors[i].b;
        palette[i].reserved = 0;
    }

    /* Fill remaining with black */
    for (uint32_t i = palette_entries; i < palette_size; i++) {
        palette[i].red = 0;
        palette[i].green = 0;
        palette[i].blue = 0;
        palette[i].reserved = 0;
    }

    /* Map image pixels to palette */
    for (uint32_t y = 0; y < height; y++) {
        const guchar* src_row = src_data + y * src_stride;
        uint8_t* dst_row = dst_data + y * dst_row_size;

        for (uint32_t x = 0; x < width; x++) {
            uint32_t* pixel = (uint32_t*)(src_row + x * 4);
            uint8_t a = (*pixel >> 24) & 0xFF;
            uint8_t r = (*pixel >> 16) & 0xFF;
            uint8_t g = (*pixel >> 8) & 0xFF;
            uint8_t b = *pixel & 0xFF;

            /* Unpremultiply if needed */
            if (a != 0 && a != 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
            }

            /* Find closest palette color */
            uint32_t best_idx = 0;
            uint32_t best_dist = UINT32_MAX;

            for (uint32_t i = 0; i < palette_entries; i++) {
                int32_t dr = r - palette[i].red;
                int32_t dg = g - palette[i].green;
                int32_t db = b - palette[i].blue;
                uint32_t dist = dr * dr + dg * dg + db * db;
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = i;
                }
            }

            dst_row[x] = (uint8_t)best_idx;
        }
    }

    g_free(colors);
    g_free(buckets);
    return true;
}

/**
 * Compress 8-bit image data using RLE8 compression
 */
static uint32_t compress_rle8(const uint8_t* src_data, uint32_t src_row_size,
                              uint32_t width, uint32_t height,
                              uint8_t* dst_data, uint32_t dst_size) {
    uint32_t dst_pos = 0;

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = src_data + y * src_row_size;
        uint32_t x = 0;

        while (x < width && dst_pos + 2 <= dst_size) {
            /* Find run length */
            uint32_t run_start = x;
            uint8_t value = row[x];
            while (x < width && row[x] == value && (x - run_start) < 255) {
                x++;
            }
            uint32_t run_length = x - run_start;

            if (run_length > 1 || (x < width && row[x] != value)) {
                /* Encode as run */
                if (dst_pos + 2 > dst_size)
                    break;
                dst_data[dst_pos++] = (uint8_t)run_length;
                dst_data[dst_pos++] = value;
            } else {
                /* Encode as literal sequence */
                uint32_t literal_start = x - 1;
                while (x < width && (x == literal_start || row[x] != row[x - 1]) && (x - literal_start) < 255) {
                    x++;
                }
                uint32_t literal_length = x - literal_start;

                if (literal_length > 2 || literal_start + literal_length == width) {
                    /* Encode as absolute run */
                    if (dst_pos + 2 + literal_length > dst_size) {
                        x = literal_start + 1; /* Back up */
                        break;
                    }
                    dst_data[dst_pos++] = 0;
                    dst_data[dst_pos++] = (uint8_t)literal_length;
                    memcpy(dst_data + dst_pos, row + literal_start, literal_length);
                    dst_pos += literal_length;
                    if (literal_length % 2 == 1) {
                        /* Pad to even */
                        if (dst_pos >= dst_size)
                            break;
                        dst_data[dst_pos++] = 0;
                    }
                } else {
                    /* Encode as single run */
                    if (dst_pos + 2 > dst_size)
                        break;
                    dst_data[dst_pos++] = 1;
                    dst_data[dst_pos++] = row[literal_start];
                }
            }
        }

        /* End of line marker */
        if (dst_pos + 2 > dst_size)
            break;
        dst_data[dst_pos++] = 0;
        dst_data[dst_pos++] = 0;
    }

    /* End of bitmap marker */
    if (dst_pos + 2 <= dst_size) {
        dst_data[dst_pos++] = 0;
        dst_data[dst_pos++] = 1;
    }

    return dst_pos;
}

/**
 * Save BMP image
 */
static PluginError save_bmp(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite;
    guchar* surface_data;
    int surface_stride;
    FILE* outfile;
    BITMAPFILEHEADER file_header;
    BITMAPINFOHEADER info_header;
    RGBQUAD* palette = NULL;
    uint32_t palette_size = 0;
    uint8_t* image_data = NULL;
    uint32_t image_data_size = 0;
    uint32_t row_size = 0;
    bool flip_row_order = true; /* Default: bottom-up BMP */
    BMPColorModel color_model = BMP_COLOR_MODEL_AUTO;
    BMPColorDepth color_depth = BMP_COLOR_DEPTH_24BPP;
    BMPGrayscaleDepth grayscale_depth = BMP_GRAYSCALE_DEPTH_8BPP;
    bool use_rle = false;
    bool restrict_palette = false;
    int32_t palette_size_opt = 256;
    bool use_legacy_15bit = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get BMP-specific options if provided */
    if (opts && opts->plugin_data) {
        BMPSaveOptions* bmp_opts = (BMPSaveOptions*)opts->plugin_data;
        flip_row_order = bmp_opts->flip_row_order;
        color_model = bmp_opts->color_model;
        color_depth = bmp_opts->color_depth;
        grayscale_depth = bmp_opts->grayscale_depth;
        use_rle = bmp_opts->use_rle_compression;
        restrict_palette = bmp_opts->restrict_palette_size;
        palette_size_opt = bmp_opts->palette_size;
        use_legacy_15bit = bmp_opts->use_legacy_15bit;
        if (palette_size_opt < 2)
            palette_size_opt = 2;
        if (palette_size_opt > 256)
            palette_size_opt = 256;
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

    uint32_t width = doc->width;
    uint32_t height = doc->height;

    /* Auto-detect color model if needed */
    if (color_model == BMP_COLOR_MODEL_AUTO) {
        bool has_transparency = false;
        bool is_grayscale = true;
        bool has_colors = false;

        /* Analyze image */
        for (uint32_t y = 0; y < height; y++) {
            const guchar* row = surface_data + y * surface_stride;
            for (uint32_t x = 0; x < width; x++) {
                uint32_t* pixel = (uint32_t*)(row + x * 4);
                uint8_t a = (*pixel >> 24) & 0xFF;
                uint8_t r = (*pixel >> 16) & 0xFF;
                uint8_t g = (*pixel >> 8) & 0xFF;
                uint8_t b = *pixel & 0xFF;

                if (a < 255) {
                    has_transparency = true;
                }

                if (r != g || g != b) {
                    is_grayscale = false;
                    has_colors = true;
                }
            }
        }

        if (has_transparency && has_colors) {
            color_model = BMP_COLOR_MODEL_COLOR_TRANSPARENCY;
        } else if (has_colors) {
            color_model = BMP_COLOR_MODEL_COLOR_ONLY;
            color_depth = BMP_COLOR_DEPTH_24BPP; /* Default to 24-bit */
        } else {
            color_model = BMP_COLOR_MODEL_GRAYSCALE;
            grayscale_depth = BMP_GRAYSCALE_DEPTH_8BPP;
        }
    }

    /* Determine bit depth and format */
    uint16_t bit_count = 24;
    uint32_t compression = 0;

    if (color_model == BMP_COLOR_MODEL_COLOR_TRANSPARENCY) {
        bit_count = 32;
    } else if (color_model == BMP_COLOR_MODEL_COLOR_ONLY) {
        switch (color_depth) {
            case BMP_COLOR_DEPTH_32BPP:
                bit_count = 32;
                break;
            case BMP_COLOR_DEPTH_24BPP:
                bit_count = 24;
                break;
            case BMP_COLOR_DEPTH_16BPP:
                bit_count = 16;
                break;
            case BMP_COLOR_DEPTH_8BPP:
                bit_count = 8;
                if (use_rle) {
                    compression = 1; /* RLE8 */
                }
                break;
        }
    } else if (color_model == BMP_COLOR_MODEL_GRAYSCALE) {
        switch (grayscale_depth) {
            case BMP_GRAYSCALE_DEPTH_8BPP:
                bit_count = 8;
                break;
            case BMP_GRAYSCALE_DEPTH_4BPP:
                bit_count = 4;
                break;
            case BMP_GRAYSCALE_DEPTH_1BPP:
                bit_count = 1;
                break;
        }
    }

    /* Calculate row size (must be aligned to 4 bytes) */
    row_size = ((width * bit_count + 31) / 32) * 4;

    /* Prepare palette for indexed modes */
    if (bit_count <= 8) {
        if (bit_count == 8) {
            palette_size = restrict_palette ? palette_size_opt : 256;
        } else if (bit_count == 4) {
            palette_size = 16;
        } else if (bit_count == 1) {
            palette_size = 2;
        }

        palette = g_malloc(sizeof(RGBQUAD) * palette_size);
        if (!palette) {
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Allocate temporary buffer for indexed data */
        uint32_t indexed_row_size = ((width * 8 + 31) / 32) * 4; /* Use 8-bit for conversion */
        image_data = g_malloc(indexed_row_size * height);
        if (!image_data) {
            g_free(palette);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Quantize to palette */
        if (!quantize_to_palette(surface_data, surface_stride, width, height,
                                 palette, palette_size, image_data, indexed_row_size)) {
            g_free(image_data);
            g_free(palette);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Convert to target bit depth */
        if (bit_count < 8) {
            uint8_t* converted_data = g_malloc(row_size * height);
            if (!converted_data) {
                g_free(image_data);
                g_free(palette);
                cairo_surface_destroy(composite);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            memset(converted_data, 0, row_size * height);

            for (uint32_t y = 0; y < height; y++) {
                const uint8_t* src_row = image_data + y * indexed_row_size;
                uint8_t* dst_row = converted_data + y * row_size;

                if (bit_count == 4) {
                    for (uint32_t x = 0; x < width; x++) {
                        uint8_t idx = src_row[x];
                        if (x % 2 == 0) {
                            dst_row[x / 2] = (idx & 0x0F) << 4;
                        } else {
                            dst_row[x / 2] |= (idx & 0x0F);
                        }
                    }
                } else if (bit_count == 1) {
                    for (uint32_t x = 0; x < width; x++) {
                        uint8_t idx = src_row[x];
                        uint32_t byte_idx = x / 8;
                        uint32_t bit_pos = 7 - (x % 8);
                        if (idx & 1) {
                            dst_row[byte_idx] |= (1 << bit_pos);
                        }
                    }
                }
            }

            g_free(image_data);
            image_data = converted_data;
            image_data_size = row_size * height;
        } else {
            /* 8-bit - adjust row size */
            image_data_size = row_size * height;
            /* Reallocate if row size changed */
            if (row_size != indexed_row_size) {
                uint8_t* converted_data = g_malloc(image_data_size);
                if (!converted_data) {
                    g_free(image_data);
                    g_free(palette);
                    cairo_surface_destroy(composite);
                    return PLUGIN_ERROR_OUT_OF_MEMORY;
                }
                for (uint32_t y = 0; y < height; y++) {
                    memcpy(converted_data + y * row_size, image_data + y * indexed_row_size, width);
                    /* Pad remaining bytes with zeros (already zeroed by allocation) */
                }
                g_free(image_data);
                image_data = converted_data;
            }
        }
    } else {
        /* Allocate image data buffer for non-indexed modes */
        image_data_size = row_size * height;
        image_data = g_malloc(image_data_size);
        if (!image_data) {
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Convert from ARGB32 to BMP format */
        for (uint32_t y = 0; y < height; y++) {
            const guchar* src_row = surface_data + y * surface_stride;
            uint8_t* dst_row = image_data + y * row_size;

            for (uint32_t x = 0; x < width; x++) {
                uint32_t* pixel = (uint32_t*)(src_row + x * 4);
                uint8_t a = (*pixel >> 24) & 0xFF;
                uint8_t r = (*pixel >> 16) & 0xFF;
                uint8_t g = (*pixel >> 8) & 0xFF;
                uint8_t b = *pixel & 0xFF;

                /* Unpremultiply if needed */
                if (a != 0 && a != 255) {
                    r = (r * 255 + a / 2) / a;
                    g = (g * 255 + a / 2) / a;
                    b = (b * 255 + a / 2) / a;
                }

                if (bit_count == 32) {
                    /* BGRA format */
                    dst_row[x * 4 + 0] = b;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = r;
                    dst_row[x * 4 + 3] = a;
                } else if (bit_count == 24) {
                    /* BGR format */
                    dst_row[x * 3 + 0] = b;
                    dst_row[x * 3 + 1] = g;
                    dst_row[x * 3 + 2] = r;
                } else if (bit_count == 16) {
                    if (use_legacy_15bit) {
                        /* RGB555 format (legacy 15-bit encoding) */
                        uint16_t r5 = (r >> 3) & 0x1F;
                        uint16_t g5 = (g >> 3) & 0x1F;
                        uint16_t b5 = (b >> 3) & 0x1F;
                        uint16_t pixel_val = (r5 << 10) | (g5 << 5) | b5;
                        *((uint16_t*)(dst_row + x * 2)) = pixel_val;
                    } else {
                        /* RGB565 format */
                        uint16_t r5 = (r >> 3) & 0x1F;
                        uint16_t g6 = (g >> 2) & 0x3F;
                        uint16_t b5 = (b >> 3) & 0x1F;
                        uint16_t pixel_val = (r5 << 11) | (g6 << 5) | b5;
                        *((uint16_t*)(dst_row + x * 2)) = pixel_val;
                    }
                }
            }
        }
    }

    /* Apply RLE compression if requested for 8-bit */
    uint32_t compressed_size = 0;
    uint8_t* compressed_data = NULL;
    uint8_t* temp_rle_data = NULL;
    if (bit_count == 8 && use_rle && compression == 1) {
        /* For bottom-up RLE, we need to compress rows in reverse order */
        if (flip_row_order) {
            /* Create temporary buffer with rows reversed */
            temp_rle_data = g_malloc(row_size * height);
            if (temp_rle_data) {
                for (uint32_t y = 0; y < height; y++) {
                    const uint8_t* src_row = image_data + (height - 1 - y) * row_size;
                    uint8_t* dst_row = temp_rle_data + y * row_size;
                    memcpy(dst_row, src_row, row_size);
                }
                compressed_data = g_malloc(row_size * height * 2); /* Allocate extra space */
                if (compressed_data) {
                    compressed_size = compress_rle8(temp_rle_data, row_size, width, height,
                                                    compressed_data, row_size * height * 2);
                }
                g_free(temp_rle_data);
                temp_rle_data = NULL;
            }
        } else {
            compressed_data = g_malloc(row_size * height * 2); /* Allocate extra space */
            if (compressed_data) {
                compressed_size = compress_rle8(image_data, row_size, width, height,
                                                compressed_data, row_size * height * 2);
            }
        }

        if (compressed_data && compressed_size > 0 && compressed_size < image_data_size) {
            /* Use compressed data */
            g_free(image_data);
            image_data = compressed_data;
            image_data_size = compressed_size;
            compressed_data = NULL;
        } else {
            /* Compression didn't help, use uncompressed */
            if (compressed_data) {
                g_free(compressed_data);
                compressed_data = NULL;
            }
            compression = 0;
            image_data_size = row_size * height;
        }
    }

    /* Initialize info header */
    memset(&info_header, 0, sizeof(BITMAPINFOHEADER));
    info_header.size = 40;
    /* For RLE compression, always use top-down (negative height) */
    if (compression != 0) {
        info_header.width = (int32_t)width;
        info_header.height = -(int32_t)height; /* Top-down for RLE */
    } else {
        info_header.width = flip_row_order ? (int32_t)width : -(int32_t)width;
        info_header.height = flip_row_order ? (int32_t)height : -(int32_t)height;
    }
    info_header.planes = 1;
    info_header.bit_count = bit_count;
    info_header.compression = compression;
    info_header.size_image = compression ? image_data_size : 0;
    info_header.clr_used = (bit_count <= 8) ? palette_size : 0;
    info_header.clr_important = 0;

    /* Calculate file offsets */
    uint32_t file_header_size = sizeof(BITMAPFILEHEADER);
    uint32_t info_header_size = sizeof(BITMAPINFOHEADER);
    uint32_t palette_data_size = (bit_count <= 8) ? (palette_size * sizeof(RGBQUAD)) : 0;
    uint32_t bitmap_data_offset = file_header_size + info_header_size + palette_data_size;
    uint32_t file_size = bitmap_data_offset + image_data_size;

    /* Initialize file header */
    file_header.type = 0x4D42; /* "BM" */
    file_header.size = file_size;
    file_header.reserved1 = 0;
    file_header.reserved2 = 0;
    file_header.off_bits = bitmap_data_offset;

    /* Open output file */
    outfile = g_fopen(filename, "wb");
    if (!outfile) {
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Write file header */
    if (fwrite(&file_header, sizeof(BITMAPFILEHEADER), 1, outfile) != 1) {
        fclose(outfile);
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Write info header */
    if (fwrite(&info_header, sizeof(BITMAPINFOHEADER), 1, outfile) != 1) {
        fclose(outfile);
        g_free(image_data);
        if (palette) {
            g_free(palette);
        }
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Write palette if present */
    if (palette && palette_data_size > 0) {
        if (fwrite(palette, sizeof(RGBQUAD), palette_size, outfile) != palette_size) {
            fclose(outfile);
            g_free(image_data);
            g_free(palette);
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    }

    /* Write bitmap data */
    if (compression && bit_count == 8) {
        /* RLE compression: write all data at once (RLE data is not row-based) */
        if (fwrite(image_data, 1, image_data_size, outfile) != image_data_size) {
            fclose(outfile);
            g_free(image_data);
            if (palette) {
                g_free(palette);
            }
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    } else if (flip_row_order) {
        /* Bottom-up: write rows in reverse order */
        for (int32_t y = height - 1; y >= 0; y--) {
            const uint8_t* row = image_data + y * row_size;
            if (fwrite(row, 1, row_size, outfile) != row_size) {
                fclose(outfile);
                g_free(image_data);
                if (palette) {
                    g_free(palette);
                }
                cairo_surface_destroy(composite);
                return PLUGIN_ERROR_FILE_WRITE_ERROR;
            }
        }
    } else {
        /* Top-down: write rows in normal order */
        if (fwrite(image_data, 1, image_data_size, outfile) != image_data_size) {
            fclose(outfile);
            g_free(image_data);
            if (palette) {
                g_free(palette);
            }
            cairo_surface_destroy(composite);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    }

    fclose(outfile);
    g_free(image_data);
    if (palette) {
        g_free(palette);
    }
    cairo_surface_destroy(composite);

    return PLUGIN_ERROR_NONE;
}

/**
 * Get size of BMP-specific save options structure
 */
static size_t get_bmp_save_options_size(void) {
    return sizeof(BMPSaveOptions);
}

/**
 * Initialize BMP-specific save options with defaults
 */
static void init_bmp_save_options(void* plugin_data) {
    BMPSaveOptions* opts = (BMPSaveOptions*)plugin_data;
    if (opts) {
        opts->flip_row_order = true;
        opts->color_model = BMP_COLOR_MODEL_AUTO;
        opts->color_depth = BMP_COLOR_DEPTH_24BPP;
        opts->grayscale_depth = BMP_GRAYSCALE_DEPTH_8BPP;
        opts->use_rle_compression = false;
        opts->restrict_palette_size = false;
        opts->palette_size = 256;
        opts->use_legacy_15bit = false;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}

/**
 * Initialize BMP plugin
 */
bool plugin_init_bmp(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "BMP - Windows Bitmap";
    out_plugin->format_info.extensions = "bmp";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 80;

    out_plugin->callbacks.can_load = can_load_bmp;
    out_plugin->callbacks.load = load_bmp;
    out_plugin->callbacks.can_save = can_save_bmp;
    out_plugin->callbacks.save = save_bmp;
    out_plugin->callbacks.get_save_options_size = get_bmp_save_options_size;
    out_plugin->callbacks.init_save_options = init_bmp_save_options;

    return true;
}
