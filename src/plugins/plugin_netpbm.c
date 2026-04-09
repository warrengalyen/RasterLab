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

/**
 * Netpbm format types
 */
typedef enum {
    NETPBM_PBM = 1, /* Portable Bitmap (black/white) */
    NETPBM_PGM = 2, /* Portable Graymap (grayscale) */
    NETPBM_PPM = 3, /* Portable Pixmap (color) */
    NETPBM_PAM = 7  /* Portable Arbitrary Map (arbitrary channels including alpha) */
} NetpbmType;

/**
 * Check if file is Netpbm format
 */
static bool can_load_netpbm(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 2) {
        return false;
    }

    /* Check for Netpbm magic numbers: P1-P6 (PBM, PGM, PPM) and P7 (PAM) */
    if (header[0] == 'P') {
        if ((header[1] >= '1' && header[1] <= '6') || header[1] == '7') {
            return true;
        }
    }

    return false;
}

/**
 * Check if plugin can save to Netpbm format
 * This plugin is read-only, so always return false
 */
static bool can_save_netpbm(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Skip whitespace and comments in Netpbm file
 */
static int skip_whitespace_and_comments(FILE* file) {
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (c == '#') {
            /* Skip comment until end of line */
            while ((c = fgetc(file)) != EOF && c != '\n') {
                /* Skip comment */
            }
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            /* Skip whitespace */
            continue;
        } else {
            /* Non-whitespace, non-comment character */
            ungetc(c, file);
            return 0;
        }
    }
    return EOF;
}

/**
 * Read an integer from Netpbm file (skipping whitespace and comments)
 */
static int read_netpbm_int(FILE* file, uint32_t* value) {
    int c;
    uint32_t result = 0;

    /* Skip whitespace and comments */
    if (skip_whitespace_and_comments(file) == EOF) {
        return EOF;
    }

    /* Read digits */
    while ((c = fgetc(file)) != EOF) {
        if (c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
        } else {
            ungetc(c, file);
            break;
        }
    }

    if (value) {
        *value = result;
    }
    return 0;
}

/**
 * Read a line from file (up to max_size bytes, excluding newline)
 * Returns number of bytes read (excluding newline), or -1 on error
 */
static int read_line(FILE* file, char* buffer, size_t max_size) {
    size_t i = 0;
    int c;

    if (!file || !buffer || max_size == 0) {
        return -1;
    }

    while (i < max_size - 1) {
        c = fgetc(file);
        if (c == EOF) {
            if (i == 0) {
                return -1; /* EOF before reading anything */
            }
            break; /* EOF after reading some data */
        }
        if (c == '\n') {
            break; /* End of line */
        }
        if (c == '\r') {
            /* Skip carriage return, but check for \n */
            c = fgetc(file);
            if (c != '\n' && c != EOF) {
                ungetc(c, file);
            }
            break;
        }
        buffer[i++] = (char)c;
    }

    buffer[i] = '\0';
    return (int)i;
}

/**
 * Parse PAM header fields
 * Returns true on success, false on error
 */
static bool parse_pam_header(FILE* file, uint32_t* width, uint32_t* height,
                             uint32_t* depth, uint32_t* maxval,
                             char* tupltype, size_t tupltype_size,
                             bool* has_alpha) {
    char line[512];
    bool found_width = false, found_height = false, found_depth = false;
    bool found_maxval = false, found_endhdr = false;

    if (!file || !width || !height || !depth || !maxval) {
        return false;
    }

    if (tupltype) {
        tupltype[0] = '\0';
    }
    if (has_alpha) {
        *has_alpha = false;
    }

    /* Read header fields until ENDHDR */
    while (!found_endhdr) {
        int line_len = read_line(file, line, sizeof(line));
        if (line_len < 0) {
            return false; /* EOF or error */
        }

        /* Skip empty lines */
        if (line_len == 0) {
            continue;
        }

        /* Skip comments */
        if (line[0] == '#') {
            continue;
        }

        /* Parse WIDTH */
        if (strncmp(line, "WIDTH", 5) == 0 && (line[5] == ' ' || line[5] == '\t')) {
            if (sscanf(line, "WIDTH %u", width) == 1) {
                found_width = true;
            }
        }
        /* Parse HEIGHT */
        else if (strncmp(line, "HEIGHT", 6) == 0 && (line[6] == ' ' || line[6] == '\t')) {
            if (sscanf(line, "HEIGHT %u", height) == 1) {
                found_height = true;
            }
        }
        /* Parse DEPTH */
        else if (strncmp(line, "DEPTH", 5) == 0 && (line[5] == ' ' || line[5] == '\t')) {
            if (sscanf(line, "DEPTH %u", depth) == 1) {
                found_depth = true;
            }
        }
        /* Parse MAXVAL */
        else if (strncmp(line, "MAXVAL", 6) == 0 && (line[6] == ' ' || line[6] == '\t')) {
            if (sscanf(line, "MAXVAL %u", maxval) == 1) {
                found_maxval = true;
            }
        }
        /* Parse TUPLTYPE */
        else if (strncmp(line, "TUPLTYPE", 8) == 0 && (line[8] == ' ' || line[8] == '\t')) {
            if (tupltype && tupltype_size > 0) {
                /* Extract the tuple type value */
                const char* value = line + 8;
                while (*value == ' ' || *value == '\t') {
                    value++;
                }
                size_t len = strlen(value);
                if (len < tupltype_size) {
                    strncpy(tupltype, value, tupltype_size - 1);
                    tupltype[tupltype_size - 1] = '\0';
                }
            }
        }
        /* Parse ENDHDR */
        else if (strcmp(line, "ENDHDR") == 0) {
            found_endhdr = true;
            break;
        }
    }

    /* Check required fields */
    if (!found_width || !found_height || !found_depth || !found_maxval || !found_endhdr) {
        return false;
    }

    /* Determine if alpha channel is present based on TUPLTYPE or DEPTH */
    if (has_alpha && tupltype && tupltype[0] != '\0') {
        /* Check for alpha in tuple type name */
        if (strstr(tupltype, "ALPHA") != NULL || strstr(tupltype, "alpha") != NULL) {
            *has_alpha = true;
        }
    } else if (has_alpha) {
        /* Infer from depth: RGB_ALPHA = 4, GRAYSCALE_ALPHA = 2 */
        if (*depth == 4 || *depth == 2) {
            *has_alpha = true;
        }
    }

    return true;
}

/**
 * Load Netpbm image
 */
static PluginError load_netpbm(ImageDocument* doc, const char* filename) {
    FILE* infile;
    uint8_t magic[2];
    NetpbmType format_type;
    bool is_binary;
    uint32_t width, height;
    uint32_t max_value = 255;
    uint32_t pam_depth = 0; /* For PAM format */
    uint8_t* image_data = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    bool has_alpha = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open Netpbm file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read magic number */
    if (fread(magic, 1, 2, infile) != 2) {
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Verify magic number */
    if (magic[0] != 'P' || (magic[1] < '1' || (magic[1] > '6' && magic[1] != '7'))) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Check if this is PAM (P7) format */
    if (magic[1] == '7') {
        /* PAM format - use special parsing */
        format_type = NETPBM_PAM;
        is_binary = true; /* PAM is always binary */
    } else {
        /* Determine format type and binary/ASCII */
        format_type = (NetpbmType)(magic[1] - '0');
        is_binary = (format_type >= 4);

        /* Adjust format type for binary formats */
        if (is_binary) {
            format_type = (NetpbmType)(format_type - 3);
        }
    }

    /* Parse header based on format type */
    if (format_type == NETPBM_PAM) {
        /* PAM format uses field-based header */
        uint32_t depth = 0;
        char tupltype[256] = {0};
        bool pam_has_alpha = false;

        if (!parse_pam_header(infile, &width, &height, &depth, &max_value,
                             tupltype, sizeof(tupltype), &pam_has_alpha)) {
            fclose(infile);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }

        has_alpha = pam_has_alpha;
        pam_depth = depth;

        /* Validate PAM parameters */
        if (width == 0 || height == 0 || depth == 0 || depth > 4) {
            fclose(infile);
            return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
        }
        if (max_value == 0 || max_value > 65535) {
            fclose(infile);
            return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
        }
    } else {
        /* Standard PBM/PGM/PPM format */
        /* Read width */
        if (read_netpbm_int(infile, &width) == EOF || width == 0) {
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }

        /* Read height */
        if (read_netpbm_int(infile, &height) == EOF || height == 0) {
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }

        /* Read max value for PGM and PPM (not needed for PBM) */
        if (format_type != NETPBM_PBM) {
            if (read_netpbm_int(infile, &max_value) == EOF) {
                fclose(infile);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }
            if (max_value == 0) {
                max_value = 255; /* Default */
            }
            if (max_value > 65535) {
                max_value = 65535; /* Clamp to 16-bit */
            }
        }

        /* Skip single whitespace character before binary data */
        if (is_binary) {
            int c = fgetc(infile);
            if (c == EOF) {
                fclose(infile);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }
        }
    }

    /* Validate dimensions */
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = 4; /* Always RGBA internally */
    doc->bit_depth = 8;
    /* Update has_alpha based on format - PAM can have alpha, others don't */
    if (format_type == NETPBM_PAM) {
        doc->has_alpha = has_alpha;
    } else {
        doc->has_alpha = false; /* PBM, PGM, PPM don't support alpha */
    }

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
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Read image data based on format */
    if (is_binary) {
        /* Binary format */
        if (format_type == NETPBM_PBM) {
            /* PBM (P4): 1 bit per pixel, packed */
            uint32_t row_bytes = (width + 7) / 8;
            uint8_t* row_data = g_malloc(row_bytes);
            if (!row_data) {
                cairo_surface_destroy(temp_surface);
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            for (uint32_t y = 0; y < height; y++) {
                if (fread(row_data, 1, row_bytes, infile) != row_bytes) {
                    g_free(row_data);
                    cairo_surface_destroy(temp_surface);
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t byte_idx = x / 8;
                    uint32_t bit_pos = 7 - (x % 8);
                    uint8_t bit = (row_data[byte_idx] >> bit_pos) & 1;
                    uint8_t gray = bit ? 255 : 0;

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = gray; /* B */
                    row[x * 4 + 1] = gray; /* G */
                    row[x * 4 + 2] = gray; /* R */
                    row[x * 4 + 3] = 255;  /* A */
                }
            }
            g_free(row_data);
        } else if (format_type == NETPBM_PGM) {
            /* PGM (P5): Grayscale, 8 or 16 bits per pixel */
            bool is_16bit = (max_value > 255);
            uint32_t bytes_per_pixel = is_16bit ? 2 : 1;
            uint8_t* row_data = g_malloc(width * bytes_per_pixel);
            if (!row_data) {
                cairo_surface_destroy(temp_surface);
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            for (uint32_t y = 0; y < height; y++) {
                if (fread(row_data, bytes_per_pixel, width, infile) != width) {
                    g_free(row_data);
                    cairo_surface_destroy(temp_surface);
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t gray;
                    if (is_16bit) {
                        uint16_t value = (row_data[x * 2] << 8) | row_data[x * 2 + 1];
                        /* Scale from 16-bit to 8-bit */
                        gray = (uint8_t)((value * 255) / max_value);
                    } else {
                        gray = row_data[x];
                    }

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = gray; /* B */
                    row[x * 4 + 1] = gray; /* G */
                    row[x * 4 + 2] = gray; /* R */
                    row[x * 4 + 3] = 255;  /* A */
                }
            }
            g_free(row_data);
        } else if (format_type == NETPBM_PPM) {
            /* PPM (P6): RGB, 8 or 16 bits per channel */
            bool is_16bit = (max_value > 255);
            uint32_t bytes_per_channel = is_16bit ? 2 : 1;
            uint32_t bytes_per_pixel = bytes_per_channel * 3;
            uint8_t* row_data = g_malloc(width * bytes_per_pixel);
            if (!row_data) {
                cairo_surface_destroy(temp_surface);
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            for (uint32_t y = 0; y < height; y++) {
                if (fread(row_data, bytes_per_pixel, width, infile) != width) {
                    g_free(row_data);
                    cairo_surface_destroy(temp_surface);
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t r, g, b;
                    if (is_16bit) {
                        uint32_t offset = x * bytes_per_pixel;
                        uint16_t r_val = (row_data[offset + 0] << 8) | row_data[offset + 1];
                        uint16_t g_val = (row_data[offset + 2] << 8) | row_data[offset + 3];
                        uint16_t b_val = (row_data[offset + 4] << 8) | row_data[offset + 5];
                        /* Scale from 16-bit to 8-bit */
                        r = (uint8_t)((r_val * 255) / max_value);
                        g = (uint8_t)((g_val * 255) / max_value);
                        b = (uint8_t)((b_val * 255) / max_value);
                    } else {
                        uint32_t offset = x * 3;
                        r = row_data[offset + 0];
                        g = row_data[offset + 1];
                        b = row_data[offset + 2];
                    }

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = b;   /* B */
                    row[x * 4 + 1] = g;   /* G */
                    row[x * 4 + 2] = r;   /* R */
                    row[x * 4 + 3] = 255; /* A */
                }
            }
            g_free(row_data);
        } else if (format_type == NETPBM_PAM) {
            /* PAM (P7): Arbitrary depth, supports alpha */
            bool is_16bit = (max_value > 255);
            uint32_t bytes_per_sample = is_16bit ? 2 : 1;
            uint32_t bytes_per_pixel = pam_depth * bytes_per_sample;
            uint8_t* row_data = g_malloc(width * bytes_per_pixel);
            if (!row_data) {
                cairo_surface_destroy(temp_surface);
                fclose(infile);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }

            for (uint32_t y = 0; y < height; y++) {
                if (fread(row_data, bytes_per_pixel, width, infile) != width) {
                    g_free(row_data);
                    cairo_surface_destroy(temp_surface);
                    fclose(infile);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    uint32_t offset = x * bytes_per_pixel;

                    if (pam_depth == 1) {
                        /* Grayscale */
                        if (is_16bit) {
                            uint16_t gray_val = (row_data[offset + 0] << 8) | row_data[offset + 1];
                            r = g = b = (uint8_t)((gray_val * 255) / max_value);
                        } else {
                            r = g = b = row_data[offset];
                        }
                    } else if (pam_depth == 2) {
                        /* Grayscale + Alpha */
                        if (is_16bit) {
                            uint16_t gray_val = (row_data[offset + 0] << 8) | row_data[offset + 1];
                            uint16_t alpha_val = (row_data[offset + 2] << 8) | row_data[offset + 3];
                            r = g = b = (uint8_t)((gray_val * 255) / max_value);
                            a = (uint8_t)((alpha_val * 255) / max_value);
                        } else {
                            r = g = b = row_data[offset + 0];
                            a = row_data[offset + 1];
                        }
                    } else if (pam_depth == 3) {
                        /* RGB */
                        if (is_16bit) {
                            uint16_t r_val = (row_data[offset + 0] << 8) | row_data[offset + 1];
                            uint16_t g_val = (row_data[offset + 2] << 8) | row_data[offset + 3];
                            uint16_t b_val = (row_data[offset + 4] << 8) | row_data[offset + 5];
                            r = (uint8_t)((r_val * 255) / max_value);
                            g = (uint8_t)((g_val * 255) / max_value);
                            b = (uint8_t)((b_val * 255) / max_value);
                        } else {
                            r = row_data[offset + 0];
                            g = row_data[offset + 1];
                            b = row_data[offset + 2];
                        }
                    } else if (pam_depth == 4) {
                        /* RGBA */
                        if (is_16bit) {
                            uint16_t r_val = (row_data[offset + 0] << 8) | row_data[offset + 1];
                            uint16_t g_val = (row_data[offset + 2] << 8) | row_data[offset + 3];
                            uint16_t b_val = (row_data[offset + 4] << 8) | row_data[offset + 5];
                            uint16_t a_val = (row_data[offset + 6] << 8) | row_data[offset + 7];
                            r = (uint8_t)((r_val * 255) / max_value);
                            g = (uint8_t)((g_val * 255) / max_value);
                            b = (uint8_t)((b_val * 255) / max_value);
                            a = (uint8_t)((a_val * 255) / max_value);
                        } else {
                            r = row_data[offset + 0];
                            g = row_data[offset + 1];
                            b = row_data[offset + 2];
                            a = row_data[offset + 3];
                        }
                    }

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = b; /* B */
                    row[x * 4 + 1] = g; /* G */
                    row[x * 4 + 2] = r; /* R */
                    row[x * 4 + 3] = a; /* A */
                }
            }
            g_free(row_data);
        }
    } else {
        /* ASCII format (P1, P2, P3) */
        if (format_type == NETPBM_PBM) {
            /* PBM (P1): ASCII, 0 or 1 per pixel */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t value;
                    if (read_netpbm_int(infile, &value) == EOF) {
                        cairo_surface_destroy(temp_surface);
                        fclose(infile);
                        return PLUGIN_ERROR_FILE_READ_ERROR;
                    }
                    uint8_t gray = (value != 0) ? 255 : 0;

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = gray; /* B */
                    row[x * 4 + 1] = gray; /* G */
                    row[x * 4 + 2] = gray; /* R */
                    row[x * 4 + 3] = 255;  /* A */
                }
            }
        } else if (format_type == NETPBM_PGM) {
            /* PGM (P2): ASCII, grayscale values */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t value;
                    if (read_netpbm_int(infile, &value) == EOF) {
                        cairo_surface_destroy(temp_surface);
                        fclose(infile);
                        return PLUGIN_ERROR_FILE_READ_ERROR;
                    }
                    /* Scale to 8-bit */
                    uint8_t gray = (uint8_t)((value * 255) / max_value);

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = gray; /* B */
                    row[x * 4 + 1] = gray; /* G */
                    row[x * 4 + 2] = gray; /* R */
                    row[x * 4 + 3] = 255;  /* A */
                }
            }
        } else if (format_type == NETPBM_PPM) {
            /* PPM (P3): ASCII, RGB triplets */
            for (uint32_t y = 0; y < height; y++) {
                guchar* row = surface_data + y * surface_stride;
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t r_val, g_val, b_val;
                    if (read_netpbm_int(infile, &r_val) == EOF ||
                        read_netpbm_int(infile, &g_val) == EOF ||
                        read_netpbm_int(infile, &b_val) == EOF) {
                        cairo_surface_destroy(temp_surface);
                        fclose(infile);
                        return PLUGIN_ERROR_FILE_READ_ERROR;
                    }
                    /* Scale to 8-bit */
                    uint8_t r = (uint8_t)((r_val * 255) / max_value);
                    uint8_t g = (uint8_t)((g_val * 255) / max_value);
                    uint8_t b = (uint8_t)((b_val * 255) / max_value);

                    /* Convert to Cairo ARGB32 (BGRA in memory) */
                    row[x * 4 + 0] = b;   /* B */
                    row[x * 4 + 1] = g;   /* G */
                    row[x * 4 + 2] = r;   /* R */
                    row[x * 4 + 3] = 255; /* A */
                }
            }
        }
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
 * Initialize Netpbm plugin
 */
bool plugin_init_netpbm(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "Netpbm - Portable Pixmap/Graymap/Bitmap/Arbitrary";
    out_plugin->format_info.extensions = "ppm,pgm,pbm,pam,pnm";
    out_plugin->format_info.supports_alpha = true; /* PAM supports alpha */
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 70;

    out_plugin->callbacks.can_load = can_load_netpbm;
    out_plugin->callbacks.load = load_netpbm;
    out_plugin->callbacks.can_save = can_save_netpbm; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
