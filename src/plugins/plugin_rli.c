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
#include "render/text_layer.h"

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

/* ======================================================================== */
/* Text layer serialization helpers (buffer-based, little-endian)           */
/* ======================================================================== */

/*
 * LTXT binary layout (all little-endian):
 *
 *   u16  text_len              + u8[text_len]  text (UTF-8)
 *   u16  font_family_len       + u8[font_family_len]  font_family (UTF-8)
 *   i32  font_size
 *   i32  font_weight           (PangoWeight value)
 *   i32  font_style            (PangoStyle value)
 *   f64  color_r
 *   f64  color_g
 *   f64  color_b
 *   f64  color_a
 *   f64  line_spacing
 *   f64  letter_spacing
 *   i32  alignment
 *   f64  rotation
 *   f64  box_x
 *   f64  box_y
 *   f64  box_width
 *   f64  box_height
 *   u8   antialias
 *   u8   kerning
 *   u16  opentype_features_len + u8[opentype_features_len]  opentype_features
 */

static inline size_t
rli_safe_strlen(const char* s) {
    return s ? strlen(s) : 0;
}

/**
 * Compute the serialized size of an LTXT payload for a TextLayer.
 */
static size_t
rli_text_layer_payload_size(const TextLayer* tl) {
    size_t size = 0;
    size += 2 + rli_safe_strlen(tl->text);            /* text */
    size += 2 + rli_safe_strlen(tl->font_family);     /* font_family */
    size += 4;                                         /* font_size */
    size += 4;                                         /* font_weight */
    size += 4;                                         /* font_style */
    size += 8 * 4;                                     /* color_r/g/b/a */
    size += 8;                                         /* line_spacing */
    size += 8;                                         /* letter_spacing */
    size += 4;                                         /* alignment */
    size += 8;                                         /* rotation */
    size += 8 * 4;                                     /* box_x/y/width/height */
    size += 1;                                         /* antialias */
    size += 1;                                         /* kerning */
    size += 2 + rli_safe_strlen(tl->opentype_features);/* opentype_features */
    return size;
}

/**
 * Write the LTXT payload for a TextLayer directly to file.
 * The caller must have already written the chunk header with the
 * correct payload length from rli_text_layer_payload_size().
 *
 * @return TRUE on success.
 */
static gboolean
rli_text_layer_write(FILE* f, const TextLayer* tl) {
    size_t len;

    /* text */
    len = rli_safe_strlen(tl->text);
    if (!rli_fwrite_u16le(f, (uint16_t)len)) return FALSE;
    if (len > 0 && fwrite(tl->text, 1, len, f) != len) return FALSE;

    /* font_family */
    len = rli_safe_strlen(tl->font_family);
    if (!rli_fwrite_u16le(f, (uint16_t)len)) return FALSE;
    if (len > 0 && fwrite(tl->font_family, 1, len, f) != len) return FALSE;

    /* font_size, font_weight, font_style */
    if (!rli_fwrite_i32le(f, tl->font_size))          return FALSE;
    if (!rli_fwrite_i32le(f, (int32_t)tl->font_weight)) return FALSE;
    if (!rli_fwrite_i32le(f, (int32_t)tl->font_style))  return FALSE;

    /* color */
    if (!rli_fwrite_f64le(f, tl->color_r)) return FALSE;
    if (!rli_fwrite_f64le(f, tl->color_g)) return FALSE;
    if (!rli_fwrite_f64le(f, tl->color_b)) return FALSE;
    if (!rli_fwrite_f64le(f, tl->color_a)) return FALSE;

    /* spacing */
    if (!rli_fwrite_f64le(f, tl->line_spacing))   return FALSE;
    if (!rli_fwrite_f64le(f, tl->letter_spacing)) return FALSE;

    /* alignment */
    if (!rli_fwrite_i32le(f, tl->alignment)) return FALSE;

    /* rotation */
    if (!rli_fwrite_f64le(f, tl->rotation)) return FALSE;

    /* text box */
    if (!rli_fwrite_f64le(f, tl->box_x))      return FALSE;
    if (!rli_fwrite_f64le(f, tl->box_y))      return FALSE;
    if (!rli_fwrite_f64le(f, tl->box_width))  return FALSE;
    if (!rli_fwrite_f64le(f, tl->box_height)) return FALSE;

    /* flags */
    uint8_t aa = tl->antialias ? 1 : 0;
    uint8_t kr = tl->kerning   ? 1 : 0;
    if (fwrite(&aa, 1, 1, f) != 1) return FALSE;
    if (fwrite(&kr, 1, 1, f) != 1) return FALSE;

    /* opentype_features */
    len = rli_safe_strlen(tl->opentype_features);
    if (!rli_fwrite_u16le(f, (uint16_t)len)) return FALSE;
    if (len > 0 && fwrite(tl->opentype_features, 1, len, f) != len) return FALSE;

    return TRUE;
}

/**
 * Read an LTXT payload from file and populate a TextLayer.
 * All string fields are newly allocated (caller owns them).
 *
 * @param f           File positioned at the start of the LTXT payload.
 * @param payload_len Expected payload length in bytes (from chunk header).
 * @param tl          TextLayer to populate (must be zero-initialised by caller).
 * @return TRUE on success.
 */
static gboolean
rli_text_layer_read(FILE* f, size_t payload_len, TextLayer* tl) {
    (void)payload_len;
    uint16_t slen;
    uint8_t byte_val;
    int32_t ival;

    /* text */
    if (!rli_fread_u16le(f, &slen)) return FALSE;
    if (slen > 0) {
        tl->text = g_malloc(slen + 1);
        if (fread(tl->text, 1, slen, f) != slen) return FALSE;
        tl->text[slen] = '\0';
    } else {
        tl->text = g_strdup("");
    }

    /* font_family */
    if (!rli_fread_u16le(f, &slen)) return FALSE;
    if (slen > 0) {
        tl->font_family = g_malloc(slen + 1);
        if (fread(tl->font_family, 1, slen, f) != slen) return FALSE;
        tl->font_family[slen] = '\0';
    } else {
        tl->font_family = g_strdup("Sans");
    }

    /* font_size */
    if (!rli_fread_i32le(f, &tl->font_size)) return FALSE;

    /* font_weight */
    if (!rli_fread_i32le(f, &ival)) return FALSE;
    tl->font_weight = (PangoWeight)ival;

    /* font_style */
    if (!rli_fread_i32le(f, &ival)) return FALSE;
    tl->font_style = (PangoStyle)ival;

    /* color */
    if (!rli_fread_f64le(f, &tl->color_r)) return FALSE;
    if (!rli_fread_f64le(f, &tl->color_g)) return FALSE;
    if (!rli_fread_f64le(f, &tl->color_b)) return FALSE;
    if (!rli_fread_f64le(f, &tl->color_a)) return FALSE;

    /* spacing */
    if (!rli_fread_f64le(f, &tl->line_spacing))   return FALSE;
    if (!rli_fread_f64le(f, &tl->letter_spacing)) return FALSE;

    /* alignment */
    if (!rli_fread_i32le(f, &tl->alignment)) return FALSE;

    /* rotation */
    if (!rli_fread_f64le(f, &tl->rotation)) return FALSE;

    /* text box */
    if (!rli_fread_f64le(f, &tl->box_x))      return FALSE;
    if (!rli_fread_f64le(f, &tl->box_y))      return FALSE;
    if (!rli_fread_f64le(f, &tl->box_width))  return FALSE;
    if (!rli_fread_f64le(f, &tl->box_height)) return FALSE;

    /* antialias */
    if (fread(&byte_val, 1, 1, f) != 1) return FALSE;
    tl->antialias = byte_val ? TRUE : FALSE;

    /* kerning */
    if (fread(&byte_val, 1, 1, f) != 1) return FALSE;
    tl->kerning = byte_val ? TRUE : FALSE;

    /* opentype_features */
    if (!rli_fread_u16le(f, &slen)) return FALSE;
    if (slen > 0) {
        tl->opentype_features = g_malloc(slen + 1);
        if (fread(tl->opentype_features, 1, slen, f) != slen) return FALSE;
        tl->opentype_features[slen] = '\0';
    } else {
        tl->opentype_features = NULL;
    }

    return TRUE;
}
