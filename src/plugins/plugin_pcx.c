/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

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
#include "debug_logger.h"

/* Helper function to read little-endian uint16_t */
static uint16_t read_le16(const uint8_t* data) {
    return (uint16_t)(data[0] | (data[1] << 8));
}

/* Round up to even as per common PCX convention for BytesPerLine */
static uint32_t pcx_even(uint32_t value) {
    return (value + 1u) & ~1u;
}

/* Some real-world PCX encoders emit RLE runs that cross scanline boundaries.
 * A strict per-scanline decoder that truncates runs will desync the stream and
 * eventually hit EOF mid-scanline. Keeping run state across scanlines makes
 * decoding robust.
 */
typedef struct {
    uint8_t value;
    uint32_t remaining;
} PCXRLEState;

/* PCX header structure (packed for file I/O) */
#pragma pack(push, 1)
typedef struct {
    uint8_t manufacturer;              /* Should be 0x0A */
    uint8_t version;                   /* Version number (0, 2, 3, 4, or 5) */
    uint8_t encoding;                  /* Compression type (1 = RLE) */
    uint8_t bits_per_pixel;            /* Bits per pixel per plane */
    uint8_t xmin_bytes[2];             /* Left coordinate (little-endian) */
    uint8_t ymin_bytes[2];             /* Top coordinate (little-endian) */
    uint8_t xmax_bytes[2];             /* Right coordinate (little-endian) */
    uint8_t ymax_bytes[2];             /* Bottom coordinate (little-endian) */
    uint8_t hdpi_bytes[2];             /* Horizontal DPI (little-endian) */
    uint8_t vdpi_bytes[2];             /* Vertical DPI (little-endian) */
    uint8_t color_map[48];             /* 16-color palette (16 RGB triplets) */
    uint8_t reserved;                  /* Reserved (should be 0) */
    uint8_t num_planes;                /* Number of color planes */
    uint8_t bytes_per_line_bytes[2];   /* Bytes per scanline per plane (little-endian) */
    uint8_t palette_type_bytes[2];     /* Palette type (little-endian) */
    uint8_t horz_screen_size_bytes[2]; /* Horizontal screen size (little-endian) */
    uint8_t vert_screen_size_bytes[2]; /* Vertical screen size (little-endian) */
    uint8_t reserved2[54];             /* Reserved padding to 128 bytes */
} PCXHeader;
#pragma pack(pop)

/**
 * Check if file is PCX format
 */
static bool can_load_pcx(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 4) {
        return false;
    }

    /* Check magic number (0x0A) and encoding (should be 1 for RLE) */
    if (header[0] == 0x0A && header[2] == 0x01) {
        /* Check version is valid (0, 2, 3, 4, or 5) */
        uint8_t version = header[1];
        if (version == 0 || version == 2 || version == 3 || version == 4 || version == 5) {
            return true;
        }
    }

    return false;
}

/**
 * Check if plugin can save to PCX format
 * This plugin is read-only, so always return false
 */
static bool can_save_pcx(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Read RLE-encoded byte from PCX file
 * Returns true on success, false on error (EOF / corrupt).
 *
 * Maintains state across scanlines so that overlong runs do not desync the stream.
 */
static bool pcx_rle_read_byte(FILE* file, PCXRLEState* st, uint8_t* out) {
    if (!file || !st || !out) {
        return false;
    }
    /* If we have buffered run bytes, serve them first */
    if (st->remaining > 0) {
        *out = st->value;
        st->remaining--;
        return true;
    }
    int c = fgetc(file);
    if (c == EOF) {
        return false;
    }
    if ((c & 0xC0) == 0xC0) {
        /* RLE packet */
        uint8_t count = (uint8_t)(c & 0x3F);
        int v = fgetc(file);
        if (v == EOF) {
            return false;
        }
        /* Treat count==0 as count==1 (malformed but seen in the wild) */
        if (count == 0) {
            count = 1;
        }
        st->value = (uint8_t)v;
        st->remaining = (uint32_t)count;
        /* Emit first byte immediately */
        *out = st->value;
        st->remaining--;
        return true;
    }
    /* Literal byte */
    *out = (uint8_t)c;
    return true;
}

/**
 * Decompress a single scanline from PCX file
 * Returns number of bytes read, or -1 on error
 */
static int decompress_pcx_scanline(FILE* file, PCXRLEState* st, uint8_t* buffer, uint32_t bytes_per_line) {
    uint32_t bytes_read = 0;
    while (bytes_read < bytes_per_line) {
        uint8_t b;
        if (!pcx_rle_read_byte(file, st, &b)) {
            return -1;
        }
        buffer[bytes_read++] = b;
    }
    return (int)bytes_read;
}

/**
 * Check for VGA palette at end of file
 * Returns true if palette found and read, false otherwise
 */
static bool read_pcx_vga_palette(FILE* file, uint8_t* palette) {
    long current_pos = ftell(file);
    long file_size;

    /* Get file size */
    if (fseek(file, 0, SEEK_END) != 0) {
        fseek(file, current_pos, SEEK_SET);
        return false;
    }
    file_size = ftell(file);

    /* Check if file is large enough for VGA palette (769 bytes from EOF) */
    if (file_size < 769) {
        fseek(file, current_pos, SEEK_SET);
        return false;
    }

    /* Seek to 769 bytes from end */
    if (fseek(file, file_size - 769, SEEK_SET) != 0) {
        fseek(file, current_pos, SEEK_SET);
        return false;
    }

    /* Check for palette identifier (0x0C) */
    int palette_id = fgetc(file);
    if (palette_id != 0x0C) {
        fseek(file, current_pos, SEEK_SET);
        return false;
    }

    /* Read 256 RGB triplets (768 bytes) */
    if (fread(palette, 1, 768, file) != 768) {
        fseek(file, current_pos, SEEK_SET);
        return false;
    }

    /* Restore file position */
    fseek(file, current_pos, SEEK_SET);
    return true;
}

/**
 * Load PCX image
 */
static PluginError load_pcx(ImageDocument* doc, const char* filename) {
    FILE* infile;
    PCXHeader header;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint32_t width, height;
    uint32_t bytes_per_line;
    PCXRLEState rle = {0, 0};
    uint8_t* scanline_buffer = NULL;
    uint8_t* plane_data = NULL;
    uint8_t* vga_palette = NULL;
    bool has_alpha = false;
    bool use_vga_palette = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open PCX file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read PCX header */
    if (fread(&header, sizeof(PCXHeader), 1, infile) != 1) {
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Validate header */
    if (header.manufacturer != 0x0A) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    if (header.encoding != 0x01) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Read little-endian multi-byte values */
    uint16_t xmin = read_le16(header.xmin_bytes);
    uint16_t ymin = read_le16(header.ymin_bytes);
    uint16_t xmax = read_le16(header.xmax_bytes);
    uint16_t ymax = read_le16(header.ymax_bytes);
    bytes_per_line = read_le16(header.bytes_per_line_bytes);

    /* Calculate image dimensions */
    width = xmax - xmin + 1;
    height = ymax - ymin + 1;

    /* Validate dimensions */
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Some encoders incorrectly store total bytes per scanline (all planes combined)
     * in the BytesPerLine header field, instead of bytes per scanline per plane.
     * This shows up most often in 24-bit (8bpp, 3 planes) PCX and causes EOF
     * during decompression. Try a conservative correction when it looks plausible.
     */
    {
        uint32_t expected_per_plane = pcx_even(width);

        if (header.bits_per_pixel == 8 && header.num_planes >= 2) {
            /* If bytes_per_line is suspiciously large and divisible by num_planes,
             * it might actually be total bytes for all planes. */
            if (bytes_per_line > expected_per_plane + 64 &&
                (bytes_per_line % header.num_planes) == 0) {
                uint32_t candidate = bytes_per_line / header.num_planes;
                /* Accept only if candidate matches the typical per-plane size closely. */
                if (candidate >= width && candidate <= expected_per_plane + 64) {
                    bytes_per_line = candidate;
                }
            }

            /* Another common broken case: BytesPerLine == width * num_planes (no padding),
             * which will also pass the above divisibility check.
             * If candidate == width, normalize to even padding.
             */
            if (bytes_per_line == width) {
                bytes_per_line = expected_per_plane;
            }
        }
    }

    /* Validate bytes_per_line is sufficient for the image width */
    uint32_t min_bytes_per_line;
    if (header.bits_per_pixel == 1) {
        min_bytes_per_line = (width + 7) / 8; /* Round up to nearest byte */
    } else if (header.bits_per_pixel == 2) {
        min_bytes_per_line = (width + 3) / 4; /* 4 pixels per byte */
    } else if (header.bits_per_pixel == 4) {
        min_bytes_per_line = (width + 1) / 2; /* 2 pixels per byte */
    } else {
        min_bytes_per_line = width; /* 1 byte per pixel */
    }

    /* bytes_per_line must be at least min_bytes_per_line */
    /* Note: spec says it should be even, but we'll be lenient */
    if (bytes_per_line < min_bytes_per_line) {
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Reject absurd bytes_per_line that would almost certainly run past EOF.
     * This keeps us from looping on corrupt headers.
     */
    if (bytes_per_line > 131072u) {
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Validate bit depth and planes */
    if (header.bits_per_pixel == 0 || header.bits_per_pixel > 8 ||
        header.num_planes == 0 || header.num_planes > 4) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Check for VGA palette (for 8-bit indexed images) */
    if (header.bits_per_pixel == 8 && header.num_planes == 1) {
        vga_palette = g_malloc(768);
        if (vga_palette && read_pcx_vga_palette(infile, vga_palette)) {
            use_vga_palette = true;
        }
        /* Reset file position after palette check */
        fseek(infile, sizeof(PCXHeader), SEEK_SET);
    }

    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = 4; /* Always RGBA internally */
    doc->bit_depth = 8;
    doc->has_alpha = has_alpha;

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
        if (vga_palette) {
            g_free(vga_palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        if (vga_palette) {
            g_free(vga_palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        if (vga_palette) {
            g_free(vga_palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate buffers for scanline and plane data */
    scanline_buffer = g_malloc(bytes_per_line);
    if (!scanline_buffer) {
        layer_free(base_layer);
        if (vga_palette) {
            g_free(vga_palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    uint32_t total_bytes_per_line = bytes_per_line * header.num_planes;
    plane_data = g_malloc(total_bytes_per_line);
    if (!plane_data) {
        g_free(scanline_buffer);
        layer_free(base_layer);
        if (vga_palette) {
            g_free(vga_palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Process image based on bit depth and number of planes */
    if (header.bits_per_pixel == 1) {
        /* 1-bit per pixel: monochrome or indexed */
        if (header.num_planes == 1) {
            /* Monochrome or 2-color indexed - use black/white palette */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;

                /* Decompress scanline */
                if (decompress_pcx_scanline(infile, &rle, scanline_buffer, bytes_per_line) < 0) {
                    g_free(plane_data);
                    g_free(scanline_buffer);
                    layer_free(base_layer);
                    if (vga_palette) {
                        g_free(vga_palette);
                    }
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                /* Convert bits to pixels - 8 pixels per byte, MSB first */
                /* Pixel 0 = bit 7, pixel 1 = bit 6, ..., pixel 7 = bit 0 */
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t byte_idx = x / 8;
                    uint32_t bit_pos = 7 - (x % 8); /* MSB first: bit 7 for x=0, bit 0 for x=7 */

                    /* Ensure we don't access beyond the buffer */
                    if (byte_idx >= bytes_per_line) {
                        break;
                    }

                    uint8_t bit = (scanline_buffer[byte_idx] >> bit_pos) & 1;

                    /* For 1-bit images, use black (0) and white (1) directly */
                    uint8_t gray = bit ? 255 : 0;

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = gray; /* B */
                    row[x * 4 + 1] = gray; /* G */
                    row[x * 4 + 2] = gray; /* R */
                    row[x * 4 + 3] = 255;  /* A */
                }
            }
        } else if (header.num_planes == 4) {
            /* 4-plane: CGA/EGA 16-color */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;

                /* Decompress all planes */
                for (uint8_t plane = 0; plane < 4; plane++) {
                    if (decompress_pcx_scanline(infile, &rle, plane_data + plane * bytes_per_line,
                                                bytes_per_line) < 0) {
                        g_free(plane_data);
                        g_free(scanline_buffer);
                        layer_free(base_layer);
                        if (vga_palette) {
                            g_free(vga_palette);
                        }
                        fclose(infile);
                        return PLUGIN_ERROR_FILE_READ_ERROR;
                    }
                }

                /* Combine planes to form pixel values - 4 planes, 1 bit each */
                /* Plane 0 = bit 0 of index, plane 1 = bit 1, plane 2 = bit 2, plane 3 = bit 3 */
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t byte_idx = x / 8;
                    uint32_t bit_pos = 7 - (x % 8); /* MSB first */

                    /* Ensure we don't access beyond the buffer */
                    if (byte_idx >= bytes_per_line) {
                        break;
                    }

                    uint8_t index = 0;

                    /* Combine bits from all 4 planes */
                    for (uint8_t plane = 0; plane < 4; plane++) {
                        uint8_t* plane_scanline = plane_data + plane * bytes_per_line;
                        uint8_t bit = (plane_scanline[byte_idx] >> bit_pos) & 1;
                        index |= (bit << plane); /* Plane 0 = LSB, plane 3 = MSB of 4-bit index */
                    }

                    uint8_t r = 0, g = 0, b = 0;
                    if (index < 16) {
                        /* Use header palette */
                        r = header.color_map[index * 3 + 0];
                        g = header.color_map[index * 3 + 1];
                        b = header.color_map[index * 3 + 2];
                    }

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = 255;
                }
            }
        }
    } else if (header.bits_per_pixel == 2) {
        /* 2-bit per pixel: 4-color indexed */
        if (header.num_planes == 1) {
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;

                /* Decompress scanline */
                if (decompress_pcx_scanline(infile, &rle, scanline_buffer, bytes_per_line) < 0) {
                    g_free(plane_data);
                    g_free(scanline_buffer);
                    layer_free(base_layer);
                    if (vga_palette) {
                        g_free(vga_palette);
                    }
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                /* Convert 2-bit pixels - 4 pixels per byte, MSB first */
                /* Pixel 0 = bits 7-6, pixel 1 = bits 5-4, pixel 2 = bits 3-2, pixel 3 = bits 1-0 */
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t byte_idx = x / 4;
                    uint32_t pixel_pos = x % 4;

                    /* Ensure we don't access beyond the buffer */
                    if (byte_idx >= bytes_per_line) {
                        break;
                    }

                    /* Extract 2-bit value: pixel 0 = bits 7-6, pixel 1 = bits 5-4, etc. */
                    uint8_t index = (scanline_buffer[byte_idx] >> (6 - pixel_pos * 2)) & 0x03;

                    uint8_t r = 0, g = 0, b = 0;
                    if (index < 16) {
                        r = header.color_map[index * 3 + 0];
                        g = header.color_map[index * 3 + 1];
                        b = header.color_map[index * 3 + 2];
                    }

                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = 255;
                }
            }
        }
    } else if (header.bits_per_pixel == 4) {
        /* 4-bit per pixel: 16-color indexed */
        if (header.num_planes == 1) {
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;

                /* Decompress scanline */
                if (decompress_pcx_scanline(infile, &rle, scanline_buffer, bytes_per_line) < 0) {
                    g_free(plane_data);
                    g_free(scanline_buffer);
                    layer_free(base_layer);
                    if (vga_palette) {
                        g_free(vga_palette);
                    }
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                /* Convert 4-bit pixels - 2 pixels per byte, MSB first */
                /* Pixel 0 = upper nibble (bits 7-4), pixel 1 = lower nibble (bits 3-0) */
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t byte_idx = x / 2;

                    /* Ensure we don't access beyond the buffer */
                    if (byte_idx >= bytes_per_line) {
                        break;
                    }

                    uint8_t index;
                    if (x % 2 == 0) {
                        index = (scanline_buffer[byte_idx] >> 4) & 0x0F; /* Upper nibble */
                    } else {
                        index = scanline_buffer[byte_idx] & 0x0F; /* Lower nibble */
                    }

                    uint8_t r = 0, g = 0, b = 0;
                    if (index < 16) {
                        r = header.color_map[index * 3 + 0];
                        g = header.color_map[index * 3 + 1];
                        b = header.color_map[index * 3 + 2];
                    }

                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = 255;
                }
            }
        }
    } else if (header.bits_per_pixel == 8) {
        /* 8-bit per pixel */
        if (header.num_planes == 1) {
            /* 8-bit indexed (256 colors) */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;

                /* Decompress scanline */
                if (decompress_pcx_scanline(infile, &rle, scanline_buffer, bytes_per_line) < 0) {
                    g_free(plane_data);
                    g_free(scanline_buffer);
                    layer_free(base_layer);
                    if (vga_palette) {
                        g_free(vga_palette);
                    }
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                /* Convert indexed pixels using VGA palette - 1 byte per pixel */
                for (uint32_t x = 0; x < width; x++) {
                    /* Ensure we don't access beyond the buffer */
                    if (x >= bytes_per_line) {
                        break;
                    }

                    uint8_t index = scanline_buffer[x];
                    uint8_t r = 0, g = 0, b = 0;

                    if (use_vga_palette && vga_palette) {
                        /* Use VGA palette */
                        r = vga_palette[index * 3 + 0];
                        g = vga_palette[index * 3 + 1];
                        b = vga_palette[index * 3 + 2];
                    } else if (index < 16) {
                        /* Fallback to header palette */
                        r = header.color_map[index * 3 + 0];
                        g = header.color_map[index * 3 + 1];
                        b = header.color_map[index * 3 + 2];
                    }

                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = 255;
                }
            }
        } else if (header.num_planes == 3) {
            /* 24-bit RGB (8 bits per plane) - planes are stored in order: R, G, B */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;

                /* Decompress all 3 planes - order is R (plane 0), G (plane 1), B (plane 2) */
                for (uint8_t plane = 0; plane < 3; plane++) {
                    if (decompress_pcx_scanline(infile, &rle, plane_data + plane * bytes_per_line,
                                                bytes_per_line) < 0) {
                        g_free(plane_data);
                        g_free(scanline_buffer);
                        layer_free(base_layer);
                        if (vga_palette) {
                            g_free(vga_palette);
                        }
                        fclose(infile);
                        debug_log("WRN", "Error decompressing scanline\n");
                        return PLUGIN_ERROR_FILE_READ_ERROR;
                    }
                }

                /* Combine planes to form RGB pixels */
                /* Plane 0 = Red, Plane 1 = Green, Plane 2 = Blue */
                for (uint32_t x = 0; x < width; x++) {
                    /* Ensure we don't access beyond the buffer */
                    if (x >= bytes_per_line) {
                        break;
                    }

                    uint8_t r = plane_data[0 * bytes_per_line + x];
                    uint8_t g = plane_data[1 * bytes_per_line + x];
                    uint8_t b = plane_data[2 * bytes_per_line + x];

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = 255;
                }
            }
        }
    }

    /* Cleanup */
    g_free(plane_data);
    g_free(scanline_buffer);
    if (vga_palette) {
        g_free(vga_palette);
    }
    fclose(infile);

    /* Mark surface as modified */
    cairo_surface_mark_dirty(temp_surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Render composite */
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

/**
 * Initialize PCX plugin
 */
bool plugin_init_pcx(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "PCX - ZSoft Paintbrush";
    out_plugin->format_info.extensions = "pcx";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_pcx;
    out_plugin->callbacks.load = load_pcx;
    out_plugin->callbacks.can_save = can_save_pcx; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
