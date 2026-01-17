#include "plugins/plugin_cut.h"
#include "document.h"
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

/* Helper function to read little-endian uint16_t */
static uint16_t read_le16(const uint8_t* data) {
    return (uint16_t)(data[0] | (data[1] << 8));
}

/* CUT header structure (6 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint8_t width_bytes[2];    /* Width in pixels (little-endian) */
    uint8_t height_bytes[2];   /* Height in pixels (little-endian) */
    uint8_t reserved_bytes[2]; /* Reserved (should be 0) */
} CUTHeader;
#pragma pack(pop)

/* PAL header structure (40 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint8_t file_id[2];             /* "AH" (0x41, 0x48) */
    uint8_t version_bytes[2];       /* Version (little-endian) */
    uint8_t size_bytes[2];          /* Size minus header (little-endian) */
    uint8_t file_type;              /* Always 0x0A */
    uint8_t sub_type;               /* 0x00 = generic, 0x01 = hardware-specific */
    uint8_t board_id_bytes[2];      /* Board ID (little-endian) */
    uint8_t graphics_mode_bytes[2]; /* Graphics mode (little-endian) */
    uint8_t max_index_bytes[2];     /* Max palette index (little-endian) */
    uint8_t max_red_bytes[2];       /* Max red value (little-endian) */
    uint8_t max_green_bytes[2];     /* Max green value (little-endian) */
    uint8_t max_blue_bytes[2];      /* Max blue value (little-endian) */
    uint8_t palette_id[20];         /* "Dr. Halo" identifier */
} PALHeader;
#pragma pack(pop)

/**
 * Check if file is CUT format
 */
static bool can_load_cut(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 6) {
        return false;
    }

    /* CUT files have a simple 6-byte header: width, height, reserved */
    /* We can't verify much from just the header, but check that reserved is 0 */
    if (header_size >= 6) {
        uint16_t reserved = read_le16(header + 4);
        /* Reserved should be 0, but be lenient - just check reasonable width/height */
        uint16_t width = read_le16(header);
        uint16_t height = read_le16(header + 2);

        /* Basic sanity check: width and height should be reasonable (not both 0) */
        if (width > 0 && width <= 65535 && height > 0 && height <= 65535) {
            return true;
        }
    }

    return false;
}

/**
 * Check if plugin can save to CUT format
 * This plugin is read-only, so always return false
 */
static bool can_save_cut(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Decompress a single RLE-encoded scanline from CUT file
 * Returns number of pixels decoded, or -1 on error
 */
static int decompress_cut_scanline(FILE* file, uint8_t* buffer, uint32_t width) {
    uint16_t encoded_length;
    uint8_t length_bytes[2];

    /* Read encoded length (2 bytes, little-endian) */
    if (fread(length_bytes, 1, 2, file) != 2) {
        return -1;
    }
    encoded_length = read_le16(length_bytes);

    if (encoded_length == 0) {
        /* Empty scanline - fill with zeros */
        memset(buffer, 0, width);
        return (int)width;
    }

    /* Save position to verify we read correct amount */
    long start_pos = ftell(file);
    if (start_pos < 0) {
        return -1;
    }

    uint32_t pixels_decoded = 0;

    /* Decode RLE runs until we've decoded 'width' pixels */
    while (pixels_decoded < width) {
        long current_pos = ftell(file);
        if (current_pos < 0) {
            return -1;
        }

        /* Check if we've read all encoded bytes */
        if (current_pos - start_pos >= encoded_length) {
            break;
        }

        /* Read Run Count byte */
        int c = fgetc(file);
        if (c == EOF) {
            return -1;
        }

        uint8_t run_count_byte = (uint8_t)c;
        uint8_t count = run_count_byte & 0x7F;       /* 7 LSBs = count */
        bool msb_set = (run_count_byte & 0x80) != 0; /* MSB = mode */

        /* End of scanline marker: 7 LSBs == 0 (could be 0x00 or 0x80) */
        if (count == 0) {
            /* End marker found - we've already consumed it with fgetc above */
            break;
        }

        if (msb_set) {
            /* MSB = 1: Repeat run - next byte repeated 'count' times */
            int pixel_value = fgetc(file);
            if (pixel_value == EOF) {
                return -1;
            }

            uint32_t pixels_to_write = count;
            if (pixels_decoded + pixels_to_write > width) {
                pixels_to_write = width - pixels_decoded;
            }

            for (uint32_t i = 0; i < pixels_to_write; i++) {
                buffer[pixels_decoded + i] = (uint8_t)pixel_value;
            }
            pixels_decoded += pixels_to_write;
        } else {
            /* MSB = 0: Literal run - next 'count' bytes are literal pixel values */
            uint32_t pixels_to_read = count;
            if (pixels_decoded + pixels_to_read > width) {
                pixels_to_read = width - pixels_decoded;
            }

            for (uint32_t i = 0; i < pixels_to_read; i++) {
                int pixel_value = fgetc(file);
                if (pixel_value == EOF) {
                    return -1;
                }
                buffer[pixels_decoded + i] = (uint8_t)pixel_value;
            }
            pixels_decoded += pixels_to_read;
        }
    }

    /* Ensure we've consumed exactly encoded_length bytes */
    long end_pos = ftell(file);
    if (end_pos > 0 && start_pos > 0) {
        long bytes_consumed = end_pos - start_pos;
        if (bytes_consumed < encoded_length) {
            /* Skip any remaining bytes to stay synchronized */
            fseek(file, start_pos + encoded_length, SEEK_SET);
        }
    }

    /* Fill remaining pixels with 0 if we didn't decode enough */
    if (pixels_decoded < width) {
        memset(buffer + pixels_decoded, 0, width - pixels_decoded);
    }

    return (int)pixels_decoded;
}

/**
 * Read PAL palette file
 * Returns true if palette was successfully read, false otherwise
 */
static bool read_pal_palette(const char* cut_filename, uint8_t* palette) {
    /* Construct PAL filename by replacing .cut/.CUT extension with .pal/.PAL */
    gchar* pal_filename = g_strdup(cut_filename);
    gchar* dot = strrchr(pal_filename, '.');
    if (dot) {
        strcpy(dot, ".pal");
    } else {
        g_free(pal_filename);
        pal_filename = g_strconcat(cut_filename, ".pal", NULL);
    }

    FILE* pal_file = g_fopen(pal_filename, "rb");
    if (!pal_file) {
        /* Try uppercase extension */
        if (dot) {
            strcpy(dot, ".PAL");
        } else {
            g_free(pal_filename);
            pal_filename = g_strconcat(cut_filename, ".PAL", NULL);
        }
        pal_file = g_fopen(pal_filename, "rb");
    }

    if (!pal_file) {
        g_free(pal_filename);
        return false;
    }

    /* Read PAL header */
    PALHeader pal_header;
    if (fread(&pal_header, sizeof(PALHeader), 1, pal_file) != 1) {
        fclose(pal_file);
        g_free(pal_filename);
        return false;
    }

    /* Validate header */
    if (pal_header.file_id[0] != 0x41 || pal_header.file_id[1] != 0x48) {
        /* Not a valid PAL file */
        fclose(pal_file);
        g_free(pal_filename);
        return false;
    }

    if (pal_header.file_type != 0x0A) {
        /* Invalid file type */
        fclose(pal_file);
        g_free(pal_filename);
        return false;
    }

    uint16_t max_index = read_le16(pal_header.max_index_bytes);
    if (max_index > 255) {
        max_index = 255;
    }

    /* Read palette entries */
    /* Palette data is stored in 512-byte blocks with RGB triplets */
    /* Each triplet is 3 bytes (R, G, B), and if a triplet would cross a block
     * boundary, the remainder of the block is padded and the triplet continues
     * in the next block */
    uint32_t palette_entries = max_index + 1;
    uint32_t entry = 0;
    uint8_t block_buffer[512];
    size_t block_size = 0;
    uint32_t block_pos = 512; /* Force initial read */

    while (entry < palette_entries && entry < 256) {
        /* Check if we need to read next block or if triplet won't fit */
        if (block_pos + 3 > block_size) {
            /* Read next 512-byte block */
            block_size = fread(block_buffer, 1, 512, pal_file);
            if (block_size == 0) {
                break; /* EOF */
            }
            block_pos = 0;
        }

        /* Read RGB triplet - all 3 bytes are guaranteed to be in current block */
        palette[entry * 3 + 0] = block_buffer[block_pos++]; /* R */
        palette[entry * 3 + 1] = block_buffer[block_pos++]; /* G */
        palette[entry * 3 + 2] = block_buffer[block_pos++]; /* B */

        entry++;
    }

    fclose(pal_file);
    g_free(pal_filename);

    /* If we didn't read all 256 entries, fill the rest with grayscale */
    for (uint32_t i = entry; i < 256; i++) {
        uint8_t gray = (uint8_t)i;
        palette[i * 3 + 0] = gray; /* R */
        palette[i * 3 + 1] = gray; /* G */
        palette[i * 3 + 2] = gray; /* B */
    }

    return true;
}

/**
 * Load CUT image
 */
static PluginError load_cut(ImageDocument* doc, const char* filename) {
    FILE* infile;
    CUTHeader header;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint16_t width, height;
    uint8_t* scanline_buffer = NULL;
    uint8_t* palette = NULL;
    bool has_palette = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open CUT file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read CUT header */
    if (fread(&header, sizeof(CUTHeader), 1, infile) != 1) {
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Read dimensions (little-endian) */
    width = read_le16(header.width_bytes);
    height = read_le16(header.height_bytes);

    /* Validate dimensions */
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Try to read palette file */
    palette = g_malloc(768); /* 256 entries * 3 bytes (RGB) */
    if (palette) {
        has_palette = read_pal_palette(filename, palette);
    }

    /* If no palette file, create a grayscale palette */
    if (!has_palette) {
        for (uint32_t i = 0; i < 256; i++) {
            uint8_t gray = (uint8_t)i;
            palette[i * 3 + 0] = gray; /* R */
            palette[i * 3 + 1] = gray; /* G */
            palette[i * 3 + 2] = gray; /* B */
        }
    }

    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = 4; /* Always RGBA internally */
    doc->bit_depth = 8;
    doc->has_alpha = false; /* CUT format doesn't support alpha */

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
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate scanline buffer */
    scanline_buffer = g_malloc(width);
    if (!scanline_buffer) {
        layer_free(base_layer);
        if (palette) {
            g_free(palette);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Decode each scanline */
    for (uint32_t y = 0; y < height; y++) {
        guchar* row = surface_data + y * surface_stride;

        /* Decompress scanline */
        int pixels_decoded = decompress_cut_scanline(infile, scanline_buffer, width);
        if (pixels_decoded < 0) {
            g_free(scanline_buffer);
            layer_free(base_layer);
            if (palette) {
                g_free(palette);
            }
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }

        /* Convert indexed pixels to RGBA using palette */
        for (uint32_t x = 0; x < width; x++) {
            uint8_t index = scanline_buffer[x];
            uint8_t r = palette[index * 3 + 0];
            uint8_t g = palette[index * 3 + 1];
            uint8_t b = palette[index * 3 + 2];

            /* Convert to Cairo ARGB32 (BGRA in memory) */
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = 255;
        }
    }

    /* Cleanup */
    g_free(scanline_buffer);
    if (palette) {
        g_free(palette);
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
 * Initialize CUT plugin
 */
bool plugin_init_cut(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "CUT - Dr. Halo";
    out_plugin->format_info.extensions = "cut";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_cut;
    out_plugin->callbacks.load = load_cut;
    out_plugin->callbacks.can_save = can_save_cut; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
