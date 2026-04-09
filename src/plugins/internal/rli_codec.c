/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/**
 * RLI Tile Codec -- tile-based pixel encoding/decoding for the Rasterlab Image format.
 *
 * The per-tile op-encoding algorithm is derived from the QOIR image codec
 * (Copyright 2022 Nigel Tao, Apache-2.0). It has been rewritten here with
 * rli_ naming, glib types, scalar-only SWAR arithmetic, and system LZ4.
 */

#include "plugins/rli_codec.h"

#include <glib.h>
#include <lz4.h>
#include <lz4hc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ======================================================================== */
/* Byte-order helpers (little-endian, safe unaligned access via memcpy)      */
/* ======================================================================== */

#if defined(_MSC_VER) && !defined(__clang__) && \
    (defined(_M_ARM64) || defined(_M_X64))
#define RLI_USE_MEMCPY_LE 1
#endif

static inline uint32_t
rli_peek_u32le(const uint8_t* p) {
#if defined(RLI_USE_MEMCPY_LE)
    uint32_t x;
    memcpy(&x, p, 4);
    return x;
#else
    return ((uint32_t)(p[0]) <<  0) | ((uint32_t)(p[1]) <<  8) |
           ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24);
#endif
}

static inline uint64_t
rli_peek_u64le(const uint8_t* p) {
#if defined(RLI_USE_MEMCPY_LE)
    uint64_t x;
    memcpy(&x, p, 8);
    return x;
#else
    return ((uint64_t)(p[0]) <<  0) | ((uint64_t)(p[1]) <<  8) |
           ((uint64_t)(p[2]) << 16) | ((uint64_t)(p[3]) << 24) |
           ((uint64_t)(p[4]) << 32) | ((uint64_t)(p[5]) << 40) |
           ((uint64_t)(p[6]) << 48) | ((uint64_t)(p[7]) << 56);
#endif
}

static inline void
rli_poke_u32le(uint8_t* p, uint32_t x) {
#if defined(RLI_USE_MEMCPY_LE)
    memcpy(p, &x, 4);
#else
    p[0] = (uint8_t)(x >>  0);
    p[1] = (uint8_t)(x >>  8);
    p[2] = (uint8_t)(x >> 16);
    p[3] = (uint8_t)(x >> 24);
#endif
}

/* ======================================================================== */
/* SWAR (SIMD Within A Register) packed-byte arithmetic                     */
/* ======================================================================== */

static inline uint32_t
rli_swar_paddb(uint32_t a, uint32_t b) {
    return ((a & 0x7F7F7F7Fu) + (b & 0x7F7F7F7Fu)) ^ ((a ^ b) & 0x80808080u);
}

static inline uint32_t
rli_swar_psubb(uint32_t a, uint32_t b) {
    return ((a | 0x80808080u) - (b & 0x7F7F7F7Fu)) ^ ((a ^ ~b) & 0x80808080u);
}

/* ======================================================================== */
/* Log2-distance table for delta magnitude classification                   */
/* ======================================================================== */

static const uint8_t rli_dists[256] = {
    0x00, 0x02, 0x04, 0x04, 0x08, 0x08, 0x08, 0x08,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x08, 0x08, 0x08, 0x08, 0x04, 0x04, 0x02, 0x01,
};

/* ======================================================================== */
/* Tile dimension helper                                                    */
/* ======================================================================== */

static inline uint32_t
rli_tile_dimension(bool interior, uint32_t pixel_dimension) {
    return interior ? RLI_TILE_SIZE
                    : (((pixel_dimension - 1) & RLI_TILE_MASK) + 1);
}

/* ======================================================================== */
/* Per-tile op encoder                                                      */
/* ======================================================================== */

size_t
rli_encode_tile_ops(uint8_t* dst_ptr,
                    const uint8_t* src_ptr,
                    uint32_t tw, uint32_t th) {
    uint32_t run_length = 0;

    uint8_t color_cache[256];
    for (int i = 0; i < 256; i += 4) {
        color_cache[i + 0] = 0x00;
        color_cache[i + 1] = 0x00;
        color_cache[i + 2] = 0x00;
        color_cache[i + 3] = 0xFF;
    }
    uint8_t next_color_index = 0;
    uint8_t color_indexes[1 << RLI_HASH_TABLE_SHIFT];
    memset(color_indexes, 0, sizeof(color_indexes));

    uint8_t* dp = dst_ptr;
    const uint8_t* sp = src_ptr + RLI_LITERALS_PRE_PADDING;
    const uint8_t* sq = sp + (4 * (size_t)tw * (size_t)th);

    for (; sp < sq; sp += 4) {
        if (!memcmp(sp, sp - 4, 4)) {
            run_length++;
            if (run_length == 256) {
                *dp++ = RLI_OP_RUNL;
                *dp++ = 0xFF;
                run_length = 0;
            }
            continue;
        }

        if (run_length > 0) {
            if (run_length <= 26) {
                *dp++ = (uint8_t)(RLI_OP_RUNS | ((run_length - 1) << 3));
            } else {
                *dp++ = RLI_OP_RUNL;
                *dp++ = (uint8_t)(run_length - 1);
            }
            run_length = 0;
        }

        uint32_t hash = (rli_peek_u32le(sp) * 2654435761u) >>
                        (32 - RLI_HASH_TABLE_SHIFT);
        uint8_t index = color_indexes[hash];
        if (!memcmp(color_cache + index, sp, 4)) {
            *dp++ = (uint8_t)(RLI_OP_INDEX | index);
            continue;
        }

        color_indexes[hash] = next_color_index;
        memcpy(color_cache + next_color_index, sp, 4);
        next_color_index += 4;

        uint8_t delta[4];
        uint32_t cp8x4, pl8x4;
        memcpy(&cp8x4, sp, 4);
        memcpy(&pl8x4, sp - 4, 4);
        uint32_t delta8x4 = rli_swar_psubb(cp8x4, pl8x4);
        memcpy(delta, &delta8x4, 4);

        if (delta[3] == 0) {
            uint8_t dist02 = rli_dists[delta[0]] | rli_dists[delta[2]];
            uint8_t dist1  = rli_dists[delta[1]];
            uint8_t dist   = dist02 | dist1;

            uint8_t d0d1 = delta[0] - delta[1];
            uint8_t d2d1 = delta[2] - delta[1];

            if (dist < 0x04) {
                *dp++ = 0x01 |
                        ((delta[0] + 0x02) << 0x02) |
                        ((delta[1] + 0x02) << 0x04) |
                        ((delta[2] + 0x02) << 0x06);

            } else if (!((dist1 >> 6) | (rli_dists[d0d1] >> 4) |
                         (rli_dists[d2d1] >> 4))) {
                *dp++ = 0x02 |
                        ((delta[1] + 0x20) << 0x02);
                *dp++ = ((d0d1 + 0x08) << 0x00) |
                        ((d2d1 + 0x08) << 0x04);

            } else if (dist < 0x80) {
                rli_poke_u32le(
                    dp,
                    0x03 |
                        ((uint32_t)(uint8_t)(delta[0] + 0x40) << 0x03) |
                        ((uint32_t)(uint8_t)(delta[1] + 0x40) << 0x0A) |
                        ((uint32_t)(uint8_t)(delta[2] + 0x40) << 0x11));
                dp += 3;

            } else {
                *dp++ = RLI_OP_BGR8;
                *dp++ = delta[0];
                *dp++ = delta[1];
                *dp++ = delta[2];
            }

        } else if ((delta[0] | delta[1] | delta[2]) == 0) {
            *dp++ = RLI_OP_A8;
            *dp++ = delta[3];

        } else {
            uint8_t dist = rli_dists[delta[0]] | rli_dists[delta[1]] |
                           rli_dists[delta[2]] | rli_dists[delta[3]];
            if (dist < 0x04) {
                *dp++ = RLI_OP_BGRA2;
                *dp++ = ((delta[0] + 0x02) << 0x00) |
                        ((delta[1] + 0x02) << 0x02) |
                        ((delta[2] + 0x02) << 0x04) |
                        ((delta[3] + 0x02) << 0x06);
            } else if (dist < 0x10) {
                *dp++ = RLI_OP_BGRA4;
                *dp++ = ((delta[0] + 0x08) << 0x00) |
                        ((delta[1] + 0x08) << 0x04);
                *dp++ = ((delta[2] + 0x08) << 0x00) |
                        ((delta[3] + 0x08) << 0x04);
            } else {
                *dp++ = RLI_OP_BGRA8;
                *dp++ = delta[0];
                *dp++ = delta[1];
                *dp++ = delta[2];
                *dp++ = delta[3];
            }
        }
    }

    if (run_length > 0) {
        if (run_length <= 26) {
            *dp++ = (uint8_t)(RLI_OP_RUNS | ((run_length - 1) << 3));
        } else {
            *dp++ = RLI_OP_RUNL;
            *dp++ = (uint8_t)(run_length - 1);
        }
    }

    return (size_t)(dp - dst_ptr);
}

/* ======================================================================== */
/* Per-tile op decoder                                                      */
/* ======================================================================== */

size_t
rli_decode_tile_ops(uint8_t* dst_ptr, size_t dst_len,
                    const uint8_t* src_ptr, size_t src_len) {
    if ((dst_len < RLI_LITERALS_PRE_PADDING) || (src_len < 8))
        return 0;

    uint8_t color_cache[256];
    for (int i = 0; i < 256; i += 4) {
        color_cache[i + 0] = 0x00;
        color_cache[i + 1] = 0x00;
        color_cache[i + 2] = 0x00;
        color_cache[i + 3] = 0xFF;
    }
    uint8_t next_color_index = 0;

    uint8_t* dp = dst_ptr + RLI_LITERALS_PRE_PADDING;
    uint8_t* dq = dst_ptr + dst_len;
    const uint8_t* sp = src_ptr;
    const uint8_t* sq = src_ptr + src_len - 8;

    while (dp < dq) {
        if (sp >= sq)
            return 0;

        uint8_t pixel[4];
        memcpy(pixel, dp - 4, 4);

        uint64_t s64 = rli_peek_u64le(sp);

        if ((s64 & 0xFF) == RLI_OP_BGR8) {
            pixel[0] += (uint8_t)(s64 >> 0x08);
            pixel[1] += (uint8_t)(s64 >> 0x10);
            pixel[2] += (uint8_t)(s64 >> 0x18);
            sp += 4;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0x03) == 0) {  /* INDEX */
            sp += 1;
            memcpy(pixel, color_cache + (uint8_t)s64, 4);
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0x03) == 1) {  /* BGR2 */
            uint32_t delta8x4 = (uint32_t)(((s64 >> 0x02) & 0x000003) |
                                           ((s64 << 0x04) & 0x000300) |
                                           ((s64 << 0x0A) & 0x030000));
            delta8x4 = rli_swar_psubb(delta8x4, 0x020202);
            uint32_t pixel8x4;
            memcpy(&pixel8x4, pixel, 4);
            pixel8x4 = rli_swar_paddb(pixel8x4, delta8x4);
            memcpy(pixel, &pixel8x4, 4);
            sp += 1;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0x03) == 2) {  /* LUMA */
            uint8_t delta_g = ((uint8_t)s64 >> 0x02) - 32;
            pixel[0] += delta_g - 8 + ((s64 >> 0x08) & 0x0F);
            pixel[1] += delta_g;
            pixel[2] += delta_g - 8 + ((s64 >> 0x0C) & 0x0F);
            sp += 2;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0x07) == 3) {  /* BGR7 */
            uint32_t delta8x4 =
                (uint32_t)((((s64 >> 0x03) - 0x000040) & 0x00007F) |
                           (((s64 >> 0x02) - 0x004000) & 0x007F00) |
                           (((s64 >> 0x01) - 0x400000) & 0x7F0000));
            delta8x4 |= (delta8x4 & 0x404040) << 1;
            uint32_t pixel8x4;
            memcpy(&pixel8x4, pixel, 4);
            pixel8x4 = rli_swar_paddb(pixel8x4, delta8x4);
            memcpy(pixel, &pixel8x4, 4);
            sp += 3;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0xFF) < RLI_OP_RUNL) {  /* RUNS */
            size_t rl = (s64 & 0xFF) >> 0x03;
            if (((size_t)(dq - dp)) < (4 * (rl + 1)))
                return 0;
            do {
                memcpy(dp, pixel, 4);
                dp += 4;
            } while (rl--);
            sp += 1;

        } else if ((s64 & 0xFF) == RLI_OP_RUNL) {
            size_t rl = (s64 >> 0x08) & 0xFF;
            if (((size_t)(dq - dp)) < (4 * (rl + 1)))
                return 0;
            do {
                memcpy(dp, pixel, 4);
                dp += 4;
            } while (rl--);
            sp += 2;

        } else if ((s64 & 0xFF) == RLI_OP_BGRA2) {
            pixel[0] += ((s64 >> 0x08) & 0x03) - 2;
            pixel[1] += ((s64 >> 0x0A) & 0x03) - 2;
            pixel[2] += ((s64 >> 0x0C) & 0x03) - 2;
            pixel[3] += ((s64 >> 0x0E) & 0x03) - 2;
            sp += 2;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0xFF) == RLI_OP_BGRA4) {
            pixel[0] += ((s64 >> 0x08) & 0x0F) - 8;
            pixel[1] += ((s64 >> 0x0C) & 0x0F) - 8;
            pixel[2] += ((s64 >> 0x10) & 0x0F) - 8;
            pixel[3] += ((s64 >> 0x14) & 0x0F) - 8;
            sp += 3;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else if ((s64 & 0xFF) == RLI_OP_BGRA8) {
            pixel[0] += (uint8_t)(s64 >> 0x08);
            pixel[1] += (uint8_t)(s64 >> 0x10);
            pixel[2] += (uint8_t)(s64 >> 0x18);
            pixel[3] += (uint8_t)(s64 >> 0x20);
            sp += 5;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;

        } else {  /* A8 */
            pixel[3] += (uint8_t)(s64 >> 0x08);
            sp += 2;
            memcpy(color_cache + next_color_index, pixel, 4);
            next_color_index += 4;
            memcpy(dp, pixel, 4);
            dp += 4;
        }
    }

    if (sp != sq)
        return 0;
    return (size_t)(dp - dst_ptr);
}

/* ======================================================================== */
/* Worst-case LZ4 output size for one tile's data                           */
/* ======================================================================== */

#define RLI_TILE_LZ4_WORST_CASE \
    ((4 * RLI_TILE_SIZE * RLI_TILE_SIZE) + \
     ((4 * RLI_TILE_SIZE * RLI_TILE_SIZE) / 255) + 16)

/* ======================================================================== */
/* Full-layer encode                                                        */
/* ======================================================================== */

uint8_t*
rli_encode_pixel_data(const uint8_t* pixels, uint32_t width,
                      uint32_t height, uint32_t stride,
                      size_t* out_len) {
    if (!pixels || width == 0 || height == 0 || !out_len)
        return NULL;

    uint32_t tiles_x = rli_tiles_1d(width);
    uint32_t tiles_y = rli_tiles_1d(height);
    if (tiles_x == 0 || tiles_y == 0)
        return NULL;

    size_t tile_worst = 4 + RLI_TILE_LZ4_WORST_CASE;
    uint64_t total_tiles = (uint64_t)tiles_x * tiles_y;
    uint64_t alloc_size = total_tiles * tile_worst;
    if (alloc_size > (uint64_t)G_MAXSIZE)
        return NULL;

    uint8_t* output = (uint8_t*)g_malloc(alloc_size);
    if (!output)
        return NULL;

    /*
     * Scratch buffers for one tile:
     *   ops_buf:      worst-case op-encoded bytes (5 bytes per pixel + 64)
     *   literals_buf: pre-padding + raw BGRA pixels
     *   lz4_buf:      worst-case LZ4 output
     */
    size_t ops_buf_size = (5 * RLI_TS2) + 64;
    size_t lit_buf_size = RLI_LITERALS_PRE_PADDING + (4 * RLI_TS2);
    size_t lz4_buf_size = RLI_TILE_LZ4_WORST_CASE;

    uint8_t* ops_buf = (uint8_t*)g_malloc(ops_buf_size);
    uint8_t* literals_buf = (uint8_t*)g_malloc(lit_buf_size);
    uint8_t* lz4_buf = (uint8_t*)g_malloc(lz4_buf_size);
    if (!ops_buf || !literals_buf || !lz4_buf) {
        g_free(ops_buf);
        g_free(literals_buf);
        g_free(lz4_buf);
        g_free(output);
        return NULL;
    }

    size_t last_tile_col = (tiles_x - 1) << RLI_TILE_SHIFT;
    size_t last_tile_row = (tiles_y - 1) << RLI_TILE_SHIFT;

    uint8_t* dp = output;

    for (size_t ty = 0; ty <= last_tile_row; ty += RLI_TILE_SIZE) {
        for (size_t tx = 0; tx <= last_tile_col; tx += RLI_TILE_SIZE) {
            uint32_t tw = rli_tile_dimension(tx < last_tile_col, width);
            uint32_t th = rli_tile_dimension(ty < last_tile_row, height);

            /* Initialize pre-padding to (0,0,0,0xFF) */
            literals_buf[0] = 0x00;
            literals_buf[1] = 0x00;
            literals_buf[2] = 0x00;
            literals_buf[3] = 0xFF;

            /* Copy tile pixels from strided source into contiguous buffer */
            const uint8_t* src_row = pixels + (stride * ty) + (4 * tx);
            uint8_t* lit_row = literals_buf + RLI_LITERALS_PRE_PADDING;
            for (uint32_t y = 0; y < th; y++) {
                memcpy(lit_row, src_row, 4 * tw);
                lit_row += 4 * tw;
                src_row += stride;
            }

            size_t literals_len = 4 * (size_t)tw * th;

            /* Op-encode the tile */
            size_t ops_len = rli_encode_tile_ops(ops_buf, literals_buf, tw, th);

            if (ops_len == 0 || ops_len >= literals_len) {
                /*
                 * Ops didn't help (or failed). Try LZ4 on raw literals.
                 * Pick the smaller of {raw literals, LZ4-literals}.
                 */
                int lz4_len = LZ4_compress_default(
                    (const char*)(literals_buf + RLI_LITERALS_PRE_PADDING),
                    (char*)lz4_buf, (int)literals_len,
                    (int)lz4_buf_size);

                if (lz4_len > 0 && (size_t)lz4_len < literals_len) {
                    rli_poke_u32le(dp,
                        ((uint32_t)RLI_TILE_FMT_LZ4_LITERALS << 24) |
                        (uint32_t)lz4_len);
                    memcpy(dp + 4, lz4_buf, lz4_len);
                    dp += 4 + lz4_len;
                } else {
                    rli_poke_u32le(dp,
                        ((uint32_t)RLI_TILE_FMT_LITERALS << 24) |
                        (uint32_t)literals_len);
                    memcpy(dp + 4,
                           literals_buf + RLI_LITERALS_PRE_PADDING,
                           literals_len);
                    dp += 4 + literals_len;
                }
            } else {
                /*
                 * Ops are smaller than literals. Try LZ4 on ops.
                 * Pick the smaller of {raw ops, LZ4-ops}.
                 */
                int lz4_len = LZ4_compress_default(
                    (const char*)ops_buf, (char*)lz4_buf,
                    (int)ops_len, (int)lz4_buf_size);

                if (lz4_len > 0 && (size_t)lz4_len < ops_len) {
                    rli_poke_u32le(dp,
                        ((uint32_t)RLI_TILE_FMT_LZ4_OPS << 24) |
                        (uint32_t)lz4_len);
                    memcpy(dp + 4, lz4_buf, lz4_len);
                    dp += 4 + lz4_len;
                } else {
                    rli_poke_u32le(dp,
                        ((uint32_t)RLI_TILE_FMT_OPS << 24) |
                        (uint32_t)ops_len);
                    memcpy(dp + 4, ops_buf, ops_len);
                    dp += 4 + ops_len;
                }
            }
        }
    }

    g_free(ops_buf);
    g_free(literals_buf);
    g_free(lz4_buf);

    *out_len = (size_t)(dp - output);
    return output;
}

/* ======================================================================== */
/* Full-layer decode                                                        */
/* ======================================================================== */

gboolean
rli_decode_pixel_data(const uint8_t* data, size_t data_len,
                      uint8_t* pixels, uint32_t width,
                      uint32_t height, uint32_t stride) {
    if (!data || !pixels || width == 0 || height == 0)
        return FALSE;

    uint32_t tiles_x = rli_tiles_1d(width);
    uint32_t tiles_y = rli_tiles_1d(height);
    if (tiles_x == 0 || tiles_y == 0)
        return FALSE;

    /*
     * Scratch buffers for one tile:
     *   ops_buf:      decompressed ops before op-decode
     *   literals_buf: pre-padding + decoded BGRA pixels
     */
    size_t ops_buf_size = (4 * RLI_TS2) + 8;
    size_t lit_buf_size = RLI_LITERALS_PRE_PADDING + (4 * RLI_TS2);

    uint8_t* ops_buf = (uint8_t*)g_malloc(ops_buf_size);
    uint8_t* literals_buf = (uint8_t*)g_malloc(lit_buf_size);
    if (!ops_buf || !literals_buf) {
        g_free(ops_buf);
        g_free(literals_buf);
        return FALSE;
    }

    size_t last_tile_col = (tiles_x - 1) << RLI_TILE_SHIFT;
    size_t last_tile_row = (tiles_y - 1) << RLI_TILE_SHIFT;

    const uint8_t* sp = data;
    size_t sn = data_len;
    gboolean ok = TRUE;

    for (size_t ty = 0; ty <= last_tile_row; ty += RLI_TILE_SIZE) {
        for (size_t tx = 0; tx <= last_tile_col; tx += RLI_TILE_SIZE) {
            uint32_t tw = rli_tile_dimension(tx < last_tile_col, width);
            uint32_t th = rli_tile_dimension(ty < last_tile_row, height);
            size_t literals_len = 4 * (size_t)tw * th;

            if (sn < 4) { ok = FALSE; goto done; }

            uint32_t prefix = rli_peek_u32le(sp);
            sp += 4;
            sn -= 4;

            uint32_t tile_fmt = prefix >> 24;
            size_t tile_len = prefix & 0x00FFFFFFu;

            if (tile_len > sn) { ok = FALSE; goto done; }

            /* Initialize pre-padding for op decoder */
            literals_buf[0] = 0x00;
            literals_buf[1] = 0x00;
            literals_buf[2] = 0x00;
            literals_buf[3] = 0xFF;

            const uint8_t* tile_pixels = NULL;

            switch (tile_fmt) {
                case RLI_TILE_FMT_LITERALS: {
                    if (tile_len != literals_len) { ok = FALSE; goto done; }
                    tile_pixels = sp;
                    break;
                }
                case RLI_TILE_FMT_OPS: {
                    /*
                     * Pass tile_len + 8 as src_len so the decoder can safely
                     * peek 8 bytes at the stream tail.
                     */
                    size_t r = rli_decode_tile_ops(
                        literals_buf,
                        RLI_LITERALS_PRE_PADDING + literals_len,
                        sp, tile_len + 8);
                    if (r != RLI_LITERALS_PRE_PADDING + literals_len)
                        { ok = FALSE; goto done; }
                    tile_pixels = literals_buf + RLI_LITERALS_PRE_PADDING;
                    break;
                }
                case RLI_TILE_FMT_LZ4_LITERALS: {
                    int dec = LZ4_decompress_safe(
                        (const char*)sp,
                        (char*)(literals_buf + RLI_LITERALS_PRE_PADDING),
                        (int)tile_len, (int)literals_len);
                    if (dec != (int)literals_len) { ok = FALSE; goto done; }
                    tile_pixels = literals_buf + RLI_LITERALS_PRE_PADDING;
                    break;
                }
                case RLI_TILE_FMT_LZ4_OPS: {
                    int dec = LZ4_decompress_safe(
                        (const char*)sp, (char*)ops_buf,
                        (int)tile_len, (int)(ops_buf_size - 8));
                    if (dec <= 0) { ok = FALSE; goto done; }

                    size_t r = rli_decode_tile_ops(
                        literals_buf,
                        RLI_LITERALS_PRE_PADDING + literals_len,
                        ops_buf, (size_t)dec + 8);
                    if (r != RLI_LITERALS_PRE_PADDING + literals_len)
                        { ok = FALSE; goto done; }
                    tile_pixels = literals_buf + RLI_LITERALS_PRE_PADDING;
                    break;
                }
                default:
                    ok = FALSE;
                    goto done;
            }

            sp += tile_len;
            sn -= tile_len;

            /* Copy decoded tile pixels into the strided output buffer */
            uint8_t* dst_row = pixels + (stride * ty) + (4 * tx);
            const uint8_t* src_row = tile_pixels;
            for (uint32_t y = 0; y < th; y++) {
                memcpy(dst_row, src_row, 4 * tw);
                dst_row += stride;
                src_row += 4 * tw;
            }
        }
    }

done:
    g_free(ops_buf);
    g_free(literals_buf);
    return ok;
}
