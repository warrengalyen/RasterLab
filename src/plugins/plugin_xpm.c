#include "plugins/plugin_xpm.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

/* XPM color entry structure */
typedef struct {
    char* symbol;       /* Symbol string (cpp characters) */
    uint8_t r, g, b, a; /* RGBA color values */
    bool has_color;     /* Whether color is defined */
} XPMColorEntry;

/* XPM parser state */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t num_colors;
    uint32_t cpp; /* Characters per pixel */
    bool has_hotspot;
    uint32_t x_hot, y_hot;
    bool has_extensions;
    XPMColorEntry* color_table;
    uint32_t color_table_size;
} XPMState;

/**
 * Check if file is XPM format
 */
static bool can_load_xpm(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 6) {
        return false;
    }

    /* Check for XPM magic: "/* XPM" or "! XPM2" */
    if (header_size >= 6 && memcmp(header, "/* XPM", 6) == 0) {
        return true;
    }
    if (header_size >= 6 && memcmp(header, "! XPM2", 6) == 0) {
        return true;
    }

    return false;
}

/**
 * Check if plugin can save to XPM format
 * This plugin is read-only, so always return false
 */
static bool can_save_xpm(const char* filename) {
    (void)filename; /* Unused */
    return false;   /* Read-only plugin, saving not supported */
}

/**
 * Skip whitespace in a string
 */
static const char* skip_whitespace(const char* str) {
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

/**
 * Read a quoted string from XPM line
 * Returns pointer to start of string content (after opening quote) and length
 */
static bool read_quoted_string(const char* line, char** out_str, size_t* out_len) {
    const char* start = strchr(line, '"');
    if (!start) {
        return false;
    }
    start++; /* Skip opening quote */

    const char* end = strchr(start, '"');
    if (!end) {
        return false;
    }

    size_t len = end - start;
    *out_str = g_strndup(start, len);
    *out_len = len;
    return true;
}

/**
 * Parse color value (hex, X11 name, or "None")
 */
static bool parse_color_value(const char* color_str, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (!color_str || !r || !g || !b || !a) {
        return false;
    }

    /* Check for transparency */
    if (g_ascii_strcasecmp(color_str, "None") == 0 || g_ascii_strcasecmp(color_str, "none") == 0) {
        *r = *g = *b = 0;
        *a = 0; /* Transparent */
        return true;
    }

    /* Check for hex color (#RRGGBB or #RGB) */
    if (color_str[0] == '#') {
        int hex_len = strlen(color_str + 1);
        if (hex_len == 6) {
            /* Full hex: #RRGGBB */
            char hex_r[3] = {color_str[1], color_str[2], '\0'};
            char hex_g[3] = {color_str[3], color_str[4], '\0'};
            char hex_b[3] = {color_str[5], color_str[6], '\0'};
            *r = (uint8_t)strtoul(hex_r, NULL, 16);
            *g = (uint8_t)strtoul(hex_g, NULL, 16);
            *b = (uint8_t)strtoul(hex_b, NULL, 16);
            *a = 255;
            return true;
        } else if (hex_len == 3) {
            /* Short hex: #RGB */
            char hex_r[2] = {color_str[1], '\0'};
            char hex_g[2] = {color_str[2], '\0'};
            char hex_b[2] = {color_str[3], '\0'};
            *r = (uint8_t)(strtoul(hex_r, NULL, 16) * 17);
            *g = (uint8_t)(strtoul(hex_g, NULL, 16) * 17);
            *b = (uint8_t)(strtoul(hex_b, NULL, 16) * 17);
            *a = 255;
            return true;
        }
    }

    /* Try X11 color names (basic set) */
    /* For a full implementation, you'd want a complete X11 color database */
    /* Here we handle some common ones */
    if (g_ascii_strcasecmp(color_str, "black") == 0) {
        *r = *g = *b = 0;
        *a = 255;
        return true;
    }
    if (g_ascii_strcasecmp(color_str, "white") == 0) {
        *r = *g = *b = 255;
        *a = 255;
        return true;
    }
    if (g_ascii_strcasecmp(color_str, "red") == 0) {
        *r = 255;
        *g = *b = 0;
        *a = 255;
        return true;
    }
    if (g_ascii_strcasecmp(color_str, "green") == 0) {
        *g = 255;
        *r = *b = 0;
        *a = 255;
        return true;
    }
    if (g_ascii_strcasecmp(color_str, "blue") == 0) {
        *b = 255;
        *r = *g = 0;
        *a = 255;
        return true;
    }

    /* Default: black if we can't parse */
    *r = *g = *b = 0;
    *a = 255;
    return false;
}

/**
 * Parse values line from XPM
 * Format: "width height num_colors cpp [x_hot y_hot] [XPMEXT]"
 */
static bool parse_xpm_values_line(const char* line, XPMState* state) {
    char* str = NULL;
    size_t len = 0;
    if (!read_quoted_string(line, &str, &len)) {
        return false;
    }

    /* Parse integers from the string */
    int width, height, num_colors, cpp;
    int x_hot = 0, y_hot = 0;
    int parsed = sscanf(str, "%d %d %d %d %d %d", &width, &height, &num_colors, &cpp, &x_hot, &y_hot);

    if (parsed < 4) {
        g_free(str);
        return false;
    }

    state->width = (uint32_t)width;
    state->height = (uint32_t)height;
    state->num_colors = (uint32_t)num_colors;
    state->cpp = (uint32_t)cpp;
    state->has_hotspot = (parsed >= 6);
    if (state->has_hotspot) {
        state->x_hot = (uint32_t)x_hot;
        state->y_hot = (uint32_t)y_hot;
    }

    /* Check for XPMEXT marker */
    state->has_extensions = (strstr(str, "XPMEXT") != NULL);

    g_free(str);
    return true;
}

/**
 * Parse color definition line
 * Format: "<symbol> c <color> [other attributes]"
 */
static bool parse_xpm_color_line(const char* line, XPMState* state, uint32_t color_index) {
    char* str = NULL;
    size_t len = 0;
    if (!read_quoted_string(line, &str, &len)) {
        return false;
    }

    if (color_index >= state->num_colors) {
        g_free(str);
        return false;
    }

    XPMColorEntry* entry = &state->color_table[color_index];

    /* Extract symbol (first cpp characters) */
    if (len < state->cpp) {
        g_free(str);
        return false;
    }

    entry->symbol = g_strndup(str, state->cpp);

    /* Find color attribute (c <color>) */
    const char* c_attr = strstr(str, " c ");
    if (!c_attr) {
        /* Try without space: "c " */
        c_attr = strstr(str, "c ");
    }
    if (!c_attr) {
        /* Try with tab */
        c_attr = strstr(str, "\tc");
    }

    if (c_attr) {
        /* Skip " c " */
        const char* color_start = c_attr + 2;
        while (*color_start && isspace((unsigned char)*color_start)) {
            color_start++;
        }

        /* Extract color value (until whitespace or end) */
        const char* color_end = color_start;
        while (*color_end && !isspace((unsigned char)*color_end)) {
            color_end++;
        }

        size_t color_len = color_end - color_start;
        char* color_str = g_strndup(color_start, color_len);

        entry->has_color = parse_color_value(color_str, &entry->r, &entry->g, &entry->b, &entry->a);
        g_free(color_str);
    } else {
        /* No color attribute found, default to black */
        entry->has_color = false;
        entry->r = entry->g = entry->b = 0;
        entry->a = 255;
    }

    g_free(str);
    return true;
}

/**
 * Find color entry by symbol
 */
static XPMColorEntry* find_color_by_symbol(XPMState* state, const char* symbol) {
    for (uint32_t i = 0; i < state->num_colors; i++) {
        if (state->color_table[i].symbol &&
            memcmp(state->color_table[i].symbol, symbol, state->cpp) == 0) {
            return &state->color_table[i];
        }
    }
    return NULL;
}

/**
 * Free XPM state
 */
static void free_xpm_state(XPMState* state) {
    if (!state) {
        return;
    }

    if (state->color_table) {
        for (uint32_t i = 0; i < state->color_table_size; i++) {
            g_free(state->color_table[i].symbol);
        }
        g_free(state->color_table);
    }
    memset(state, 0, sizeof(XPMState));
}

/**
 * Load XPM image
 */
static PluginError load_xpm(ImageDocument* doc, const char* filename) {
    FILE* infile;
    char line[8192]; /* Buffer for reading lines */
    XPMState state = {0};
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    bool found_header = false;
    bool found_array_start = false;
    bool found_values = false;
    uint32_t colors_read = 0;
    uint32_t pixels_read = 0;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open XPM file */
    infile = g_fopen(filename, "r");
    if (!infile) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Read file line by line */
    while (fgets(line, sizeof(line), infile) != NULL) {
        const char* trimmed = skip_whitespace(line);

        /* Skip empty lines */
        if (*trimmed == '\0' || *trimmed == '\n') {
            continue;
        }

        /* Look for XPM header comment or XPM2 format */
        if (!found_header) {
            if (strstr(trimmed, "/* XPM") != NULL || strstr(trimmed, "! XPM2") != NULL) {
                found_header = true;
                /* XPM2 format - try to parse values immediately */
                if (strstr(trimmed, "! XPM2") != NULL) {
                    found_array_start = true; /* XPM2 doesn't have array declaration */
                    /* Try to parse values line if it's in the header */
                    if (strchr(trimmed, '"') != NULL) {
                        if (parse_xpm_values_line(trimmed, &state)) {
                            found_values = true;
                            /* Validate and allocate */
                            if (state.width > 0 && state.height > 0 &&
                                state.num_colors > 0 && state.cpp > 0 &&
                                state.cpp <= 10) {
                                state.color_table = g_malloc0(sizeof(XPMColorEntry) * state.num_colors);
                                state.color_table_size = state.num_colors;
                            }
                        }
                    }
                }
                continue;
            }
        }

        /* Look for array declaration start (XPM3 only) */
        if (found_header && !found_array_start) {
            if (strstr(trimmed, "static char") != NULL) {
                found_array_start = true;
                /* Check if opening brace is on same line */
                if (strchr(trimmed, '{') != NULL) {
                    /* Opening brace on same line, continue to next line */
                    continue;
                }
                /* Opening brace might be on next line, continue */
                continue;
            }
            /* Also check for opening brace separately (in case declaration spans lines) */
            if (strchr(trimmed, '{') != NULL) {
                found_array_start = true;
                continue;
            }
        }

        /* Parse values line */
        if (found_array_start && !found_values) {
            if (strchr(trimmed, '"') != NULL) {
                if (parse_xpm_values_line(trimmed, &state)) {
                    found_values = true;

                    /* Validate values */
                    if (state.width == 0 || state.height == 0 ||
                        state.num_colors == 0 || state.cpp == 0 ||
                        state.cpp > 10) {
                        free_xpm_state(&state);
                        fclose(infile);
                        return PLUGIN_ERROR_CORRUPT_FILE;
                    }

                    /* Allocate color table */
                    state.color_table = g_malloc0(sizeof(XPMColorEntry) * state.num_colors);
                    state.color_table_size = state.num_colors;
                    continue;
                }
            }
        }

        /* Parse color definitions */
        if (found_values && colors_read < state.num_colors) {
            if (strchr(trimmed, '"') != NULL) {
                if (parse_xpm_color_line(trimmed, &state, colors_read)) {
                    colors_read++;
                    continue;
                }
            }
        }

        /* Parse pixel data */
        if (found_values && colors_read == state.num_colors && pixels_read < state.height) {
            if (strchr(trimmed, '"') != NULL) {
                char* pixel_str = NULL;
                size_t pixel_len = 0;
                if (read_quoted_string(trimmed, &pixel_str, &pixel_len)) {
                    /* Check if this is pixel data (not extension) */
                    /* Pixel data should be at least width * cpp characters */
                    /* But allow for slight variations (trailing spaces trimmed by read_quoted_string) */
                    if (pixel_len >= state.width * state.cpp &&
                        strncmp(pixel_str, "XPMEXT", 6) != 0 &&
                        !(pixel_len == 6 && strcmp(pixel_str, "XPMEXT") == 0)) {
                        /* This is pixel data */
                        if (pixels_read == 0) {
                            /* First pixel row - create layer now */
                            doc->width = state.width;
                            doc->height = state.height;
                            doc->channels = 4;
                            doc->bit_depth = 8;
                            doc->has_alpha = true; /* XPM can have transparency */

                            /* Free old layers */
                            for (GList* iter = doc->layers; iter; iter = iter->next) {
                                layer_free((ImageLayer*)iter->data);
                            }
                            g_list_free(doc->layers);
                            doc->layers = NULL;

                            /* Create base layer */
                            base_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                                                   LAYER_BACKGROUND_TRANSPARENT,
                                                   LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
                            if (!base_layer) {
                                free_xpm_state(&state);
                                g_free(pixel_str);
                                fclose(infile);
                                return PLUGIN_ERROR_OUT_OF_MEMORY;
                            }

                            /* Get surface data */
                            temp_surface = base_layer->surface;
                            if (!temp_surface) {
                                layer_free(base_layer);
                                free_xpm_state(&state);
                                g_free(pixel_str);
                                fclose(infile);
                                return PLUGIN_ERROR_OUT_OF_MEMORY;
                            }

                            cairo_surface_flush(temp_surface);
                            surface_data = cairo_image_surface_get_data(temp_surface);
                            surface_stride = cairo_image_surface_get_stride(temp_surface);

                            if (!surface_data) {
                                layer_free(base_layer);
                                free_xpm_state(&state);
                                g_free(pixel_str);
                                fclose(infile);
                                return PLUGIN_ERROR_OUT_OF_MEMORY;
                            }
                        }

                        /* Process pixel row */
                        guchar* row = surface_data + pixels_read * surface_stride;
                        for (uint32_t x = 0; x < state.width; x++) {
                            uint32_t symbol_offset = x * state.cpp;
                            if (symbol_offset + state.cpp > pixel_len) {
                                break;
                            }

                            /* Extract symbol */
                            char* symbol = g_strndup(pixel_str + symbol_offset, state.cpp);

                            /* Look up color */
                            XPMColorEntry* color = find_color_by_symbol(&state, symbol);
                            if (color) {
                                /* Convert to Cairo ARGB32 (BGRA in memory) */
                                row[x * 4 + 0] = color->b;
                                row[x * 4 + 1] = color->g;
                                row[x * 4 + 2] = color->r;
                                row[x * 4 + 3] = color->a;
                            } else {
                                /* Unknown symbol - use transparent */
                                row[x * 4 + 0] = 0;
                                row[x * 4 + 1] = 0;
                                row[x * 4 + 2] = 0;
                                row[x * 4 + 3] = 0;
                            }

                            g_free(symbol);
                        }

                        pixels_read++;
                        g_free(pixel_str);

                        /* If we've read all pixels, we're done */
                        if (pixels_read >= state.height) {
                            break;
                        }
                    } else {
                        /* Extension or other data - skip for now */
                        g_free(pixel_str);
                    }
                }
            }
        }

        /* Check for array end - but only if we've read all the data */
        /* Don't break early if we haven't finished reading pixels */
        if ((strstr(trimmed, "};") != NULL || strstr(trimmed, "}") != NULL) &&
            pixels_read >= state.height) {
            break;
        }
    }

    /* Close file */
    fclose(infile);

    /* Validate we read everything */
    if (!found_values || colors_read != state.num_colors || pixels_read != state.height) {
        if (base_layer) {
            layer_free(base_layer);
        }
        free_xpm_state(&state);
        debug_log("WRN", "Invalid XPM values: width: %u, height: %u, num_colors: %u, cpp: %u, found_values: %d, colors_read: %u, pixels_read: %u\n",
                  state.width, state.height, state.num_colors, state.cpp, found_values, colors_read, pixels_read);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Cleanup */
    free_xpm_state(&state);

    if (!base_layer) {
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(temp_surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Render composite */
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

/**
 * Initialize XPM plugin
 */
bool plugin_init_xpm(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host; /* Host API not needed for this plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "XPM - X PixMap";
    out_plugin->format_info.extensions = "xpm";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_xpm;
    out_plugin->callbacks.load = load_xpm;
    out_plugin->callbacks.can_save = can_save_xpm; /* Read-only plugin, saving not supported */
    out_plugin->callbacks.save = NULL;

    return true;
}
