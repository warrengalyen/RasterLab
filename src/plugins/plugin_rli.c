/**
 * RLI (Rasterlab Image) format plugin.
 *
 * Chunk-based, multi-layer, lossless image format with tile-based pixel
 * encoding and LZ4 compression.
 *
 * Each chunk: [4-byte type LE][8-byte payload length LE][payload bytes]
 */

#include "plugins/plugin_rli.h"
#include "plugins/rli_tile_codec.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Host API pointer, set during plugin_init_rli() */
static const ImageFormatHostAPI* rli_host = NULL;

/* ======================================================================== */
/* Chunk header: 4-byte type (LE) + 8-byte payload length (LE) = 12 bytes   */
/* ======================================================================== */

#define RLI_CHUNK_HEADER_SIZE 12

typedef struct {
    uint32_t type;
    uint64_t payload_len;
} RliChunkHeader;

/* ======================================================================== */
/* Little-endian write helpers (file I/O)                                   */
/* ======================================================================== */

static gboolean
rli_fwrite_u16le(FILE* f, uint16_t v) {
    uint8_t buf[2];
    buf[0] = (uint8_t)(v >>  0);
    buf[1] = (uint8_t)(v >>  8);
    return fwrite(buf, 1, 2, f) == 2;
}

static gboolean
rli_fwrite_u32le(FILE* f, uint32_t v) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(v >>  0);
    buf[1] = (uint8_t)(v >>  8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
    return fwrite(buf, 1, 4, f) == 4;
}

static gboolean
rli_fwrite_u64le(FILE* f, uint64_t v) {
    uint8_t buf[8];
    buf[0] = (uint8_t)(v >>  0);
    buf[1] = (uint8_t)(v >>  8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
    buf[4] = (uint8_t)(v >> 32);
    buf[5] = (uint8_t)(v >> 40);
    buf[6] = (uint8_t)(v >> 48);
    buf[7] = (uint8_t)(v >> 56);
    return fwrite(buf, 1, 8, f) == 8;
}

static gboolean
rli_fwrite_i32le(FILE* f, int32_t v) {
    return rli_fwrite_u32le(f, (uint32_t)v);
}

static gboolean
rli_fwrite_f64le(FILE* f, double v) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    return rli_fwrite_u64le(f, bits);
}

/* ======================================================================== */
/* Little-endian read helpers (file I/O)                                    */
/* ======================================================================== */

static gboolean
rli_fread_u16le(FILE* f, uint16_t* out) {
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2)
        return FALSE;
    *out = ((uint16_t)buf[0] << 0) | ((uint16_t)buf[1] << 8);
    return TRUE;
}

static gboolean
rli_fread_u32le(FILE* f, uint32_t* out) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4)
        return FALSE;
    *out = ((uint32_t)buf[0] <<  0) | ((uint32_t)buf[1] <<  8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    return TRUE;
}

static gboolean
rli_fread_u64le(FILE* f, uint64_t* out) {
    uint8_t buf[8];
    if (fread(buf, 1, 8, f) != 8)
        return FALSE;
    *out = ((uint64_t)buf[0] <<  0) | ((uint64_t)buf[1] <<  8) |
           ((uint64_t)buf[2] << 16) | ((uint64_t)buf[3] << 24) |
           ((uint64_t)buf[4] << 32) | ((uint64_t)buf[5] << 40) |
           ((uint64_t)buf[6] << 48) | ((uint64_t)buf[7] << 56);
    return TRUE;
}

static gboolean
rli_fread_i32le(FILE* f, int32_t* out) {
    uint32_t u;
    if (!rli_fread_u32le(f, &u))
        return FALSE;
    *out = (int32_t)u;
    return TRUE;
}

static gboolean
rli_fread_f64le(FILE* f, double* out) {
    uint64_t bits;
    if (!rli_fread_u64le(f, &bits))
        return FALSE;
    memcpy(out, &bits, 8);
    return TRUE;
}

/* ======================================================================== */
/* Chunk I/O helpers                                                        */
/* ======================================================================== */

/**
 * Write a chunk header (type + payload length) to file.
 */
static gboolean
rli_write_chunk_header(FILE* f, uint32_t type, uint64_t payload_len) {
    if (!rli_fwrite_u32le(f, type))
        return FALSE;
    if (!rli_fwrite_u64le(f, payload_len))
        return FALSE;
    return TRUE;
}

/**
 * Read a chunk header from file.
 */
static gboolean
rli_read_chunk_header(FILE* f, RliChunkHeader* out) {
    if (!rli_fread_u32le(f, &out->type))
        return FALSE;
    if (!rli_fread_u64le(f, &out->payload_len))
        return FALSE;
    return TRUE;
}

/**
 * Write a complete chunk (header + payload) to file.
 */
static gboolean
rli_write_chunk(FILE* f, uint32_t type, const void* data, uint64_t len) {
    if (!rli_write_chunk_header(f, type, len))
        return FALSE;
    if (len > 0 && data) {
        if (fwrite(data, 1, (size_t)len, f) != (size_t)len)
            return FALSE;
    }
    return TRUE;
}

/**
 * Skip over a chunk's payload bytes (seek forward).
 */
static gboolean
rli_skip_chunk_payload(FILE* f, uint64_t len) {
    if (len == 0)
        return TRUE;
    /*
     * Use multiple fseek calls for payloads exceeding LONG_MAX,
     * though in practice this is extremely unlikely.
     */
    while (len > 0) {
        long step = (len > (uint64_t)LONG_MAX) ? LONG_MAX : (long)len;
        if (fseek(f, step, SEEK_CUR) != 0)
            return FALSE;
        len -= (uint64_t)step;
    }
    return TRUE;
}

/**
 * Read a chunk's payload into a newly allocated buffer.
 * Caller must free with g_free().
 */
static uint8_t*
rli_read_chunk_payload(FILE* f, uint64_t len) {
    if (len == 0)
        return NULL;
    if (len > (uint64_t)G_MAXSIZE)
        return NULL;
    uint8_t* buf = (uint8_t*)g_malloc((size_t)len);
    if (!buf)
        return NULL;
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        g_free(buf);
        return NULL;
    }
    return buf;
}
