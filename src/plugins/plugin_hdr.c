#include "plugins/plugin_hdr.h"
#include "app/settings.h"
#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "tone_mapping.h"
#include "ui.h"
#include "ui/dialogs/hdr_image_dialog.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* HDR file signature */
#define HDR_SIGNATURE_RADIANCE "#?RADIANCE"
#define HDR_SIGNATURE_RGBE "#?RGBE"

/* Maximum header size to prevent reading too much */
#define HDR_MAX_HEADER_SIZE 4096

/**
 * Check if file is HDR format
 */
static bool can_load_hdr(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 10) {
        return false;
    }

    /* Check for Radiance signature */
    if (memcmp(header, HDR_SIGNATURE_RADIANCE, 10) == 0) {
        return true;
    }

    /* Check for RGBE signature */
    if (memcmp(header, HDR_SIGNATURE_RGBE, 6) == 0) {
        return true;
    }

    return false;
}

/**
 * Check if plugin can save to HDR format
 * This plugin is read-only for now
 */
static bool can_save_hdr(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Read a line from file (up to max_size bytes)
 * Returns number of bytes read (excluding newline), or -1 on error
 */
static int read_line(FILE* f, char* buffer, size_t max_size) {
    size_t i = 0;
    int c;

    if (!f || !buffer || max_size == 0) {
        return -1;
    }

    while (i < max_size - 1) {
        c = fgetc(f);
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
            c = fgetc(f);
            if (c != '\n' && c != EOF) {
                ungetc(c, f);
            }
            break;
        }
        buffer[i++] = (char)c;
    }

    buffer[i] = '\0';
    return (int)i;
}

/**
 * Parse resolution line (e.g., "-Y 1024 +X 2048", "+X 2048 -Y 1024", etc.)
 * Returns true on success, false on error
 * Handles all orientation variations: +/-X, +/-Y in any order
 */
static bool parse_resolution_line(const char* line, uint32_t* width, uint32_t* height) {
    int x_val = 0, y_val = 0;
    bool found_x = false, found_y = false;
    const char* p = line;

    if (!line || !width || !height) {
        return false;
    }

    /* Parse both dimensions (order can vary) */
    while (*p && (!found_x || !found_y)) {
        /* Skip whitespace */
        while (*p && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (!*p) {
            break;
        }

        /* Check for sign */
        bool negative = false;
        if (*p == '-') {
            negative = true;
            p++;
        } else if (*p == '+') {
            p++;
        }

        /* Check for X or Y */
        if ((*p == 'X' || *p == 'x') && !found_x) {
            p++;
            /* Skip whitespace */
            while (*p && (*p == ' ' || *p == '\t')) {
                p++;
            }
            if (sscanf(p, "%d", &x_val) == 1 && x_val > 0) {
                found_x = true;
                *width = (uint32_t)x_val;
                /* Skip past the number */
                while (*p && (*p >= '0' && *p <= '9')) {
                    p++;
                }
                continue;
            }
        } else if ((*p == 'Y' || *p == 'y') && !found_y) {
            p++;
            /* Skip whitespace */
            while (*p && (*p == ' ' || *p == '\t')) {
                p++;
            }
            if (sscanf(p, "%d", &y_val) == 1 && y_val > 0) {
                found_y = true;
                *height = (uint32_t)y_val;
                /* Skip past the number */
                while (*p && (*p >= '0' && *p <= '9')) {
                    p++;
                }
                continue;
            }
        }

        /* If we get here, we didn't match X or Y, skip to next token */
        while (*p && *p != ' ' && *p != '\t' && *p != '-' && *p != '+') {
            p++;
        }
    }

    return found_x && found_y;
}

/**
 * Convert RGBE pixel to linear RGB float
 */
static void rgbe_to_rgb_float(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float* out_r, float* out_g, float* out_b) {
    if (e == 0) {
        *out_r = *out_g = *out_b = 0.0f;
    } else {
        float f = ldexpf(1.0f, (int)e - 128);
        *out_r = ((float)r + 0.5f) / 256.0f * f;
        *out_g = ((float)g + 0.5f) / 256.0f * f;
        *out_b = ((float)b + 0.5f) / 256.0f * f;
    }
}

/**
 * Decompress a single component of an RLE-encoded scanline
 * Returns number of bytes written, or -1 on error
 * Based on Radiance RGBE RLE format specification (rgbe.c reference implementation)
 */
static int decompress_rle_component(FILE* f, uint8_t* output, uint32_t width) {
    uint32_t x = 0;
    uint8_t buf[2];
    int count;

    while (x < width) {
        /* Read 2-byte packet header */
        if (fread(buf, 1, 2, f) != 2) {
            g_warning("HDR plugin: Unexpected EOF while decompressing RLE component at position %u/%u", x, width);
            return -1;
        }

        if (buf[0] > 128) {
            /* Run: repeat buf[1] (buf[0] - 128) times */
            /* 129 = run of 1, 130 = run of 2, ..., 255 = run of 127 */
            count = buf[0] - 128;

            /* Validate run length */
            if (count == 0 || count > (int)(width - x)) {
                g_warning("HDR plugin: RLE run length %d invalid or exceeds remaining width %u", count, width - x);
                return -1;
            }

            /* Write run value count times */
            for (int i = 0; i < count; i++) {
                output[x++] = buf[1];
            }
        } else {
            /* Non-run: buf[0] is count of literal bytes, buf[1] is first literal */
            count = buf[0];

            /* Validate count */
            if (count == 0 || count > (int)(width - x)) {
                g_warning("HDR plugin: RLE non-run count %d invalid or exceeds remaining width %u", count, width - x);
                return -1;
            }

            /* Write first literal byte */
            output[x++] = buf[1];

            /* Read remaining literal bytes if count > 1 */
            if (--count > 0) {
                if (fread(&output[x], 1, count, f) != (size_t)count) {
                    g_warning("HDR plugin: Failed to read %d literal bytes", count);
                    return -1;
                }
                x += count;
            }
        }
    }

    if (x != width) {
        g_warning("HDR plugin: RLE component decompression incomplete: got %u, expected %u", x, width);
        return -1;
    }

    return (int)(x);
}

/**
 * Read a scanline from HDR file (handles both RLE and uncompressed)
 * Returns true on success, false on error
 */
static bool read_hdr_scanline(FILE* f, uint8_t* output, uint32_t width) {
    uint8_t header[4];
    long file_pos;

    /* Save current file position */
    file_pos = ftell(f);
    if (file_pos < 0) {
        return false;
    }

    /* Read first 4 bytes to check for RLE marker */
    if (fread(header, 1, 4, f) != 4) {
        return false;
    }

    if (header[0] == 0x02 && header[1] == 0x02) {
        /* RLE compressed scanline */
        /* header[2] and header[3] form a big-endian 16-bit width */
        uint32_t scanline_width = ((uint32_t)header[2] << 8) | (uint32_t)header[3];

        /* Use the scanline width from the header, but ensure it doesn't exceed expected width */
        if (scanline_width > width) {
            g_warning("HDR plugin: RLE scanline width %u exceeds expected width %u - using expected width", scanline_width, width);
            scanline_width = width;
        } else if (scanline_width == 0) {
            g_warning("HDR plugin: Invalid RLE scanline width 0");
            return false;
        }

        /* Allocate temporary buffer for component data (use scanline_width, not width) */
        uint8_t* temp_components = g_malloc(scanline_width * 4);
        if (!temp_components) {
            return false;
        }

        /* Decompress each component separately */
        uint8_t* r_component = temp_components + 0;
        uint8_t* g_component = temp_components + scanline_width;
        uint8_t* b_component = temp_components + scanline_width * 2;
        uint8_t* e_component = temp_components + scanline_width * 3;

        if (decompress_rle_component(f, r_component, scanline_width) != (int)scanline_width) {
            g_warning("HDR plugin: Failed to decompress R component");
            g_free(temp_components);
            return false;
        }
        if (decompress_rle_component(f, g_component, scanline_width) != (int)scanline_width) {
            g_warning("HDR plugin: Failed to decompress G component");
            g_free(temp_components);
            return false;
        }
        if (decompress_rle_component(f, b_component, scanline_width) != (int)scanline_width) {
            g_warning("HDR plugin: Failed to decompress B component");
            g_free(temp_components);
            return false;
        }
        if (decompress_rle_component(f, e_component, scanline_width) != (int)scanline_width) {
            g_warning("HDR plugin: Failed to decompress E component");
            g_free(temp_components);
            return false;
        }

        /* Reorganize from component-interleaved to pixel-interleaved */
        /* Copy to output buffer, handling case where scanline_width < width */
        uint32_t copy_width = (scanline_width < width) ? scanline_width : width;
        for (uint32_t i = 0; i < copy_width; i++) {
            output[i * 4 + 0] = r_component[i];
            output[i * 4 + 1] = g_component[i];
            output[i * 4 + 2] = b_component[i];
            output[i * 4 + 3] = e_component[i];
        }

        /* If scanline is shorter than expected, pad with zeros */
        if (scanline_width < width) {
            memset(output + copy_width * 4, 0, (width - copy_width) * 4);
        }

        g_free(temp_components);
    } else {
        /* Uncompressed scanline - read directly */
        /* Seek back to start of scanline */
        if (fseek(f, file_pos, SEEK_SET) != 0) {
            return false;
        }

        /* Read pixel-interleaved RGBE data */
        if (fread(output, 1, width * 4, f) != width * 4) {
            g_warning("HDR plugin: Failed to read uncompressed scanline (expected %u bytes)", width * 4);
            return false;
        }
    }

    return true;
}

/**
 * Load HDR image
 */
static PluginError load_hdr(ImageDocument* doc, const char* filename) {
    FILE* infile;
    char line_buffer[256];
    uint32_t width = 0, height = 0;
    bool found_resolution = false;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    uint8_t* rgbe_data = NULL;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open HDR file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read and parse header */
    bool found_signature = false;
    bool is_rle_format = false;
    int line_count = 0;
    while (line_count < 100) { /* Limit header lines */
        int line_len = read_line(infile, line_buffer, sizeof(line_buffer));
        if (line_len < 0) {
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }

        /* Check for signature */
        if (strncmp(line_buffer, HDR_SIGNATURE_RADIANCE, 10) == 0 ||
            strncmp(line_buffer, HDR_SIGNATURE_RGBE, 6) == 0) {
            found_signature = true;
        }

        /* Check for format specification */
        if (strncmp(line_buffer, "FORMAT=", 7) == 0) {
            if (strstr(line_buffer, "rle") != NULL || strstr(line_buffer, "RLE") != NULL) {
                is_rle_format = true;
            }
        }

        /* Check for resolution line (format: "-Y height +X width" or variations) */
        if (!found_resolution && (line_buffer[0] == '-' || line_buffer[0] == '+')) {
            if (parse_resolution_line(line_buffer, &width, &height)) {
                found_resolution = true;
                break; /* Found resolution, header is done */
            }
        }

        /* Empty line terminates header */
        if (line_len == 0) {
            /* Next line should be resolution */
            line_len = read_line(infile, line_buffer, sizeof(line_buffer));
            if (line_len < 0) {
                fclose(infile);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }
            if (parse_resolution_line(line_buffer, &width, &height)) {
                found_resolution = true;
                break;
            } else {
                fclose(infile);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }
        }

        line_count++;
    }

    if (!found_signature) {
        g_warning("HDR plugin: File does not contain Radiance signature");
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    if (!found_resolution || width == 0 || height == 0) {
        g_warning("HDR plugin: Failed to parse resolution (width=%u, height=%u)", width, height);
        fclose(infile);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    g_message("HDR plugin: Loading %ux%u image (RLE=%s)",
              width, height, is_rle_format ? "yes" : "auto-detect");

    /* Validate dimensions */
    if (width > 65535 || height > 65535) {
        fclose(infile);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Allocate buffer for RGBE data */
    size_t rgbe_size = width * height * 4; /* 4 bytes per pixel (R, G, B, E) */
    rgbe_data = g_malloc(rgbe_size);
    if (!rgbe_data) {
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Read RGBE pixel data (handles both RLE and uncompressed) */
    for (uint32_t y = 0; y < height; y++) {
        uint8_t* scanline = rgbe_data + y * width * 4;
        if (!read_hdr_scanline(infile, scanline, width)) {
            g_warning("HDR plugin: Failed to read scanline %u of %u", y + 1, height);
            g_free(rgbe_data);
            fclose(infile);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
    }

    fclose(infile);

    /* Get tone mapping parameters - check settings first */
    ToneMapParams tone_params;
    tone_map_params_init(&tone_params);

    /* Try to get AppContext from document's drawing_area to access settings */
    AppContext* ctx = NULL;
    Settings* settings = NULL;
    const char* app_dir = NULL;
    gboolean auto_apply_enabled = FALSE;
    
    if (doc && doc->drawing_area) {
        ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
        if (ctx) {
            settings = ctx->settings;
            app_dir = ctx->app_dir;
            if (settings) {
                auto_apply_enabled = settings_get_tone_map_auto_apply(settings);
                
                /* Always load settings from saved values (for dialog to show last used, or to use if auto_apply) */
                tone_params.operator = (ToneMapOperator)settings_get_tone_map_operator(settings);
                tone_params.normalize = (ToneMapNormalize)settings_get_tone_map_normalize(settings);
                tone_params.gamma = (float)settings_get_tone_map_gamma(settings);
                tone_params.exposure = (float)settings_get_tone_map_exposure(settings);
                tone_params.white_point = (float)settings_get_tone_map_white_point(settings);
                tone_params.intensity = (float)settings_get_tone_map_intensity(settings);
                tone_params.adaptation = (float)settings_get_tone_map_adaptation(settings);
                tone_params.color_correction = (float)settings_get_tone_map_color_correction(settings);
            }
        }
    }

    /* Get parent window from AppContext if available */
    GtkWindow* parent_window = NULL;
    if (ctx && ctx->window) {
        parent_window = GTK_WINDOW(ctx->window);
    }

    /* Show tone mapping dialog with preview BEFORE creating document */
    /* Skip dialog if auto apply is enabled */
    gboolean auto_apply = FALSE;
    gint dialog_response = GTK_RESPONSE_OK;
    
    if (!auto_apply_enabled) {
        /* Show dialog */
        dialog_response = hdr_image_dialog_show(parent_window, &tone_params, &auto_apply, 
                                                rgbe_data, width, height, settings, app_dir);
        
        /* If dialog was canceled, cancel loading without modifying document */
        if (dialog_response != GTK_RESPONSE_OK) {
            g_free(rgbe_data);
            return PLUGIN_ERROR_UNSUPPORTED_FEATURE; /* User canceled */
        }
    } else {
        /* Auto apply is enabled - use saved settings without showing dialog */
        g_message("HDR plugin: Auto-apply enabled, using saved tone mapping settings");
    }

    /* User confirmed dialog - now create/modify document */
    /* Set document metadata */
    doc->width = width;
    doc->height = height;
    doc->channels = 3; /* RGB (no alpha in HDR) */
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
        g_free(rgbe_data);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get surface data */
    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        g_free(rgbe_data);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        g_free(rgbe_data);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert RGBE to ARGB32 */
    for (uint32_t y = 0; y < height; y++) {
        guchar* dst_row = surface_data + y * surface_stride;
        uint8_t* src_row = rgbe_data + y * width * 4;

        for (uint32_t x = 0; x < width; x++) {
            uint8_t r_gbe = src_row[x * 4 + 0];
            uint8_t g_gbe = src_row[x * 4 + 1];
            uint8_t b_gbe = src_row[x * 4 + 2];
            uint8_t e_gbe = src_row[x * 4 + 3];

            /* Convert RGBE to linear RGB float */
            float r_float, g_float, b_float;
            rgbe_to_rgb_float(r_gbe, g_gbe, b_gbe, e_gbe, &r_float, &g_float, &b_float);

            /* Tone map to 8-bit using tone mapping handler */
            uint8_t r, g, b;
            tone_map_rgb(r_float, g_float, b_float, &tone_params, &r, &g, &b);
            uint8_t a = 255; /* No alpha in HDR */

            /* Cairo ARGB32: BGRA in memory (little-endian) */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
        }
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(temp_surface);

    /* Cleanup */
    g_free(rgbe_data);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Render composite */
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

/**
 * Initialize HDR plugin
 */
bool plugin_init_hdr(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "HDR - Radiance RGBE";
    out_plugin->format_info.extensions = "hdr,pic";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.supports_hdr = true;
    out_plugin->format_info.priority = 70;

    out_plugin->callbacks.can_load = can_load_hdr;
    out_plugin->callbacks.load = load_hdr;
    out_plugin->callbacks.can_save = can_save_hdr; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
