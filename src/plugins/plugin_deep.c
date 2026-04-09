/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "plugins/plugin_deep.h"
#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

/* Reference: https://wiki.amigaos.net/wiki/DEEP_IFF_Chunky_Pixel_Image */

/* IFF chunk header structure */
typedef struct {
    char id[4];    /* 4-byte chunk ID */
    uint32_t size; /* Chunk size (big-endian) */
} IFFChunkHeader;

/* DEEP compression types */
#define DEEP_COMPRESSION_NONE 0
#define DEEP_COMPRESSION_RLE 1
#define DEEP_COMPRESSION_HUFFMAN 2
#define DEEP_COMPRESSION_DYNAMICHUFF 3
#define DEEP_COMPRESSION_JPEG 4
#define DEEP_COMPRESSION_TVDC 5

/* DEEP pixel element types */
#define DEEP_ELEMENT_RED 1
#define DEEP_ELEMENT_GREEN 2
#define DEEP_ELEMENT_BLUE 3
#define DEEP_ELEMENT_ALPHA 4
#define DEEP_ELEMENT_YELLOW 5
#define DEEP_ELEMENT_CYAN 6
#define DEEP_ELEMENT_MAGENTA 7
#define DEEP_ELEMENT_BLACK 8
#define DEEP_ELEMENT_MASK 9
#define DEEP_ELEMENT_ZBUFFER 10
#define DEEP_ELEMENT_OPACITY 11
#define DEEP_ELEMENT_LINEARKEY 12
#define DEEP_ELEMENT_BINARYKEY 13

/* DEEP global structure (DGBL chunk) */
typedef struct {
    uint16_t display_width;  /* Display width (big-endian) */
    uint16_t display_height; /* Display height (big-endian) */
    uint16_t compression;    /* Compression type (big-endian) */
    uint8_t x_aspect;        /* Pixel aspect ratio width */
    uint8_t y_aspect;        /* Pixel aspect ratio height */
} DEEPGlobal;

/* DEEP pixel element definition */
typedef struct {
    uint16_t c_type;      /* Component type (big-endian) */
    uint16_t c_bit_depth; /* Bits per component (big-endian) */
} DEEPPixelElement;

/* DEEP location structure (DLOC chunk) */
typedef struct {
    uint16_t w; /* Body width (big-endian) */
    uint16_t h; /* Body height (big-endian) */
    int16_t x;  /* X offset (big-endian) */
    int16_t y;  /* Y offset (big-endian) */
} DEEPLocation;

/* Pixel element info for decoding */
typedef struct {
    uint16_t type;        /* Element type (RED, GREEN, BLUE, ALPHA, etc.) */
    uint16_t bit_depth;   /* Bits per component */
    uint32_t byte_offset; /* Byte offset in pixel data */
    uint32_t bit_offset;  /* Bit offset within byte (for sub-byte depths) */
} PixelElementInfo;

/**
 * Read a 16-bit big-endian integer from file
 * Returns true on success, false on error
 */
static bool read_be16_safe(FILE* f, uint16_t* out_value) {
    uint8_t bytes[2];
    if (fread(bytes, 1, 2, f) != 2) {
        if (ferror(f)) {
            debug_log("WRN", "DEEP plugin: Error reading 16-bit value at position %ld", ftell(f));
        } else {
            debug_log("WRN", "DEEP plugin: EOF reading 16-bit value at position %ld", ftell(f));
        }
        return false;
    }
    *out_value = ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
    return true;
}

/**
 * Read a 32-bit big-endian integer from file
 * Returns true on success, false on error
 */
static bool read_be32_safe(FILE* f, uint32_t* out_value) {
    uint8_t bytes[4];
    if (fread(bytes, 1, 4, f) != 4) {
        if (ferror(f)) {
            debug_log("WRN", "DEEP plugin: Error reading 32-bit value at position %ld", ftell(f));
        } else {
            debug_log("WRN", "DEEP plugin: EOF reading 32-bit value at position %ld", ftell(f));
        }
        return false;
    }
    *out_value = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                 ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
    return true;
}

/**
 * Read a 16-bit big-endian integer from file (legacy, returns 0 on error)
 */
static uint16_t read_be16(FILE* f) {
    uint16_t value;
    if (!read_be16_safe(f, &value)) {
        return 0;
    }
    return value;
}

/**
 * Read a 32-bit big-endian integer from file (legacy, returns 0 on error)
 */
static uint32_t read_be32(FILE* f) {
    uint32_t value;
    if (!read_be32_safe(f, &value)) {
        return 0;
    }
    return value;
}

/**
 * Read a 16-bit big-endian integer from buffer
 */
static uint16_t read_be16_from_buffer(const uint8_t* buffer) {
    return ((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1];
}

/**
 * Read a 32-bit big-endian integer from buffer
 */
static uint32_t read_be32_from_buffer(const uint8_t* buffer) {
    return ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
}

/**
 * Read a signed 16-bit big-endian integer from file
 */
static int16_t read_be16_signed(FILE* f) {
    return (int16_t)read_be16(f);
}

/**
 * Read IFF chunk header
 */
static bool read_iff_chunk_header(FILE* f, IFFChunkHeader* header) {
    long pos = ftell(f);
    if (fread(header->id, 1, 4, f) != 4) {
        if (ferror(f)) {
            debug_log("WRN", "DEEP plugin: Error reading chunk ID at position %ld", pos);
        } else {
            debug_log("WRN", "DEEP plugin: EOF reading chunk ID at position %ld", pos);
        }
        return false;
    }
    if (!read_be32_safe(f, &header->size)) {
        debug_log("WRN", "DEEP plugin: Failed to read chunk size for chunk %c%c%c%c at position %ld",
                  header->id[0], header->id[1], header->id[2], header->id[3], pos);
        return false;
    }
    return true;
}

/**
 * Skip to next even boundary (IFF chunks are padded to even sizes)
 */
static void skip_iff_padding(FILE* f, uint32_t size) {
    if (size & 1) {
        /* Odd size, skip one byte */
        fseek(f, 1, SEEK_CUR);
    }
}

/**
 * Read and skip IFF chunk (if we don't need its data)
 */
static bool skip_iff_chunk(FILE* f, uint32_t size) {
    if (fseek(f, size, SEEK_CUR) != 0) {
        return false;
    }
    skip_iff_padding(f, size);
    return true;
}

/**
 * Decompress RLE-encoded data (IFF-style RLE)
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
        if (in_pos >= compressed_size) {
            break;
        }

        uint8_t c = compressed_data[in_pos++];

        if (c == 0x80) {
            /* RLE escape sequence */
            if (in_pos >= compressed_size) {
                break;
            }
            uint8_t count = compressed_data[in_pos++];

            if (count == 0) {
                /* Literal 0x80 */
                if (out_pos < decompressed_size) {
                    output[out_pos++] = 0x80;
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
        debug_log("WRN", "DEEP plugin: RLE decompression incomplete (expected %zu bytes, got %zu, input_pos=%zu/%zu)",
                  decompressed_size, out_pos, in_pos, compressed_size);
        /* Don't return partial data - it will cause crashes. Try alternative decompression methods */
        g_free(output);
        return NULL;
    }

    return output;
}

/**
 * Decompress RLE-encoded data per scanline
 * TVPaint may compress each scanline separately
 */
static uint8_t* decompress_rle_per_scanline(const uint8_t* compressed_data, size_t compressed_size,
                                            uint32_t width, uint32_t height, uint32_t bytes_per_pixel) {
    uint8_t* output = g_malloc(width * height * bytes_per_pixel);
    if (!output) {
        return NULL;
    }

    size_t in_pos = 0;
    size_t row_size = width * bytes_per_pixel;

    for (uint32_t y = 0; y < height && in_pos < compressed_size; y++) {
        uint8_t* row_output = output + y * row_size;
        size_t row_out_pos = 0;

        /* Decompress one scanline */
        while (row_out_pos < row_size && in_pos < compressed_size) {
            uint8_t c = compressed_data[in_pos++];

            if (c == 0x80) {
                /* RLE escape sequence */
                if (in_pos >= compressed_size) {
                    break;
                }
                uint8_t count = compressed_data[in_pos++];

                if (count == 0) {
                    /* Literal 0x80 */
                    if (row_out_pos < row_size) {
                        row_output[row_out_pos++] = 0x80;
                    }
                } else {
                    /* Repeat next byte (count + 1) times */
                    if (in_pos >= compressed_size) {
                        break;
                    }
                    uint8_t value = compressed_data[in_pos++];

                    int repeat = count + 1;
                    for (int i = 0; i < repeat && row_out_pos < row_size; i++) {
                        row_output[row_out_pos++] = value;
                    }
                }
            } else {
                /* Literal byte */
                if (row_out_pos < row_size) {
                    row_output[row_out_pos++] = c;
                }
            }
        }

        /* If scanline not fully decompressed, fill remainder with zeros */
        while (row_out_pos < row_size) {
            row_output[row_out_pos++] = 0;
        }
    }

    return output;
}

/**
 * Decompress RLE-encoded data per component per scanline
 * DEEP format compresses each component separately, line by line
 */
static uint8_t* decompress_rle_per_component(const uint8_t* compressed_data, size_t compressed_size,
                                             uint32_t width, uint32_t height, uint32_t num_elements) {
    uint8_t* output = g_malloc(width * height * num_elements);
    if (!output) {
        return NULL;
    }

    /* Initialize output to zero */
    memset(output, 0, width * height * num_elements);

    size_t in_pos = 0;

    /* For each component */
    for (uint32_t comp = 0; comp < num_elements && in_pos < compressed_size; comp++) {
        /* For each scanline of this component */
        for (uint32_t y = 0; y < height && in_pos < compressed_size; y++) {
            size_t row_out_pos = 0;

            /* Decompress one scanline of this component */
            /* Output format is chunky: for pixel (x,y), component c is at offset (y * width + x) * num_elements + c */
            while (row_out_pos < width && in_pos < compressed_size) {
                uint8_t c = compressed_data[in_pos++];

                if (c == 0x80) {
                    /* RLE escape sequence */
                    if (in_pos >= compressed_size) {
                        break;
                    }
                    uint8_t count = compressed_data[in_pos++];

                    if (count == 0) {
                        /* Literal 0x80 */
                        if (row_out_pos < width) {
                            size_t pixel_idx = (y * width + row_out_pos) * num_elements + comp;
                            if (pixel_idx < width * height * num_elements) {
                                output[pixel_idx] = 0x80;
                            }
                            row_out_pos++;
                        }
                    } else {
                        /* Repeat next byte (count + 1) times */
                        if (in_pos >= compressed_size) {
                            break;
                        }
                        uint8_t value = compressed_data[in_pos++];

                        int repeat = count + 1;
                        for (int i = 0; i < repeat && row_out_pos < width; i++) {
                            size_t pixel_idx = (y * width + row_out_pos) * num_elements + comp;
                            if (pixel_idx < width * height * num_elements) {
                                output[pixel_idx] = value;
                            }
                            row_out_pos++;
                        }
                    }
                } else {
                    /* Literal byte */
                    if (row_out_pos < width) {
                        size_t pixel_idx = (y * width + row_out_pos) * num_elements + comp;
                        if (pixel_idx < width * height * num_elements) {
                            output[pixel_idx] = c;
                        }
                        row_out_pos++;
                    }
                }
            }
        }
    }

    /* Validate we used a reasonable amount of input data */
    /* For RLE compression, we might not use all input if there are many repeated values */
    if (in_pos < compressed_size / 4) {
        /* Used less than 25% of input - probably wrong format */
        debug_log("WRN", "DEEP plugin: Per-component RLE used only %zu/%zu bytes (%.1f%%) of input - may be wrong format",
                  in_pos, compressed_size, (in_pos * 100.0) / compressed_size);
        g_free(output);
        return NULL;
    }

    /* Check if we decompressed enough output data */
    size_t expected_output = width * height * num_elements;
    if (expected_output == 0) {
        debug_log("WRN", "DEEP plugin: Invalid output size calculation");
        g_free(output);
        return NULL;
    }

    return output;
}

/**
 * Decompress TVDC (TVPaint Deep Compression) data
 * TVDC uses delta compression with a 16-word lookup table and run-length limiting
 * Compression is done line by line for each element (component)
 * 
 * @param source Compressed data (read in nibbles)
 * @param dest Output buffer for decompressed data
 * @param table 16-word lookup table (delta values)
 * @param size Size of decompressed data (number of bytes)
 * @return Number of source bytes consumed (in nibbles: (pos+1)/2)
 */
static size_t decompress_tvdc(const uint8_t* source, size_t source_size, uint8_t* dest, const uint16_t* table, size_t size) {
    size_t pos = 0; /* Position in source (in nibbles) */
    uint8_t v = 0;  /* Current value */

    /* Match the original algorithm exactly: for loop with i, and dest[++i] in run-length */
    /* But add safety checks to prevent buffer overruns */
    for (size_t i = 0; i < size; i++) {
        /* Check if we have enough source data */
        size_t source_byte = pos >> 1;
        if (source_byte >= source_size) {
            /* Out of source data - fill remainder with last value */
            debug_log("WRN", "DEEP plugin: TVDC decompression ran out of source data (pos=%zu, source_size=%zu, i=%zu/%zu), filling remainder",
                      pos, source_size, i, size);
            /* Fill remaining bytes with last value */
            while (i < size) {
                dest[i++] = v;
            }
            break;
        }

        /* Read nibble from source */
        uint8_t d = source[source_byte];
        if (pos++ & 1) {
            d &= 0xf; /* Lower nibble */
        } else {
            d >>= 4; /* Upper nibble */
        }

        /* Add delta from lookup table */
        /* Original algorithm: v += table[d] where v is UBYTE and table[d] is WORD (signed) */
        /* In C, we need to handle signed addition correctly */
        /* Convert table value to signed, add to v, then convert back to uint8_t (wraps) */
        int16_t delta = (int16_t)table[d];

        /* Add delta: use signed arithmetic then convert to uint8_t for wrapping */
        /* This matches the original: unsigned addition with signed delta */
        v = (uint8_t)((int32_t)v + (int32_t)delta);

        /* Safety check: ensure we don't write beyond buffer */
        if (i >= size) {
            debug_log("WRN", "DEEP plugin: TVDC decompression index %zu exceeds size %zu", i, size);
            break;
        }

        dest[i] = v;

        /* If delta is 0, it's a run - next nibble gives run length */
        if (!table[d]) {
            /* Check if we have enough source data for run length nibble */
            source_byte = pos >> 1;
            if (source_byte >= source_size) {
                debug_log("WRN", "DEEP plugin: TVDC decompression ran out of source data for run length (pos=%zu, source_size=%zu)",
                          pos, source_size);
                break;
            }

            /* Read run length nibble */
            d = source[source_byte];
            if (pos++ & 1) {
                d &= 0xf;
            } else {
                d >>= 4;
            }

            /* Repeat current value 'd' times (matches original: while(d--) dest[++i]=v) */
            /* Note: This increments i, and the for loop will also increment i at the end */
            /* So if d=3, we write to dest[++i] 3 times, then the for loop increments i once more */
            /* CRITICAL: Check bounds BEFORE incrementing i to prevent buffer overrun */
            while (d > 0) {
                if (i >= size - 1) {
                    /* Can't write more - we're at or past the last valid index */
                    /* This can happen if compressed data is malformed */
                    debug_log("WRN", "DEEP plugin: TVDC run-length would exceed buffer (i=%zu, size=%zu, d=%u)",
                              i, size, d);
                    break;
                }
                dest[++i] = v; /* Match original: dest[++i] = v */
                d--;
            }
        }
    }

    return (pos + 1) / 2; /* Return number of source bytes consumed */
}

/**
 * Decompress TVDC-encoded data per component per scanline
 * TVDC compression is applied line by line for each element (component)
 */
static uint8_t* decompress_tvdc_per_component(const uint8_t* compressed_data, size_t compressed_size,
                                              uint32_t width, uint32_t height, uint32_t num_elements,
                                              const uint16_t* tvdc_table) {
    /* Calculate output size with proper type casting to avoid overflow */
    size_t output_size = (size_t)width * (size_t)height * (size_t)num_elements;

    uint8_t* output = g_malloc(output_size);
    if (!output) {
        return NULL;
    }

    /* Initialize output to zero */
    memset(output, 0, output_size);

    size_t in_pos = 0;

    /* For each component - decompress all scanlines of component 0, then all of component 1, etc. */
    for (uint32_t comp = 0; comp < num_elements && in_pos < compressed_size; comp++) {
        /* For each scanline of this component */
        for (uint32_t y = 0; y < height && in_pos < compressed_size; y++) {
            /* Decompress to temporary buffer first */
            uint8_t* temp_row = g_malloc(width);
            if (!temp_row) {
                g_free(output);
                return NULL;
            }

            /* Decompress one scanline of this component */
            /* Check if we have enough source data remaining */
            size_t remaining_source = compressed_size - in_pos;
            size_t bytes_consumed = 0;

            if (remaining_source == 0) {
                debug_log("WRN", "DEEP plugin: TVDC decompression ran out of source data (comp=%u, y=%u), filling with zeros",
                          comp, y);
                /* Fill remaining scanlines with zeros */
                memset(temp_row, 0, width);
                /* Don't advance in_pos - we're already at the end */
            } else {
                /* Decompress with source size check - pass all remaining source data */
                /* The decompression function will handle bounds checking internally */
                bytes_consumed = decompress_tvdc(compressed_data + in_pos, remaining_source, temp_row,
                                                 tvdc_table, width);

                /* Validate bytes_consumed is reasonable */
                if (bytes_consumed == 0 || bytes_consumed > remaining_source) {
                    debug_log("WRN", "DEEP plugin: TVDC decompression returned invalid bytes_consumed=%zu (remaining=%zu, width=%u), filling with zeros",
                              bytes_consumed, remaining_source, width);
                    /* Fill with zeros on error */
                    memset(temp_row, 0, width);
                    /* Advance by at least 1 byte to avoid infinite loop, but don't exceed bounds */
                    bytes_consumed = 1;
                }

                /* Advance input position */
                in_pos += bytes_consumed;
            }

            /* Copy decompressed data to correct positions (chunky format) */
            /* For pixel (x,y), component c is at offset (y * width + x) * num_elements + c */
            for (uint32_t x = 0; x < width; x++) {
                size_t pixel_idx = ((size_t)y * (size_t)width + (size_t)x) * (size_t)num_elements + (size_t)comp;
                if (pixel_idx < output_size) {
                    output[pixel_idx] = temp_row[x];
                } else {
                    debug_log("WRN", "DEEP plugin: TVDC pixel index %zu exceeds output size %zu (x=%u, y=%u, comp=%u)",
                              pixel_idx, output_size, x, y, comp);
                    g_free(temp_row);
                    g_free(output);
                    return NULL;
                }
            }

            g_free(temp_row);

            /* Input position was already advanced above, or set to 0 on error */
            if (in_pos > compressed_size) {
                debug_log("WRN", "DEEP plugin: TVDC decompression exceeded input buffer (pos=%zu, size=%zu)",
                          in_pos, compressed_size);
                g_free(output);
                return NULL;
            }
        }
    }

    return output;
}

/**
 * Check if file is DEEP format
 */
static bool can_load_deep(const char* filename, const uint8_t* header, size_t header_size) {

    if (!header) {
        return false;
    }

    if (header_size < 12) {
        return false;
    }

    /* Check for IFF FORM header: "FORM" followed by size, then "DEEP" */
    if (memcmp(header, "FORM", 4) == 0) {
        /* Check for "DEEP" at offset 8 (standard DEEP format) */
        if (header_size >= 12 && memcmp(header + 8, "DEEP", 4) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * Check if plugin can save to DEEP format
 * This plugin is read-only for now
 */
static bool can_save_deep(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Load DEEP image
 */
static PluginError load_deep(ImageDocument* doc, const char* filename) {
    FILE* infile;
    IFFChunkHeader chunk_header;
    DEEPGlobal global = {0};
    DEEPLocation location = {0};
    DEEPPixelElement* pixel_elements = NULL;
    uint32_t num_elements = 0;
    PixelElementInfo* element_info = NULL;
    uint8_t* compressed_data = NULL;
    uint8_t* image_data = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    bool has_dgbl = false;
    bool has_dpel = false;
    bool has_dbod = false;
    bool has_dloc = false;
    bool has_tvdc = false;
    uint16_t tvdc_table[16] = {0}; /* TVDC lookup table (16 words) */
    uint32_t body_width = 0;
    uint32_t body_height = 0;
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    uint32_t bytes_per_pixel = 0;

    if (!doc || !filename) {
        debug_log("WRN", "DEEP plugin: Invalid parameters (doc=%p, filename=%p)", doc, filename);
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open DEEP file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        debug_log("WRN", "DEEP plugin: Failed to open file: %s (errno=%d)", filename, errno);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read FORM header */
    long file_start = ftell(infile); /* Remember where we started */

    if (!read_iff_chunk_header(infile, &chunk_header)) {
        debug_log("WRN", "DEEP plugin: Failed to read FORM header at position %ld", file_start);
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    if (memcmp(chunk_header.id, "FORM", 4) != 0) {
        debug_log("WRN", "DEEP plugin: Invalid FORM header");
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Read form type (should be "DEEP") */
    char form_type[4];
    if (fread(form_type, 1, 4, infile) != 4) {
        debug_log("WRN", "DEEP plugin: Failed to read form type");
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    if (memcmp(form_type, "DEEP", 4) != 0) {
        debug_log("WRN", "DEEP plugin: Invalid form type (expected DEEP, got %.4s)", form_type);
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Parse chunks */
    /* In IFF format, FORM size includes the 4-byte form type ("DEEP") */
    /* FORM structure: "FORM" (4) + size (4) + "DEEP" (4) + chunks */
    /* chunk_header.size is the size of data after "FORM" + size, which includes "DEEP" */
    long form_start = file_start;                       /* Start of "FORM" */
    long form_end = form_start + 8 + chunk_header.size; /* FORM header (8) + size includes "DEEP" */
    long current_pos;

    while ((current_pos = ftell(infile)) < form_end) {
        /* Check if we have enough bytes left for a chunk header */
        if (form_end - current_pos < 8) {
            break; /* Not enough space for another chunk */
        }

        if (!read_iff_chunk_header(infile, &chunk_header)) {
            debug_log("WRN", "DEEP plugin: Failed to read chunk header at position %ld", ftell(infile));
            break;
        }

        /* Validate chunk size is reasonable */
        long remaining = form_end - ftell(infile);
        if (chunk_header.size > (uint32_t)remaining) {
            debug_log("WRN", "DEEP plugin: Chunk %c%c%c%c size %u exceeds remaining form size %ld",
                      chunk_header.id[0], chunk_header.id[1], chunk_header.id[2], chunk_header.id[3],
                      chunk_header.size, remaining);
            break;
        }

        if (memcmp(chunk_header.id, "DGBL", 4) == 0) {
            /* DEEP Global chunk */
            if (!read_be16_safe(infile, &global.display_width) ||
                !read_be16_safe(infile, &global.display_height) ||
                !read_be16_safe(infile, &global.compression)) {
                debug_log("WRN", "DEEP plugin: Failed to read DGBL chunk data");
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }
            int x_aspect = fgetc(infile);
            int y_aspect = fgetc(infile);
            if (x_aspect == EOF || y_aspect == EOF) {
                debug_log("WRN", "DEEP plugin: Failed to read DGBL aspect ratios");
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }
            global.x_aspect = (uint8_t)x_aspect;
            global.y_aspect = (uint8_t)y_aspect;

            display_width = global.display_width;
            display_height = global.display_height;

            has_dgbl = true;

            /* Skip padding if needed */
            uint32_t chunk_data_size = 8; /* 2+2+2+1+1 = 8 bytes */
            if (chunk_data_size < chunk_header.size) {
                skip_iff_chunk(infile, chunk_header.size - chunk_data_size);
            } else {
                skip_iff_padding(infile, chunk_header.size);
            }
        } else if (memcmp(chunk_header.id, "DPEL", 4) == 0) {
            /* DEEP Pixel Elements chunk */
            if (chunk_header.size < 4) {
                debug_log("WRN", "DEEP plugin: DPEL chunk too small (%u bytes, expected at least 4)", chunk_header.size);
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }

            if (!read_be32_safe(infile, &num_elements)) {
                debug_log("WRN", "DEEP plugin: Failed to read DPEL element count");
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }
            if (num_elements == 0 || num_elements > 16) {
                debug_log("WRN", "DEEP plugin: Invalid number of elements: %u", num_elements);
                skip_iff_chunk(infile, chunk_header.size - 4);
                continue;
            }

            /* Validate chunk has enough data for all elements */
            uint32_t expected_size = 4 + (num_elements * 4);
            if (chunk_header.size < expected_size) {
                debug_log("WRN", "DEEP plugin: DPEL chunk too small for %u elements (got %u, expected %u)",
                          num_elements, chunk_header.size, expected_size);
                skip_iff_chunk(infile, chunk_header.size - 4);
                continue;
            }

            pixel_elements = g_malloc(sizeof(DEEPPixelElement) * num_elements);
            if (!pixel_elements) {
                debug_log("WRN", "DEEP plugin: Failed to allocate pixel elements");
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            for (uint32_t i = 0; i < num_elements; i++) {
                if (!read_be16_safe(infile, &pixel_elements[i].c_type) ||
                    !read_be16_safe(infile, &pixel_elements[i].c_bit_depth)) {
                    debug_log("WRN", "DEEP plugin: Failed to read DPEL element %u", i);
                    g_free(pixel_elements);
                    skip_iff_chunk(infile, chunk_header.size - 4 - (i * 4));
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }
            }

            /* Calculate bytes per pixel and set up element info */
            /* DEEP format stores pixel elements in sequence, with elements byte-aligned */
            /* Each element is stored in its natural byte size (rounded up) */
            element_info = g_malloc(sizeof(PixelElementInfo) * num_elements);
            if (!element_info) {
                debug_log("WRN", "DEEP plugin: Failed to allocate element info");
                g_free(pixel_elements);
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            /* DEEP format can use bit-packed or byte-aligned elements */
            /* We'll try bit-packed first (more common), fall back to byte-aligned if needed */
            /* Check if total bits fit in a reasonable number of bytes */
            uint32_t total_bits = 0;
            for (uint32_t i = 0; i < num_elements; i++) {
                total_bits += pixel_elements[i].c_bit_depth;
            }
            uint32_t bits_per_pixel = (total_bits + 7) & ~7; /* Round up to byte boundary */
            bool use_bit_packed = (bits_per_pixel <= 32);    /* Use bit-packed if <= 32 bits total */

            uint32_t current_byte_offset = 0;
            uint32_t current_bit_offset = 0;
            for (uint32_t i = 0; i < num_elements; i++) {
                element_info[i].type = pixel_elements[i].c_type;
                element_info[i].bit_depth = pixel_elements[i].c_bit_depth;

                if (use_bit_packed) {
                    /* Bit-packed: elements can share bytes */
                    element_info[i].byte_offset = current_bit_offset / 8;
                    element_info[i].bit_offset = current_bit_offset % 8;
                    current_bit_offset += pixel_elements[i].c_bit_depth;
                } else {
                    /* Byte-aligned: each element uses whole bytes */
                    element_info[i].byte_offset = current_byte_offset;
                    element_info[i].bit_offset = 0;
                    uint32_t element_bytes = (pixel_elements[i].c_bit_depth + 7) / 8;
                    current_byte_offset += element_bytes;
                }
            }
            bytes_per_pixel = use_bit_packed ? ((current_bit_offset + 7) / 8) : current_byte_offset;

            has_dpel = true;

            /* Skip padding if needed */
            uint32_t chunk_data_size = 4 + (num_elements * 4); /* 4 bytes for count + 4 per element */
            if (chunk_data_size < chunk_header.size) {
                skip_iff_chunk(infile, chunk_header.size - chunk_data_size);
            } else {
                skip_iff_padding(infile, chunk_header.size);
            }
        } else if (memcmp(chunk_header.id, "DLOC", 4) == 0) {
            /* DEEP Location chunk */
            if (chunk_header.size < 8) {
                debug_log("WRN", "DEEP plugin: DLOC chunk too small (%u bytes, expected 8)", chunk_header.size);
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }
            if (!read_be16_safe(infile, &location.w) ||
                !read_be16_safe(infile, &location.h)) {
                debug_log("WRN", "DEEP plugin: Failed to read DLOC dimensions");
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }
            int16_t x_val, y_val;
            if (!read_be16_safe(infile, (uint16_t*)&x_val) ||
                !read_be16_safe(infile, (uint16_t*)&y_val)) {
                debug_log("WRN", "DEEP plugin: Failed to read DLOC offsets");
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }
            location.x = x_val;
            location.y = y_val;

            /* DLOC offsets should be reasonable (within ±32767, but typically much smaller) */
            /* If offsets are too large, they're probably wrong - ignore DLOC */
            if (location.x < -10000 || location.x > 10000 ||
                location.y < -10000 || location.y > 10000) {
                debug_log("WRN", "DEEP plugin: DLOC offsets out of range (x=%d, y=%d), ignoring DLOC chunk",
                          location.x, location.y);
                /* Use display dimensions as body dimensions */
                body_width = display_width;
                body_height = display_height;
                has_dloc = false;
            } else {
                body_width = location.w;
                body_height = location.h;
                has_dloc = true;
            }

            /* Skip any extra data and padding if needed */
            if (chunk_header.size > 8) {
                skip_iff_chunk(infile, chunk_header.size - 8);
            } else {
                skip_iff_padding(infile, chunk_header.size);
            }
        } else if (memcmp(chunk_header.id, "DBOD", 4) == 0) {
            /* DEEP Body chunk - pixel data */
            if (!has_dgbl || !has_dpel) {
                debug_log("WRN", "DEEP plugin: DBOD chunk found before DGBL or DPEL");
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }

            /* Use body dimensions if DLOC was present, otherwise use display dimensions */
            if (!has_dloc) {
                body_width = display_width;
                body_height = display_height;
            }

            if (body_width == 0 || body_height == 0) {
                debug_log("WRN", "DEEP plugin: Invalid dimensions: body=%ux%u, display=%ux%u",
                          body_width, body_height, display_width, display_height);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }

            if (bytes_per_pixel == 0) {
                debug_log("WRN", "DEEP plugin: Invalid bytes_per_pixel: %u", bytes_per_pixel);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }

            /* Read compressed data */
            /* IFF chunk size is the actual data size (may be odd, padding added after) */
            size_t data_size = chunk_header.size;

            /* Validate we have enough data in file */
            long current_file_pos = ftell(infile);
            long remaining_in_form = form_end - current_file_pos;
            long file_size;

            /* Get file size to validate against */
            long saved_pos = ftell(infile);
            if (fseek(infile, 0, SEEK_END) == 0) {
                file_size = ftell(infile);
                fseek(infile, saved_pos, SEEK_SET);
            } else {
                file_size = -1; /* Can't determine file size */
            }

            long remaining_in_file = (file_size >= 0) ? (file_size - current_file_pos) : -1;

            /* Check both form boundary and file boundary */
            if (remaining_in_form < 0 || (size_t)remaining_in_form < data_size) {
                debug_log("WRN", "DEEP plugin: DBOD chunk size %zu exceeds remaining form size %ld",
                          data_size, remaining_in_form);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }

            if (remaining_in_file >= 0 && (size_t)remaining_in_file < data_size) {
                debug_log("WRN", "DEEP plugin: DBOD chunk size %zu exceeds remaining file size %ld, limiting to available data",
                          data_size, remaining_in_file);
                data_size = (size_t)remaining_in_file; /* Limit to what's available */
            }

            /* Calculate expected uncompressed size */
            size_t decompressed_size = body_width * body_height * bytes_per_pixel;

            /* For uncompressed data, validate size matches (allow some tolerance for padding) */
            if (global.compression == DEEP_COMPRESSION_NONE) {
                if (data_size < decompressed_size) {
                    debug_log("WRN", "DEEP plugin: DBOD chunk too small for uncompressed data (got %zu, expected %zu)",
                              data_size, decompressed_size);
                    skip_iff_chunk(infile, chunk_header.size);
                    g_free(pixel_elements);
                    g_free(element_info);
                    fclose(infile);
                    return PLUGIN_ERROR_CORRUPT_FILE;
                }
                /* For uncompressed, we only need exactly decompressed_size bytes */
                if (data_size > decompressed_size) {
                    data_size = decompressed_size; /* Only read what we need */
                }
            }

            compressed_data = g_malloc(data_size);
            if (!compressed_data) {
                debug_log("WRN", "DEEP plugin: Failed to allocate compressed data buffer (%zu bytes)", data_size);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            long read_start_pos = ftell(infile);
            size_t bytes_read = fread(compressed_data, 1, data_size, infile);
            long read_end_pos = ftell(infile);

            if (bytes_read != data_size) {
                debug_log("WRN", "DEEP plugin: Failed to read pixel data - expected %zu bytes, got %zu, start_pos=%ld, end_pos=%ld, errno=%d",
                          data_size, bytes_read, read_start_pos, read_end_pos, ferror(infile) ? errno : 0);
                if (ferror(infile)) {
                    debug_log("WRN", "DEEP plugin: File read error occurred");
                }
                if (feof(infile)) {
                    debug_log("WRN", "DEEP plugin: End of file reached");
                }
                g_free(compressed_data);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }

            /* Decompress if needed */
            if (global.compression == DEEP_COMPRESSION_RLE) {
                /* For TVPaint files, try per-component RLE first (most likely format) */
                image_data = decompress_rle_per_component(compressed_data, data_size,
                                                          body_width, body_height, num_elements);

                /* If per-component RLE fails, try per-scanline RLE */
                if (!image_data) {
                    image_data = decompress_rle_per_scanline(compressed_data, data_size,
                                                             body_width, body_height, bytes_per_pixel);
                }

                /* If per-scanline RLE fails, try standard RLE */
                if (!image_data) {
                    image_data = decompress_rle(compressed_data, data_size, decompressed_size);
                }

                g_free(compressed_data);
                compressed_data = NULL; /* Mark as freed to prevent double-free */
                if (!image_data) {
                    debug_log("WRN", "DEEP plugin: All RLE decompression methods failed (compressed: %zu, decompressed: %zu)",
                              data_size, decompressed_size);
                    g_free(pixel_elements);
                    g_free(element_info);
                    fclose(infile);
                    return PLUGIN_ERROR_CORRUPT_FILE;
                }

            } else if (global.compression == DEEP_COMPRESSION_TVDC) {
                /* TODO: TVDC (TVPaint Deep Compression) support */
                /* Compression is done line by line for each element (component) */
                /* For RGBA for example we have a Red line, a Green line, and so on. */
                debug_log("WRN", "DEEP plugin: TVDC compression not yet implemented");
                g_free(compressed_data);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
            } else if (global.compression == DEEP_COMPRESSION_NONE) {
                /* Uncompressed - use data directly */
                image_data = compressed_data;
                compressed_data = NULL; /* Don't free it, we're using it */
            } else {
                debug_log("WRN", "DEEP plugin: Unsupported compression type: %u", global.compression);
                g_free(compressed_data);
                g_free(pixel_elements);
                g_free(element_info);
                fclose(infile);
                return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
            }

            has_dbod = true;
            /* Skip IFF padding (1 byte if chunk size is odd) */
            skip_iff_padding(infile, chunk_header.size);
        } else if (memcmp(chunk_header.id, "TVDC", 4) == 0) {
            /* TVDC (TVPaint Deep Compression) chunk - contains lookup table */
            if (chunk_header.size < 32) {
                debug_log("WRN", "DEEP plugin: TVDC chunk too small (%u bytes, expected at least 32)", chunk_header.size);
                skip_iff_chunk(infile, chunk_header.size);
                continue;
            }

            /* Read 16-word lookup table (32 bytes total) */
            for (int i = 0; i < 16; i++) {
                if (!read_be16_safe(infile, &tvdc_table[i])) {
                    debug_log("WRN", "DEEP plugin: Failed to read TVDC table entry %d", i);
                    skip_iff_chunk(infile, chunk_header.size - (i * 2));
                    break;
                }
            }

            has_tvdc = true;
            /* Skip padding if needed */
            uint32_t chunk_data_size = 32; /* 16 words * 2 bytes */
            if (chunk_data_size < chunk_header.size) {
                skip_iff_chunk(infile, chunk_header.size - chunk_data_size);
            } else {
                skip_iff_padding(infile, chunk_header.size);
            }
        } else {
            /* Unknown chunk - skip it */
            skip_iff_chunk(infile, chunk_header.size);
        }
    }

    fclose(infile);

    /* Validate we have required chunks */
    if (!has_dgbl || !has_dpel || !has_dbod) {
        debug_log("WRN", "DEEP plugin: Missing required chunks (DGBL=%d, DPEL=%d, DBOD=%d)", has_dgbl, has_dpel, has_dbod);
        g_free(image_data);
        g_free(compressed_data);
        g_free(pixel_elements);
        g_free(element_info);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Set document metadata */
    doc->width = display_width;
    doc->height = display_height;
    doc->channels = 4; /* RGBA */
    doc->bit_depth = 8;

    /* Check if we have alpha channel */
    bool has_alpha = false;
    for (uint32_t i = 0; i < num_elements; i++) {
        if (element_info[i].type == DEEP_ELEMENT_ALPHA) {
            has_alpha = true;
            break;
        }
    }
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
        debug_log("WRN", "DEEP plugin: layer_new returned NULL for %ux%u layer", doc->width, doc->height);
        g_free(image_data);
        g_free(compressed_data);
        g_free(pixel_elements);
        g_free(element_info);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        debug_log("WRN", "DEEP plugin: base_layer->surface is NULL");
        g_free(image_data);
        g_free(compressed_data);
        g_free(pixel_elements);
        g_free(element_info);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        debug_log("WRN", "DEEP plugin: cairo_image_surface_get_data returned NULL");
        g_free(image_data);
        g_free(compressed_data);
        g_free(pixel_elements);
        g_free(element_info);
        layer_free(base_layer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert DEEP pixel data to Cairo ARGB32 format */
    if (!image_data) {
        debug_log("WRN", "DEEP plugin: image_data is NULL after decompression");
        g_free(compressed_data);
        g_free(pixel_elements);
        g_free(element_info);
        layer_free(base_layer);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Safety check: ensure display dimensions are valid */
    if (display_height == 0) {
        debug_log("WRN", "DEEP plugin: display_height is 0! Using body_height %u instead", body_height);
        display_height = body_height;
        doc->height = display_height;
    }
    if (display_width == 0) {
        debug_log("WRN", "DEEP plugin: display_width is 0! Using body_width %u instead", body_width);
        display_width = body_width;
        doc->width = display_width;
    }

    /* Safety check: ensure display dimensions are valid */
    if (display_height == 0) {
        debug_log("WRN", "DEEP plugin: display_height is 0! Using body_height %u instead", body_height);
        display_height = body_height;
        doc->height = display_height;
    }
    if (display_width == 0) {
        debug_log("WRN", "DEEP plugin: display_width is 0! Using body_width %u instead", body_width);
        display_width = body_width;
        doc->width = display_width;
    }

    /* Initialize surface to transparent */
    memset(surface_data, 0, display_height * surface_stride);

    /* Calculate expected decompressed size */
    size_t expected_size = body_width * body_height * bytes_per_pixel;
    size_t actual_data_size = expected_size; /* Assume full decompression - will be validated */

    /* Convert DEEP pixel data to Cairo ARGB32 format */
    uint32_t pixels_written = 0;
    uint32_t rows_processed = 0;
    uint32_t pixels_skipped_x = 0;
    uint32_t pixels_skipped_y = 0;

    for (uint32_t y = 0; y < body_height; y++) {
        size_t row_offset = y * body_width * bytes_per_pixel;
        size_t row_size = body_width * bytes_per_pixel;

        /* Safety check - prevent accessing beyond available data */
        if (row_offset >= actual_data_size) {
            debug_log("WRN", "DEEP plugin: Row %u offset %zu exceeds available data %zu - stopping pixel conversion",
                      y, row_offset, actual_data_size);
            pixels_skipped_y += (body_height - y);
            break; /* Stop processing remaining rows */
        }

        if (row_offset + row_size > actual_data_size) {
            /* Partial row available - only process what we have */
            size_t available_pixels = (actual_data_size - row_offset) / bytes_per_pixel;
            debug_log("WRN", "DEEP plugin: Row %u partially available (%zu/%u pixels)", y, available_pixels, body_width);
            /* Process partial row - will be handled in inner loop */
        }

        uint8_t* src_row = image_data + row_offset;

        /* Calculate display Y position (accounting for DLOC offset) */
        int32_t display_y = has_dloc ? (y + location.y) : (int32_t)y;

        if (display_y < 0 || display_y >= (int32_t)display_height) {
            pixels_skipped_y++;
            continue; /* Skip rows outside display area */
        }

        guchar* dst_row = surface_data + display_y * surface_stride;
        rows_processed++;

        /* Calculate how many pixels we can safely process in this row */
        size_t max_x = body_width;
        if (row_offset + body_width * bytes_per_pixel > actual_data_size) {
            max_x = (actual_data_size - row_offset) / bytes_per_pixel;
            if (max_x == 0) {
                pixels_skipped_x += body_width;
                continue; /* Skip entire row if no data available */
            }
        }

        for (uint32_t x = 0; x < max_x; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            /* Calculate display X position (accounting for DLOC offset) */
            int32_t display_x = has_dloc ? (x + location.x) : (int32_t)x;
            if (display_x < 0 || display_x >= (int32_t)display_width) {
                pixels_skipped_x++;
                continue; /* Skip pixels outside display area */
            }

            /* Extract pixel data */
            uint8_t* pixel_data = src_row + x * bytes_per_pixel;

            /* Extract each element */
            bool has_r = false, has_g = false, has_b = false;
            for (uint32_t i = 0; i < num_elements; i++) {
                PixelElementInfo* info = &element_info[i];
                uint32_t byte_idx;
                uint32_t bit_idx;
                uint16_t bit_depth = info->bit_depth;

                /* For TVDC decompression, data is in chunky format: component i is at offset i */
                /* For other formats, use the original byte_offset from element_info */
                if (global.compression == DEEP_COMPRESSION_TVDC) {
                    byte_idx = i; /* Chunky format: component index = byte offset */
                    bit_idx = 0;  /* Always byte-aligned in chunky format */
                } else {
                    byte_idx = info->byte_offset;
                    bit_idx = info->bit_offset;
                }

                if (byte_idx >= bytes_per_pixel) {
                    continue;
                }

                uint32_t value = 0;

                if (bit_idx == 0 && bit_depth == 8 && (byte_idx + 1) <= bytes_per_pixel) {
                    /* Simple case: whole byte, byte-aligned */
                    value = pixel_data[byte_idx];
                } else if (bit_idx == 0 && bit_depth >= 8) {
                    /* Multi-byte element, byte-aligned - read as big-endian */
                    uint32_t element_bytes = (bit_depth + 7) / 8;
                    for (uint32_t j = 0; j < element_bytes && (byte_idx + j) < bytes_per_pixel; j++) {
                        value = (value << 8) | pixel_data[byte_idx + j];
                    }
                    /* Scale down to 8-bit if needed */
                    if (bit_depth > 8) {
                        value = value >> (bit_depth - 8);
                    }
                } else {
                    /* Bit-packed element - extract bits */
                    uint32_t bits_remaining = 8 - bit_idx;
                    if (bit_depth <= bits_remaining) {
                        /* Fits in current byte */
                        uint32_t mask = ((1U << bit_depth) - 1) << (8 - bit_idx - bit_depth);
                        value = (pixel_data[byte_idx] & mask) >> (8 - bit_idx - bit_depth);
                    } else {
                        /* Spans multiple bytes */
                        uint32_t mask1 = ((1U << bits_remaining) - 1) << (8 - bit_idx);
                        value = (pixel_data[byte_idx] & mask1) >> (8 - bit_idx);
                        uint32_t bits_needed = bit_depth - bits_remaining;
                        for (uint32_t j = 1; j < ((bit_depth + 7) / 8) && (byte_idx + j) < bytes_per_pixel; j++) {
                            if (bits_needed >= 8) {
                                value = (value << 8) | pixel_data[byte_idx + j];
                                bits_needed -= 8;
                            } else {
                                value = (value << bits_needed) | (pixel_data[byte_idx + j] >> (8 - bits_needed));
                                bits_needed = 0;
                            }
                        }
                    }

                    /* Scale to 8-bit if needed */
                    if (bit_depth < 8) {
                        if (bit_depth > 0) {
                            value = (value * 255) / ((1U << bit_depth) - 1);
                        }
                    } else if (bit_depth > 8 && bit_depth <= 16) {
                        value = (value * 255) / ((1U << bit_depth) - 1);
                    }
                }

                /* Map element to RGBA */
                switch (info->type) {
                    case DEEP_ELEMENT_RED:
                        r = (uint8_t)value;
                        has_r = true;
                        break;
                    case DEEP_ELEMENT_GREEN:
                        g = (uint8_t)value;
                        has_g = true;
                        break;
                    case DEEP_ELEMENT_BLUE:
                        b = (uint8_t)value;
                        has_b = true;
                        break;
                    case DEEP_ELEMENT_ALPHA:
                        a = (uint8_t)value;
                        break;
                    case DEEP_ELEMENT_OPACITY:
                        /* Opacity is inverse of alpha */
                        a = 255 - (uint8_t)value;
                        break;
                    case DEEP_ELEMENT_MASK:
                        /* Use mask as alpha */
                        a = (uint8_t)value;
                        break;
                    default:
                        /* Other element types not used for display */
                        break;
                }
            }

            /* If no RGB components found, use grayscale from first element or default to black */
            if (!has_r && !has_g && !has_b && num_elements > 0) {
                /* Try to extract first element as grayscale */
                PixelElementInfo* info = &element_info[0];
                uint32_t byte_idx = info->byte_offset;
                if (byte_idx < bytes_per_pixel) {
                    uint32_t gray_value = pixel_data[byte_idx];
                    if (info->bit_depth < 8 && info->bit_depth > 0) {
                        gray_value = (gray_value * 255) / ((1U << info->bit_depth) - 1);
                    }
                    r = g = b = (uint8_t)gray_value;
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
            uint32_t pixel_offset = (uint32_t)display_x * 4;
            /* Each pixel is 4 bytes, so we need pixel_offset + 4 <= stride */
            if (pixel_offset + 4 <= (uint32_t)surface_stride) {
                dst_row[pixel_offset + 0] = b;
                dst_row[pixel_offset + 1] = g;
                dst_row[pixel_offset + 2] = r;
                dst_row[pixel_offset + 3] = a;
                pixels_written++;
            } else {
                if (y == 0 && x < 5) {
                    debug_log("WRN", "DEEP plugin: Pixel offset %u + 4 exceeds stride %d at x=%u, display_x=%d",
                              pixel_offset, surface_stride, x, display_x);
                }
            }
        }

        if (y == 0 && pixels_written > 0) {
        }
    }

    cairo_surface_mark_dirty(temp_surface);

    /* Cleanup */
    g_free(image_data);
    if (compressed_data) {
        g_free(compressed_data);
        compressed_data = NULL;
    }
    g_free(pixel_elements);
    g_free(element_info);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Render composite */
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

/**
 * DEEP plugin initialization
 */
bool plugin_init_deep(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "DEEP - TVPaint IFF DEEP Image";
    out_plugin->format_info.extensions = "deep";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_deep;
    out_plugin->callbacks.load = load_deep;
    out_plugin->callbacks.can_save = can_save_deep;
    out_plugin->callbacks.save = NULL; /* Read-only for now */

    return true;
}
