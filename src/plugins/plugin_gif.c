/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/*
 * GIF plugin
 * Supports GIF87a and GIF89a (static and animated).
 *
 * Loading:  full LZW decompression, interlace de-interlacing, transparency,
 *           per-frame disposal methods.  Each GIF frame becomes a layer.
 *
 * Saving:   LZW compression (12-bit variable-width codes, hash-table encoder),
 *           median-cut palette quantisation, optional single transparent index.
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Save-option types (forward-declared here so the dialog can reuse them)
 * ====================================================================== */

typedef enum {
    GIF_COLOR_MODEL_AUTO = 0,
    GIF_COLOR_MODEL_COLOR = 1,
    GIF_COLOR_MODEL_GRAYSCALE = 2
} GIFColorModel;

typedef enum {
    GIF_TRANSPARENCY_AUTO = 0,
    GIF_TRANSPARENCY_NONE = 1,
    GIF_TRANSPARENCY_BY_CUTOFF = 2,
    GIF_TRANSPARENCY_BY_COLOR = 3
} GIFTransparency;

typedef struct {
    GIFColorModel color_model; /* auto / color / grayscale */
    int palette_size;          /* 2–256, used when NOT auto */
    uint8_t bg_color_r;        /* background color (compositing) */
    uint8_t bg_color_g;
    uint8_t bg_color_b;
    GIFTransparency transparency; /* auto / none / by cut-off / by color */
    uint8_t alpha_cutoff;         /* 0–254, used for BY_CUTOFF */
    uint8_t transparent_color_r;  /* used for BY_COLOR */
    uint8_t transparent_color_g;
    uint8_t transparent_color_b;
    uint32_t reserved[4];
} GIFSaveOptions;

/* =========================================================================
 * GIF signature check
 * ====================================================================== */

static const uint8_t GIF87A[6] = {'G', 'I', 'F', '8', '7', 'a'};
static const uint8_t GIF89A[6] = {'G', 'I', 'F', '8', '9', 'a'};

static bool can_load_gif(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;
    if (!header || header_size < 6)
        return false;
    return memcmp(header, GIF87A, 6) == 0 || memcmp(header, GIF89A, 6) == 0;
}

static bool can_save_gif(const char* filename) {
    if (!filename)
        return false;
    const char* ext = strrchr(filename, '.');
    if (!ext)
        return false;
    return g_ascii_strcasecmp(ext + 1, "gif") == 0;
}

/* =========================================================================
 * Save-options callbacks
 * ====================================================================== */

static size_t get_gif_save_options_size(void) {
    return sizeof(GIFSaveOptions);
}

static void init_gif_save_options(void* plugin_data) {
    GIFSaveOptions* o = (GIFSaveOptions*)plugin_data;
    if (!o)
        return;
    o->color_model = GIF_COLOR_MODEL_AUTO;
    o->palette_size = 256;
    o->bg_color_r = 255;
    o->bg_color_g = 255;
    o->bg_color_b = 255;
    o->transparency = GIF_TRANSPARENCY_AUTO;
    o->alpha_cutoff = 64;
    o->transparent_color_r = 255;
    o->transparent_color_g = 0;
    o->transparent_color_b = 255;
    memset(o->reserved, 0, sizeof(o->reserved));
}

/* =========================================================================
 * Low-level file helpers
 * ====================================================================== */

static bool gif_read_byte(FILE* f, uint8_t* out) {
    int c = fgetc(f);
    if (c == EOF)
        return false;
    *out = (uint8_t)c;
    return true;
}

static bool gif_read_u16le(FILE* f, uint16_t* out) {
    uint8_t lo, hi;
    if (!gif_read_byte(f, &lo) || !gif_read_byte(f, &hi))
        return false;
    *out = (uint16_t)(lo | (hi << 8));
    return true;
}

static bool gif_write_byte(FILE* f, uint8_t v) {
    return fputc(v, f) != EOF;
}

static bool gif_write_u16le(FILE* f, uint16_t v) {
    return gif_write_byte(f, (uint8_t)(v & 0xFF)) &&
           gif_write_byte(f, (uint8_t)((v >> 8) & 0xFF));
}

/* Skip over a chain of GIF sub-blocks */
static bool gif_skip_sub_blocks(FILE* f) {
    for (;;) {
        uint8_t block_size;
        if (!gif_read_byte(f, &block_size))
            return false;
        if (block_size == 0)
            return true;
        if (fseek(f, block_size, SEEK_CUR) != 0)
            return false;
    }
}

/* Read a chain of GIF sub-blocks into a flat buffer.
   The returned buffer is g_malloc'd; caller must g_free it. */
static uint8_t* gif_read_sub_blocks(FILE* f, size_t* out_size) {
    GByteArray* arr = g_byte_array_new();
    for (;;) {
        uint8_t block_size;
        if (!gif_read_byte(f, &block_size)) {
            g_byte_array_free(arr, TRUE);
            return NULL;
        }
        if (block_size == 0)
            break;
        uint8_t buf[255];
        if (fread(buf, 1, block_size, f) != block_size) {
            g_byte_array_free(arr, TRUE);
            return NULL;
        }
        g_byte_array_append(arr, buf, block_size);
    }
    *out_size = arr->len;
    uint8_t* data = (uint8_t*)g_memdup2(arr->data, arr->len > 0 ? arr->len : 1);
    g_byte_array_free(arr, TRUE);
    return data;
}

/*
 * Count 0x2C image blocks from current file position to trailer. Restores
 * file position. Returns -1 on parse error, otherwise number of image frames.
 */
static int gif_count_image_frames_in_stream(FILE* f) {
    long start = ftell(f);
    if (start < 0)
        return -1;

    int count = 0;
    for (;;) {
        uint8_t block_type;
        if (!gif_read_byte(f, &block_type))
            break;
        if (block_type == 0x3B)
            break;
        if (block_type == 0x21) {
            uint8_t ext_label;
            if (!gif_read_byte(f, &ext_label)) {
                fseek(f, start, SEEK_SET);
                return -1;
            }
            if (!gif_skip_sub_blocks(f)) {
                fseek(f, start, SEEK_SET);
                return -1;
            }
            (void)ext_label;
            continue;
        }
        if (block_type == 0x2C) {
            count++;
            uint16_t fr_left, fr_top, fr_w, fr_h;
            uint8_t packed_id;
            if (!gif_read_u16le(f, &fr_left) || !gif_read_u16le(f, &fr_top) ||
                !gif_read_u16le(f, &fr_w) || !gif_read_u16le(f, &fr_h) || !gif_read_byte(f, &packed_id)) {
                fseek(f, start, SEEK_SET);
                return -1;
            }
            bool has_lct = (packed_id & 0x80) != 0;
            int lct_size = has_lct ? (2 << (packed_id & 0x07)) : 0;
            if (has_lct && fseek(f, lct_size * 3, SEEK_CUR) != 0) {
                fseek(f, start, SEEK_SET);
                return -1;
            }
            uint8_t lzw_min;
            if (!gif_read_byte(f, &lzw_min) || lzw_min < 2 || lzw_min > 8) {
                fseek(f, start, SEEK_SET);
                return -1;
            }
            if (!gif_skip_sub_blocks(f)) {
                fseek(f, start, SEEK_SET);
                return -1;
            }
            continue;
        }
        if (!gif_skip_sub_blocks(f)) {
            fseek(f, start, SEEK_SET);
            return -1;
        }
    }

    if (fseek(f, start, SEEK_SET) != 0)
        return -1;
    return count;
}

/* =========================================================================
 * LZW decompressor
 * ====================================================================== */

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t byte_pos;
    uint32_t bit_buf;
    int bit_count;
} BitReader;

static void br_init(BitReader* r, const uint8_t* data, size_t size) {
    r->data = data;
    r->size = size;
    r->byte_pos = 0;
    r->bit_buf = 0;
    r->bit_count = 0;
}

/* Read n bits (LSB-first). Returns -1 on end-of-data. */
static int br_read(BitReader* r, int n) {
    while (r->bit_count < n) {
        if (r->byte_pos >= r->size)
            return -1;
        r->bit_buf |= ((uint32_t)r->data[r->byte_pos++] << r->bit_count);
        r->bit_count += 8;
    }
    int val = (int)(r->bit_buf & ((1u << n) - 1));
    r->bit_buf >>= n;
    r->bit_count -= n;
    return val;
}

/*
 * Decode LZW-compressed GIF image data.
 * data / data_size: flat sub-block content (without sub-block size bytes).
 * min_code_size: from the LZW Minimum Code Size field in the image data.
 * pixel_count: expected number of output pixels.
 * Returns g_malloc'd pixel array (caller must g_free), or NULL on error.
 */
static uint8_t* gif_lzw_decode(const uint8_t* data, size_t data_size,
                               int min_code_size, int pixel_count) {
    if (min_code_size < 2 || min_code_size > 8 || pixel_count <= 0)
        return NULL;

    uint8_t* output = (uint8_t*)g_malloc((gsize)pixel_count);
    if (!output)
        return NULL;

    /* Code table */
    int table_prefix[4096];
    uint8_t table_suffix[4096];

    int clear_code = 1 << min_code_size;
    int eoi_code = clear_code + 1;
    int code_size = min_code_size + 1;
    int next_code = eoi_code + 1;
    int output_pos = 0;
    int prev_code = -1;

    /* Initialise root codes */
    for (int i = 0; i < clear_code; i++) {
        table_prefix[i] = -1;
        table_suffix[i] = (uint8_t)i;
    }

    BitReader br;
    br_init(&br, data, data_size);

    bool started = false;

    for (;;) {
        int code = br_read(&br, code_size);
        if (code < 0)
            break;

        if (code == clear_code) {
            code_size = min_code_size + 1;
            next_code = eoi_code + 1;
            prev_code = -1;
            started = false;
            continue;
        }
        if (code == eoi_code)
            break;

        /* First code after a clear must be a root code */
        if (!started) {
            if (code < 0 || code >= clear_code)
                break;
            if (output_pos < pixel_count)
                output[output_pos++] = table_suffix[code];
            prev_code = code;
            started = true;
            continue;
        }

        /* Determine which code to follow down the prefix chain */
        bool kwkwk = (code == next_code && prev_code >= 0);
        int decode_src = kwkwk ? prev_code : code;

        if (!kwkwk && (code < 0 || code >= next_code))
            break; /* invalid */

        /* Follow prefix chain, pushing chars in reverse order */
        uint8_t tmp[4096];
        int n = 0;
        int c = decode_src;
        while (n < 4096) {
            if (c < 0 || (c >= next_code && !kwkwk))
                break;
            tmp[n++] = table_suffix[c];
            if (table_prefix[c] < 0)
                break; /* root reached */
            c = table_prefix[c];
        }

        /* Reverse into forward order */
        uint8_t seq[4096];
        for (int i = 0; i < n; i++)
            seq[i] = tmp[n - 1 - i];
        uint8_t first_char = (n > 0) ? seq[0] : 0;

        if (kwkwk && n < 4096) {
            seq[n++] = first_char;
        } /* KwKwK append */

        /* Emit */
        for (int i = 0; i < n && output_pos < pixel_count; i++)
            output[output_pos++] = seq[i];

        /* Add new table entry */
        if (prev_code >= 0 && next_code < 4096) {
            table_prefix[next_code] = prev_code;
            table_suffix[next_code] = first_char;
            next_code++;
            if (next_code >= (1 << code_size) && code_size < 12)
                code_size++;
        }

        prev_code = code;
    }

    if (output_pos < pixel_count)
        memset(output + output_pos, 0, (gsize)(pixel_count - output_pos));

    return output;
}

/* =========================================================================
 * GIF loader
 * ====================================================================== */

/* De-interlace pixel data in-place. The compressed rows arrive in pass order;
   this function maps them to their correct scan-line positions. */
static void gif_deinterlace(uint8_t* pixels, int width, int height) {
    static const int starts[4] = {0, 4, 2, 1};
    static const int steps[4] = {8, 8, 4, 2};

    uint8_t* temp = (uint8_t*)g_malloc((gsize)(width * height));
    if (!temp)
        return;
    memcpy(temp, pixels, (gsize)(width * height));

    int src_row = 0;
    for (int pass = 0; pass < 4; pass++) {
        for (int y = starts[pass]; y < height; y += steps[pass]) {
            memcpy(pixels + y * width, temp + src_row * width, (gsize)width);
            src_row++;
        }
    }
    g_free(temp);
}

static PluginError load_gif(ImageDocument* doc, const char* filename) {
    FILE* f = NULL;
    PluginError result = PLUGIN_ERROR_NONE;
    ImageFormatHostAPI* host = plugin_host_api_get();
    gboolean load_progress_shown = FALSE;
    int total_image_frames = 0;

    /* Per-frame compositing canvas (RGBA order, straight alpha) */
    uint8_t* canvas = NULL;
    uint8_t* prev_canvas = NULL;
    guint32 pending_frame_delay_ms = 0;

    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    f = g_fopen(filename, "rb");
    if (!f)
        return PLUGIN_ERROR_FILE_NOT_FOUND;

    /* --- Header -------------------------------------------------------- */
    uint8_t sig[6];
    if (fread(sig, 1, 6, f) != 6 ||
        (memcmp(sig, GIF87A, 6) != 0 && memcmp(sig, GIF89A, 6) != 0)) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* --- Logical Screen Descriptor ------------------------------------- */
    uint16_t canvas_w, canvas_h;
    uint8_t packed_lsd, bg_idx, aspect;
    if (!gif_read_u16le(f, &canvas_w) || !gif_read_u16le(f, &canvas_h) ||
        !gif_read_byte(f, &packed_lsd) || !gif_read_byte(f, &bg_idx) ||
        !gif_read_byte(f, &aspect)) {
        fclose(f);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    int width = (int)canvas_w;
    int height = (int)canvas_h;
    if (width == 0 || height == 0) {
        fclose(f);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* --- Global Color Table ------------------------------------------ */
    bool has_gct = (packed_lsd & 0x80) != 0;
    int gct_size = has_gct ? (2 << (packed_lsd & 0x07)) : 0;

    uint8_t gct[256 * 3];
    memset(gct, 0, sizeof(gct));
    if (has_gct && fread(gct, 3, (size_t)gct_size, f) != (size_t)gct_size) {
        fclose(f);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    total_image_frames = gif_count_image_frames_in_stream(f);
    if (total_image_frames < 0) {
        fclose(f);
        return PLUGIN_ERROR_CORRUPT_FILE;
    } /* 0 is allowed (parsing will fail if no images); count only errors on parse. */

    /* --- Initialize compositing canvas --------------------------------- */
    canvas = (uint8_t*)g_malloc0((gsize)(width * height * 4));
    prev_canvas = (uint8_t*)g_malloc0((gsize)(width * height * 4));
    if (!canvas || !prev_canvas) {
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    /* Canvas starts fully transparent (g_malloc0 already zeroed it to 0,0,0,0).
       Transparent pixels in GIF frames are preserved as a=0 in the output layers,
       matching the layer-editor convention where transparency is composited by the
       application (like GIMP).  We do NOT pre-fill with the GIF background colour —
       that background concept belongs to the animation playback loop, not to a
       static layer stack. */

    /* --- Free existing layers ----------------------------------------- */
    for (GList* it = doc->layers; it; it = it->next)
        layer_free((ImageLayer*)it->data);
    g_list_free(doc->layers);
    doc->layers = NULL;

    doc->width = (uint32_t)width;
    doc->height = (uint32_t)height;
    doc->channels = 4;
    doc->bit_depth = 8;
    doc->has_alpha = true;

    {
        gboolean want_progress = host && host->load_progress_show && host->load_progress_set && host->load_progress_hide &&
                                 total_image_frames >= 2;
        if (want_progress) {
            gchar* base = g_path_get_basename(filename);
            gchar* msg = g_strdup_printf(_("Loading %s - frame 1 of %d…"), base ? base : filename, total_image_frames);
            host->load_progress_show(msg, 0.0);
            g_free(msg);
            g_free(base);
            load_progress_shown = TRUE;
        }
    }

    /* --- Main parsing loop --------------------------------------------- */
    int frame_index = 0;
    int disposal = 0;     /* from GCE – reset per frame */
    int transparent = -1; /* transparent color index, -1 = none */

    for (;;) {
        uint8_t block_type;
        if (!gif_read_byte(f, &block_type))
            break;

        if (block_type == 0x3B)
            break; /* Trailer */

        if (block_type == 0x21) {
            /* --- Extension -------------------------------------------- */
            uint8_t ext_label;
            if (!gif_read_byte(f, &ext_label)) {
                result = PLUGIN_ERROR_CORRUPT_FILE;
                goto cleanup;
            }

            if (ext_label == 0xF9) {
                /* Graphic Control Extension */
                uint8_t blk, packed_gce, dl, dh, ti;
                if (!gif_read_byte(f, &blk) || blk < 4 ||
                    !gif_read_byte(f, &packed_gce) ||
                    !gif_read_byte(f, &dl) || !gif_read_byte(f, &dh) ||
                    !gif_read_byte(f, &ti)) {
                    result = PLUGIN_ERROR_CORRUPT_FILE;
                    goto cleanup;
                }
                uint8_t term;
                gif_read_byte(f, &term); /* block terminator */
                (void)term;
                {
                    uint16_t delay_centi = (uint16_t)dl | ((uint16_t)dh << 8);
                    pending_frame_delay_ms = (guint32)delay_centi * 10u;
                }
                disposal = (packed_gce >> 2) & 0x07;
                transparent = (packed_gce & 0x01) ? (int)ti : -1;
            } else {
                if (!gif_skip_sub_blocks(f)) {
                    result = PLUGIN_ERROR_CORRUPT_FILE;
                    goto cleanup;
                }
            }
            continue;
        }

        if (block_type == 0x2C) {
            /* --- Image Descriptor ------------------------------------- */
            uint16_t fr_left, fr_top, fr_w, fr_h;
            uint8_t packed_id;
            if (!gif_read_u16le(f, &fr_left) || !gif_read_u16le(f, &fr_top) ||
                !gif_read_u16le(f, &fr_w) || !gif_read_u16le(f, &fr_h) ||
                !gif_read_byte(f, &packed_id)) {
                result = PLUGIN_ERROR_CORRUPT_FILE;
                goto cleanup;
            }

            bool is_interlaced = (packed_id & 0x40) != 0;
            bool has_lct = (packed_id & 0x80) != 0;
            int lct_size = has_lct ? (2 << (packed_id & 0x07)) : 0;

            uint8_t lct[256 * 3];
            memset(lct, 0, sizeof(lct));
            if (has_lct && fread(lct, 3, (size_t)lct_size, f) != (size_t)lct_size) {
                result = PLUGIN_ERROR_CORRUPT_FILE;
                goto cleanup;
            }

            const uint8_t* cmap = has_lct ? lct : gct;
            int cmap_size = has_lct ? lct_size : gct_size;

            /* LZW minimum code size */
            uint8_t lzw_min;
            if (!gif_read_byte(f, &lzw_min) || lzw_min < 2 || lzw_min > 8) {
                result = PLUGIN_ERROR_CORRUPT_FILE;
                goto cleanup;
            }

            /* Read compressed sub-blocks */
            size_t compressed_size = 0;
            uint8_t* compressed = gif_read_sub_blocks(f, &compressed_size);
            if (!compressed) {
                result = PLUGIN_ERROR_CORRUPT_FILE;
                goto cleanup;
            }

            int fw = (int)fr_w, fh = (int)fr_h;
            int pixel_count = fw * fh;

            uint8_t* pixels = gif_lzw_decode(compressed, compressed_size, lzw_min, pixel_count);
            g_free(compressed);

            if (!pixels) {
                result = PLUGIN_ERROR_OUT_OF_MEMORY;
                goto cleanup;
            }

            /* De-interlace if necessary */
            if (is_interlaced)
                gif_deinterlace(pixels, fw, fh);

            /* Save canvas before disposal */
            memcpy(prev_canvas, canvas, (gsize)(width * height * 4));

            /* Blit frame onto canvas */
            int fl = (int)fr_left, ft = (int)fr_top;
            for (int y = 0; y < fh; y++) {
                int dy = ft + y;
                if (dy < 0 || dy >= height)
                    continue;
                for (int x = 0; x < fw; x++) {
                    int dx = fl + x;
                    if (dx < 0 || dx >= width)
                        continue;
                    int ci = (int)(unsigned char)pixels[y * fw + x];
                    if (ci == transparent)
                        continue; /* transparent pixel – leave canvas */
                    if (ci >= cmap_size)
                        continue;
                    uint8_t* dp = canvas + (dy * width + dx) * 4;
                    dp[0] = cmap[ci * 3];
                    dp[1] = cmap[ci * 3 + 1];
                    dp[2] = cmap[ci * 3 + 2];
                    dp[3] = 255;
                }
            }

            g_free(pixels);

            /* Create a layer for this frame */
            gchar* name = frame_index == 0
                              ? g_strdup_printf(_("Background (%ums)"), (unsigned)pending_frame_delay_ms)
                              : g_strdup_printf(_("Frame %d (%ums)"), frame_index + 1, (unsigned)pending_frame_delay_ms);
            ImageLayer* layer = layer_new(name, (guint)width, (guint)height, TRUE,
                                          LAYER_BACKGROUND_TRANSPARENT,
                                          LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
            g_free(name);

            if (!layer) {
                result = PLUGIN_ERROR_OUT_OF_MEMORY;
                goto cleanup;
            }

            /* Copy canvas → Cairo surface (Cairo ARGB32 = BGRA in memory, premultiplied) */
            cairo_surface_t* surf = layer->surface;
            if (surf) {
                cairo_surface_flush(surf);
                guchar* sd = cairo_image_surface_get_data(surf);
                int ss = cairo_image_surface_get_stride(surf);
                if (sd) {
                    for (int y = 0; y < height; y++) {
                        guchar* dr = sd + y * ss;
                        const uint8_t* sr = canvas + y * width * 4;
                        for (int x = 0; x < width; x++) {
                            uint8_t r = sr[x * 4 + 0], g = sr[x * 4 + 1], b = sr[x * 4 + 2], a = sr[x * 4 + 3];
                            if (a > 0 && a < 255) {
                                r = (uint8_t)((r * a + 127) / 255);
                                g = (uint8_t)((g * a + 127) / 255);
                                b = (uint8_t)((b * a + 127) / 255);
                            } else if (a == 0) {
                                r = g = b = 0;
                            }
                            dr[x * 4 + 0] = b;
                            dr[x * 4 + 1] = g;
                            dr[x * 4 + 2] = r;
                            dr[x * 4 + 3] = a;
                        }
                    }
                    cairo_surface_mark_dirty(surf);
                }
            }

            doc->layers = g_list_append(doc->layers, layer);
            frame_index++;

            if (load_progress_shown && host && host->load_progress_set && total_image_frames > 0) {
                gdouble fr = (gdouble)frame_index / (gdouble)total_image_frames;
                if (fr > 1.0)
                    fr = 1.0;
                gchar* base = g_path_get_basename(filename);
                gchar* msg = g_strdup_printf(_("Loading %s - frame %d of %d…"),
                                             base ? base : filename, frame_index, total_image_frames);
                host->load_progress_set(fr, msg);
                g_free(msg);
                g_free(base);
            }

            /* Apply disposal for next frame */
            if (disposal == 2) {
                /* Restore to transparent for layer-editor use: each frame's transparent
                   pixels are preserved as a=0 so the layer stack composes correctly. */
                for (int y = ft; y < ft + fh && y < height; y++) {
                    for (int x = fl; x < fl + fw && x < width; x++) {
                        uint8_t* p = canvas + (y * width + x) * 4;
                        p[0] = p[1] = p[2] = p[3] = 0;
                    }
                }
            } else if (disposal == 3) {
                /* Restore to previous */
                memcpy(canvas, prev_canvas, (gsize)(width * height * 4));
            }
            /* disposal 0/1: leave canvas as-is */

            disposal = 0;
            transparent = -1;
            pending_frame_delay_ms = 0;
            continue;
        }

        /* Unknown block type – skip */
        gif_skip_sub_blocks(f);
    }

    if (frame_index == 0)
        result = PLUGIN_ERROR_CORRUPT_FILE;

cleanup:
    if (load_progress_shown && host && host->load_progress_hide) {
        host->load_progress_hide();
        load_progress_shown = FALSE;
    }
    g_free(canvas);
    g_free(prev_canvas);
    fclose(f);
    return result;
}

/* =========================================================================
 * Median-cut palette quantisation
 * ====================================================================== */

typedef struct {
    uint8_t r, g, b;
} RGBPixel;

typedef struct {
    RGBPixel* pixels;
    int count;
} ColorBox;

static int cmp_r(const void* a, const void* b) {
    return (int)((const RGBPixel*)a)->r - (int)((const RGBPixel*)b)->r;
}
static int cmp_g(const void* a, const void* b) {
    return (int)((const RGBPixel*)a)->g - (int)((const RGBPixel*)b)->g;
}
static int cmp_b(const void* a, const void* b) {
    return (int)((const RGBPixel*)a)->b - (int)((const RGBPixel*)b)->b;
}

/*
 * Build a palette of up to palette_size entries using median cut.
 * out_palette must hold at least palette_size entries (uint8_t[palette_size][3]).
 * Returns the number of colors actually produced.
 */
static int median_cut(RGBPixel* pixels, int pixel_count,
                      int palette_size, uint8_t out_palette[][3]) {
    if (pixel_count == 0 || palette_size <= 0)
        return 0;

    ColorBox* boxes = (ColorBox*)g_malloc0(sizeof(ColorBox) * (size_t)palette_size);
    if (!boxes)
        return 0;

    boxes[0].pixels = pixels;
    boxes[0].count = pixel_count;
    int box_count = 1;

    while (box_count < palette_size) {
        /* Find largest box by count */
        int largest = 0;
        for (int i = 1; i < box_count; i++)
            if (boxes[i].count > boxes[largest].count)
                largest = i;

        if (boxes[largest].count <= 1)
            break;

        ColorBox* box = &boxes[largest];
        uint8_t rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;
        for (int i = 0; i < box->count; i++) {
            uint8_t r = box->pixels[i].r, g = box->pixels[i].g, b = box->pixels[i].b;
            if (r < rmin)
                rmin = r;
            if (r > rmax)
                rmax = r;
            if (g < gmin)
                gmin = g;
            if (g > gmax)
                gmax = g;
            if (b < bmin)
                bmin = b;
            if (b > bmax)
                bmax = b;
        }
        int rr = rmax - rmin, gr = gmax - gmin, br = bmax - bmin;
        if (rr >= gr && rr >= br)
            qsort(box->pixels, (size_t)box->count, sizeof(RGBPixel), cmp_r);
        else if (gr >= rr && gr >= br)
            qsort(box->pixels, (size_t)box->count, sizeof(RGBPixel), cmp_g);
        else
            qsort(box->pixels, (size_t)box->count, sizeof(RGBPixel), cmp_b);

        int mid = box->count / 2;
        ColorBox new_box = {box->pixels + mid, box->count - mid};
        box->count = mid;
        boxes[box_count++] = new_box;
    }

    int actual = 0;
    for (int i = 0; i < box_count; i++) {
        if (boxes[i].count == 0)
            continue;
        uint64_t rs = 0, gs = 0, bs = 0;
        for (int j = 0; j < boxes[i].count; j++) {
            rs += boxes[i].pixels[j].r;
            gs += boxes[i].pixels[j].g;
            bs += boxes[i].pixels[j].b;
        }
        out_palette[actual][0] = (uint8_t)(rs / (uint64_t)boxes[i].count);
        out_palette[actual][1] = (uint8_t)(gs / (uint64_t)boxes[i].count);
        out_palette[actual][2] = (uint8_t)(bs / (uint64_t)boxes[i].count);
        actual++;
    }

    g_free(boxes);
    return actual;
}

/* Euclidean-distance palette lookup */
static int find_nearest(uint8_t r, uint8_t g, uint8_t b,
                        const uint8_t palette[][3], int count) {
    int best = 0, best_d = INT_MAX;
    for (int i = 0; i < count; i++) {
        int dr = (int)r - palette[i][0];
        int dg = (int)g - palette[i][1];
        int db = (int)b - palette[i][2];
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
            if (d == 0)
                break;
        }
    }
    return best;
}

/* Round up to next power-of-two (GIF palette must be power-of-two sized) */
static int next_pow2(int n) {
    if (n <= 2)
        return 2;
    if (n <= 4)
        return 4;
    if (n <= 8)
        return 8;
    if (n <= 16)
        return 16;
    if (n <= 32)
        return 32;
    if (n <= 64)
        return 64;
    if (n <= 128)
        return 128;
    return 256;
}

/* LZW minimum code size for a palette of the given (power-of-two) size */
static int lzw_min_code_size(int palette_size) {
    int n = 2;
    while ((1 << n) < palette_size)
        n++;
    return n < 2 ? 2 : n;
}

/* =========================================================================
 * LZW compressor
 * ====================================================================== */

/* Bit writer to an in-memory buffer */
typedef struct {
    uint8_t* data;
    size_t cap;
    size_t size;
    uint32_t bit_buf;
    int bit_count;
} BitWriter;

static bool bw_init(BitWriter* w) {
    w->cap = 1024;
    w->data = (uint8_t*)g_malloc(w->cap);
    w->size = 0;
    w->bit_buf = 0;
    w->bit_count = 0;
    return w->data != NULL;
}

static bool bw_ensure(BitWriter* w, size_t need) {
    if (w->size + need <= w->cap)
        return true;
    size_t nc = w->cap * 2 + need;
    uint8_t* nd = (uint8_t*)g_realloc(w->data, nc);
    if (!nd)
        return false;
    w->data = nd;
    w->cap = nc;
    return true;
}

static bool bw_write(BitWriter* w, uint32_t val, int bits) {
    w->bit_buf |= (val << w->bit_count);
    w->bit_count += bits;
    while (w->bit_count >= 8) {
        if (!bw_ensure(w, 1))
            return false;
        w->data[w->size++] = (uint8_t)(w->bit_buf & 0xFF);
        w->bit_buf >>= 8;
        w->bit_count -= 8;
    }
    return true;
}

static bool bw_flush(BitWriter* w) {
    if (w->bit_count > 0) {
        if (!bw_ensure(w, 1))
            return false;
        w->data[w->size++] = (uint8_t)(w->bit_buf & 0xFF);
        w->bit_buf = 0;
        w->bit_count = 0;
    }
    return true;
}

static void bw_free(BitWriter* w) {
    g_free(w->data);
    w->data = NULL;
}

/* Write BitWriter data to file as GIF sub-blocks (≤255 bytes each) */
static bool bw_write_sub_blocks(const BitWriter* w, FILE* f) {
    size_t pos = 0;
    while (pos < w->size) {
        size_t chunk = w->size - pos;
        if (chunk > 255)
            chunk = 255;
        if (fputc((int)chunk, f) == EOF)
            return false;
        if (fwrite(w->data + pos, 1, chunk, f) != chunk)
            return false;
        pos += chunk;
    }
    return fputc(0, f) != EOF; /* block terminator */
}

/* Hash table for LZW string table: key = (prefix << 8) | suffix */
#define LZW_HASH_SIZE 5003 /* prime > 4096 */

typedef struct {
    int prefix;
    uint8_t suffix;
    uint16_t code;
} LZWEntry;

static LZWEntry lzw_hash[LZW_HASH_SIZE]; /* static to avoid stack allocation */

static void lzw_hash_clear(void) {
    for (int i = 0; i < LZW_HASH_SIZE; i++)
        lzw_hash[i].prefix = -1;
}

static int lzw_hash_find(int prefix, uint8_t suffix) {
    unsigned h = (unsigned)((prefix * 257) ^ suffix) % LZW_HASH_SIZE;
    for (int p = 0; p < LZW_HASH_SIZE; p++) {
        unsigned idx = (h + (unsigned)p) % LZW_HASH_SIZE;
        if (lzw_hash[idx].prefix < 0)
            return -1;
        if (lzw_hash[idx].prefix == prefix && lzw_hash[idx].suffix == suffix)
            return (int)lzw_hash[idx].code;
    }
    return -1;
}

static void lzw_hash_insert(int prefix, uint8_t suffix, int code) {
    unsigned h = (unsigned)((prefix * 257) ^ suffix) % LZW_HASH_SIZE;
    for (int p = 0; p < LZW_HASH_SIZE; p++) {
        unsigned idx = (h + (unsigned)p) % LZW_HASH_SIZE;
        if (lzw_hash[idx].prefix < 0) {
            lzw_hash[idx].prefix = prefix;
            lzw_hash[idx].suffix = suffix;
            lzw_hash[idx].code = (uint16_t)code;
            return;
        }
    }
}

/*
 * LZW-encode pixels and write the LZW min-code-size byte + sub-blocks to f.
 * min_cs: LZW minimum code size (2–8).
 */
static bool gif_lzw_encode(FILE* f, const uint8_t* pixels, int pixel_count, int min_cs) {
    if (!gif_write_byte(f, (uint8_t)min_cs))
        return false;

    int clear_code = 1 << min_cs;
    int eoi_code = clear_code + 1;
    int code_size = min_cs + 1;
    int next_code = eoi_code + 1;

    lzw_hash_clear();

    BitWriter bw;
    if (!bw_init(&bw))
        return false;

    bw_write(&bw, (uint32_t)clear_code, code_size);

    if (pixel_count == 0) {
        bw_write(&bw, (uint32_t)eoi_code, code_size);
        bw_flush(&bw);
        bool ok = bw_write_sub_blocks(&bw, f);
        bw_free(&bw);
        return ok;
    }

    int prev = (int)(unsigned char)pixels[0];

    for (int i = 1; i < pixel_count; i++) {
        uint8_t ch = pixels[i];
        int found = lzw_hash_find(prev, ch);
        if (found >= 0) {
            prev = found;
        } else {
            if (!bw_write(&bw, (uint32_t)prev, code_size)) {
                bw_free(&bw);
                return false;
            }

            if (next_code < 4096) {
                lzw_hash_insert(prev, ch, next_code);
                next_code++;
                if (next_code > (1 << code_size) && code_size < 12)
                    code_size++;
            }

            if (next_code >= 4096) {
                /* Table full: emit clear, reset */
                bw_write(&bw, (uint32_t)clear_code, code_size);
                code_size = min_cs + 1;
                next_code = eoi_code + 1;
                lzw_hash_clear();
            }

            prev = (int)(unsigned char)ch;
        }
    }

    bw_write(&bw, (uint32_t)prev, code_size);
    bw_write(&bw, (uint32_t)eoi_code, code_size);
    bw_flush(&bw);
    bool ok = bw_write_sub_blocks(&bw, f);
    bw_free(&bw);
    return ok;
}

/* =========================================================================
 * GIF saver
 * ====================================================================== */

static PluginError save_gif(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    GIFSaveOptions* go = opts && opts->plugin_data ? (GIFSaveOptions*)opts->plugin_data : NULL;

    GIFColorModel color_model = go ? go->color_model : GIF_COLOR_MODEL_AUTO;
    int palette_size = go ? go->palette_size : 256;
    uint8_t bg_r = go ? go->bg_color_r : 255;
    uint8_t bg_g = go ? go->bg_color_g : 255;
    uint8_t bg_b = go ? go->bg_color_b : 255;
    GIFTransparency transparency = go ? go->transparency : GIF_TRANSPARENCY_AUTO;
    uint8_t alpha_cutoff = go ? go->alpha_cutoff : 64;
    uint8_t tc_r = go ? go->transparent_color_r : 255;
    uint8_t tc_g = go ? go->transparent_color_g : 0;
    uint8_t tc_b = go ? go->transparent_color_b : 255;

    if (palette_size < 2)
        palette_size = 2;
    if (palette_size > 256)
        palette_size = 256;

    /* Get composite surface */
    cairo_surface_t* composite = document_export_composite_surface(doc);
    if (!composite)
        return PLUGIN_ERROR_FILE_WRITE_ERROR;

    cairo_surface_flush(composite);
    guchar* sd = cairo_image_surface_get_data(composite);
    int ss = cairo_image_surface_get_stride(composite);
    if (!sd) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    int width = (int)doc->width;
    int height = (int)doc->height;
    int total = width * height;

    PluginError result = PLUGIN_ERROR_FILE_WRITE_ERROR;
    FILE* f = NULL;
    RGBPixel* rgb_pixels = NULL;
    uint8_t* indexed = NULL;

    /* --- Detect transparency ------------------------------------------ */
    bool has_any_transparent = false;
    for (int y = 0; y < height && !has_any_transparent; y++) {
        guchar* row = sd + y * ss;
        for (int x = 0; x < width; x++)
            if (row[x * 4 + 3] < 255) {
                has_any_transparent = true;
                break;
            }
    }

    if (transparency == GIF_TRANSPARENCY_AUTO)
        transparency = has_any_transparent ? GIF_TRANSPARENCY_BY_CUTOFF : GIF_TRANSPARENCY_NONE;

    bool use_trans_idx = (transparency == GIF_TRANSPARENCY_BY_CUTOFF ||
                          transparency == GIF_TRANSPARENCY_BY_COLOR);

    /* --- Detect grayscale (auto) --------------------------------------- */
    if (color_model == GIF_COLOR_MODEL_AUTO) {
        bool gray = true;
        for (int y = 0; y < height && gray; y += 4) {
            guchar* row = sd + y * ss;
            for (int x = 0; x < width; x += 4) {
                guchar b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];
                if (a > 0 && a < 255) {
                    r = (guchar)((r * 255 + a / 2) / a);
                    g = (guchar)((g * 255 + a / 2) / a);
                    b = (guchar)((b * 255 + a / 2) / a);
                } else if (a == 0) {
                    r = g = b = 0;
                }
                if (abs(r - g) > 2 || abs(g - b) > 2) {
                    gray = false;
                }
            }
        }
        color_model = gray ? GIF_COLOR_MODEL_GRAYSCALE : GIF_COLOR_MODEL_COLOR;
    }
    bool is_gray = (color_model == GIF_COLOR_MODEL_GRAYSCALE);

    /* --- Build palette ------------------------------------------------- */
    /* Reserve one extra slot for the transparent index if needed */
    int usable = use_trans_idx ? (palette_size - 1) : palette_size;
    if (usable < 1)
        usable = 1;

    int actual_palette = next_pow2(palette_size);

    uint8_t palette[256][3];
    memset(palette, 0, sizeof(palette));

    if (is_gray) {
        for (int i = 0; i < usable; i++) {
            uint8_t v = (usable > 1) ? (uint8_t)((i * 255 + (usable - 1) / 2) / (usable - 1)) : 0;
            palette[i][0] = palette[i][1] = palette[i][2] = v;
        }
    } else {
        /* Collect opaque/composited pixels for median cut */
        rgb_pixels = (RGBPixel*)g_malloc((gsize)(total * sizeof(RGBPixel)));
        if (!rgb_pixels) {
            result = PLUGIN_ERROR_OUT_OF_MEMORY;
            goto cleanup;
        }

        int count = 0;
        for (int y = 0; y < height; y++) {
            guchar* row = sd + y * ss;
            for (int x = 0; x < width; x++) {
                guchar b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];

                /* Skip pixels that will become transparent */
                if (transparency == GIF_TRANSPARENCY_BY_CUTOFF && a < alpha_cutoff)
                    continue;

                /* Un-premultiply */
                if (a == 0) {
                    r = bg_r;
                    g = bg_g;
                    b = bg_b;
                } else if (a < 255) {
                    r = (guchar)((r * 255 + a / 2) / a);
                    g = (guchar)((g * 255 + a / 2) / a);
                    b = (guchar)((b * 255 + a / 2) / a);
                }

                rgb_pixels[count].r = r;
                rgb_pixels[count].g = g;
                rgb_pixels[count].b = b;
                count++;
            }
        }

        if (count > 0) {
            int built = median_cut(rgb_pixels, count, usable, palette);
            (void)built;
        }
    }

    /* Transparent index = last slot in the actual palette */
    int trans_idx = use_trans_idx ? (actual_palette - 1) : -1;
    if (trans_idx >= 0) {
        palette[trans_idx][0] = 255;
        palette[trans_idx][1] = 0;
        palette[trans_idx][2] = 255;
    }

    /* --- Map pixels to palette indices --------------------------------- */
    indexed = (uint8_t*)g_malloc((gsize)total);
    if (!indexed) {
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    int map_size = use_trans_idx ? (actual_palette - 1) : actual_palette;

    for (int y = 0; y < height; y++) {
        guchar* row = sd + y * ss;
        for (int x = 0; x < width; x++) {
            guchar b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];

            /* Un-premultiply */
            if (a == 0) {
                r = bg_r;
                g = bg_g;
                b = bg_b;
            } else if (a < 255) {
                r = (guchar)((r * 255 + a / 2) / a);
                g = (guchar)((g * 255 + a / 2) / a);
                b = (guchar)((b * 255 + a / 2) / a);
            }

            bool is_trans = false;
            if (transparency == GIF_TRANSPARENCY_BY_CUTOFF)
                is_trans = (a < alpha_cutoff);
            else if (transparency == GIF_TRANSPARENCY_BY_COLOR)
                is_trans = (r == tc_r && g == tc_g && b == tc_b);

            if (is_trans && trans_idx >= 0) {
                indexed[y * width + x] = (uint8_t)trans_idx;
            } else if (is_gray) {
                uint8_t lum = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
                int best = 0, best_d = 255 * 255;
                for (int i = 0; i < map_size; i++) {
                    int d = (int)lum - (int)palette[i][0];
                    d *= d;
                    if (d < best_d) {
                        best_d = d;
                        best = i;
                        if (d == 0)
                            break;
                    }
                }
                indexed[y * width + x] = (uint8_t)best;
            } else {
                indexed[y * width + x] = (uint8_t)find_nearest(r, g, b, palette, map_size);
            }
        }
    }

    /* --- Write GIF file ------------------------------------------------ */
    f = g_fopen(filename, "wb");
    if (!f) {
        result = PLUGIN_ERROR_FILE_WRITE_ERROR;
        goto cleanup;
    }

    /* Header: GIF89a if using transparency, otherwise GIF87a */
    if (fwrite(use_trans_idx ? "GIF89a" : "GIF87a", 1, 6, f) != 6)
        goto cleanup;

    /* Logical Screen Descriptor */
    int ct_bits = lzw_min_code_size(actual_palette) - 1; /* N where palette = 2^(N+1) */
    gif_write_u16le(f, (uint16_t)width);
    gif_write_u16le(f, (uint16_t)height);
    gif_write_byte(f, (uint8_t)(0x80 | (ct_bits & 0x07))); /* global CT flag + CT size */
    gif_write_byte(f, 0);                                  /* background color index */
    gif_write_byte(f, 0);                                  /* pixel aspect ratio */

    /* Global Color Table */
    for (int i = 0; i < actual_palette; i++) {
        gif_write_byte(f, palette[i][0]);
        gif_write_byte(f, palette[i][1]);
        gif_write_byte(f, palette[i][2]);
    }

    /* Graphic Control Extension (for transparency only) */
    if (use_trans_idx && trans_idx >= 0) {
        gif_write_byte(f, 0x21); /* extension introducer */
        gif_write_byte(f, 0xF9); /* GCE label */
        gif_write_byte(f, 0x04); /* block size */
        gif_write_byte(f, 0x01); /* packed: transparent color flag */
        gif_write_byte(f, 0x00); /* delay lo */
        gif_write_byte(f, 0x00); /* delay hi */
        gif_write_byte(f, (uint8_t)trans_idx);
        gif_write_byte(f, 0x00); /* block terminator */
    }

    /* Image Descriptor */
    gif_write_byte(f, 0x2C);
    gif_write_u16le(f, 0);
    gif_write_u16le(f, 0);
    gif_write_u16le(f, (uint16_t)width);
    gif_write_u16le(f, (uint16_t)height);
    gif_write_byte(f, 0x00); /* no local CT, not interlaced */

    /* LZW compressed image data */
    int min_cs = lzw_min_code_size(actual_palette);
    if (!gif_lzw_encode(f, indexed, total, min_cs))
        goto cleanup;

    /* Trailer */
    gif_write_byte(f, 0x3B);
    result = PLUGIN_ERROR_NONE;

cleanup:
    if (f)
        fclose(f);
    g_free(rgb_pixels);
    g_free(indexed);
    cairo_surface_destroy(composite);
    return result;
}

/* =========================================================================
 * Plugin entry point
 * ====================================================================== */

bool plugin_init_gif(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;
    if (!out_plugin)
        return false;

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "GIF - Graphics Interchange Format";
    out_plugin->format_info.extensions = "gif";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = true;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_gif;
    out_plugin->callbacks.load = load_gif;
    out_plugin->callbacks.can_save = can_save_gif;
    out_plugin->callbacks.save = save_gif;
    out_plugin->callbacks.get_save_options_size = get_gif_save_options_size;
    out_plugin->callbacks.init_save_options = init_gif_save_options;

    return true;
}
