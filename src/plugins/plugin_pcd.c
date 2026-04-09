/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "plugins/plugin_pcd.h"
#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "ui.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PCD_DEBUG
#define PCD_DEBUG 0
#endif
#define PCD_DBG(...)                              \
    do {                                          \
        if (PCD_DEBUG)                            \
            fprintf(stderr, "PCD: " __VA_ARGS__); \
    } while (0)

/* ========================================================================
 * PCD Format Constants and Type Definitions
 * ======================================================================== */

#define PCD_SIGNATURE "PCD_IPI"
#define PCD_SIGNATURE_OFFSET 2048
#define PCD_SIGNATURE_LEN 7
#define PCD_ORIENTATION_OFFSET 0x0E02

typedef enum {
    PCD_RES_OVERVIEW = 0, /* 192×128 */
    PCD_RES_BASE16,       /* 384×256 */
    PCD_RES_BASE4,        /* 768×512 */
    PCD_RES_BASE,         /* 1536×1024 */
    PCD_RES_4BASE,        /* 3072×2048 */
    PCD_RES_16BASE        /* 6144×4096 */
} PCDResolutionLevel;

typedef struct {
    const char* name;
    const char* description;
    uint32_t width;
    uint32_t height;
    bool strip_based;
} PCDResolutionInfo;

static const PCDResolutionInfo pcd_resolutions[] = {
    {"Overview", "192x128", 192, 128, true},
    {"Base/16", "384x256", 384, 256, true},
    {"Base/4", "768x512", 768, 512, true},
    {"Base", "1536x1024", 1536, 1024, false},
    {"4Base", "3072x2048", 3072, 2048, false},
    {"16Base", "6144x4096", 6144, 4096, false}};

#define PCD_NUM_LEVELS (sizeof(pcd_resolutions) / sizeof(pcd_resolutions[0]))
/* Sentinel for "user cancelled resolution dialog" */
#define PCD_RES_CANCELLED ((PCDResolutionLevel)-1)

/* ========================================================================
 * PhotoYCC to RGB - LUT-based conversion
 * ======================================================================== */

#define PCD_YCC_RANGE 320
#define PCD_RED_NUL 137
#define PCD_BLUE_NUL 156
#define PCD_LUM_MUL 360
#define PCD_RED_MUL 512
#define PCD_BLUE_MUL 512
#define PCD_GREEN1_MUL (-(PCD_RED_MUL / 2))
#define PCD_GREEN2_MUL (-(PCD_BLUE_MUL / 6))
#define PCD_RED_ADD (-(PCD_RED_NUL * PCD_RED_MUL))
#define PCD_BLUE_ADD (-(PCD_BLUE_NUL * PCD_BLUE_MUL))
#define PCD_GREEN1_ADD (-(PCD_RED_ADD / 2))
#define PCD_GREEN2_ADD (-(PCD_BLUE_ADD / 6))

static int pcd_ycc_lut_initialized = 0;
static int16_t pcd_lut_gray[256];
static int16_t pcd_lut_red[256];
static int16_t pcd_lut_blue[256];
static int16_t pcd_lut_green1[256];
static int16_t pcd_lut_green2[256];
static int32_t pcd_lut_range[256 + 2 * PCD_YCC_RANGE];

static void pcd_ycc_lut_init(void) {
    int i;
    if (pcd_ycc_lut_initialized)
        return;
    pcd_ycc_lut_initialized = 1;

    for (i = 0; i < 256; i++) {
        pcd_lut_gray[i] = (int16_t)((i * PCD_LUM_MUL) >> 8);
        pcd_lut_red[i] = (int16_t)((PCD_RED_ADD + i * PCD_RED_MUL) >> 8);
        pcd_lut_blue[i] = (int16_t)((PCD_BLUE_ADD + i * PCD_BLUE_MUL) >> 8);
        pcd_lut_green1[i] = (int16_t)((PCD_GREEN1_ADD + i * PCD_GREEN1_MUL) >> 8);
        pcd_lut_green2[i] = (int16_t)((PCD_GREEN2_ADD + i * PCD_GREEN2_MUL) >> 8);
    }
    for (i = 0; i < PCD_YCC_RANGE; i++)
        pcd_lut_range[i] = 0;
    for (; i < PCD_YCC_RANGE + 256; i++)
        pcd_lut_range[i] = i - PCD_YCC_RANGE;
    for (; i < 256 + 2 * PCD_YCC_RANGE; i++)
        pcd_lut_range[i] = 255;
}

static inline void photoycc_to_rgb(uint8_t Y, uint8_t Cb, uint8_t Cr,
                                   uint8_t* r, uint8_t* g, uint8_t* b) {
    int gray = pcd_lut_gray[Y];
    int idx_r = PCD_YCC_RANGE + gray + pcd_lut_red[Cr];
    int idx_g = PCD_YCC_RANGE + gray + pcd_lut_green1[Cr] + pcd_lut_green2[Cb];
    int idx_b = PCD_YCC_RANGE + gray + pcd_lut_blue[Cb];

    if (idx_r < 0)
        idx_r = 0;
    else if (idx_r >= 256 + 2 * PCD_YCC_RANGE)
        idx_r = 256 + 2 * PCD_YCC_RANGE - 1;
    if (idx_g < 0)
        idx_g = 0;
    else if (idx_g >= 256 + 2 * PCD_YCC_RANGE)
        idx_g = 256 + 2 * PCD_YCC_RANGE - 1;
    if (idx_b < 0)
        idx_b = 0;
    else if (idx_b >= 256 + 2 * PCD_YCC_RANGE)
        idx_b = 256 + 2 * PCD_YCC_RANGE - 1;

    *r = (uint8_t)pcd_lut_range[idx_r];
    *g = (uint8_t)pcd_lut_range[idx_g];
    *b = (uint8_t)pcd_lut_range[idx_b];
}

/* ========================================================================
 * Orientation Handling
 * ======================================================================== */

static uint8_t read_orientation(FILE* f) {
    long saved_pos = ftell(f);

    if (fseek(f, PCD_ORIENTATION_OFFSET, SEEK_SET) != 0) {
        fseek(f, saved_pos, SEEK_SET);
        return 0;
    }

    uint8_t orient = 0;
    if (fread(&orient, 1, 1, f) != 1) {
        fseek(f, saved_pos, SEEK_SET);
        return 0;
    }

    fseek(f, saved_pos, SEEK_SET);
    if (orient == 8)
        orient = 0; /* variant encoding */
    return orient;
}

/* ========================================================================
 * File Offset Calculation
 * ======================================================================== */

static uint32_t pcd_data_offset_for_level(int level) {
    /* res 1 at 8192, res 2 at 47104, res 3 at 196608 */
    static const uint32_t pcd_strip_start[] = {8192, 47104, 196608};

    if (level <= 2)
        return pcd_strip_start[level];

    /* Base and higher: we decode by upsampling; no raw macroblock read */
    (void)level;
    return 196608; /* unused for level > 2 */
}

/* ========================================================================
 * Strip Decoder (Overview / Base/16 / Base/4)
 * ======================================================================== */

/* Strip layout: per 2-line strip:
 *   luma line0 (width bytes), luma line1 (width bytes),
 *   Cb (width/2 bytes), Cr (width/2 bytes) — chroma planar, not interleaved. */
static bool decode_strip_level(FILE* f, const PCDResolutionInfo* res,
                               uint8_t** out_buffer, uint32_t* out_stride,
                               uint8_t orientation) {
    uint32_t width = res->width;
    uint32_t height = res->height;
    uint32_t stride = width * 4;
    uint32_t chroma_w = width / 2;

    uint8_t* buffer = g_malloc(height * stride);
    if (!buffer)
        return false;

    uint8_t* y0 = g_malloc(width);
    uint8_t* y1 = g_malloc(width);
    uint8_t* cb = g_malloc(chroma_w);
    uint8_t* cr = g_malloc(chroma_w);
    if (!y0 || !y1 || !cb || !cr)
        goto fail;

    uint32_t strips = height / 2;
    for (uint32_t s = 0; s < strips; s++) {
        if (fread(y0, 1, width, f) != width ||
            fread(y1, 1, width, f) != width ||
            fread(cb, 1, chroma_w, f) != chroma_w ||
            fread(cr, 1, chroma_w, f) != chroma_w)
            goto fail;

        uint8_t* out0 = buffer + (s * 2 + 0) * stride;
        uint8_t* out1 = buffer + (s * 2 + 1) * stride;

        for (uint32_t x = 0; x < width; x++) {
            uint8_t cb_val = cb[x >> 1];
            uint8_t cr_val = cr[x >> 1];
            uint8_t r, g, b;

            photoycc_to_rgb(y0[x], cb_val, cr_val, &r, &g, &b);
            out0[x * 4 + 0] = b;
            out0[x * 4 + 1] = g;
            out0[x * 4 + 2] = r;
            out0[x * 4 + 3] = 255;

            photoycc_to_rgb(y1[x], cb_val, cr_val, &r, &g, &b);
            out1[x * 4 + 0] = b;
            out1[x * 4 + 1] = g;
            out1[x * 4 + 2] = r;
            out1[x * 4 + 3] = 255;
        }
    }

    /* Apply rotation if needed */
    if (orientation != 0) {
        uint32_t rot_width = (orientation == 1 || orientation == 3) ? height : width;
        uint32_t rot_height = (orientation == 1 || orientation == 3) ? width : height;
        uint8_t* rotated = g_malloc(rot_width * rot_height * 4);
        if (!rotated)
            goto fail;

        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint8_t* src = buffer + y * stride + x * 4;
                uint8_t* dst = NULL;

                switch (orientation) {
                    case 1:
                        dst = rotated + x * rot_width * 4 + (rot_width - 1 - y) * 4;
                        break;
                    case 2:
                        dst = rotated + (rot_height - 1 - y) * rot_width * 4 + (rot_width - 1 - x) * 4;
                        break;
                    case 3:
                        dst = rotated + (rot_height - 1 - x) * rot_width * 4 + y * 4;
                        break;
                    case 8:
                        dst = rotated + x * rot_width * 4 + y * 4;
                        break;
                }

                if (dst)
                    memcpy(dst, src, 4);
            }
        }

        g_free(buffer);
        buffer = rotated;
        stride = rot_width * 4;
    }

    *out_buffer = buffer;
    *out_stride = stride;
    g_free(y0);
    g_free(y1);
    g_free(cb);
    g_free(cr);
    return true;

fail:
    g_free(buffer);
    g_free(y0);
    g_free(y1);
    g_free(cb);
    g_free(cr);
    return false;
}

/* Forward declaration: high-res path calls load_pcd_image recursively */
static bool load_pcd_image(FILE* f, PCDResolutionLevel level, uint8_t orientation,
                           uint8_t** out_buffer, uint32_t* out_stride);

/* ========================================================================
 * Planar strip decode (Y, Cb, Cr) for Huffman pipeline
 * ======================================================================== */
static bool decode_strip_to_planar(FILE* f, int level,
                                   uint8_t** out_luma, uint8_t** out_cb, uint8_t** out_cr,
                                   uint32_t* out_w, uint32_t* out_h) {
    const PCDResolutionInfo* res = &pcd_resolutions[level];
    uint32_t width = res->width;
    uint32_t height = res->height;
    uint32_t chroma_w = width / 2;
    uint32_t chroma_h = height / 2;

    uint32_t offset = pcd_data_offset_for_level(level);
    if (fseek(f, offset, SEEK_SET) != 0)
        return false;

    uint8_t* luma = g_malloc(width * height);
    uint8_t* cb = g_malloc(chroma_w * chroma_h);
    uint8_t* cr = g_malloc(chroma_w * chroma_h);
    if (!luma || !cb || !cr) {
        g_free(luma);
        g_free(cb);
        g_free(cr);
        return false;
    }

    uint8_t* y0 = g_malloc(width);
    uint8_t* y1 = g_malloc(width);
    uint8_t* cb_row = g_malloc(chroma_w);
    uint8_t* cr_row = g_malloc(chroma_w);
    if (!y0 || !y1 || !cb_row || !cr_row) {
        g_free(luma);
        g_free(cb);
        g_free(cr);
        g_free(y0);
        g_free(y1);
        g_free(cb_row);
        g_free(cr_row);
        return false;
    }

    uint32_t strips = height / 2;
    for (uint32_t s = 0; s < strips; s++) {
        if (fread(y0, 1, width, f) != width ||
            fread(y1, 1, width, f) != width ||
            fread(cb_row, 1, chroma_w, f) != chroma_w ||
            fread(cr_row, 1, chroma_w, f) != chroma_w) {
            g_free(luma);
            g_free(cb);
            g_free(cr);
            g_free(y0);
            g_free(y1);
            g_free(cb_row);
            g_free(cr_row);
            return false;
        }
        memcpy(luma + (s * 2 + 0) * width, y0, width);
        memcpy(luma + (s * 2 + 1) * width, y1, width);
        memcpy(cb + s * chroma_w, cb_row, chroma_w);
        memcpy(cr + s * chroma_w, cr_row, chroma_w);
    }

    g_free(y0);
    g_free(y1);
    g_free(cb_row);
    g_free(cr_row);

    *out_luma = luma;
    *out_cb = cb;
    *out_cr = cr;
    *out_w = width;
    *out_h = height;
    return true;
}

/* Planar 2x horizontal upsample: read from src (half_w per row), write to dest (width per row). */
static void inter_pixels_planar_srcdest(const uint8_t* src, uint8_t* dest,
                                        uint32_t width, uint32_t height) {
    uint32_t half_w = width / 2;
    int y;
    for (y = (int)(height - 2); y >= 0; y -= 2) {
        const uint8_t* row_src = src + (y >> 1) * half_w;
        uint8_t* row_dest = dest + y * width;
        uint32_t x;
        row_dest[width - 2] = row_dest[width - 1] = row_src[half_w - 1];
        for (x = half_w - 1; x > 0; x--) {
            uint32_t sx = x - 1;
            row_dest[sx * 2] = row_src[sx];
            row_dest[sx * 2 + 1] = (row_src[sx] + row_src[sx + 1] + 1) >> 1;
        }
        row_dest[0] = row_src[0];
        row_dest[1] = (row_src[0] + row_src[1] + 1) >> 1;
    }
}

static void inter_lines_planar(uint8_t* plane, uint32_t width, uint32_t height) {
    uint32_t y;
    for (y = 0; y + 2 < height; y += 2) {
        uint8_t* src1 = plane + y * width;
        uint8_t* src2 = plane + (y + 2) * width;
        uint8_t* dest = plane + (y + 1) * width;
        uint32_t x;
        for (x = 0; x + 2 < width; x += 2) {
            dest[x] = (src1[x] + src2[x] + 1) >> 1;
            dest[x + 1] = (src1[x] + src2[x] + src1[x + 2] + src2[x + 2] + 2) >> 2;
        }
        dest[x] = dest[x + 1] = (src1[x] + src2[x] + 1) >> 1;
    }
    /* last odd line: copy from row height-2 */
    if (height >= 2) {
        uint8_t* src1 = plane + (height - 2) * width;
        uint8_t* dest = plane + (height - 1) * width;
        uint32_t x;
        for (x = 0; x + 2 < width; x += 2) {
            dest[x] = src1[x];
            dest[x + 1] = (src1[x] + src1[x + 2] + 1) >> 1;
        }
        dest[width - 2] = dest[width - 1] = src1[width - 2];
    }
}

/* Upsample planar from (w,h) to (2w,2h). Allocates new buffers; caller frees old. */
static bool upsample_planar_2x(uint8_t* luma, uint8_t* cb, uint8_t* cr,
                               uint32_t w, uint32_t h,
                               uint8_t** out_luma, uint8_t** out_cb, uint8_t** out_cr,
                               uint32_t* out_w, uint32_t* out_h) {
    uint32_t w2 = w * 2;
    uint32_t h2 = h * 2;
    uint32_t cw = w / 2;
    uint32_t ch = h / 2;
    uint32_t cw2 = w;
    uint32_t ch2 = h;

    uint8_t* L = g_malloc(w2 * h2);
    uint8_t* Cb = g_malloc(cw2 * ch2);
    uint8_t* Cr = g_malloc(cw2 * ch2);
    if (!L || !Cb || !Cr) {
        g_free(L);
        g_free(Cb);
        g_free(Cr);
        return false;
    }
    inter_pixels_planar_srcdest(luma, L, w2, h2);
    inter_lines_planar(L, w2, h2);
    inter_pixels_planar_srcdest(cb, Cb, cw2, ch2);
    inter_lines_planar(Cb, cw2, ch2);
    inter_pixels_planar_srcdest(cr, Cr, cw2, ch2);
    inter_lines_planar(Cr, cw2, ch2);

    g_free(luma);
    g_free(cb);
    g_free(cr);

    *out_luma = L;
    *out_cb = Cb;
    *out_cr = Cr;
    *out_w = w2;
    *out_h = h2;
    return true;
}

/* ========================================================================
 * Huffman decode
 * HUFF1 = 0xc2000; stream after table is (pos+2047)&~0x3ff; next block +0x6000 aligned
 * ======================================================================== */
#define PCD_HUFF1 0xc2000
#define PCD_HTABLE_MAX 0x10000

/* Read up to 3 bytes at stream into 24-bit value (high byte first); missing bytes are 0. */
static inline uint32_t pcd_peek24(const uint8_t* stream, const uint8_t* stream_end) {
    uint32_t v = 0;
    if (stream < stream_end) {
        v = (uint32_t)*stream << 16;
        stream++;
        if (stream < stream_end) {
            v |= (uint32_t)*stream << 8;
            stream++;
            if (stream < stream_end)
                v |= *stream;
        }
    }
    return v;
}

static int pcd_read_htable(const uint8_t* src, uint8_t** pseq, uint8_t** pbits) {
    int i, len, seq, seq2, bits, j;

    if (*pseq)
        g_free(*pseq);
    if (*pbits)
        g_free(*pbits);
    *pseq = g_malloc(PCD_HTABLE_MAX);
    *pbits = g_malloc(PCD_HTABLE_MAX);
    if (!*pseq || !*pbits)
        return -1;
    memset(*pseq, 0, PCD_HTABLE_MAX);
    memset(*pbits, 0, PCD_HTABLE_MAX);

    for (i = 1, len = src[0]; len >= 0; i += 4, len--) {
        seq = ((int)src[i + 1] << 8) | src[i + 2];
        bits = src[i] + 1;
        seq2 = seq + (PCD_HTABLE_MAX >> bits);
        for (j = seq; j < seq2; j++) {
            (*pseq)[j] = src[i + 3];
            (*pbits)[j] = (uint8_t)bits;
        }
    }
    PCD_DBG("htable: consumed %d bytes (len=%d)\n", i, (int)(unsigned char)src[0]);
    return i;
}

/* Decode Huffman residual stream (run=1: Base 1536×1024; run=2: 4Base 3072×2048; run=3: 16Base 6144×4096). */
static int pcd_un_huff(const uint8_t* start, size_t stream_size,
                       int run, uint32_t width, uint32_t height,
                       uint8_t* luma, uint8_t* cb, uint8_t* cr,
                       uint8_t* seq1, uint8_t* len1,
                       uint8_t* seq2, uint8_t* len2,
                       uint8_t* seq3, uint8_t* len3) {
    const uint8_t* stream = start;
    const uint8_t* stream_end = start + stream_size;
    int shiftreg, bit = 0;
    uint32_t h = (run == 1) ? 1024u : (run == 2) ? 2048u
                                                 : 4096u;
    uint32_t y1 = 0, y2 = h;
    uint32_t num_lines = (run == 1) ? (1024u + 512u + 512u) : (run == 2) ? (2048u + 1024u + 1024u)
                                                                         : (4096u + 2048u + 2048u);
    uint32_t line_count = 0;

    (void)height;
    PCD_DBG("un_huff run=%d stream_size=%zu num_lines=%u\n", run, stream_size, num_lines);

    while (line_count < num_lines) {
        if (stream >= stream_end) {
            PCD_DBG("un_huff: stream end (line_count=%u)\n", line_count);
            break;
        }
        if (stream + 4 > stream_end) {
            if (line_count > 0)
                break; /* trailing padding bytes */
            PCD_DBG("un_huff: need 4 bytes, have %zu (line_count=0)\n", (size_t)(stream_end - stream));
            return -1;
        }
        for (;;) {
            size_t search_len = (size_t)(stream_end - stream);
            const uint8_t* p = search_len ? memchr(stream, 0xff, search_len) : NULL;
            if (!p || p + 2 > stream_end) {
                if (line_count > 0) {
                    /* No more sync in remaining bytes; treat as end of stream (padding) */
                    PCD_DBG("un_huff: no sync in %zu remaining bytes, line_count=%u (padding)\n", search_len, line_count);
                    goto done;
                }
                PCD_DBG("un_huff: no 0xff 0xfe sync (p=%p search_len=%zu)\n", (void*)p, search_len);
                if (PCD_DEBUG && search_len >= 64) {
                    fprintf(stderr, "PCD: first 64 bytes at stream: ");
                    for (size_t i = 0; i < 64; i++)
                        fprintf(stderr, "%02x ", (unsigned)stream[i]);
                    fprintf(stderr, "\n");
                }
                return -1;
            }
            stream = p;
            if (stream[1] == 0xfe) /* sync 0xff 0xfe */
                break;
            stream++;
        }
        if (stream + 4 > stream_end) {
            PCD_DBG("un_huff: after sync need 4 bytes\n");
            return -1;
        }
        shiftreg = ((stream[0] << 16) | (stream[1] << 8) | stream[2]) >> 8;
        bit = 0;
        while (shiftreg != 0xfffe) {
            shiftreg = ((shiftreg << 1) & 0xffff) | ((stream[2] >> (7 - bit)) & 1);
            bit++;
            if (bit == 8) {
                stream += 3;
                bit = 0;
                if (stream + 3 > stream_end) {
                    PCD_DBG("un_huff: in 0xfffe loop need 3 bytes\n");
                    return -1;
                }
                shiftreg = ((stream[0] << 16) | (stream[1] << 8) | stream[2]) >> 8;
            }
        }
        stream += 2;
        if (stream >= stream_end) {
            PCD_DBG("un_huff: after 0xfffe need 1 byte\n");
            return -1;
        }
        bit += 8;
        stream += bit >> 3;
        bit &= 7;
        if (stream >= stream_end) {
            PCD_DBG("un_huff: before line header need 1 byte\n");
            return -1;
        }
        shiftreg = (int)(pcd_peek24(stream, stream_end) >> (8 - bit));
        {
            /* yy: 11 bits run=1 (2048 lines), 12 bits run=2 (4096), 13 bits run=3 (8192) */
            uint32_t yy_bits = (run == 1) ? 11u : (run == 2) ? 12u
                                                             : 13u;
            uint32_t yy_mask = (1u << yy_bits) - 1u;
            int yy = (int)((shiftreg >> 1) & yy_mask);
            int type = (shiftreg >> (1 + yy_bits)) & 3;
            {
                size_t step = (size_t)(stream_end - stream);
                stream += (step >= 2) ? 2 : step;
            }
            if (stream > stream_end) {
                PCD_DBG("un_huff: stream past end after line header\n");
                return -1;
            }
            if ((uint32_t)yy >= num_lines) {
                PCD_DBG("un_huff: yy=%d >= num_lines=%u\n", yy, num_lines);
                return -1;
            }
            if ((uint32_t)yy >= y1 && (uint32_t)yy < y2) {
                uint8_t* data;
                uint8_t* seq = seq1;
                uint8_t* bits = len1;
                int x, shift = 0;
                int x2;

                if (type == 2) {
                    seq = seq2;
                    bits = len2;
                    shift = 1;
                    data = cb ? cb + ((yy - y1) >> shift) * (width >> shift) : NULL;
                } else if (type == 3) {
                    seq = seq3;
                    bits = len3;
                    shift = 1;
                    data = cr ? cr + ((yy - y1) >> shift) * (width >> shift) : NULL;
                } else {
                    data = luma ? luma + (yy - y1) * width : NULL;
                }

                x2 = (int)(width >> shift);
                for (x = 0; x < x2 && stream < stream_end; x++) {
                    if (shiftreg >= PCD_HTABLE_MAX)
                        break;
                    if (data) {
                        int sum = (int)data[x] + (signed char)seq[shiftreg];
                        data[x] = (uint8_t)pcd_lut_range[PCD_YCC_RANGE + sum];
                    }
                    bit += bits[shiftreg];
                    stream += bit >> 3;
                    bit &= 7;
                    if (stream >= stream_end)
                        break;
                    shiftreg = (int)(pcd_peek24(stream, stream_end) >> (8 - bit));
                }
            }
            line_count++;
        }
    }

done:
    /* Return bytes consumed so caller can advance (pos += rc). */
    {
        int ret = (int)(stream - start);
        PCD_DBG("un_huff: ok line_count=%u consumed=%d\n", line_count, ret);
        return ret;
    }
}

static uint8_t* upsample_2x_bgra(const uint8_t* src, uint32_t w, uint32_t h, uint32_t src_stride) {
    uint32_t w2 = w * 2;
    uint32_t h2 = h * 2;
    uint32_t dst_stride = w2 * 4;
    uint8_t* dst = g_malloc(h2 * dst_stride);
    if (!dst)
        return NULL;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* row = src + y * src_stride;
        uint8_t* out0 = dst + (y * 2 + 0) * dst_stride;
        uint8_t* out1 = dst + (y * 2 + 1) * dst_stride;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t* p = row + x * 4;
            uint8_t* q0 = out0 + (x * 2) * 4;
            uint8_t* q1 = out1 + (x * 2) * 4;
            memcpy(q0 + 0, p, 4);
            memcpy(q0 + 4, p, 4);
            memcpy(q1 + 0, p, 4);
            memcpy(q1 + 4, p, 4);
        }
    }
    return dst;
}

/* Convert planar Y (WxH), Cb/Cr (W/2 x H/2) to BGRA. */
static uint8_t* planar_to_bgra(const uint8_t* luma, const uint8_t* cb, const uint8_t* cr,
                               uint32_t width, uint32_t height) {
    uint32_t stride = width * 4;
    uint8_t* out = g_malloc(height * stride);
    if (!out)
        return NULL;
    uint32_t chroma_w = width / 2;
    uint32_t y, x;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t Y = luma[y * width + x];
            uint8_t Cb = cb[(y >> 1) * chroma_w + (x >> 1)];
            uint8_t Cr = cr[(y >> 1) * chroma_w + (x >> 1)];
            uint8_t r, g, b;
            photoycc_to_rgb(Y, Cb, Cr, &r, &g, &b);
            uint8_t* p = out + y * stride + x * 4;
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = 255;
        }
    }
    return out;
}

/* Base (1536×1024) with Huffman residual. */
static bool decode_base_with_huffman(FILE* f, uint8_t** out_buffer, uint32_t* out_stride,
                                     uint8_t orientation) {
    uint8_t *luma = NULL, *cb = NULL, *cr = NULL;
    uint32_t w, h;
    uint8_t *seq1 = NULL, *len1 = NULL;
    uint8_t* buf = NULL;
    size_t buf_size;
    long file_end;

    PCD_DBG("decode_base_with_huffman: start\n");
    if (!decode_strip_to_planar(f, PCD_RES_BASE4, &luma, &cb, &cr, &w, &h)) {
        PCD_DBG("decode_base_with_huffman: decode_strip_to_planar failed\n");
        return false;
    }
    if (w != 768 || h != 512) {
        PCD_DBG("decode_base_with_huffman: bad dimensions %u x %u\n", w, h);
        g_free(luma);
        g_free(cb);
        g_free(cr);
        return false;
    }
    if (!upsample_planar_2x(luma, cb, cr, w, h, &luma, &cb, &cr, &w, &h)) {
        PCD_DBG("decode_base_with_huffman: upsample_planar_2x failed\n");
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        PCD_DBG("decode_base_with_huffman: fseek END failed\n");
        goto fail_planar;
    }
    file_end = ftell(f);
    if (file_end < (long)(PCD_HUFF1 + 65536)) {
        PCD_DBG("decode_base_with_huffman: file too small %ld\n", (long)file_end);
        goto fail_planar;
    }
    buf_size = (size_t)(file_end - PCD_HUFF1);
    buf = g_malloc(buf_size);
    if (!buf || fseek(f, PCD_HUFF1, SEEK_SET) != 0 || fread(buf, 1, buf_size, f) != buf_size) {
        PCD_DBG("decode_base_with_huffman: alloc/seek/read Huff block failed buf_size=%zu\n", buf_size);
        goto fail_planar;
    }

    pcd_ycc_lut_init();
    {
        int rc = pcd_read_htable(buf, &seq1, &len1);
        if (rc < 0) {
            PCD_DBG("decode_base_with_huffman: pcd_read_htable failed\n");
            goto fail_buf;
        }
        rc = (rc + 2047) & ~0x3ff;
        if ((size_t)rc >= buf_size) {
            PCD_DBG("decode_base_with_huffman: htable aligned %d >= buf_size %zu\n", rc, buf_size);
            goto fail_buf;
        }
        rc = pcd_un_huff(buf + rc, buf_size - (size_t)rc, 1, 1536, 1024,
                         luma, cb, cr, seq1, len1, seq1, len1, seq1, len1);
        if (rc < 0) {
            PCD_DBG("decode_base_with_huffman: pcd_un_huff(Base) failed rc=%d\n", rc);
            goto fail_buf;
        }
    }

    {
        uint8_t* bgra = planar_to_bgra(luma, cb, cr, 1536, 1024);
        g_free(luma);
        g_free(cb);
        g_free(cr);
        g_free(seq1);
        g_free(len1);
        g_free(buf);
        if (!bgra)
            return false;
        if (orientation != 0) {
            uint32_t rot_w = (orientation == 1 || orientation == 3) ? 1024u : 1536u;
            uint32_t rot_h = (orientation == 1 || orientation == 3) ? 1536u : 1024u;
            uint8_t* rot = g_malloc(rot_w * rot_h * 4);
            if (!rot) {
                g_free(bgra);
                return false;
            }
            uint32_t x, y;
            for (y = 0; y < 1024; y++)
                for (x = 0; x < 1536; x++) {
                    uint8_t* src = bgra + y * 1536 * 4 + x * 4;
                    uint8_t* dst = NULL;
                    if (orientation == 1)
                        dst = rot + x * rot_w * 4 + (rot_w - 1 - y) * 4;
                    else if (orientation == 2)
                        dst = rot + (rot_h - 1 - y) * rot_w * 4 + (rot_w - 1 - x) * 4;
                    else if (orientation == 3)
                        dst = rot + (rot_h - 1 - x) * rot_w * 4 + y * 4;
                    if (dst)
                        memcpy(dst, src, 4);
                }
            g_free(bgra);
            bgra = rot;
            *out_stride = rot_w * 4;
        } else
            *out_stride = 1536 * 4;
        *out_buffer = bgra;
    }
    PCD_DBG("decode_base_with_huffman: ok\n");
    return true;

fail_buf:
    PCD_DBG("decode_base_with_huffman: fail_buf\n");
    g_free(buf);
fail_planar:
    g_free(luma);
    g_free(cb);
    g_free(cr);
    g_free(seq1);
    g_free(len1);
    return false;
}

/* 4Base (3072×2048) with Huffman residual. */
static bool decode_4base_with_huffman(FILE* f, uint8_t** out_buffer, uint32_t* out_stride,
                                      uint8_t orientation) {
    uint8_t *luma = NULL, *cb = NULL, *cr = NULL;
    uint32_t w = 768, h = 512;
    uint8_t *seq1 = NULL, *len1 = NULL, *seq2 = NULL, *len2 = NULL, *seq3 = NULL, *len3 = NULL;
    uint8_t* buf = NULL;
    size_t buf_size;
    long file_end;
    int pos, rc;

    PCD_DBG("decode_4base_with_huffman: start\n");
    if (!decode_strip_to_planar(f, PCD_RES_BASE4, &luma, &cb, &cr, &w, &h)) {
        PCD_DBG("decode_4base_with_huffman: decode_strip_to_planar failed\n");
        return false;
    }
    if (!upsample_planar_2x(luma, cb, cr, w, h, &luma, &cb, &cr, &w, &h)) {
        PCD_DBG("decode_4base_with_huffman: upsample_planar_2x(1) failed\n");
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0)
        goto fail_planar;
    file_end = ftell(f);
    if (file_end < (long)(PCD_HUFF1 + 131072)) {
        PCD_DBG("decode_4base_with_huffman: file too small %ld\n", (long)file_end);
        goto fail_planar;
    }
    buf_size = (size_t)(file_end - PCD_HUFF1);
    buf = g_malloc(buf_size);
    if (!buf || fseek(f, PCD_HUFF1, SEEK_SET) != 0 || fread(buf, 1, buf_size, f) != buf_size) {
        PCD_DBG("decode_4base_with_huffman: alloc/seek/read failed\n");
        goto fail_planar;
    }

    pcd_ycc_lut_init();
    pos = pcd_read_htable(buf, &seq1, &len1);
    if (pos < 0) {
        PCD_DBG("decode_4base_with_huffman: htable(1) failed\n");
        goto fail_buf;
    }
    pos = (pos + 2047) & ~0x3ff;
    rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 1, 1536, 1024,
                     luma, cb, cr, seq1, len1, seq1, len1, seq1, len1);
    if (rc < 0) {
        PCD_DBG("decode_4base_with_huffman: un_huff(Base) failed rc=%d\n", rc);
        goto fail_buf;
    }
    pos += rc;
    if (!upsample_planar_2x(luma, cb, cr, 1536, 1024, &luma, &cb, &cr, &w, &h)) {
        PCD_DBG("decode_4base_with_huffman: upsample_planar_2x(2) failed\n");
        goto fail_buf;
    }
    if ((size_t)pos + 4096 > buf_size) {
        PCD_DBG("decode_4base_with_huffman: not enough for 4Base residual (%zu left)\n", buf_size - (size_t)pos);
        goto fail_buf;
    }
    rc = pcd_read_htable(buf + pos, &seq1, &len1);
    if (rc < 0) {
        PCD_DBG("decode_4base_with_huffman: htable(seq1) failed\n");
        goto fail_buf;
    }
    pos += rc;
    rc = pcd_read_htable(buf + pos, &seq2, &len2);
    if (rc < 0) {
        PCD_DBG("decode_4base_with_huffman: htable(seq2) failed\n");
        goto fail_buf;
    }
    pos += rc;
    rc = pcd_read_htable(buf + pos, &seq3, &len3);
    if (rc < 0) {
        PCD_DBG("decode_4base_with_huffman: htable(seq3) failed\n");
        goto fail_buf;
    }
    pos += rc;
    pos = (pos + 2047) & ~0x3ff;
    if ((size_t)pos >= buf_size) {
        PCD_DBG("decode_4base_with_huffman: pos %d >= buf_size %zu\n", pos, buf_size);
        goto fail_buf;
    }
    rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 2, 3072, 2048,
                     luma, cb, cr, seq1, len1, seq2, len2, seq3, len3);
    if (rc < 0) {
        PCD_DBG("decode_4base_with_huffman: un_huff(4Base) failed rc=%d\n", rc);
        goto fail_buf;
    }

    {
        uint8_t* bgra = planar_to_bgra(luma, cb, cr, 3072, 2048);
        g_free(luma);
        g_free(cb);
        g_free(cr);
        g_free(seq1);
        g_free(len1);
        g_free(seq2);
        g_free(len2);
        g_free(seq3);
        g_free(len3);
        g_free(buf);
        if (!bgra)
            return false;
        if (orientation != 0) {
            uint32_t rot_w = (orientation == 1 || orientation == 3) ? 2048u : 3072u;
            uint32_t rot_h = (orientation == 1 || orientation == 3) ? 3072u : 2048u;
            uint8_t* rot = g_malloc(rot_w * rot_h * 4);
            if (!rot) {
                g_free(bgra);
                return false;
            }
            uint32_t x, y;
            for (y = 0; y < 2048; y++)
                for (x = 0; x < 3072; x++) {
                    uint8_t* src = bgra + y * 3072 * 4 + x * 4;
                    uint8_t* dst = NULL;
                    if (orientation == 1)
                        dst = rot + x * rot_w * 4 + (rot_w - 1 - y) * 4;
                    else if (orientation == 2)
                        dst = rot + (rot_h - 1 - y) * rot_w * 4 + (rot_w - 1 - x) * 4;
                    else if (orientation == 3)
                        dst = rot + (rot_h - 1 - x) * rot_w * 4 + y * 4;
                    if (dst)
                        memcpy(dst, src, 4);
                }
            g_free(bgra);
            bgra = rot;
            *out_stride = rot_w * 4;
        } else
            *out_stride = 3072 * 4;
        *out_buffer = bgra;
    }
    PCD_DBG("decode_4base_with_huffman: ok\n");
    return true;

fail_buf:
    PCD_DBG("decode_4base_with_huffman: fail_buf\n");
    g_free(buf);
fail_planar:
    g_free(luma);
    g_free(cb);
    g_free(cr);
    g_free(seq1);
    g_free(len1);
    g_free(seq2);
    g_free(len2);
    g_free(seq3);
    g_free(len3);
    return false;
}

/* 16Base (6144×4096) with Huffman residual (PhotoCD Pro optional). */
static bool decode_16base_with_huffman(FILE* f, uint8_t** out_buffer, uint32_t* out_stride,
                                       uint8_t orientation) {
    uint8_t *luma = NULL, *cb = NULL, *cr = NULL;
    uint32_t w = 768, h = 512;
    uint8_t *seq1 = NULL, *len1 = NULL, *seq2 = NULL, *len2 = NULL, *seq3 = NULL, *len3 = NULL;
    uint8_t* buf = NULL;
    size_t buf_size;
    long file_end;
    int pos, rc;

    PCD_DBG("decode_16base_with_huffman: start\n");
    if (!decode_strip_to_planar(f, PCD_RES_BASE4, &luma, &cb, &cr, &w, &h))
        return false;
    if (!upsample_planar_2x(luma, cb, cr, w, h, &luma, &cb, &cr, &w, &h))
        return false;
    if (fseek(f, 0, SEEK_END) != 0)
        goto fail_planar;
    file_end = ftell(f);
    if (file_end < (long)(PCD_HUFF1 + 262144))
        goto fail_planar;
    buf_size = (size_t)(file_end - PCD_HUFF1);
    buf = g_malloc(buf_size);
    if (!buf || fseek(f, PCD_HUFF1, SEEK_SET) != 0 || fread(buf, 1, buf_size, f) != buf_size)
        goto fail_planar;

    pcd_ycc_lut_init();
    pos = pcd_read_htable(buf, &seq1, &len1);
    if (pos < 0)
        goto fail_buf;
    pos = (pos + 2047) & ~0x3ff;
    rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 1, 1536, 1024,
                     luma, cb, cr, seq1, len1, seq1, len1, seq1, len1);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    if (!upsample_planar_2x(luma, cb, cr, 1536, 1024, &luma, &cb, &cr, &w, &h))
        goto fail_buf;
    if ((size_t)pos + 4096 > buf_size)
        goto fail_buf;
    rc = pcd_read_htable(buf + pos, &seq1, &len1);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    rc = pcd_read_htable(buf + pos, &seq2, &len2);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    rc = pcd_read_htable(buf + pos, &seq3, &len3);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    pos = (pos + 2047) & ~0x3ff;
    if ((size_t)pos >= buf_size)
        goto fail_buf;
    rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 2, 3072, 2048,
                     luma, cb, cr, seq1, len1, seq2, len2, seq3, len3);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    if (!upsample_planar_2x(luma, cb, cr, 3072, 2048, &luma, &cb, &cr, &w, &h))
        goto fail_buf;
    if ((size_t)pos + 4096 > buf_size) {
        PCD_DBG("decode_16base_with_huffman: not enough for 16Base residual\n");
        goto fail_buf;
    }
    rc = pcd_read_htable(buf + pos, &seq1, &len1);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    rc = pcd_read_htable(buf + pos, &seq2, &len2);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    rc = pcd_read_htable(buf + pos, &seq3, &len3);
    if (rc < 0)
        goto fail_buf;
    pos += rc;
    pos = (pos + 2047) & ~0x3ff;
    if ((size_t)pos >= buf_size)
        goto fail_buf;
    rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 3, 6144, 4096,
                     luma, cb, cr, seq1, len1, seq2, len2, seq3, len3);
    if (rc < 0) {
        PCD_DBG("decode_16base_with_huffman: un_huff(16Base) failed rc=%d\n", rc);
        goto fail_buf;
    }

    {
        uint8_t* bgra = planar_to_bgra(luma, cb, cr, 6144, 4096);
        g_free(luma);
        g_free(cb);
        g_free(cr);
        g_free(seq1);
        g_free(len1);
        g_free(seq2);
        g_free(len2);
        g_free(seq3);
        g_free(len3);
        g_free(buf);
        if (!bgra)
            return false;
        if (orientation != 0) {
            uint32_t rot_w = (orientation == 1 || orientation == 3) ? 4096u : 6144u;
            uint32_t rot_h = (orientation == 1 || orientation == 3) ? 6144u : 4096u;
            uint8_t* rot = g_malloc(rot_w * rot_h * 4);
            if (!rot) {
                g_free(bgra);
                return false;
            }
            uint32_t x, y;
            for (y = 0; y < 4096; y++)
                for (x = 0; x < 6144; x++) {
                    uint8_t* src = bgra + y * 6144 * 4 + x * 4;
                    uint8_t* dst = NULL;
                    if (orientation == 1)
                        dst = rot + x * rot_w * 4 + (rot_w - 1 - y) * 4;
                    else if (orientation == 2)
                        dst = rot + (rot_h - 1 - y) * rot_w * 4 + (rot_w - 1 - x) * 4;
                    else if (orientation == 3)
                        dst = rot + (rot_h - 1 - x) * rot_w * 4 + y * 4;
                    if (dst)
                        memcpy(dst, src, 4);
                }
            g_free(bgra);
            bgra = rot;
            *out_stride = rot_w * 4;
        } else
            *out_stride = 6144 * 4;
        *out_buffer = bgra;
    }
    PCD_DBG("decode_16base_with_huffman: ok\n");
    return true;

fail_buf:
    g_free(buf);
fail_planar:
    g_free(luma);
    g_free(cb);
    g_free(cr);
    g_free(seq1);
    g_free(len1);
    g_free(seq2);
    g_free(len2);
    g_free(seq3);
    g_free(len3);
    return false;
}

static bool decode_highres_level(FILE* f, PCDResolutionLevel level,
                                 uint8_t** out_buffer, uint32_t* out_stride,
                                 uint8_t orientation) {
    const PCDResolutionInfo* res = &pcd_resolutions[level];

    PCD_DBG("decode_highres_level: level=%d\n", (int)level);
    if (level == PCD_RES_BASE) {
        long sz = 0;
        if (fseek(f, 0, SEEK_END) == 0)
            sz = ftell(f);
        PCD_DBG("decode_highres_level: BASE file_sz=%ld need=%ld\n", sz, (long)(PCD_HUFF1 + 65536));
        if (sz >= (long)(PCD_HUFF1 + 65536))
            return decode_base_with_huffman(f, out_buffer, out_stride, orientation);
    }
    if (level == PCD_RES_4BASE) {
        long sz = 0;
        if (fseek(f, 0, SEEK_END) == 0)
            sz = ftell(f);
        PCD_DBG("decode_highres_level: 4BASE file_sz=%ld need=%ld\n", sz, (long)(PCD_HUFF1 + 262144));
        if (sz >= (long)(PCD_HUFF1 + 262144))
            return decode_4base_with_huffman(f, out_buffer, out_stride, orientation);
    }
    if (level == PCD_RES_16BASE) {
        long sz = 0;
        if (fseek(f, 0, SEEK_END) == 0)
            sz = ftell(f);
        PCD_DBG("decode_highres_level: 16BASE file_sz=%ld\n", sz);
        if (sz >= (long)(PCD_HUFF1 + 524288))
            return decode_16base_with_huffman(f, out_buffer, out_stride, orientation);
    }

    PCD_DBG("decode_highres_level: fallback upsample from lower\n");
    /* Fallback: upsample from next-lower (no Huffman, or 16Base) */
    {
        PCDResolutionLevel lower = (PCDResolutionLevel)(level - 1);
        const PCDResolutionInfo* lower_res = &pcd_resolutions[lower];
        uint8_t* lower_buf = NULL;
        uint32_t lower_stride = 0;

        if (!load_pcd_image(f, lower, 0, &lower_buf, &lower_stride))
            return false;
        uint32_t lw = lower_res->width;
        uint32_t lh = lower_res->height;
        uint8_t* up = upsample_2x_bgra(lower_buf, lw, lh, lower_stride);
        g_free(lower_buf);
        if (!up)
            return false;
        uint32_t width = res->width;
        uint32_t height = res->height;
        uint32_t stride = width * 4;
        if (orientation != 0) {
            uint32_t rot_width = (orientation == 1 || orientation == 3) ? height : width;
            uint32_t rot_height = (orientation == 1 || orientation == 3) ? width : height;
            uint8_t* rotated = g_malloc(rot_width * rot_height * 4);
            if (!rotated) {
                g_free(up);
                return false;
            }
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t* src = up + y * stride + x * 4;
                    uint8_t* dst = NULL;
                    switch (orientation) {
                        case 1:
                            dst = rotated + x * rot_width * 4 + (rot_width - 1 - y) * 4;
                            break;
                        case 2:
                            dst = rotated + (rot_height - 1 - y) * rot_width * 4 + (rot_width - 1 - x) * 4;
                            break;
                        case 3:
                            dst = rotated + (rot_height - 1 - x) * rot_width * 4 + y * 4;
                            break;
                    }
                    if (dst)
                        memcpy(dst, src, 4);
                }
            g_free(up);
            up = rotated;
            stride = rot_width * 4;
        }
        *out_buffer = up;
        *out_stride = stride;
        return true;
    }
}

static bool load_pcd_image(FILE* f, PCDResolutionLevel level, uint8_t orientation,
                           uint8_t** out_buffer, uint32_t* out_stride) {
    const PCDResolutionInfo* res = &pcd_resolutions[level];

    PCD_DBG("load_pcd_image: level=%d strip_based=%d\n", (int)level, res->strip_based ? 1 : 0);
    if (res->strip_based) {
        uint32_t offset = pcd_data_offset_for_level(level);
        if (fseek(f, offset, SEEK_SET) != 0)
            return false;
        return decode_strip_level(f, res, out_buffer, out_stride, orientation);
    }

    /* Base / 4Base / 16Base */
    if (!decode_highres_level(f, level, out_buffer, out_stride, orientation)) {
        PCD_DBG("load_pcd_image: decode_highres_level failed level=%d\n", (int)level);
        return false;
    }
    return true;
}

static bool detect_available_resolutions(FILE* f, bool* available) {
    if (fseek(f, 0, SEEK_END) != 0)
        return false;
    long file_size = ftell(f);
    if (file_size < 0)
        return false;

    /* Strip levels: need enough file for that resolution's data . */
    available[PCD_RES_OVERVIEW] = (file_size >= 49152); /* 8192 + 64*576 */
    available[PCD_RES_BASE16] = (file_size >= 196608);  /* 47104 + 128*1152 */
    available[PCD_RES_BASE4] = (file_size >= 786432);   /* 196608 + 256*2304 */

    /* Base: only if file has Huffman block (strip BASE4 + Huffman residual). */
    available[PCD_RES_BASE] = (available[PCD_RES_BASE4] && file_size >= (long)(PCD_HUFF1 + 65536));

    /* 4Base / 16Base: measure Base and optionally 4Base stream consumption (PhotoCD Pro has 16Base). */
    available[PCD_RES_4BASE] = false;
    available[PCD_RES_16BASE] = false;
    if (available[PCD_RES_BASE] && file_size >= (long)(PCD_HUFF1 + 131072)) {
        size_t buf_size = (size_t)(file_size - PCD_HUFF1);
        uint8_t* buf = g_malloc(buf_size);
        uint8_t *seq1 = NULL, *len1 = NULL, *seq2 = NULL, *len2 = NULL, *seq3 = NULL, *len3 = NULL;
        if (buf && fseek(f, PCD_HUFF1, SEEK_SET) == 0 && fread(buf, 1, buf_size, f) == buf_size) {
            int pos = pcd_read_htable(buf, &seq1, &len1);
            if (pos >= 0) {
                pos = (pos + 2047) & ~0x3ff;
                if ((size_t)pos < buf_size) {
                    int rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 1, 1536, 1024,
                                         NULL, NULL, NULL, seq1, len1, seq1, len1, seq1, len1);
                    if (rc >= 0 && (size_t)pos + (size_t)rc + 4096 <= buf_size) {
                        available[PCD_RES_4BASE] = true;
                        pos += rc;
                        rc = pcd_read_htable(buf + pos, &seq1, &len1);
                        if (rc >= 0) {
                            pos += rc;
                            rc = pcd_read_htable(buf + pos, &seq2, &len2);
                            if (rc >= 0) {
                                pos += rc;
                                rc = pcd_read_htable(buf + pos, &seq3, &len3);
                                if (rc >= 0) {
                                    pos += rc;
                                    pos = (pos + 2047) & ~0x3ff;
                                    if ((size_t)pos < buf_size) {
                                        rc = pcd_un_huff(buf + pos, buf_size - (size_t)pos, 2, 3072, 2048,
                                                         NULL, NULL, NULL, seq1, len1, seq2, len2, seq3, len3);
                                        if (rc >= 0 && (size_t)pos + (size_t)rc + 4096 <= buf_size)
                                            available[PCD_RES_16BASE] = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            g_free(seq1);
            g_free(len1);
            g_free(seq2);
            g_free(len2);
            g_free(seq3);
            g_free(len3);
        }
        g_free(buf);
    }

    return true;
}

typedef struct {
    GtkWidget* dialog;
    GtkWidget* radio_buttons[6];
    PCDResolutionLevel selected;
    bool* available;
} PCDResolutionDialog;

static void on_radio_toggled(GtkToggleButton* button, gpointer user_data) {
    PCDResolutionDialog* dlg = (PCDResolutionDialog*)user_data;
    for (int i = 0; i < 6; i++) {
        if (GTK_WIDGET(button) == dlg->radio_buttons[i]) {
            if (gtk_toggle_button_get_active(button))
                dlg->selected = (PCDResolutionLevel)i;
            break;
        }
    }
}

static PCDResolutionLevel show_resolution_dialog(GtkWindow* parent, bool* available) {
    PCDResolutionDialog dlg;
    dlg.selected = PCD_RES_BASE4;
    dlg.available = available;

    dlg.dialog = gtk_dialog_new_with_buttons(
        "Select PhotoCD Resolution", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dlg.dialog), parent);
        gtk_window_set_position(GTK_WINDOW(dlg.dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    }

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg.dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(content), vbox);

    GtkWidget* label = gtk_label_new(_("Choose resolution to load:"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GSList* group = NULL;
    int default_selection = -1;

    for (int i = 0; i < 6; i++) {
        if (!available[i])
            continue;
        char text[256];
        snprintf(text, sizeof(text), "%s - %s", pcd_resolutions[i].name, pcd_resolutions[i].description);
        dlg.radio_buttons[i] = gtk_radio_button_new_with_label(group, text);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dlg.radio_buttons[i]));

        if (i == PCD_RES_BASE4 || default_selection == -1) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dlg.radio_buttons[i]), TRUE);
            dlg.selected = (PCDResolutionLevel)i;
            default_selection = i;
        }

        g_signal_connect(dlg.radio_buttons[i], "toggled", G_CALLBACK(on_radio_toggled), &dlg);
        gtk_box_pack_start(GTK_BOX(vbox), dlg.radio_buttons[i], FALSE, FALSE, 0);
    }

    GtkWidget* info = gtk_label_new(
        "\nNote: Only resolutions actually stored in the file are shown.");
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_widget_set_sensitive(info, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 5);

    gtk_widget_show_all(dlg.dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dlg.dialog));
    PCDResolutionLevel selected = dlg.selected;
    gtk_widget_destroy(dlg.dialog);

    if (result != GTK_RESPONSE_OK)
        return PCD_RES_CANCELLED;

    return selected;
}

static PCDResolutionLevel select_resolution(FILE* f, GtkWindow* parent) {
    bool available[6] = {false};
    if (!detect_available_resolutions(f, available))
        return PCD_RES_BASE4;

    int count = 0;
    for (int i = 0; i < 6; i++)
        if (available[i])
            count++;

    if (count <= 1) {
        for (int i = 0; i < 6; i++)
            if (available[i])
                return (PCDResolutionLevel)i;
        return PCD_RES_BASE4;
    }

    return show_resolution_dialog(parent, available);
}

/* ========================================================================
 * Plugin Interface Functions
 * ======================================================================== */

static bool can_load_pcd(const char* filename,
                         const uint8_t* header,
                         size_t header_size) {
    if (header && header_size >= PCD_SIGNATURE_OFFSET + PCD_SIGNATURE_LEN) {
        return memcmp(header + PCD_SIGNATURE_OFFSET,
                      PCD_SIGNATURE,
                      PCD_SIGNATURE_LEN) == 0;
    }

    const char* dot = strrchr(filename, '.');
    return dot && g_ascii_strcasecmp(dot + 1, "pcd") == 0;
}

static bool can_save_pcd(const char* filename) {
    (void)filename;
    return false;
}

static PluginError load_pcd(ImageDocument* doc, const char* filename) {
    FILE* f = g_fopen(filename, "rb");
    if (!f)
        return PLUGIN_ERROR_FILE_NOT_FOUND;

    /* Verify signature */
    if (fseek(f, PCD_SIGNATURE_OFFSET, SEEK_SET) != 0) {
        fclose(f);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    char sig[PCD_SIGNATURE_LEN];
    if (fread(sig, 1, PCD_SIGNATURE_LEN, f) != PCD_SIGNATURE_LEN ||
        memcmp(sig, PCD_SIGNATURE, PCD_SIGNATURE_LEN) != 0) {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Read orientation */
    uint8_t orientation = read_orientation(f);

    /* Get parent window for resolution dialog (center + cancel = abort load) */
    GtkWindow* parent = NULL;
    if (doc->drawing_area) {
        AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
        if (ctx && ctx->window)
            parent = GTK_WINDOW(ctx->window);
    }

    /* Select resolution (will show dialog if multiple available) */
    PCDResolutionLevel selected_res = select_resolution(f, parent);
    if (selected_res == PCD_RES_CANCELLED) {
        fclose(f);
        return PLUGIN_ERROR_USER_CANCELLED;
    }

    pcd_ycc_lut_init();

    /* Load image data into buffer */
    uint8_t* image_buffer = NULL;
    uint32_t buffer_stride = 0;

    if (!load_pcd_image(f, selected_res, orientation, &image_buffer, &buffer_stride)) {
        fclose(f);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    fclose(f);

    /* Determine final image dimensions */
    uint32_t img_width = pcd_resolutions[selected_res].width;
    uint32_t img_height = pcd_resolutions[selected_res].height;
    uint32_t final_width = (orientation == 1 || orientation == 3) ? img_height : img_width;
    uint32_t final_height = (orientation == 1 || orientation == 3) ? img_width : img_height;

    /* Reset document */
    for (GList* it = doc->layers; it; it = it->next)
        layer_free(it->data);
    g_list_free(doc->layers);
    doc->layers = NULL;

    doc->width = final_width;
    doc->height = final_height;
    doc->channels = 4;
    doc->bit_depth = 8;
    doc->has_alpha = false;

    ImageLayer* layer =
        layer_new(_("Background"), final_width, final_height, TRUE,
                  LAYER_BACKGROUND_TRANSPARENT,
                  LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!layer) {
        g_free(image_buffer);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_t* surface = layer->surface;
    cairo_surface_flush(surface);
    uint8_t* dst = cairo_image_surface_get_data(surface);
    int dst_stride = cairo_image_surface_get_stride(surface);

    /* Copy buffer (already rotated in decoder) */
    for (uint32_t y = 0; y < final_height; y++) {
        memcpy(dst + y * dst_stride,
               image_buffer + y * buffer_stride,
               final_width * 4);
    }

    g_free(image_buffer);
    cairo_surface_mark_dirty(surface);
    doc->layers = g_list_append(doc->layers, layer);
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

bool plugin_init_pcd(const ImageFormatHostAPI* host,
                     ImageFormatPlugin* out_plugin) {
    (void)host;
    memset(out_plugin, 0, sizeof(*out_plugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "PCD - Kodak Photo CD";
    out_plugin->format_info.extensions = "pcd";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_pcd;
    out_plugin->callbacks.load = load_pcd;
    out_plugin->callbacks.can_save = can_save_pcd;
    return true;
}