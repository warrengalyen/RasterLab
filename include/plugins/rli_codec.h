/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef RLI_TILE_CODEC_H
#define RLI_TILE_CODEC_H

/**
 * RLI Tile Codec
 *
 * Tile-based pixel encoding/decoding for the Rasterlab Image format.
 * Each layer's pixel data is partitioned into 64x64 independent tiles,
 * delta/run-length op-encoded, and optionally LZ4-compressed.
 *
 * The op-encoding scheme is derived from the QOI/QOIR family of image
 * codecs and has been rewritten with rli_ naming to fit the project.
 */

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- Tile Grid Constants -------- */

#define RLI_TILE_SIZE   64
#define RLI_TILE_MASK   0x3F
#define RLI_TILE_SHIFT  6

/** Maximum pixels per tile (64 * 64 = 4096). */
#define RLI_TS2  (RLI_TILE_SIZE * RLI_TILE_SIZE)

/**
 * Pre-padding bytes before pixel data in the tile encode/decode buffer,
 * large enough to hold one previous pixel (4 bytes for BGRA).
 */
#define RLI_LITERALS_PRE_PADDING  4

/* -------- Tile Storage Formats (high byte of per-tile u32 header) -------- */

#define RLI_TILE_FMT_LITERALS      0x00  /* Raw BGRA pixel data */
#define RLI_TILE_FMT_OPS           0x01  /* Raw op-encoded bytes */
#define RLI_TILE_FMT_LZ4_LITERALS  0x02  /* LZ4-compressed raw BGRA pixel data */
#define RLI_TILE_FMT_LZ4_OPS      0x03  /* LZ4-compressed op-encoded bytes */

/* -------- Op Codes -------- */

/*
 * Encoding uses a variable-length byte stream of op codes. Each pixel is
 * encoded relative to the previous pixel via one of these ops:
 *
 *  Tag byte     Name       Size  Description
 *  xxxxxxxx00   INDEX      1     Color cache hit (6-bit index in high bits)
 *  xxxxxxxx01   BGR2       1     Small BGR delta (2-bit per channel, +/-1)
 *  xxxxxxxx10   LUMA       2     Luma-based delta (green +/-31, r-g/b-g +/-7)
 *  xxxxxxxx11   BGR7       3     Medium BGR delta (7-bit per channel, +/-63)
 *  xxxx x111    RUNS       1     Short run-length (1..26 identical pixels)
 *  0xD7         RUNL       2     Long run-length (1..256 identical pixels)
 *  0xDF         BGRA2      2     Small BGRA delta (2-bit per channel)
 *  0xE7         BGRA4      3     Medium BGRA delta (4-bit per channel)
 *  0xEF         BGRA8      5     Full BGRA delta (8-bit per channel)
 *  0xF7         BGR8       4     Full BGR delta (8-bit per channel, alpha unchanged)
 *  0xFF         A8         2     Alpha-only delta (8-bit)
 */
#define RLI_OP_INDEX  0x00
#define RLI_OP_BGR2   0x01
#define RLI_OP_LUMA   0x02
#define RLI_OP_BGR7   0x03
#define RLI_OP_RUNS   0x07
#define RLI_OP_RUNL   0xD7
#define RLI_OP_BGRA2  0xDF
#define RLI_OP_BGRA4  0xE7
#define RLI_OP_BGRA8  0xEF
#define RLI_OP_BGR8   0xF7
#define RLI_OP_A8     0xFF

/** Hash table shift for the color cache during op encoding. */
#define RLI_HASH_TABLE_SHIFT  10

/* -------- Tile Grid Helpers -------- */

/** Number of tiles along one dimension, rounding up. */
static inline uint32_t
rli_tiles_1d(uint32_t pixels) {
    return (uint32_t)(((uint64_t)pixels + RLI_TILE_MASK) >> RLI_TILE_SHIFT);
}

/** Total number of tiles for a 2D image. */
static inline uint64_t
rli_tiles_2d(uint32_t width, uint32_t height) {
    uint64_t w = ((uint64_t)width  + RLI_TILE_MASK) >> RLI_TILE_SHIFT;
    uint64_t h = ((uint64_t)height + RLI_TILE_MASK) >> RLI_TILE_SHIFT;
    return w * h;
}

/* -------- Tile-Level Encode / Decode -------- */

/**
 * Op-encode a single tile's pixels.
 *
 * @param dst_ptr   Output buffer for op bytes. Must be at least (5 * tw * th + 64) bytes.
 * @param src_ptr   Input pixel buffer. The first RLI_LITERALS_PRE_PADDING bytes are
 *                  padding (initialised to the "previous pixel" state, typically 0,0,0,FF).
 *                  Actual pixel data starts at src_ptr + RLI_LITERALS_PRE_PADDING.
 * @param tw        Tile width in pixels (1..RLI_TILE_SIZE).
 * @param th        Tile height in pixels (1..RLI_TILE_SIZE).
 * @return Number of op bytes written to dst_ptr, or 0 on error.
 */
size_t rli_encode_tile_ops(uint8_t* dst_ptr,
                           const uint8_t* src_ptr,
                           uint32_t tw, uint32_t th);

/**
 * Decode op-encoded bytes back into BGRA pixels for a single tile.
 *
 * @param dst_ptr   Output buffer. Must hold RLI_LITERALS_PRE_PADDING + (4 * tw * th) bytes.
 * @param dst_len   Total size of dst_ptr in bytes.
 * @param src_ptr   Op-encoded byte stream.
 * @param src_len   Length of op-encoded data plus 8 bytes of safe over-read padding.
 * @return Number of bytes written to dst_ptr (including pre-padding), or 0 on error.
 */
size_t rli_decode_tile_ops(uint8_t* dst_ptr, size_t dst_len,
                           const uint8_t* src_ptr, size_t src_len);

/* -------- Full-Layer Encode / Decode -------- */

/**
 * Encode a full layer's pixel data into RLI tile format with LZ4 compression.
 *
 * Partitions the image into 64x64 tiles, op-encodes each, and selects the
 * smallest representation per tile (ops, lz4-ops, literals, or lz4-literals).
 *
 * @param pixels    Source pixel data (BGRA premultiplied, 4 bytes/pixel).
 * @param width     Image width in pixels.
 * @param height    Image height in pixels.
 * @param stride    Source row stride in bytes.
 * @param out_len   Receives the length of the returned buffer.
 * @return Newly allocated buffer (caller frees with g_free), or NULL on error.
 */
uint8_t* rli_encode_pixel_data(const uint8_t* pixels, uint32_t width,
                               uint32_t height, uint32_t stride,
                               size_t* out_len);

/**
 * Decode RLI tile-encoded pixel data back into a pixel buffer.
 *
 * @param data       Tile-encoded data (as written by rli_encode_pixel_data).
 * @param data_len   Length of the encoded data in bytes.
 * @param pixels     Destination pixel buffer (BGRA premultiplied).
 * @param width      Image width in pixels.
 * @param height     Image height in pixels.
 * @param stride     Destination row stride in bytes.
 * @return TRUE on success, FALSE on error.
 */
gboolean rli_decode_pixel_data(const uint8_t* data, size_t data_len,
                               uint8_t* pixels, uint32_t width,
                               uint32_t height, uint32_t stride);

#ifdef __cplusplus
}
#endif

#endif /* RLI_TILE_CODEC_H */
