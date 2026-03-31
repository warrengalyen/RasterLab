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

/* TGA image types */
#define TGA_TYPE_NO_DATA 0
#define TGA_TYPE_INDEXED 1
#define TGA_TYPE_RGB 2
#define TGA_TYPE_GRAYSCALE 3
#define TGA_TYPE_RLE_INDEXED 9
#define TGA_TYPE_RLE_RGB 10
#define TGA_TYPE_RLE_GRAYSCALE 11

/* TGA header structure (packed for file I/O) */
#pragma pack(push, 1)
typedef struct {
    uint8_t id_length;              /* Length of image ID field (0-255) */
    uint8_t color_map_type;         /* Color map type (0 = no color map, 1 = color map) */
    uint8_t image_type;             /* Image type (0-11) */
    uint16_t color_map_first_entry; /* First entry index in color map */
    uint16_t color_map_length;      /* Number of entries in color map */
    uint8_t color_map_entry_size;   /* Size of color map entry in bits (15, 16, 24, 32) */
    uint16_t x_origin;              /* X origin of image */
    uint16_t y_origin;              /* Y origin of image */
    uint16_t width;                 /* Image width */
    uint16_t height;                /* Image height */
    uint8_t pixel_depth;            /* Bits per pixel (8, 15, 16, 24, 32) */
    uint8_t image_descriptor;       /* Image descriptor byte */
} TGAHeader;
#pragma pack(pop)

/* TGA image descriptor flags */
#define TGA_DESC_ALPHA_BITS_MASK 0x0F /* Bits 0-3: number of alpha bits */
#define TGA_DESC_ORIGIN_MASK 0x30 /* Bits 4-5: origin position */
#define TGA_DESC_ORIGIN_BOTTOM_LEFT 0x00
#define TGA_DESC_ORIGIN_BOTTOM_RIGHT 0x10
#define TGA_DESC_ORIGIN_TOP_LEFT 0x20
#define TGA_DESC_ORIGIN_TOP_RIGHT 0x30

/**
 * Check if file is TGA format
 */
static bool can_load_tga(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 18) {
        return false;
    }

    /* TGA files don't have a magic number, but we can check the image_type field */
    /* Valid image types are 0-3, 9-11 */
    uint8_t image_type = header[2];
    if (image_type <= 3 || (image_type >= 9 && image_type <= 11)) {
        /* Also check color_map_type (should be 0 or 1) */
        uint8_t color_map_type = header[1];
        if (color_map_type <= 1) {
            return true;
        }
    }

    return false;
}

/**
 * Check if plugin can save to TGA format
 * This plugin is read-only, so always return false
 */
static bool can_save_tga(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Read RLE packet header from TGA file
 */
static int read_tga_rle_packet_header(FILE* file, uint8_t* packet_type, uint32_t* count) {
    uint8_t packet_header;

    if (fread(&packet_header, 1, 1, file) != 1) {
        return -1;
    }

    *count = (packet_header & 0x7F) + 1;           /* Count is stored as (count - 1) */
    *packet_type = (packet_header & 0x80) ? 1 : 0; /* 1 = RLE, 0 = raw */

    return 0;
}

/**
 * Load TGA image
 */
static PluginError load_tga(ImageDocument* doc, const char* filename) {
    FILE* infile;
    TGAHeader header;
    uint8_t* image_id = NULL;
    uint8_t* color_map = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint32_t width, height;
    uint8_t pixel_depth;
    bool has_alpha = false;
    bool is_rle = false;
    bool top_down = false;
    uint32_t color_map_size = 0;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open TGA file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read TGA header */
    if (fread(&header, sizeof(TGAHeader), 1, infile) != 1) {
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Validate header */
    if (header.image_type == TGA_TYPE_NO_DATA) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Check if RLE compressed */
    is_rle = (header.image_type >= TGA_TYPE_RLE_INDEXED);

    /* Determine image type */
    uint8_t base_type = header.image_type;
    if (is_rle) {
        base_type -= 8; /* Convert RLE types to base types */
    }

    /* Read image ID field if present */
    if (header.id_length > 0) {
        image_id = g_malloc(header.id_length);
        if (!image_id) {
            fclose(infile);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }
        if (fread(image_id, 1, header.id_length, infile) != header.id_length) {
            g_free(image_id);
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
        /* Image ID is typically ignored for loading */
        g_free(image_id);
        image_id = NULL;
    }

    /* Read color map if present */
    if (header.color_map_type == 1 && header.color_map_length > 0) {
        uint32_t color_map_entry_bytes = (header.color_map_entry_size + 7) / 8;
        color_map_size = header.color_map_length * color_map_entry_bytes;
        color_map = g_malloc(color_map_size);
        if (!color_map) {
            fclose(infile);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }
        if (fread(color_map, 1, color_map_size, infile) != color_map_size) {
            g_free(color_map);
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
    }

    /* Get image dimensions */
    width = header.width;
    height = header.height;
    pixel_depth = header.pixel_depth;

    /* Determine orientation from image descriptor */
    uint8_t origin = header.image_descriptor & TGA_DESC_ORIGIN_MASK;
    top_down = (origin == TGA_DESC_ORIGIN_TOP_LEFT || origin == TGA_DESC_ORIGIN_TOP_RIGHT);

    /* Determine alpha channel presence */
    uint8_t alpha_bits = header.image_descriptor & TGA_DESC_ALPHA_BITS_MASK;
    if (pixel_depth == 32 || alpha_bits > 0) {
        has_alpha = true;
    }

    /* Validate dimensions */
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        if (color_map) {
            g_free(color_map);
        }
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

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
    base_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        if (color_map) {
            g_free(color_map);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        if (color_map) {
            g_free(color_map);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        if (color_map) {
            g_free(color_map);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Calculate pixel size in bytes */
    uint32_t pixel_size = (pixel_depth + 7) / 8;
    uint8_t* pixel_buffer = g_malloc(pixel_size);
    if (!pixel_buffer) {
        layer_free(base_layer);
        if (color_map) {
            g_free(color_map);
        }
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Read and convert image data */
    if (is_rle) {
        /* RLE compressed format */
        for (uint32_t y = 0; y < height; y++) {
            uint32_t dst_y = top_down ? y : (height - 1 - y);
            guchar* row = surface_data + dst_y * surface_stride;
            uint32_t x = 0;

            while (x < width) {
                uint8_t packet_type;
                uint32_t packet_count;

                /* Read packet header */
                if (read_tga_rle_packet_header(infile, &packet_type, &packet_count) != 0) {
                    g_free(pixel_buffer);
                    layer_free(base_layer);
                    if (color_map) {
                        g_free(color_map);
                    }
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                /* Read pixel data for packet */
                if (packet_type) {
                    /* RLE packet: read one pixel, repeat it */
                    if (fread(pixel_buffer, 1, pixel_size, infile) != pixel_size) {
                        g_free(pixel_buffer);
                        layer_free(base_layer);
                        if (color_map) {
                            g_free(color_map);
                        }
                        fclose(infile);
                        return PLUGIN_ERROR_FILE_READ_ERROR;
                    }
                } else {
                    /* Raw packet: will read pixels one by one in loop */
                }

                /* Process pixels in packet */
                for (uint32_t i = 0; i < packet_count && x < width; i++) {
                    if (!packet_type && i > 0) {
                        /* Raw packet: read next pixel */
                        if (fread(pixel_buffer, 1, pixel_size, infile) != pixel_size) {
                            g_free(pixel_buffer);
                            layer_free(base_layer);
                            if (color_map) {
                                g_free(color_map);
                            }
                            fclose(infile);
                            return PLUGIN_ERROR_FILE_READ_ERROR;
                        }
                    }

                    uint8_t r = 0, g = 0, b = 0, a = 255;

                    if (base_type == TGA_TYPE_INDEXED) {
                        /* Indexed color: use color map */
                        uint8_t index = pixel_buffer[0];
                        if (index < header.color_map_length && color_map) {
                            uint32_t map_entry_bytes = (header.color_map_entry_size + 7) / 8;
                            uint32_t map_offset = (index - header.color_map_first_entry) * map_entry_bytes;
                            if (map_offset < color_map_size) {
                                if (map_entry_bytes == 2) {
                                    /* 15/16-bit color map entry */
                                    uint16_t entry = (color_map[map_offset + 1] << 8) | color_map[map_offset];
                                    r = ((entry >> 10) & 0x1F) << 3;
                                    g = ((entry >> 5) & 0x1F) << 3;
                                    b = (entry & 0x1F) << 3;
                                    if (header.color_map_entry_size == 16) {
                                        a = (entry & 0x8000) ? 255 : 0;
                                    }
                                } else if (map_entry_bytes == 3) {
                                    /* 24-bit color map entry (BGR) */
                                    b = color_map[map_offset + 0];
                                    g = color_map[map_offset + 1];
                                    r = color_map[map_offset + 2];
                                } else if (map_entry_bytes == 4) {
                                    /* 32-bit color map entry (BGRA) */
                                    b = color_map[map_offset + 0];
                                    g = color_map[map_offset + 1];
                                    r = color_map[map_offset + 2];
                                    a = color_map[map_offset + 3];
                                }
                            }
                        }
                    } else if (base_type == TGA_TYPE_GRAYSCALE) {
                        /* Grayscale */
                        uint8_t gray = pixel_buffer[0];
                        r = g = b = gray;
                        if (pixel_depth == 16) {
                            /* 16-bit grayscale with alpha */
                            a = pixel_buffer[1];
                        }
                    } else if (base_type == TGA_TYPE_RGB) {
                        /* RGB color */
                        if (pixel_depth == 15 || pixel_depth == 16) {
                            /* 15/16-bit RGB */
                            uint16_t pixel = (pixel_buffer[1] << 8) | pixel_buffer[0];
                            r = ((pixel >> 10) & 0x1F) << 3;
                            g = ((pixel >> 5) & 0x1F) << 3;
                            b = (pixel & 0x1F) << 3;
                            if (pixel_depth == 16) {
                                a = (pixel & 0x8000) ? 255 : 0;
                            }
                        } else if (pixel_depth == 24) {
                            /* 24-bit RGB (BGR in file) */
                            b = pixel_buffer[0];
                            g = pixel_buffer[1];
                            r = pixel_buffer[2];
                        } else if (pixel_depth == 32) {
                            /* 32-bit RGBA (BGRA in file) */
                            b = pixel_buffer[0];
                            g = pixel_buffer[1];
                            r = pixel_buffer[2];
                            a = pixel_buffer[3];
                        }
                    }

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = a;
                    x++;
                }
            }
        }
    } else {
        /* Uncompressed format */
        uint8_t* row_data = g_malloc(width * pixel_size);
        if (!row_data) {
            g_free(pixel_buffer);
            layer_free(base_layer);
            if (color_map) {
                g_free(color_map);
            }
            fclose(infile);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        for (uint32_t y = 0; y < height; y++) {
            uint32_t dst_y = top_down ? y : (height - 1 - y);
            guchar* row = surface_data + dst_y * surface_stride;

            /* Read row data */
            if (fread(row_data, pixel_size, width, infile) != width) {
                g_free(row_data);
                g_free(pixel_buffer);
                layer_free(base_layer);
                if (color_map) {
                    g_free(color_map);
                }
                fclose(infile);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }

            /* Convert pixels */
            for (uint32_t x = 0; x < width; x++) {
                uint8_t* pixel = row_data + x * pixel_size;
                uint8_t r = 0, g = 0, b = 0, a = 255;

                if (base_type == TGA_TYPE_INDEXED) {
                    /* Indexed color: use color map */
                    uint8_t index = pixel[0];
                    if (index < header.color_map_length && color_map) {
                        uint32_t map_entry_bytes = (header.color_map_entry_size + 7) / 8;
                        uint32_t map_offset = (index - header.color_map_first_entry) * map_entry_bytes;
                        if (map_offset < color_map_size) {
                            if (map_entry_bytes == 2) {
                                /* 15/16-bit color map entry */
                                uint16_t entry = (color_map[map_offset + 1] << 8) | color_map[map_offset];
                                r = ((entry >> 10) & 0x1F) << 3;
                                g = ((entry >> 5) & 0x1F) << 3;
                                b = (entry & 0x1F) << 3;
                                if (header.color_map_entry_size == 16) {
                                    a = (entry & 0x8000) ? 255 : 0;
                                }
                            } else if (map_entry_bytes == 3) {
                                /* 24-bit color map entry (BGR) */
                                b = color_map[map_offset + 0];
                                g = color_map[map_offset + 1];
                                r = color_map[map_offset + 2];
                            } else if (map_entry_bytes == 4) {
                                /* 32-bit color map entry (BGRA) */
                                b = color_map[map_offset + 0];
                                g = color_map[map_offset + 1];
                                r = color_map[map_offset + 2];
                                a = color_map[map_offset + 3];
                            }
                        }
                    }
                } else if (base_type == TGA_TYPE_GRAYSCALE) {
                    /* Grayscale */
                    uint8_t gray = pixel[0];
                    r = g = b = gray;
                    if (pixel_depth == 16) {
                        /* 16-bit grayscale with alpha */
                        a = pixel[1];
                    }
                } else if (base_type == TGA_TYPE_RGB) {
                    /* RGB color */
                    if (pixel_depth == 15 || pixel_depth == 16) {
                        /* 15/16-bit RGB */
                        uint16_t pixel_val = (pixel[1] << 8) | pixel[0];
                        r = ((pixel_val >> 10) & 0x1F) << 3;
                        g = ((pixel_val >> 5) & 0x1F) << 3;
                        b = (pixel_val & 0x1F) << 3;
                        if (pixel_depth == 16) {
                            a = (pixel_val & 0x8000) ? 255 : 0;
                        }
                    } else if (pixel_depth == 24) {
                        /* 24-bit RGB (BGR in file) */
                        b = pixel[0];
                        g = pixel[1];
                        r = pixel[2];
                    } else if (pixel_depth == 32) {
                        /* 32-bit RGBA (BGRA in file) */
                        b = pixel[0];
                        g = pixel[1];
                        r = pixel[2];
                        a = pixel[3];
                    }
                }

                /* Convert to Cairo ARGB32 (BGRA in memory) */
                row[x * 4 + 0] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = a;
            }
        }
        g_free(row_data);
    }

    g_free(pixel_buffer);
    if (color_map) {
        g_free(color_map);
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
 * Initialize TGA plugin
 */
bool plugin_init_tga(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "TGA - True Vision Targa";
    out_plugin->format_info.extensions = "tga";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 60;

    out_plugin->callbacks.can_load = can_load_tga;
    out_plugin->callbacks.load = load_tga;
    out_plugin->callbacks.can_save = can_save_tga; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
