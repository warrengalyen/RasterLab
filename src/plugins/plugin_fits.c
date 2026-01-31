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
 * FITS header card image size
 */
#define FITS_CARD_SIZE 80
#define FITS_BLOCK_SIZE 2880
#define FITS_CARDS_PER_BLOCK 36

/**
 * FITS BITPIX values
 */
typedef enum {
    FITS_BITPIX_FLOAT64 = -64,
    FITS_BITPIX_FLOAT32 = -32,
    FITS_BITPIX_INT8 = 8,
    FITS_BITPIX_INT16 = 16,
    FITS_BITPIX_INT32 = 32,
    FITS_BITPIX_INT64 = 64
} FitsBitpix;

/**
 * FITS header structure
 */
typedef struct {
    int32_t bitpix;
    int32_t naxis;
    int32_t naxis1; /* Width */
    int32_t naxis2; /* Height */
    int32_t naxis3; /* Depth (for 3D) */
    double bscale;
    double bzero;
    bool is_big_endian;
    bool has_bscale;
    bool has_bzero;
} FitsHeader;

/**
 * Check if file is FITS format
 */
static bool can_load_fits(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 6) {
        return false;
    }

    /* FITS files start with "SIMPLE" or "XTENSION" in first 8 characters */
    if (strncmp((const char*)header, "SIMPLE", 6) == 0 ||
        strncmp((const char*)header, "XTENSION", 8) == 0) {
        return true;
    }

    return false;
}

/**
 * Check if plugin can save to FITS format
 * This plugin is read-only, so always return false
 */
static bool can_save_fits(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Read a FITS header card (80 bytes)
 */
static bool read_fits_card(FILE* file, char* card) {
    if (fread(card, 1, FITS_CARD_SIZE, file) != FITS_CARD_SIZE) {
        return false;
    }
    return true;
}

/**
 * Parse a FITS header keyword value
 * Format: "KEYWORD = 'value' / comment" or "KEYWORD = value / comment"
 */
static bool parse_fits_keyword(const char* card, const char* keyword, char* value, size_t value_size) {
    char key[9];
    char equals[2];
    char val[71];
    int result;

    /* Extract keyword (8 chars) */
    strncpy(key, card, 8);
    key[8] = '\0';
    /* Trim trailing spaces */
    for (int i = 7; i >= 0 && key[i] == ' '; i--) {
        key[i] = '\0';
    }

    if (strcmp(key, keyword) != 0) {
        return false;
    }

    /* Skip to '=' sign (should be at position 8) */
    if (card[8] != ' ' || card[9] != '=' || card[10] != ' ') {
        return false;
    }

    /* Extract value (from position 10 onwards, up to '/' or end) */
    result = sscanf(card + 10, "%70s", val);
    if (result != 1) {
        return false;
    }

    /* Remove quotes if present */
    if (val[0] == '\'' && val[strlen(val) - 1] == '\'') {
        size_t len = strlen(val);
        memmove(val, val + 1, len - 2);
        val[len - 2] = '\0';
    }

    if (value && value_size > 0) {
        strncpy(value, val, value_size - 1);
        value[value_size - 1] = '\0';
    }

    return true;
}

/**
 * Parse integer value from FITS header card
 */
static bool parse_fits_int(const char* card, const char* keyword, int32_t* value) {
    char key[9];
    int32_t val;
    int result;
    int i;
    int eq_pos = -1;

    /* Extract keyword (8 chars) */
    strncpy(key, card, 8);
    key[8] = '\0';
    /* Trim trailing spaces */
    for (i = 7; i >= 0 && key[i] == ' '; i--) {
        key[i] = '\0';
    }

    if (strcmp(key, keyword) != 0) {
        return false;
    }

    /* Find '=' sign (can be at position 8, 9, or 10) */
    for (i = 8; i < 11 && i < 80; i++) {
        if (card[i] == '=') {
            eq_pos = i;
            break;
        }
    }

    if (eq_pos == -1) {
        /* No equals sign found - might be a logical value (T/F) or missing */
        return false;
    }

    i = eq_pos + 1;

    /* Skip spaces after '=' */
    while (i < 80 && card[i] == ' ') {
        i++;
    }

    if (i >= 80) {
        return false;
    }

    /* Parse integer value (stop at '/' for comments, whitespace, or end of value field) */
    result = sscanf(card + i, "%d", &val);
    if (result != 1) {
        return false;
    }

    if (value) {
        *value = val;
    }

    return true;
}

/**
 * Parse double value from FITS header card
 */
static bool parse_fits_double(const char* card, const char* keyword, double* value) {
    char key[9];
    double val;
    int result;
    int i;

    /* Extract keyword (8 chars) */
    strncpy(key, card, 8);
    key[8] = '\0';
    /* Trim trailing spaces */
    for (i = 7; i >= 0 && key[i] == ' '; i--) {
        key[i] = '\0';
    }

    if (strcmp(key, keyword) != 0) {
        return false;
    }

    /* Find '=' sign (can be at position 8 or 9) */
    if (card[8] == '=') {
        i = 9;
    } else if (card[8] == ' ' && card[9] == '=') {
        i = 10;
    } else {
        return false;
    }

    /* Skip spaces after '=' */
    while (i < 80 && card[i] == ' ') {
        i++;
    }

    if (i >= 80) {
        return false;
    }

    /* Parse double value (stop at '/' for comments or end of value field) */
    result = sscanf(card + i, "%lf", &val);
    if (result != 1) {
        return false;
    }

    if (value) {
        *value = val;
    }

    return true;
}

/**
 * Check if system is big-endian
 */
static bool is_big_endian(void) {
    union {
        uint32_t i;
        uint8_t c[4];
    } test = {0x01020304};
    return test.c[0] == 0x01;
}

/**
 * Swap 16-bit value
 */
static uint16_t swap16(uint16_t value) {
    return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
}

/**
 * Swap 32-bit value
 */
static uint32_t swap32(uint32_t value) {
    return ((value & 0xFF000000) >> 24) | ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) | ((value & 0x000000FF) << 24);
}

/**
 * Swap 64-bit value
 */
static uint64_t swap64(uint64_t value) {
    return ((value & 0xFF00000000000000ULL) >> 56) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x000000FF00000000ULL) >> 8) |
           ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x00000000000000FFULL) << 56);
}

/**
 * Read and parse FITS header (primary HDU or extension)
 */
static bool read_fits_header(FILE* file, FitsHeader* header, bool* is_extension) {
    char card[FITS_CARD_SIZE + 1];
    bool found_simple = false;
    bool found_xtension = false;
    bool found_bitpix = false;
    bool found_naxis = false;
    bool found_naxis1 = false;
    bool found_naxis2 = false;
    bool found_end = false;
    int32_t naxis = 0;
    int32_t bitpix = 0;
    int32_t naxis1 = 0;
    int32_t naxis2 = 0;
    int32_t naxis3 = 0;
    double bscale = 1.0;
    double bzero = 0.0;
    bool has_bscale = false;
    bool has_bzero = false;
    int card_count = 0;
    const int max_cards = 1000; /* Limit header size */

    memset(header, 0, sizeof(FitsHeader));
    if (is_extension) {
        *is_extension = false;
    }

    /* Read header cards until END */
    while (!found_end && card_count < max_cards) {
        if (!read_fits_card(file, card)) {
            return false;
        }
        card[FITS_CARD_SIZE] = '\0';
        card_count++;

        /* Check for SIMPLE or XTENSION */
        if (strncmp(card, "SIMPLE", 6) == 0) {
            found_simple = true;
        } else if (strncmp(card, "XTENSION", 8) == 0) {
            found_xtension = true;
            if (is_extension) {
                *is_extension = true;
            }
        }
        /* Parse BITPIX */
        else if (parse_fits_int(card, "BITPIX", &bitpix)) {
            found_bitpix = true;
        }
        /* Parse NAXIS */
        else if (parse_fits_int(card, "NAXIS", &naxis)) {
            found_naxis = true;
        }
        /* Parse NAXIS1 (width) */
        else if (parse_fits_int(card, "NAXIS1", &naxis1)) {
            found_naxis1 = true;
        }
        /* Parse NAXIS2 (height) */
        else if (parse_fits_int(card, "NAXIS2", &naxis2)) {
            found_naxis2 = true;
        }
        /* Parse NAXIS3 (depth, optional) */
        else if (parse_fits_int(card, "NAXIS3", &naxis3)) {
            /* 3D data - we'll use first plane */
        }
        /* Parse BSCALE */
        else if (parse_fits_double(card, "BSCALE", &bscale)) {
            has_bscale = true;
        }
        /* Parse BZERO */
        else if (parse_fits_double(card, "BZERO", &bzero)) {
            has_bzero = true;
        }
        /* Check for END */
        else if (strncmp(card, "END", 3) == 0) {
            found_end = true;
            break;
        }
    }

    if (card_count >= max_cards) {
        return false; /* Header too large or no END found */
    }

    /* Validate required keywords */
    if (!found_simple && !found_xtension) {
        g_warning("FITS plugin: Missing SIMPLE or XTENSION keyword");
        return false;
    }
    if (!found_bitpix) {
        g_warning("FITS plugin: Missing BITPIX keyword");
        return false;
    }
    if (!found_naxis) {
        g_warning("FITS plugin: Missing NAXIS keyword");
        return false;
    }

    /* If NAXIS = 0, this is an empty HDU - we need to skip to an extension */
    if (naxis == 0) {
        return false; /* Signal to caller to skip to next HDU */
    }

    /* For image HDUs, we need NAXIS1 and NAXIS2 */
    if (naxis >= 1 && !found_naxis1) {
        g_warning("FITS plugin: Missing NAXIS1 keyword");
        return false;
    }
    if (naxis >= 2 && !found_naxis2) {
        g_warning("FITS plugin: Missing NAXIS2 keyword");
        return false;
    }

    /* Validate dimensions */
    if (naxis1 <= 0 || naxis2 <= 0 || naxis1 > 65535 || naxis2 > 65535) {
        return false;
    }

    /* Validate BITPIX */
    if (bitpix != FITS_BITPIX_INT8 && bitpix != FITS_BITPIX_INT16 &&
        bitpix != FITS_BITPIX_INT32 && bitpix != FITS_BITPIX_FLOAT32) {
        /* Unsupported bit depth */
        return false;
    }

    /* FITS standard specifies big-endian by default */
    header->is_big_endian = true;

    /* Skip to end of header block (2880-byte boundary) */
    long pos = ftell(file);
    long block_end = ((pos + FITS_BLOCK_SIZE - 1) / FITS_BLOCK_SIZE) * FITS_BLOCK_SIZE;
    fseek(file, block_end, SEEK_SET);

    /* Fill header structure */
    header->bitpix = bitpix;
    header->naxis = naxis;
    header->naxis1 = naxis1;
    header->naxis2 = naxis2;
    header->naxis3 = naxis3;
    header->bscale = bscale;
    header->bzero = bzero;
    header->has_bscale = has_bscale;
    header->has_bzero = has_bzero;

    return true;
}

/**
 * Convert FITS pixel value to 8-bit (value is already scaled with BSCALE/BZERO)
 */
static uint8_t fits_to_8bit(double value, double min_val, double max_val) {
    double normalized;

    /* Normalize to 0-1 range */
    if (max_val > min_val) {
        normalized = (value - min_val) / (max_val - min_val);
    } else {
        normalized = 0.5;
    }

    /* Clamp and convert to 8-bit */
    if (normalized < 0.0) {
        normalized = 0.0;
    } else if (normalized > 1.0) {
        normalized = 1.0;
    }

    return (uint8_t)(normalized * 255.0);
}

static inline uint8_t clamp_u8(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

/**
 * Load FITS image
 */
static PluginError load_fits(ImageDocument* doc, const char* filename) {
    FILE* infile;
    FitsHeader header;
    uint8_t* image_data = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    bool needs_byte_swap;

    double min_val = 0.0, max_val = 0.0;
    bool min_max_calculated = false;

    /* Per-channel min/max for RGB (only used when we must normalize RGB) */
    double rgb_min[3] = {0.0, 0.0, 0.0};
    double rgb_max[3] = {0.0, 0.0, 0.0};
    bool rgb_min_max_calculated = false;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read FITS header - try primary HDU first, then extensions if needed */
    {
        bool is_extension = false;
        bool header_found = false;
        int hdu_count = 0;
        const int max_hdus = 10;

        while (!header_found && hdu_count < max_hdus) {
            bool header_parsed = read_fits_header(infile, &header, &is_extension);

            if (!header_parsed) {
                /* If we fail after the first HDU, treat as corrupt */
                if (hdu_count > 0) {
                    g_warning("FITS plugin: Failed to parse header from %s (HDU %d)", filename, hdu_count);
                    fclose(infile);
                    return PLUGIN_ERROR_CORRUPT_FILE;
                }
                hdu_count++;
                continue;
            }

            if (header.naxis > 0 && header.naxis1 > 0 && header.naxis2 > 0) {
                header_found = true;
            } else {
                /* This HDU doesn't have image data, skip to next */
                long pos = ftell(infile);
                long block_end = ((pos + FITS_BLOCK_SIZE - 1) / FITS_BLOCK_SIZE) * FITS_BLOCK_SIZE;
                fseek(infile, block_end, SEEK_SET);

                if (header.naxis > 0) {
                    size_t pixel_count = 1;
                    int i;

                    for (i = 1; i <= header.naxis; i++) {
                        int32_t naxis_val = 0;
                        if (i == 1)
                            naxis_val = header.naxis1;
                        else if (i == 2)
                            naxis_val = header.naxis2;
                        else if (i == 3)
                            naxis_val = header.naxis3;

                        if (naxis_val > 0) {
                            pixel_count *= (size_t)naxis_val;
                        }
                    }

                    {
                        size_t bytes_per_pixel = (size_t)(abs(header.bitpix) / 8);
                        size_t data_size;
                        long data_end;

                        if (bytes_per_pixel == 0) {
                            bytes_per_pixel = 1;
                        }

                        data_size = pixel_count * bytes_per_pixel;
                        data_end = (long)(((data_size + FITS_BLOCK_SIZE - 1) / FITS_BLOCK_SIZE) * FITS_BLOCK_SIZE);
                        fseek(infile, data_end, SEEK_CUR);
                    }
                }

                hdu_count++;
            }
        }

        if (!header_found) {
            g_warning("FITS plugin: No image data found in any HDU in %s", filename);
            fclose(infile);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
    }

    needs_byte_swap = (header.is_big_endian != is_big_endian());

    /* Set document metadata */
    doc->width = (uint32_t)header.naxis1;
    doc->height = (uint32_t)header.naxis2;
    doc->channels = 4;
    doc->bit_depth = 8;
    doc->has_alpha = false;

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
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

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

    /* Determine layout and allocation sizes */
    {
        const bool is_rgb_3d = (header.naxis == 3 && header.naxis3 == 3);
        const size_t plane_pixels = (size_t)header.naxis1 * (size_t)header.naxis2;
        const size_t pixel_count = is_rgb_3d ? (plane_pixels * 3u) : plane_pixels;

        size_t bytes_per_pixel = 0;
        size_t data_size;

        switch (header.bitpix) {
            case FITS_BITPIX_INT8:
            case -8: /* your unsigned-flag */
                bytes_per_pixel = 1;
                break;
            case FITS_BITPIX_INT16:
                bytes_per_pixel = 2;
                break;
            case FITS_BITPIX_INT32:
            case FITS_BITPIX_FLOAT32:
                bytes_per_pixel = 4;
                break;
            default:
                layer_free(base_layer);
                fclose(infile);
                return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
        }

        data_size = pixel_count * bytes_per_pixel;
        image_data = (uint8_t*)g_malloc(data_size);
        if (!image_data) {
            layer_free(base_layer);
            fclose(infile);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        if (fread(image_data, 1, data_size, infile) != data_size) {
            g_free(image_data);
            layer_free(base_layer);
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }

        /* Swap bytes if needed */
        if (needs_byte_swap && bytes_per_pixel > 1) {
            uint8_t* bytes = (uint8_t*)image_data;
            size_t i;

            for (i = 0; i < pixel_count; i++) {
                if (bytes_per_pixel == 2) {
                    uint8_t tmp = bytes[i * 2];
                    bytes[i * 2] = bytes[i * 2 + 1];
                    bytes[i * 2 + 1] = tmp;
                } else if (bytes_per_pixel == 4) {
                    uint8_t tmp0 = bytes[i * 4];
                    uint8_t tmp1 = bytes[i * 4 + 1];
                    bytes[i * 4] = bytes[i * 4 + 3];
                    bytes[i * 4 + 1] = bytes[i * 4 + 2];
                    bytes[i * 4 + 2] = tmp1;
                    bytes[i * 4 + 3] = tmp0;
                }
            }
        }

        /* ------------------------------------------------------------
         * Min/max calculation
         * ------------------------------------------------------------ */

        if (is_rgb_3d) {
            /* RGB cube: never compute a single global min/max across all channels.
             * Use direct copy for common 8-bit display RGB (BSCALE=1, BZERO=0).
             * Otherwise compute per-channel min/max for normalization.
             */
            const bool rgb_direct =
                ((header.bitpix == FITS_BITPIX_INT8 || header.bitpix == -8) &&
                 header.bscale == 1.0 && header.bzero == 0.0);

            if (!rgb_direct) {
                size_t p;

                rgb_min[0] = DBL_MAX;
                rgb_max[0] = -DBL_MAX;
                rgb_min[1] = DBL_MAX;
                rgb_max[1] = -DBL_MAX;
                rgb_min[2] = DBL_MAX;
                rgb_max[2] = -DBL_MAX;

                for (p = 0; p < plane_pixels; p++) {
                    double vr = 0.0, vg = 0.0, vb = 0.0;

                    if (header.bitpix == FITS_BITPIX_INT8 || header.bitpix == -8) {
                        uint8_t* d = (uint8_t*)image_data;

                        if (header.bitpix == -8) {
                            vr = (double)d[p] * header.bscale + header.bzero;
                            vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                            vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                        } else {
                            vr = (double)(int8_t)d[p] * header.bscale + header.bzero;
                            vg = (double)(int8_t)d[plane_pixels + p] * header.bscale + header.bzero;
                            vb = (double)(int8_t)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                        }
                    } else if (header.bitpix == FITS_BITPIX_INT16) {
                        int16_t* d = (int16_t*)image_data;
                        vr = (double)d[p] * header.bscale + header.bzero;
                        vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                        vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                    } else if (header.bitpix == FITS_BITPIX_INT32) {
                        int32_t* d = (int32_t*)image_data;
                        vr = (double)d[p] * header.bscale + header.bzero;
                        vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                        vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                    } else {
                        float* d = (float*)image_data;
                        vr = (double)d[p] * header.bscale + header.bzero;
                        vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                        vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                    }

                    if (vr < rgb_min[0])
                        rgb_min[0] = vr;
                    if (vr > rgb_max[0])
                        rgb_max[0] = vr;

                    if (vg < rgb_min[1])
                        rgb_min[1] = vg;
                    if (vg > rgb_max[1])
                        rgb_max[1] = vg;

                    if (vb < rgb_min[2])
                        rgb_min[2] = vb;
                    if (vb > rgb_max[2])
                        rgb_max[2] = vb;
                }

                rgb_min_max_calculated = true;
            }
        } else {
            /* Grayscale: keep your existing behavior: global min/max for the plane */
            size_t i;

            min_val = DBL_MAX;
            max_val = -DBL_MAX;

            for (i = 0; i < plane_pixels; i++) {
                double value = 0.0;

                if (header.bitpix == FITS_BITPIX_INT8 || header.bitpix == -8) {
                    uint8_t* d = (uint8_t*)image_data;
                    uint8_t byte_val = d[i];

                    if (header.bitpix == -8) {
                        value = (double)byte_val;
                    } else {
                        value = (double)(int8_t)byte_val;
                    }
                } else if (header.bitpix == FITS_BITPIX_INT16) {
                    int16_t* d = (int16_t*)image_data;
                    value = (double)d[i];
                } else if (header.bitpix == FITS_BITPIX_INT32) {
                    int32_t* d = (int32_t*)image_data;
                    value = (double)d[i];
                } else {
                    float* d = (float*)image_data;
                    value = (double)d[i];
                }

                value = value * header.bscale + header.bzero;

                if (value < min_val)
                    min_val = value;
                if (value > max_val)
                    max_val = value;
            }

            min_max_calculated = true;

            /* Preserve your "detect unsigned 8-bit" heuristic for grayscale */
            if (header.bitpix == FITS_BITPIX_INT8 && min_val < 0 && max_val <= 127) {
                int negative_count = 0;
                int high_byte_count = 0;
                size_t s;
                size_t sample_n = plane_pixels < 1000 ? plane_pixels : 1000;

                for (s = 0; s < sample_n; s++) {
                    uint8_t* d = (uint8_t*)image_data;
                    uint8_t byte_val = d[s];
                    int8_t signed_val = (int8_t)byte_val;
                    if (signed_val < 0)
                        negative_count++;
                    if (byte_val >= 0x80)
                        high_byte_count++;
                }

                if (high_byte_count > (int)(negative_count * 0.5)) {
                    /* Recompute grayscale min/max treating as unsigned */
                    min_val = DBL_MAX;
                    max_val = -DBL_MAX;

                    for (i = 0; i < plane_pixels; i++) {
                        uint8_t* d = (uint8_t*)image_data;
                        double v = (double)d[i];
                        v = v * header.bscale + header.bzero;

                        if (v < min_val)
                            min_val = v;
                        if (v > max_val)
                            max_val = v;
                    }

                    header.bitpix = -8; /* mark unsigned */
                }
            }
        }

        /* ------------------------------------------------------------
         * Convert to Cairo BGRA
         * ------------------------------------------------------------ */
        {
            uint32_t y, x;
            const bool is_rgb_3d2 = is_rgb_3d; /* keep name stable */
            const bool rgb_direct =
                (is_rgb_3d2 &&
                 (header.bitpix == FITS_BITPIX_INT8 || header.bitpix == -8) &&
                 header.bscale == 1.0 && header.bzero == 0.0);

            for (y = 0; y < doc->height; y++) {
                guchar* row = surface_data + y * surface_stride;
                uint32_t fits_y = doc->height - 1 - y;

                for (x = 0; x < doc->width; x++) {
                    size_t p = (size_t)fits_y * (size_t)header.naxis1 + (size_t)x;

                    if (p >= plane_pixels) {
                        row[x * 4 + 0] = 0;
                        row[x * 4 + 1] = 0;
                        row[x * 4 + 2] = 0;
                        row[x * 4 + 3] = 255;
                        continue;
                    }

                    if (is_rgb_3d2) {
                        /* Planar RGB: plane0=R, plane1=G, plane2=B */
                        if (rgb_direct) {
                            uint8_t* d = (uint8_t*)image_data;
                            uint8_t r, g, b;

                            if (header.bitpix == -8) {
                                r = d[p];
                                g = d[plane_pixels + p];
                                b = d[2 * plane_pixels + p];
                            } else {
                                /* If someone stored signed bytes, keep same mapping but clamp via cast */
                                r = (uint8_t)((int8_t)d[p]);
                                g = (uint8_t)((int8_t)d[plane_pixels + p]);
                                b = (uint8_t)((int8_t)d[2 * plane_pixels + p]);
                            }

                            row[x * 4 + 0] = b;
                            row[x * 4 + 1] = g;
                            row[x * 4 + 2] = r;
                            row[x * 4 + 3] = 255;
                        } else {
                            double vr = 0.0, vg = 0.0, vb = 0.0;

                            if (header.bitpix == FITS_BITPIX_INT8 || header.bitpix == -8) {
                                uint8_t* d = (uint8_t*)image_data;

                                if (header.bitpix == -8) {
                                    vr = (double)d[p] * header.bscale + header.bzero;
                                    vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                                    vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                                } else {
                                    vr = (double)(int8_t)d[p] * header.bscale + header.bzero;
                                    vg = (double)(int8_t)d[plane_pixels + p] * header.bscale + header.bzero;
                                    vb = (double)(int8_t)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                                }
                            } else if (header.bitpix == FITS_BITPIX_INT16) {
                                int16_t* d = (int16_t*)image_data;
                                vr = (double)d[p] * header.bscale + header.bzero;
                                vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                                vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                            } else if (header.bitpix == FITS_BITPIX_INT32) {
                                int32_t* d = (int32_t*)image_data;
                                vr = (double)d[p] * header.bscale + header.bzero;
                                vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                                vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                            } else {
                                float* d = (float*)image_data;
                                vr = (double)d[p] * header.bscale + header.bzero;
                                vg = (double)d[plane_pixels + p] * header.bscale + header.bzero;
                                vb = (double)d[2 * plane_pixels + p] * header.bscale + header.bzero;
                            }

                            /* Per-channel normalization */
                            if (rgb_min_max_calculated) {
                                row[x * 4 + 2] = fits_to_8bit(vr, rgb_min[0], rgb_max[0]); /* R */
                                row[x * 4 + 1] = fits_to_8bit(vg, rgb_min[1], rgb_max[1]); /* G */
                                row[x * 4 + 0] = fits_to_8bit(vb, rgb_min[2], rgb_max[2]); /* B */
                            } else {
                                /* Fallback: clamp after scaling into [0..255] if possible */
                                int rr = (int)(vr + 0.5);
                                int gg = (int)(vg + 0.5);
                                int bb = (int)(vb + 0.5);
                                if (rr < 0)
                                    rr = 0;
                                else if (rr > 255)
                                    rr = 255;
                                if (gg < 0)
                                    gg = 0;
                                else if (gg > 255)
                                    gg = 255;
                                if (bb < 0)
                                    bb = 0;
                                else if (bb > 255)
                                    bb = 255;
                                row[x * 4 + 2] = (guchar)rr;
                                row[x * 4 + 1] = (guchar)gg;
                                row[x * 4 + 0] = (guchar)bb;
                            }

                            row[x * 4 + 3] = 255;
                        }
                    } else {
                        /* Grayscale */
                        double v = 0.0;

                        if (header.bitpix == FITS_BITPIX_INT8 || header.bitpix == -8) {
                            uint8_t* d = (uint8_t*)image_data;
                            uint8_t byte_val = d[p];

                            if (header.bitpix == -8) {
                                v = (double)byte_val;
                            } else {
                                v = (double)(int8_t)byte_val;
                            }
                        } else if (header.bitpix == FITS_BITPIX_INT16) {
                            int16_t* d = (int16_t*)image_data;
                            v = (double)d[p];
                        } else if (header.bitpix == FITS_BITPIX_INT32) {
                            int32_t* d = (int32_t*)image_data;
                            v = (double)d[p];
                        } else {
                            float* d = (float*)image_data;
                            v = (double)d[p];
                        }

                        v = v * header.bscale + header.bzero;

                        {
                            uint8_t gray = 0;
                            if (min_max_calculated) {
                                gray = fits_to_8bit(v, min_val, max_val);
                            }
                            row[x * 4 + 0] = gray;
                            row[x * 4 + 1] = gray;
                            row[x * 4 + 2] = gray;
                            row[x * 4 + 3] = 255;
                        }
                    }
                }
            }
        }
    }

    g_free(image_data);
    fclose(infile);

    cairo_surface_mark_dirty(temp_surface);

    doc->layers = g_list_append(doc->layers, base_layer);
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

/**
 * Initialize FITS plugin
 */
bool plugin_init_fits(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "FITS - Flexible Image Transport System";
    out_plugin->format_info.extensions = "fits,fit,fts";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.supports_hdr = true;
    out_plugin->format_info.priority = 80;

    out_plugin->callbacks.can_load = can_load_fits;
    out_plugin->callbacks.load = load_fits;
    out_plugin->callbacks.can_save = can_save_fits; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
