/*
 * SGI (Silicon Graphics) image format plugin
 * Supports .rgb, .rgba, .sgi, .bw (and .int, .inta) - IRIS image file format.
 * Specification: Paul Bourke / SGI Image File Format (magic 474, big-endian).
 */

#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_sgi.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SGI_HEADER_SIZE 512
#define SGI_MAGIC       474 /* 0x01DA as big-endian short */

/* Storage */
#define SGI_VERBATIM    0
#define SGI_RLE         1

/* Bytes per channel */
#define SGI_BPC_1       1
#define SGI_BPC_2       2

/* Colormap: we only support NORMAL (0) */
#define SGI_COLORMAP_NORMAL 0

typedef struct {
    uint16_t magic;      /* 474 */
    uint8_t  storage;    /* 0=verbatim, 1=RLE */
    uint8_t  bpc;       /* 1 or 2 */
    uint16_t dimension;  /* 1, 2, or 3 */
    uint16_t xsize;     /* width */
    uint16_t ysize;     /* height */
    uint16_t zsize;     /* channels: 1=bw, 3=rgb, 4=rgba */
    int32_t  pixmin;
    int32_t  pixmax;
    /* rest of header skipped except COLORMAP at 0x68 */
    uint32_t colormap;  /* 0=NORMAL */
} SGIHeader;

static uint16_t read_be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t read_be16_file(FILE* f) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

static uint32_t read_be32_file(FILE* f) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static int32_t read_be32_signed_file(FILE* f) {
    return (int32_t)read_be32_file(f);
}

static bool can_load_sgi(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;
    if (!header || header_size < 2) return false;
    return read_be16(header) == SGI_MAGIC;
}

static bool can_save_sgi(const char* filename) {
    (void)filename;
    return false; /* read-only */
}

/**
 * Expand one RLE scanline (BPC=1): exactly xsize bytes into out.
 * Returns false on error (wrong length).
 */
static bool expand_rle_byte(const uint8_t* in, size_t in_len, uint8_t* out, uint32_t xsize) {
    size_t out_pos = 0;
    size_t in_pos = 0;

    while (out_pos < (size_t)xsize && in_pos < in_len) {
        uint8_t c = in[in_pos++];
        uint8_t count = c & 0x7f;
        if (count == 0) break;

        if (c & 0x80) {
            /* Copy count bytes */
            if (in_pos + count > in_len || out_pos + count > (size_t)xsize) return false;
            memcpy(out + out_pos, in + in_pos, count);
            in_pos += count;
            out_pos += count;
        } else {
            /* Repeat next byte count times */
            if (in_pos >= in_len) return false;
            uint8_t val = in[in_pos++];
            if (out_pos + count > (size_t)xsize) return false;
            memset(out + out_pos, val, count);
            out_pos += count;
        }
    }

    return (out_pos == (size_t)xsize);
}

/**
 * Expand one RLE scanline (BPC=2): exactly xsize shorts (big-endian) into out.
 */
static bool expand_rle_short(const uint8_t* in, size_t in_len, uint16_t* out, uint32_t xsize) {
    size_t out_pos = 0;
    size_t in_pos = 0;

    while (out_pos < (size_t)xsize && in_pos + 2 <= in_len) {
        uint16_t c = (uint16_t)in[in_pos] << 8 | (uint16_t)in[in_pos + 1];
        in_pos += 2;
        uint8_t count = (uint8_t)(c & 0x7f);
        if (count == 0) break;

        if (c & 0x80) {
            if (in_pos + 2 * (size_t)count > in_len || out_pos + (size_t)count > (size_t)xsize)
                return false;
            for (uint8_t i = 0; i < count; i++) {
                out[out_pos++] = (uint16_t)in[in_pos] << 8 | (uint16_t)in[in_pos + 1];
                in_pos += 2;
            }
        } else {
            if (in_pos + 2 > in_len) return false;
            uint16_t val = (uint16_t)in[in_pos] << 8 | (uint16_t)in[in_pos + 1];
            in_pos += 2;
            for (uint8_t i = 0; i < count && out_pos < (size_t)xsize; i++)
                out[out_pos++] = val;
        }
    }

    return (out_pos == (size_t)xsize);
}

/**
 * Scale 16-bit value to 8-bit using pixmin/pixmax.
 */
static uint8_t scale16_to_8(uint16_t val, int32_t pixmin, int32_t pixmax) {
    int32_t range = pixmax - pixmin;
    if (range <= 0) return (val >> 8) & 0xff;
    int32_t v = (int32_t)val - pixmin;
    if (v < 0) v = 0;
    if (v > range) v = range;
    return (uint8_t)((v * 255) / range);
}

static PluginError load_sgi(ImageDocument* doc, const char* filename) {
    FILE* f;
    uint8_t header_buf[SGI_HEADER_SIZE];
    SGIHeader hdr;
    uint32_t width, height, channels;
    uint8_t** channel_rows = NULL; /* channel_rows[channel][y * rowbytes] - planar, row 0 = bottom */
    size_t rowbytes;
    uint32_t tablen;
    uint32_t* starttab = NULL;
    uint32_t* lengthtab = NULL;
    uint8_t* rle_buf = NULL;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    bool has_alpha;
    int32_t pixmin, pixmax;
    uint8_t bpc;

    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    f = g_fopen(filename, "rb");
    if (!f) return PLUGIN_ERROR_FILE_NOT_FOUND;

    if (fread(header_buf, 1, SGI_HEADER_SIZE, f) != SGI_HEADER_SIZE) {
        fclose(f);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    hdr.magic     = read_be16(header_buf + 0);
    hdr.storage   = header_buf[2];
    hdr.bpc       = header_buf[3];
    hdr.dimension = read_be16(header_buf + 4);
    hdr.xsize     = read_be16(header_buf + 6);
    hdr.ysize     = read_be16(header_buf + 8);
    hdr.zsize     = read_be16(header_buf + 10);
    hdr.pixmin    = (int32_t)read_be32(header_buf + 12);
    hdr.pixmax    = (int32_t)read_be32(header_buf + 16);
    hdr.colormap  = read_be32(header_buf + 0x68);

    if (hdr.magic != SGI_MAGIC) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }
    if (hdr.storage != SGI_VERBATIM && hdr.storage != SGI_RLE) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }
    if (hdr.bpc != SGI_BPC_1 && hdr.bpc != SGI_BPC_2) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }
    if (hdr.dimension != 2 && hdr.dimension != 3) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }
    if (hdr.dimension == 2)
        channels = 1;
    else
        channels = (uint32_t)hdr.zsize;

    if (channels != 1 && channels != 3 && channels != 4) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }
    if (hdr.colormap != SGI_COLORMAP_NORMAL) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT; /* DITHERED/SCREEN/COLORMAP not supported */
    }

    width  = (uint32_t)hdr.xsize;
    height = (uint32_t)hdr.ysize;
    has_alpha = (channels == 4);
    pixmin = hdr.pixmin;
    pixmax = hdr.pixmax;
    bpc = hdr.bpc;
    rowbytes = (size_t)width * (size_t)bpc;

    channel_rows = (uint8_t**)g_malloc0((size_t)channels * sizeof(uint8_t*));
    if (!channel_rows) {
        fclose(f);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }
    for (uint32_t c = 0; c < channels; c++) {
        channel_rows[c] = (uint8_t*)g_malloc(rowbytes * (size_t)height);
        if (!channel_rows[c]) {
            for (uint32_t i = 0; i < c; i++) g_free(channel_rows[i]);
            g_free(channel_rows);
            fclose(f);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }
    }

    if (hdr.storage == SGI_VERBATIM) {
        /* Data: channel 0 all rows, channel 1 all rows, ...; row 0 = bottom */
        for (uint32_t c = 0; c < channels; c++) {
            for (uint32_t y = 0; y < height; y++) {
                if (fread(channel_rows[c] + y * rowbytes, 1, rowbytes, f) != rowbytes) {
                    for (uint32_t i = 0; i < channels; i++) g_free(channel_rows[i]);
                    g_free(channel_rows);
                    fclose(f);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }
            }
        }
    } else {
        /* RLE: offset tables then RLE data */
        tablen = height * channels;
        starttab  = (uint32_t*)g_malloc(tablen * sizeof(uint32_t));
        lengthtab = (uint32_t*)g_malloc(tablen * sizeof(uint32_t));
        if (!starttab || !lengthtab) {
            g_free(starttab);
            g_free(lengthtab);
            for (uint32_t c = 0; c < channels; c++) g_free(channel_rows[c]);
            g_free(channel_rows);
            fclose(f);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        for (uint32_t i = 0; i < tablen; i++)
            starttab[i] = read_be32_file(f);
        for (uint32_t i = 0; i < tablen; i++)
            lengthtab[i] = read_be32_file(f);

        /* Find max RLE line length for temp buffer */
        size_t max_rle = 0;
        for (uint32_t i = 0; i < tablen; i++)
            if (lengthtab[i] > max_rle) max_rle = lengthtab[i];
        if (max_rle == 0) max_rle = rowbytes * 2;
        rle_buf = (uint8_t*)g_malloc(max_rle);
        if (!rle_buf) {
            g_free(starttab);
            g_free(lengthtab);
            for (uint32_t c = 0; c < channels; c++) g_free(channel_rows[c]);
            g_free(channel_rows);
            fclose(f);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        for (uint32_t c = 0; c < channels; c++) {
            for (uint32_t y = 0; y < height; y++) {
                uint32_t idx = y + c * height;
                uint32_t off = starttab[idx];
                uint32_t len = lengthtab[idx];
                if (len > max_rle) {
                    g_free(rle_buf);
                    g_free(starttab);
                    g_free(lengthtab);
                    for (uint32_t i = 0; i < channels; i++) g_free(channel_rows[i]);
                    g_free(channel_rows);
                    fclose(f);
                    return PLUGIN_ERROR_CORRUPT_FILE;
                }
                if (fseek(f, (long)off, SEEK_SET) != 0) {
                    g_free(rle_buf);
                    g_free(starttab);
                    g_free(lengthtab);
                    for (uint32_t i = 0; i < channels; i++) g_free(channel_rows[i]);
                    g_free(channel_rows);
                    fclose(f);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }
                if (fread(rle_buf, 1, len, f) != len) {
                    g_free(rle_buf);
                    g_free(starttab);
                    g_free(lengthtab);
                    for (uint32_t i = 0; i < channels; i++) g_free(channel_rows[i]);
                    g_free(channel_rows);
                    fclose(f);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }

                if (bpc == 1) {
                    if (!expand_rle_byte(rle_buf, len, channel_rows[c] + y * rowbytes, width)) {
                        g_free(rle_buf);
                        g_free(starttab);
                        g_free(lengthtab);
                        for (uint32_t i = 0; i < channels; i++) g_free(channel_rows[i]);
                        g_free(channel_rows);
                        fclose(f);
                        return PLUGIN_ERROR_CORRUPT_FILE;
                    }
                } else {
                    if (!expand_rle_short(rle_buf, len, (uint16_t*)(channel_rows[c] + y * rowbytes), width)) {
                        g_free(rle_buf);
                        g_free(starttab);
                        g_free(lengthtab);
                        for (uint32_t i = 0; i < channels; i++) g_free(channel_rows[i]);
                        g_free(channel_rows);
                        fclose(f);
                        return PLUGIN_ERROR_CORRUPT_FILE;
                    }
                }
            }
        }

        g_free(rle_buf);
        g_free(starttab);
        g_free(lengthtab);
    }

    fclose(f);

    /* Set document and create layer */
    doc->width    = width;
    doc->height   = height;
    doc->channels = has_alpha ? 4 : 3;
    doc->bit_depth = 8;
    doc->has_alpha = has_alpha;

    for (GList* iter = doc->layers; iter; iter = iter->next)
        layer_free((ImageLayer*)iter->data);
    g_list_free(doc->layers);
    doc->layers = NULL;

    base_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        for (uint32_t c = 0; c < channels; c++) g_free(channel_rows[c]);
        g_free(channel_rows);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        for (uint32_t c = 0; c < channels; c++) g_free(channel_rows[c]);
        g_free(channel_rows);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    surface_data = cairo_image_surface_get_data(temp_surface);
    surface_stride = cairo_image_surface_get_stride(temp_surface);
    if (!surface_data) {
        layer_free(base_layer);
        for (uint32_t c = 0; c < channels; c++) g_free(channel_rows[c]);
        g_free(channel_rows);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert planar SGI (row 0 = bottom) to Cairo ARGB32 (row 0 = top), BGRA in memory */
    for (uint32_t y = 0; y < height; y++) {
        uint32_t sgi_row = y; /* SGI row 0 = bottom */
        uint32_t cairo_row = height - 1 - sgi_row;
        guchar* dst = surface_data + (size_t)cairo_row * (size_t)surface_stride;

        for (uint32_t x = 0; x < width; x++) {
            uint8_t r, g, b, a = 255;

            if (bpc == 1) {
                if (channels >= 3) {
                    r = channel_rows[0][sgi_row * rowbytes + x];
                    g = channel_rows[1][sgi_row * rowbytes + x];
                    b = channel_rows[2][sgi_row * rowbytes + x];
                    if (channels == 4) a = channel_rows[3][sgi_row * rowbytes + x];
                } else {
                    r = g = b = channel_rows[0][sgi_row * rowbytes + x];
                }
            } else {
                uint16_t* row0 = (uint16_t*)(channel_rows[0] + sgi_row * rowbytes);
                if (channels >= 3) {
                    r = scale16_to_8(row0[x], pixmin, pixmax);
                    g = scale16_to_8(((uint16_t*)(channel_rows[1] + sgi_row * rowbytes))[x], pixmin, pixmax);
                    b = scale16_to_8(((uint16_t*)(channel_rows[2] + sgi_row * rowbytes))[x], pixmin, pixmax);
                    if (channels == 4)
                        a = scale16_to_8(((uint16_t*)(channel_rows[3] + sgi_row * rowbytes))[x], pixmin, pixmax);
                } else {
                    r = g = b = scale16_to_8(row0[x], pixmin, pixmax);
                }
            }

            if (a == 0) r = g = b = 0;
            else if (a < 255) {
                r = (uint8_t)((r * a + 127) / 255);
                g = (uint8_t)((g * a + 127) / 255);
                b = (uint8_t)((b * a + 127) / 255);
            }

            dst[x * 4 + 0] = b;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = r;
            dst[x * 4 + 3] = a;
        }
    }

    for (uint32_t c = 0; c < channels; c++)
        g_free(channel_rows[c]);
    g_free(channel_rows);

    cairo_surface_mark_dirty(temp_surface);
    doc->layers = g_list_append(doc->layers, base_layer);
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

bool plugin_init_sgi(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

    if (!out_plugin) return false;

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "SGI - Silicon Graphics Image";
    out_plugin->format_info.extensions = "rgb,rgba,sgi,bw,int,inta";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_sgi;
    out_plugin->callbacks.load = load_sgi;
    out_plugin->callbacks.can_save = can_save_sgi;
    out_plugin->callbacks.save = NULL;

    return true;
}
