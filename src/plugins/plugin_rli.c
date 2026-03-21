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
#include "color_manager/icc_utils.h"
#include "document.h"
#include "render/layer.h"
#include "render/text_layer.h"

#include <cairo/cairo.h>
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

/* ======================================================================== */
/* LAYR chunk helpers                                                       */
/* ======================================================================== */

/**
 * Compute the LAYR chunk payload size for a given layer.
 */
static size_t
rli_layr_payload_size(const ImageLayer* layer) {
    size_t name_len = layer->name ? strlen(layer->name) : 0;
    /* u16 type + u16 flags + u32 blend + f64 opacity +
     * i32 offset_x + i32 offset_y + u32 width + u32 height + u16 name_len */
    return 2 + 2 + 4 + 8 + 4 + 4 + 4 + 4 + 2 + name_len;
}

/**
 * Write the LAYR chunk payload for a layer to file.
 */
static gboolean
rli_layr_write(FILE* f, const ImageLayer* layer) {
    uint16_t layer_type = (uint16_t)layer->layer_type;
    uint16_t flags = layer->visible ? 0x0001 : 0x0000;
    size_t name_len = layer->name ? strlen(layer->name) : 0;

    if (!rli_fwrite_u16le(f, layer_type))                  return FALSE;
    if (!rli_fwrite_u16le(f, flags))                       return FALSE;
    if (!rli_fwrite_u32le(f, (uint32_t)layer->blend_mode)) return FALSE;
    if (!rli_fwrite_f64le(f, layer->opacity))              return FALSE;
    if (!rli_fwrite_i32le(f, layer->offset_x))             return FALSE;
    if (!rli_fwrite_i32le(f, layer->offset_y))             return FALSE;
    if (!rli_fwrite_u32le(f, layer->width))                return FALSE;
    if (!rli_fwrite_u32le(f, layer->height))               return FALSE;
    if (!rli_fwrite_u16le(f, (uint16_t)name_len))          return FALSE;
    if (name_len > 0) {
        if (fwrite(layer->name, 1, name_len, f) != name_len)
            return FALSE;
    }
    return TRUE;
}

/* ======================================================================== */
/* Save                                                                     */
/* ======================================================================== */

static PluginError
rli_save(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    (void)opts;

    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    FILE* f = g_fopen(filename, "wb");
    if (!f)
        return PLUGIN_ERROR_FILE_WRITE_ERROR;

    PluginError err = PLUGIN_ERROR_NONE;
    guint layer_count = g_list_length(doc->layers);

    /* ---- RLIB header chunk (20-byte payload) ---- */
    {
        uint64_t payload = 20;
        if (!rli_write_chunk_header(f, RLI_CHUNK_RLIB, payload))
            { err = PLUGIN_ERROR_FILE_WRITE_ERROR; goto fail; }
        if (!rli_fwrite_u16le(f, RLI_FORMAT_VERSION))   goto wfail;
        if (!rli_fwrite_u16le(f, 0))                    goto wfail; /* flags */
        if (!rli_fwrite_u32le(f, doc->width))            goto wfail;
        if (!rli_fwrite_u32le(f, doc->height))           goto wfail;
        if (!rli_fwrite_u32le(f, layer_count))           goto wfail;
        if (!rli_fwrite_u16le(f, (uint16_t)doc->bit_depth)) goto wfail;
        if (!rli_fwrite_u16le(f, 0))                    goto wfail; /* reserved */
    }

    /* ---- ICCP chunk (optional) ---- */
    if (doc->original_icc_data && doc->original_icc_size > 0) {
        if (!rli_write_chunk(f, RLI_CHUNK_ICCP,
                             doc->original_icc_data,
                             doc->original_icc_size))
            goto wfail;
    }

    /* ---- Layers (bottom to top) ---- */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;
        if (!layer)
            continue;

        /* LAYR chunk */
        {
            size_t plen = rli_layr_payload_size(layer);
            if (!rli_write_chunk_header(f, RLI_CHUNK_LAYR, plen))
                goto wfail;
            if (!rli_layr_write(f, layer))
                goto wfail;
        }

        /* For text layers, write LTXT then fall through to LPIX for the
         * rasterized fallback (so non-text-aware readers can still show pixels). */
        if (layer->layer_type == LAYER_TYPE_TEXT && layer->text_data) {
            TextLayer* tl = (TextLayer*)layer->text_data;
            size_t plen = rli_text_layer_payload_size(tl);
            if (!rli_write_chunk_header(f, RLI_CHUNK_LTXT, plen))
                goto wfail;
            if (!rli_text_layer_write(f, tl))
                goto wfail;

            /* Ensure the rasterized surface is up to date */
            text_layer_render_to_surface(layer);
        }

        /* LPIX chunk -- encode layer pixel data */
        if (layer->surface && layer->width > 0 && layer->height > 0) {
            cairo_surface_flush(layer->surface);
            const uint8_t* pixels = cairo_image_surface_get_data(layer->surface);
            int stride = cairo_image_surface_get_stride(layer->surface);

            if (pixels && stride > 0) {
                size_t tile_data_len = 0;
                uint8_t* tile_data = rli_encode_pixel_data(
                    pixels, layer->width, layer->height,
                    (uint32_t)stride, &tile_data_len);

                if (!tile_data)
                    goto wfail;

                /* LPIX payload: 1 byte pixfmt + 3 reserved + tile data */
                uint64_t lpix_payload = 4 + tile_data_len;
                if (!rli_write_chunk_header(f, RLI_CHUNK_LPIX, lpix_payload)) {
                    g_free(tile_data);
                    goto wfail;
                }

                uint8_t lpix_hdr[4] = { RLI_PIXFMT_BGRA_PREMUL, 0, 0, 0 };
                if (fwrite(lpix_hdr, 1, 4, f) != 4) {
                    g_free(tile_data);
                    goto wfail;
                }
                if (fwrite(tile_data, 1, tile_data_len, f) != tile_data_len) {
                    g_free(tile_data);
                    goto wfail;
                }
                g_free(tile_data);
            }
        }
    }

    /* ---- REND chunk ---- */
    if (!rli_write_chunk(f, RLI_CHUNK_REND, NULL, 0))
        goto wfail;

    fclose(f);
    return PLUGIN_ERROR_NONE;

wfail:
    err = PLUGIN_ERROR_FILE_WRITE_ERROR;
fail:
    fclose(f);
    return err;
}

/* ======================================================================== */
/* LAYR chunk read helper                                                   */
/* ======================================================================== */

/**
 * Read a LAYR chunk payload into an existing ImageLayer.
 * Sets layer_type, visibility, blend_mode, opacity, offset, dimensions, name.
 * The layer's surface is NOT created here (done when LPIX is encountered).
 *
 * @return TRUE on success.
 */
static gboolean
rli_layr_read(FILE* f, size_t payload_len,
              uint16_t* out_type, uint16_t* out_flags,
              uint32_t* out_blend, double* out_opacity,
              int32_t* out_ox, int32_t* out_oy,
              uint32_t* out_w, uint32_t* out_h,
              gchar** out_name) {
    (void)payload_len;

    if (!rli_fread_u16le(f, out_type))   return FALSE;
    if (!rli_fread_u16le(f, out_flags))  return FALSE;
    if (!rli_fread_u32le(f, out_blend))  return FALSE;
    if (!rli_fread_f64le(f, out_opacity)) return FALSE;
    if (!rli_fread_i32le(f, out_ox))     return FALSE;
    if (!rli_fread_i32le(f, out_oy))     return FALSE;
    if (!rli_fread_u32le(f, out_w))      return FALSE;
    if (!rli_fread_u32le(f, out_h))      return FALSE;

    uint16_t name_len;
    if (!rli_fread_u16le(f, &name_len))  return FALSE;

    if (name_len > 0) {
        *out_name = g_malloc(name_len + 1);
        if (fread(*out_name, 1, name_len, f) != name_len) {
            g_free(*out_name);
            *out_name = NULL;
            return FALSE;
        }
        (*out_name)[name_len] = '\0';
    } else {
        *out_name = g_strdup("Layer");
    }
    return TRUE;
}

/* ======================================================================== */
/* Load                                                                     */
/* ======================================================================== */

static PluginError
rli_load(ImageDocument* doc, const char* filename) {
    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    FILE* f = g_fopen(filename, "rb");
    if (!f)
        return PLUGIN_ERROR_FILE_NOT_FOUND;

    PluginError err = PLUGIN_ERROR_NONE;
    RliChunkHeader hdr;

    /* ---- Read and validate RLIB header ---- */
    if (!rli_read_chunk_header(f, &hdr) || hdr.type != RLI_CHUNK_RLIB) {
        err = PLUGIN_ERROR_CORRUPT_FILE;
        goto done;
    }

    uint16_t version, flags, bit_depth, reserved;
    uint32_t canvas_w, canvas_h, layer_count;

    if (!rli_fread_u16le(f, &version))      { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }
    if (!rli_fread_u16le(f, &flags))        { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }
    if (!rli_fread_u32le(f, &canvas_w))     { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }
    if (!rli_fread_u32le(f, &canvas_h))     { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }
    if (!rli_fread_u32le(f, &layer_count))  { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }
    if (!rli_fread_u16le(f, &bit_depth))    { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }
    if (!rli_fread_u16le(f, &reserved))     { err = PLUGIN_ERROR_CORRUPT_FILE; goto done; }

    if (version > RLI_FORMAT_VERSION) {
        g_warning("RLI: unsupported format version %u (max supported: %u)",
                  version, RLI_FORMAT_VERSION);
        err = PLUGIN_ERROR_UNSUPPORTED_FORMAT;
        goto done;
    }
    if (canvas_w == 0 || canvas_h == 0) {
        err = PLUGIN_ERROR_CORRUPT_FILE;
        goto done;
    }

    /* Set document metadata */
    doc->width     = canvas_w;
    doc->height    = canvas_h;
    doc->channels  = 4;
    doc->bit_depth = (bit_depth > 0) ? bit_depth : 8;
    doc->has_alpha = TRUE;

    /* Free any existing layers */
    for (GList* iter = doc->layers; iter; iter = iter->next)
        layer_free((ImageLayer*)iter->data);
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* ---- Chunk loop ---- */
    ImageLayer* current_layer = NULL;

    while (TRUE) {
        if (!rli_read_chunk_header(f, &hdr)) {
            err = PLUGIN_ERROR_CORRUPT_FILE;
            goto done;
        }

        if (hdr.type == RLI_CHUNK_REND) {
            break;
        }

        if (hdr.type == RLI_CHUNK_ICCP) {
            /* ICC color profile */
            if (hdr.payload_len > 0 && hdr.payload_len < (uint64_t)G_MAXSIZE) {
                uint8_t* icc_blob = rli_read_chunk_payload(f, hdr.payload_len);
                if (icc_blob) {
                    /* Store raw ICC data for "preserve on re-save" */
                    g_free(doc->original_icc_data);
                    doc->original_icc_data = icc_blob;
                    doc->original_icc_size = (size_t)hdr.payload_len;

                    /* Create lcms profile for load-time conversion */
                    if (rli_host && rli_host->document_set_load_icc_profile) {
                        if (rli_host->get_use_embedded_icc &&
                            !rli_host->get_use_embedded_icc()) {
                            /* User disabled ICC -- don't convert */
                        } else {
                            cmsHPROFILE profile = icc_profile_from_memory(
                                icc_blob, (size_t)hdr.payload_len);
                            if (profile) {
                                rli_host->document_set_load_icc_profile(doc, profile);
                            }
                        }
                    }
                } else {
                    rli_skip_chunk_payload(f, hdr.payload_len);
                }
            } else {
                rli_skip_chunk_payload(f, hdr.payload_len);
            }

        } else if (hdr.type == RLI_CHUNK_LAYR) {
            /* Layer header */
            uint16_t ltype, lflags;
            uint32_t lblend, lw, lh;
            double lopacity;
            int32_t lox, loy;
            gchar* lname = NULL;

            if (!rli_layr_read(f, (size_t)hdr.payload_len,
                               &ltype, &lflags, &lblend, &lopacity,
                               &lox, &loy, &lw, &lh, &lname)) {
                err = PLUGIN_ERROR_CORRUPT_FILE;
                goto done;
            }

            /* Create layer (raster for now; text_data attached when LTXT arrives) */
            ImageLayer* layer = layer_new(
                lname ? lname : "Layer",
                lw, lh, TRUE,
                LAYER_BACKGROUND_TRANSPARENT,
                LAYER_POSITION_ABOVE_CURRENT,
                NULL, doc);
            g_free(lname);

            if (!layer) {
                err = PLUGIN_ERROR_OUT_OF_MEMORY;
                goto done;
            }

            layer->layer_type = (LayerType)ltype;
            layer->visible    = (lflags & 0x0001) ? TRUE : FALSE;
            layer->blend_mode = (BlendMode)lblend;
            layer->opacity    = lopacity;
            layer->offset_x   = lox;
            layer->offset_y   = loy;

            doc->layers = g_list_append(doc->layers, layer);
            current_layer = layer;

        } else if (hdr.type == RLI_CHUNK_LPIX) {
            /* Raster pixel data -- decode into current layer's surface */
            if (!current_layer || !current_layer->surface) {
                rli_skip_chunk_payload(f, hdr.payload_len);
                continue;
            }

            if (hdr.payload_len < 4) {
                err = PLUGIN_ERROR_CORRUPT_FILE;
                goto done;
            }

            /* Read the 4-byte LPIX header (pixfmt + reserved) */
            uint8_t lpix_hdr[4];
            if (fread(lpix_hdr, 1, 4, f) != 4) {
                err = PLUGIN_ERROR_FILE_READ_ERROR;
                goto done;
            }

            size_t tile_data_len = (size_t)(hdr.payload_len - 4);
            if (tile_data_len == 0) {
                continue;
            }

            uint8_t* tile_data = (uint8_t*)g_malloc(tile_data_len);
            if (!tile_data) {
                err = PLUGIN_ERROR_OUT_OF_MEMORY;
                goto done;
            }
            if (fread(tile_data, 1, tile_data_len, f) != tile_data_len) {
                g_free(tile_data);
                err = PLUGIN_ERROR_FILE_READ_ERROR;
                goto done;
            }

            cairo_surface_flush(current_layer->surface);
            uint8_t* pixels = cairo_image_surface_get_data(current_layer->surface);
            int stride = cairo_image_surface_get_stride(current_layer->surface);

            if (pixels && stride > 0) {
                if (!rli_decode_pixel_data(tile_data, tile_data_len,
                                           pixels,
                                           current_layer->width,
                                           current_layer->height,
                                           (uint32_t)stride)) {
                    g_free(tile_data);
                    g_warning("RLI: failed to decode pixel data for layer '%s'",
                              current_layer->name ? current_layer->name : "?");
                    err = PLUGIN_ERROR_CORRUPT_FILE;
                    goto done;
                }
                cairo_surface_mark_dirty(current_layer->surface);
            }
            g_free(tile_data);

        } else if (hdr.type == RLI_CHUNK_LTXT) {
            /* Text layer data -- attach to current layer */
            if (!current_layer) {
                rli_skip_chunk_payload(f, hdr.payload_len);
                continue;
            }

            TextLayer* tl = g_new0(TextLayer, 1);
            if (!rli_text_layer_read(f, (size_t)hdr.payload_len, tl)) {
                text_layer_free(tl);
                err = PLUGIN_ERROR_CORRUPT_FILE;
                goto done;
            }

            current_layer->layer_type = LAYER_TYPE_TEXT;
            current_layer->text_data  = tl;

            /* Re-render text onto the layer surface */
            text_layer_render_to_surface(current_layer);

        } else if (hdr.type == RLI_CHUNK_EXIF || hdr.type == RLI_CHUNK_XMP) {
            /* Known metadata chunks -- skip for now (future expansion) */
            rli_skip_chunk_payload(f, hdr.payload_len);

        } else {
            /* Unknown chunk -- skip for forward compatibility */
            rli_skip_chunk_payload(f, hdr.payload_len);
        }
    }

    /* Select first layer and render composite */
    if (doc->layers) {
        doc->selected_layer = (ImageLayer*)doc->layers->data;
    }
    document_render_composite(doc);

done:
    fclose(f);
    return err;
}
