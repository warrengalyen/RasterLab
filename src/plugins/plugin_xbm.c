#include "debug_logger.h"
/*
 * XBM (X Bitmap) image format plugin
 * Supports .xbm and .h - monochrome C source bitmaps (X11 and X10 style).
 */

#include "plugins/plugin_xbm.h"
#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <ctype.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Safe memory search (like memmem) - find needle in haystack within bounds
 */
static const char* safe_memmem(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len) {
    if (needle_len == 0 || needle_len > haystack_len)
        return NULL;
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0)
            return haystack + i;
    }
    return NULL;
}

/**
 * Check if file is XBM format
 */
static bool can_load_xbm(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;

    if (!header || header_size < 16)
        return false;

    const char* h = (const char*)header;

    /* Must start with #define and contain _width in first line */
    if (!safe_memmem(h, header_size, "#define", 7))
        return false;
    if (!safe_memmem(h, header_size, "_width", 6))
        return false;

    return true;
}

static bool can_save_xbm(const char* filename) {
    (void)filename;
    return false; /* read-only */
}

/**
 * Skip whitespace in string; return pointer to first non-space character.
 */
static const char* skip_ws(const char* s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

/**
 * Parse decimal or hex number at *s; advance *s past the number.
 * Returns value or (uint32_t)-1 on failure.
 */
static uint32_t parse_number(const char** s) {
    const char* p = skip_ws(*s);
    if (!*p)
        return (uint32_t)-1;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!*p)
            return (uint32_t)-1;
        uint32_t val = 0;
        for (;;) {
            char c = *p;
            if (c >= '0' && c <= '9')
                val = val * 16 + (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                val = val * 16 + (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                val = val * 16 + (uint32_t)(c - 'A' + 10);
            else
                break;
            p++;
        }
        *s = p;
        return val;
    }

    uint32_t val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (uint32_t)(*p - '0');
        p++;
    }
    *s = p;
    return val;
}

/**
 * Find #define ..._width N or ..._height N in line; set *width or *height and return true.
 * Searches for _width/_height as substrings to handle identifiers with non-ASCII chars
 * (e.g. "sample_1920×1280_width" where × is Unicode).
 */
static bool parse_define_dim(const char* line, uint32_t* width, uint32_t* height) {
    if (strstr(line, "#define") == NULL)
        return false;

    const char* m;

    m = strstr(line, "_width");
    if (m) {
        /* Reject substrings like "world_widthd" - next char must not be alphanumeric */
        char next = (unsigned char)m[6];
        if (!isalnum((unsigned char)next) && next != '_') {
            const char* p = m + 6; /* past "_width" (6 chars) */
            uint32_t n = parse_number(&p);
            if (n != (uint32_t)-1 && width) {
                *width = n;
                return true;
            }
        }
    }
    m = strstr(line, "_height");
    if (m) {
        /* Reject substrings like "world_heightd" */
        char next = (unsigned char)m[7];
        if (!isalnum((unsigned char)next) && next != '_') {
            const char* p = m + 7; /* past "_height" (7 chars) */
            uint32_t n = parse_number(&p);
            if (n != (uint32_t)-1 && height) {
                *height = n;
                return true;
            }
        }
    }
    /* Also support bare "width" / "height" (e.g. #define width 64) */
    m = strstr(line, " width ");
    if (!m)
        m = strstr(line, "\twidth ");
    if (m) {
        const char* p = m + 7; /* past " width " or "\twidth " */
        uint32_t n = parse_number(&p);
        if (n != (uint32_t)-1 && width) {
            *width = n;
            return true;
        }
    }
    m = strstr(line, " height ");
    if (!m)
        m = strstr(line, "\theight ");
    if (m) {
        const char* p = m + 8; /* past " height " or "\theight " */
        uint32_t n = parse_number(&p);
        if (n != (uint32_t)-1 && height) {
            *height = n;
            return true;
        }
    }
    return false;
}

/**
 * Check if line contains bits array declaration (char or short).
 * Accepts: static unsigned char, static char, static const unsigned char, etc.
 * Handles multi-line declarations - any line with "bits" and "char"/"short" counts.
 */
static void check_bits_declaration(const char* line, bool* out_is_short) {
    if (!line || (!strstr(line, "bits") && !strstr(line, "_bits")))
        return;
    /* X10 format uses "short" */
    if (strstr(line, "short") && (strstr(line, "bits") || strstr(line, "_bits"))) {
        *out_is_short = true;
    }
    /* X11 uses "char" - only set if we haven't already seen short */
    if (strstr(line, "char") && (strstr(line, "bits") || strstr(line, "_bits"))) {
        *out_is_short = false;
    }
}

/**
 * Parse one hex byte (0xN or 0xNN) from *s; advance *s. Returns true on success.
 */
static bool parse_hex_byte(const char** s, uint8_t* out) {
    const char* p = skip_ws(*s);
    if ((p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) || !p[2])
        return false;
    p += 2;
    uint32_t v = 0;
    int n = 0;
    while (n < 2) {
        char c = *p;
        if (c >= '0' && c <= '9')
            v = v * 16 + (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = v * 16 + (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (uint32_t)(c - 'A' + 10);
        else
            break;
        p++;
        n++;
    }
    if (n == 0)
        return false;
    *out = (uint8_t)v;
    *s = p;
    return true;
}

/**
 * Parse one hex word (0xNNNN) for X10 format.
 */
static bool parse_hex_word(const char** s, uint16_t* out) {
    const char* p = skip_ws(*s);
    if ((p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) || !p[2])
        return false;
    p += 2;
    uint32_t v = 0;
    int n = 0;
    while (n < 4) {
        char c = *p;
        if (c >= '0' && c <= '9')
            v = v * 16 + (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = v * 16 + (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (uint32_t)(c - 'A' + 10);
        else
            break;
        p++;
        n++;
    }
    if (n == 0)
        return false;
    *out = (uint16_t)v;
    *s = p;
    return true;
}

/**
 * Load XBM image
 */
static PluginError load_xbm(ImageDocument* doc, const char* filename) {
    FILE* f;
    char line[8192];
    uint32_t width = 0, height = 0;
    uint8_t* bits = NULL;    /* flattened bitmap bytes (X11: 1 byte per 8 pixels) */
    uint16_t* bits16 = NULL; /* X10: 1 word per 16 pixels */
    size_t bits_size = 0;
    size_t bits_count = 0; /* number of bytes or words read */
    bool use_short = false;
    bool in_array = false;
    uint32_t row_stride_bytes; /* bytes per row in source */
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;

    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    f = g_fopen(filename, "rb");
    if (!f)
        return PLUGIN_ERROR_FILE_NOT_FOUND;

    while (fgets(line, sizeof(line), f) != NULL) {
        const char* p = line; /* current parse position */

        if (!in_array) {
            uint32_t w = 0, h = 0;
            if (parse_define_dim(line, &w, NULL))
                width = w;
            if (parse_define_dim(line, NULL, &h))
                height = h;

            /* Track char vs short format from any line mentioning bits */
            check_bits_declaration(line, &use_short);

            /* Once we have dimensions, any "{" starts the array (handles multi-line declarations) */
            if (width && height && strchr(line, '{')) {
                p = strchr(line, '{');
                if (p) {
                    p++;
                    in_array = true;
                }
            }
        }

        if (in_array) {
            while (*p) {
                if (*p == '}' || (*p == ';' && strchr(p, '}'))) {
                    in_array = false;
                    break;
                }
                if (use_short) {
                    uint16_t word;
                    if (parse_hex_word(&p, &word)) {
                        if (bits_count >= bits_size / sizeof(uint16_t)) {
                            size_t new_size = bits_size + 256 * sizeof(uint16_t);
                            uint16_t* new_bits = (uint16_t*)g_realloc(bits16, new_size);
                            if (!new_bits) {
                                g_free(bits);
                                g_free(bits16);
                                fclose(f);
                                return PLUGIN_ERROR_OUT_OF_MEMORY;
                            }
                            bits16 = new_bits;
                            bits_size = new_size;
                        }
                        bits16[bits_count++] = word;
                    } else
                        p++;
                } else {
                    uint8_t byte;
                    if (parse_hex_byte(&p, &byte)) {
                        if (bits_count >= bits_size) {
                            size_t new_size = bits_size + 256;
                            uint8_t* new_bits = (uint8_t*)g_realloc(bits, new_size);
                            if (!new_bits) {
                                g_free(bits);
                                g_free(bits16);
                                fclose(f);
                                return PLUGIN_ERROR_OUT_OF_MEMORY;
                            }
                            bits = new_bits;
                            bits_size = new_size;
                        }
                        bits[bits_count++] = byte;
                    } else
                        p++;
                }
            }
            if (!in_array)
                break;
        }
    }

    fclose(f);

    if (width == 0 || height == 0) {
        debug_log("WRN", "XBM plugin: Failed to parse dimensions (width=%u, height=%u)", width, height);
        g_free(bits);
        g_free(bits16);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    if (use_short) {
        row_stride_bytes = ((width + 15) / 16) * 2; /* words to bytes */
        size_t expected_words = (size_t)height * ((width + 15) / 16);
        /* Allow some extra data (padding), but must have at least the expected amount */
        if (bits_count < expected_words) {
            debug_log("WRN", "XBM plugin: Not enough data - expected at least %zu words, got %zu", expected_words, bits_count);
            g_free(bits16);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
    } else {
        row_stride_bytes = (width + 7) / 8;
        size_t expected_bytes = (size_t)height * row_stride_bytes;
        /* Allow some extra data (padding), but must have at least the expected amount */
        if (bits_count < expected_bytes) {
            debug_log("WRN", "XBM plugin: Not enough data - expected at least %zu bytes, got %zu", expected_bytes, bits_count);
            g_free(bits);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
    }

    /* Set document and create layer */
    doc->width = width;
    doc->height = height;
    doc->channels = 4;
    doc->bit_depth = 8;
    doc->has_alpha = false;

    for (GList* iter = doc->layers; iter; iter = iter->next)
        layer_free((ImageLayer*)iter->data);
    g_list_free(doc->layers);
    doc->layers = NULL;

    base_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        g_free(bits);
        g_free(bits16);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        g_free(bits);
        g_free(bits16);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);
    if (!surface_data) {
        layer_free(base_layer);
        g_free(bits);
        g_free(bits16);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert bitmap to Cairo ARGB32: 1 = foreground (black), 0 = background (white).
     * XBM stores pixels LSB first: bit 0 is leftmost pixel in each byte. */
    for (uint32_t y = 0; y < height; y++) {
        guchar* row = surface_data + (size_t)y * (size_t)surface_stride;
        for (uint32_t x = 0; x < width; x++) {
            unsigned int bit;
            if (use_short) {
                uint32_t word_idx = y * ((width + 15) / 16) + x / 16;
                uint16_t w = bits16[word_idx];
                int bit_idx = (int)(x % 16); /* LSB first */
                bit = (w >> bit_idx) & 1u;
            } else {
                uint32_t byte_idx = y * row_stride_bytes + x / 8;
                uint8_t b = bits[byte_idx];
                int bit_idx = (int)(x % 8); /* LSB first */
                bit = (b >> bit_idx) & 1u;
            }
            uint8_t v = bit ? 0 : 255; /* 1 = black, 0 = white */
            row[x * 4 + 0] = v;        /* B */
            row[x * 4 + 1] = v;        /* G */
            row[x * 4 + 2] = v;        /* R */
            row[x * 4 + 3] = 255;
        }
    }

    g_free(bits);
    g_free(bits16);

    cairo_surface_mark_dirty(temp_surface);
    doc->layers = g_list_append(doc->layers, base_layer);
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

bool plugin_init_xbm(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

    if (!out_plugin)
        return false;

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "XBM - X Bitmap";
    out_plugin->format_info.extensions = "xbm,h";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_xbm;
    out_plugin->callbacks.load = load_xbm;
    out_plugin->callbacks.can_save = can_save_xbm;
    out_plugin->callbacks.save = NULL;

    return true;
}
