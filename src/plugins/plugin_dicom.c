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
 * DICOM (Digital Imaging and Communications in Medicine) image format plugin.
 * Supports loading native (uncompressed), RLE-compressed, and JPEG-compressed
 * DICOM images: monochrome, RGB, palette color, YBR, and multi-frame.
 */

#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBJPEG
#include <jerror.h>
#include <jpeglib.h>
#endif

/* DICOM file: 128-byte preamble + "DICM" */
#define DICOM_PREAMBLE_SIZE 128
#define DICOM_MAGIC "DICM"
#define DICOM_MAGIC_LEN 4

/* Tag (group, element) as single 32-bit for comparison (group in low word, element in high word when little-endian) */
#define DICOM_TAG(g, e) (((uint32_t)(e) << 16) | (uint32_t)(g))

/* Required image tags */
#define TAG_TRANSFER_SYNTAX_UID DICOM_TAG(0x0002, 0x0010)
#define TAG_ROWS DICOM_TAG(0x0028, 0x0010)
#define TAG_COLUMNS DICOM_TAG(0x0028, 0x0011)
#define TAG_SAMPLES_PER_PIXEL DICOM_TAG(0x0028, 0x0002)
#define TAG_PHOTOMETRIC DICOM_TAG(0x0028, 0x0004)
#define TAG_BITS_ALLOCATED DICOM_TAG(0x0028, 0x0100)
#define TAG_BITS_STORED DICOM_TAG(0x0028, 0x0101)
#define TAG_HIGH_BIT DICOM_TAG(0x0028, 0x0102)
#define TAG_PIXEL_REPRESENTATION DICOM_TAG(0x0028, 0x0103)
#define TAG_NUMBER_OF_FRAMES DICOM_TAG(0x0028, 0x0008)
#define TAG_PIXEL_DATA DICOM_TAG(0x7FE0, 0x0010)
#define TAG_RED_LUT_DESCRIPTOR DICOM_TAG(0x0028, 0x1101)
#define TAG_GREEN_LUT_DESCRIPTOR DICOM_TAG(0x0028, 0x1102)
#define TAG_BLUE_LUT_DESCRIPTOR DICOM_TAG(0x0028, 0x1103)
#define TAG_RED_PALETTE DICOM_TAG(0x0028, 0x1201)
#define TAG_GREEN_PALETTE DICOM_TAG(0x0028, 0x1202)
#define TAG_BLUE_PALETTE DICOM_TAG(0x0028, 0x1203)
#define TAG_ITEM DICOM_TAG(0xFFFE, 0xE000)
#define TAG_ITEM_DELIMITATION DICOM_TAG(0xFFFE, 0xE0DD)

/* Undefined length for encapsulated pixel data */
#define DICOM_UNDEFINED_LENGTH 0xFFFFFFFFU

/* Transfer syntax UIDs (null-padded in file, we compare first N chars) */
#define TS_IMPLICIT_VR_LE "1.2.840.10008.1.2"
#define TS_EXPLICIT_VR_LE "1.2.840.10008.1.2.1"
#define TS_EXPLICIT_VR_BE "1.2.840.10008.1.2.2"
#define TS_RLE_LOSSLESS "1.2.840.10008.1.2.5"
#define TS_JPEG_BASELINE "1.2.840.10008.1.2.4.50" /* JPEG Baseline (Process 1) */
#define TS_JPEG_EXTENDED "1.2.840.10008.1.2.4.51" /* JPEG Extended (Process 2 & 4) */

typedef enum {
    DICOM_VR_UNKNOWN = 0,
    DICOM_VR_OB, /* Other Byte */
    DICOM_VR_OW, /* Other Word */
    DICOM_VR_US, /* Unsigned Short */
    DICOM_VR_SS, /* Signed Short */
    DICOM_VR_UL, /* Unsigned Long */
    DICOM_VR_SL, /* Signed Long */
    DICOM_VR_FL, /* Float */
    DICOM_VR_FD, /* Double */
    DICOM_VR_CS, /* Code String */
    DICOM_VR_UI, /* Unique Identifier */
    DICOM_VR_SQ, /* Sequence */
    DICOM_VR_UN  /* Unknown */
} DicomVR;

typedef enum {
    DICOM_PHOTOMETRIC_UNKNOWN = 0,
    DICOM_PHOTOMETRIC_MONOCHROME1,
    DICOM_PHOTOMETRIC_MONOCHROME2,
    DICOM_PHOTOMETRIC_RGB,
    DICOM_PHOTOMETRIC_PALETTE,
    DICOM_PHOTOMETRIC_YBR_FULL_422,
    DICOM_PHOTOMETRIC_YBR_FULL
} DicomPhotometric;

#define DICOM_RLE_HEADER_SIZE 64
#define DICOM_MAX_RLE_SEGMENTS 15

typedef struct {
    uint32_t rows;
    uint32_t columns;
    uint32_t samples_per_pixel;
    uint32_t bits_allocated;
    uint32_t bits_stored;
    uint32_t high_bit;
    uint32_t pixel_representation; /* 0 = unsigned, 1 = signed */
    DicomPhotometric photometric;
    uint32_t number_of_frames;
    bool explicit_vr;
    bool big_endian;
    bool is_rle;  /* RLE transfer syntax: pixel data is encapsulated */
    bool is_jpeg; /* JPEG transfer syntax: pixel data is encapsulated JPEG bitstreams */
    /* Palette: LUT descriptor (num_entries, first_value, bits_per_entry) per channel */
    uint32_t lut_num_entries;
    uint32_t lut_first_value;
    uint32_t lut_bits_per_entry;
    uint16_t* red_lut;
    uint16_t* green_lut;
    uint16_t* blue_lut;
} DicomImageInfo;

static bool is_big_endian(void) {
    union {
        uint32_t i;
        uint8_t c[4];
    } u = {0x01020304};
    return u.c[0] == 0x01;
}

static uint16_t read_u16(const uint8_t* p, bool swap) {
    uint16_t v;
    memcpy(&v, p, 2);
    if (swap) {
        v = (uint16_t)(((v & 0xFF00) >> 8) | ((v & 0x00FF) << 8));
    }
    return v;
}

static uint32_t read_u32(const uint8_t* p, bool swap) {
    uint32_t v;
    memcpy(&v, p, 4);
    if (swap) {
        v = ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) |
            ((v & 0x0000FF00) << 8) | ((v & 0x000000FF) << 24);
    }
    return v;
}

static bool vr_has_32bit_length(uint16_t vr) {
    /* OB, OW, OF, SQ, UN, UC, UR, UT use 4-byte length in explicit VR */
    return (vr == 0x424F) || (vr == 0x574F) || (vr == 0x464F) || (vr == 0x5153) ||
           (vr == 0x4E55) || (vr == 0x4355) || (vr == 0x5255) || (vr == 0x5455) ||
           (vr == 0x4C4F) || (vr == 0x564F); /* OB, OW, OF, SQ, UN, UC, UR, UT, OL, OV */
}

static bool can_load_dicom(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;
    if (!header || header_size < DICOM_PREAMBLE_SIZE + DICOM_MAGIC_LEN) {
        return false;
    }
    return memcmp(header + DICOM_PREAMBLE_SIZE, DICOM_MAGIC, DICOM_MAGIC_LEN) == 0;
}

static bool can_save_dicom(const char* filename) {
    (void)filename;
    return false; /* Read-only: DICOM write not implemented */
}

/* Parse transfer syntax from (0002,0010) value to set explicit_vr, big_endian, is_rle, is_jpeg.
 * UID is already trimmed (no trailing space/null). Use full string compare so padding doesn't matter.
 * Check most specific UIDs first. */
static void parse_transfer_syntax(const char* uid, size_t len, DicomImageInfo* info) {
    (void)len;
    info->explicit_vr = true;
    info->big_endian = false;
    info->is_rle = false;
    info->is_jpeg = false;
    if (strcmp(uid, TS_JPEG_BASELINE) == 0) {
        info->is_jpeg = true; /* JPEG Baseline (Process 1): encapsulated JPEG bitstreams */
    } else if (strcmp(uid, TS_JPEG_EXTENDED) == 0) {
        info->is_jpeg = true; /* JPEG Extended (Process 2 & 4) */
    } else if (strcmp(uid, TS_RLE_LOSSLESS) == 0) {
        info->is_rle = true; /* RLE: pixel data will be encapsulated */
    } else if (strcmp(uid, TS_EXPLICIT_VR_BE) == 0) {
        info->big_endian = true;
    } else if (strcmp(uid, TS_EXPLICIT_VR_LE) == 0) {
        /* default */
    } else if (strcmp(uid, TS_IMPLICIT_VR_LE) == 0) {
        /* Implicit VR LE: exact "1.2.840.10008.1.2" only */
        info->explicit_vr = false;
    }
}

/* Read next data element; returns length (or 0 on skip/error). *data_offset is file offset of value. */
static uint32_t read_element(FILE* f, bool explicit_vr, bool big_endian,
                             uint32_t* tag_out, uint32_t* value_offset_out) {
    uint8_t buf[8];
    uint32_t tag, length;
    bool swap = big_endian != is_big_endian();

    if (fread(buf, 1, 4, f) != 4) {
        return 0;
    }
    tag = read_u32(buf, swap);
    if (tag_out) {
        *tag_out = tag;
    }

    if (explicit_vr) {
        if (fread(buf, 1, 2, f) != 2) {
            return 0;
        }
        uint16_t vr = read_u16(buf, swap);
        if (vr_has_32bit_length(vr)) {
            if (fread(buf, 1, 6, f) != 6) {
                return 0;
            }
            length = read_u32(buf + 2, swap);
        } else {
            if (fread(buf, 1, 2, f) != 2) {
                return 0;
            }
            length = read_u16(buf, swap);
        }
    } else {
        if (fread(buf, 1, 4, f) != 4) {
            return 0;
        }
        length = read_u32(buf, swap);
    }

    if (value_offset_out) {
        *value_offset_out = (uint32_t)ftell(f);
    }
    return length;
}

/* Skip element value */
static bool skip_element_value(FILE* f, uint32_t length) {
    if (length == DICOM_UNDEFINED_LENGTH) {
        /* Sequence or encapsulated data - scan for (FFFE,E0DD) Sequence Delimitation or (FFFE,E00D) Item Delimitation */
        uint8_t buf[4];
        uint32_t seq_depth = 0;
        for (;;) {
            if (fread(buf, 1, 4, f) != 4) {
                return false;
            }
            uint32_t item_tag = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
            if (item_tag == 0xE0DDFFFE) { /* (FFFE,E0DD) Sequence Delimitation Item */
                if (seq_depth == 0) {
                    /* Consume the 4-byte length so next read_element is at a real tag */
                    if (fread(buf, 1, 4, f) != 4) {
                        return false;
                    }
                    break;
                }
                seq_depth--;
            } else if (item_tag == 0xE00DFFFE) { /* (FFFE,E00D) Item Delimitation Tag - skip length (4 bytes) */
                if (fread(buf, 1, 4, f) != 4) {
                    return false;
                }
                if (seq_depth > 0) {
                    seq_depth--;
                }
            } else if (item_tag == 0xE000FFFE) { /* (FFFE,E000) Item */
                if (fread(buf, 1, 4, f) != 4) {
                    return false;
                }
                uint32_t item_len = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
                if (item_len != DICOM_UNDEFINED_LENGTH) {
                    if (fseek(f, item_len, SEEK_CUR) != 0) {
                        return false;
                    }
                } else {
                    seq_depth++;
                }
            } else {
                /* Unknown 4 bytes: may be misaligned (e.g. private undefined-length value).
                 * Back up 3 bytes so next read advances by 1; resync until we find (FFFE,E0DD). */
                if (fseek(f, -3, SEEK_CUR) != 0) {
                    return false;
                }
            }
        }
        return true;
    }
    return fseek(f, length, SEEK_CUR) == 0;
}

/* DICOM RLE (PackBits) decode: decode one segment from src (length src_len) into dst (length dst_len). */
static bool rle_decode_segment(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len) {
    size_t out = 0;
    size_t in = 0;
    while (out < dst_len && in < src_len) {
        int8_t n = (int8_t)src[in++];
        if (n >= 0 && n <= 127) {
            size_t copy = (size_t)n + 1;
            if (in + copy > src_len || out + copy > dst_len)
                return false;
            memcpy(dst + out, src + in, copy);
            in += copy;
            out += copy;
        } else if (n >= -127 && n <= -1) {
            size_t replicate = (size_t)(1 - n);
            if (in >= src_len || out + replicate > dst_len)
                return false;
            uint8_t byte = src[in++];
            for (size_t i = 0; i < replicate; i++)
                dst[out++] = byte;
        }
        /* n == -128: no-op */
    }
    return out == dst_len;
}

/* Decode RLE-compressed fragment into raw frame. RLE header is 64 bytes (16 x uint32 LE). */
static bool rle_decode_frame(const uint8_t* fragment, uint32_t fragment_len,
                             uint8_t* frame_out, uint32_t frame_size,
                             uint32_t rows, uint32_t columns, uint32_t samples_per_pixel,
                             uint32_t bytes_per_sample) {
    if (fragment_len < DICOM_RLE_HEADER_SIZE)
        return false;
    uint32_t num_segments = (uint32_t)fragment[0] | ((uint32_t)fragment[1] << 8) |
                            ((uint32_t)fragment[2] << 16) | ((uint32_t)fragment[3] << 24);
    if (num_segments == 0 || num_segments > DICOM_MAX_RLE_SEGMENTS)
        return false;
    uint32_t offsets[16];
    for (int i = 0; i < 16; i++) {
        offsets[i] = (uint32_t)fragment[4 + i * 4] | ((uint32_t)fragment[5 + i * 4] << 8) |
                     ((uint32_t)fragment[6 + i * 4] << 16) | ((uint32_t)fragment[7 + i * 4] << 24);
    }
    uint32_t segment_size = frame_size / num_segments;
    if (segment_size * num_segments != frame_size)
        return false;
    for (uint32_t s = 0; s < num_segments; s++) {
        uint32_t start = offsets[s];
        uint32_t end = (s + 1 < num_segments) ? offsets[s + 1] : (uint32_t)fragment_len;
        if (start >= end || end > fragment_len)
            return false;
        if (!rle_decode_segment(fragment + start, end - start, frame_out + s * segment_size, segment_size)) {
            return false;
        }
    }
    /* Interleave segments into final frame: for 1 segment done; for 2 (16-bit) high/low; for 3 (RGB) R,G,B */
    if (num_segments == 2 && bytes_per_sample == 2) {
        uint32_t n = rows * columns;
        uint8_t* high = frame_out;
        uint8_t* low = frame_out + n;
        for (uint32_t i = n; i > 0; i--) {
            uint8_t h = high[i - 1], l = low[i - 1];
            frame_out[(i - 1) * 2] = l;
            frame_out[(i - 1) * 2 + 1] = h;
        }
    } else if (num_segments == 3 && samples_per_pixel == 3 && bytes_per_sample == 1) {
        uint32_t n = rows * columns;
        uint8_t* R = frame_out;
        uint8_t* G = frame_out + n;
        uint8_t* B = frame_out + 2 * n;
        for (uint32_t i = n; i > 0; i--) {
            uint32_t idx = i - 1;
            frame_out[idx * 3] = R[idx];
            frame_out[idx * 3 + 1] = G[idx];
            frame_out[idx * 3 + 2] = B[idx];
        }
    }
    return true;
}

#ifdef HAVE_LIBJPEG
/* Decode a raw JPEG bitstream from memory into out_pixels (row-major, components per pixel).
 * Expected dimensions and component count must match the JPEG stream; returns false otherwise. */
typedef struct {
    struct jpeg_source_mgr pub;
    const JOCTET* data;
    size_t size;
    size_t pos;
    bool at_eoi;
} mem_source_mgr;

static void mem_init_source(j_decompress_ptr cinfo) {
    mem_source_mgr* src = (mem_source_mgr*)cinfo->src;
    src->pos = 0;
    src->at_eoi = false;
}

static boolean mem_fill_input_buffer(j_decompress_ptr cinfo) {
    static const JOCTET eoi[2] = {0xFF, 0xD9};
    mem_source_mgr* src = (mem_source_mgr*)cinfo->src;
    if (src->at_eoi) {
        src->pub.next_input_byte = eoi;
        src->pub.bytes_in_buffer = 2;
        return TRUE;
    }
    if (src->pos < src->size) {
        src->pub.next_input_byte = src->data + src->pos;
        src->pub.bytes_in_buffer = src->size - src->pos;
        src->pos = src->size;
        return TRUE;
    }
    src->at_eoi = true;
    src->pub.next_input_byte = eoi;
    src->pub.bytes_in_buffer = 2;
    return TRUE;
}

static void mem_skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
    mem_source_mgr* src = (mem_source_mgr*)cinfo->src;
    if (num_bytes > 0 && (size_t)num_bytes <= src->pub.bytes_in_buffer) {
        src->pub.next_input_byte += (size_t)num_bytes;
        src->pub.bytes_in_buffer -= (size_t)num_bytes;
    } else if (num_bytes > 0) {
        src->pos += (size_t)num_bytes - src->pub.bytes_in_buffer;
        src->pub.bytes_in_buffer = 0;
    }
}

static boolean mem_resync_to_restart(j_decompress_ptr cinfo, int desired) {
    (void)cinfo;
    (void)desired;
    return TRUE;
}

static void mem_term_source(j_decompress_ptr cinfo) {
    (void)cinfo;
}

struct dicom_jpeg_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void dicom_jpeg_error_exit(j_common_ptr cinfo) {
    struct dicom_jpeg_error_mgr* err = (struct dicom_jpeg_error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(err->setjmp_buffer, 1);
}

static bool jpeg_decode_from_memory(const uint8_t* jpeg_data, size_t jpeg_len,
                                    uint8_t* out_pixels, uint32_t expected_width,
                                    uint32_t expected_height, uint32_t expected_components) {
    struct jpeg_decompress_struct cinfo;
    struct dicom_jpeg_error_mgr jerr;
    mem_source_mgr mem_src;
    JSAMPARRAY buffer;
    int row_stride;
    bool rgb_to_gray; /* DICOM monochrome but JPEG is 3-component: convert to grayscale */

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = dicom_jpeg_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    mem_src.pub.init_source = mem_init_source;
    mem_src.pub.fill_input_buffer = mem_fill_input_buffer;
    mem_src.pub.skip_input_data = mem_skip_input_data;
    mem_src.pub.resync_to_restart = mem_resync_to_restart;
    mem_src.pub.term_source = mem_term_source;
    mem_src.data = (const JOCTET*)jpeg_data;
    mem_src.size = jpeg_len;
    mem_src.pos = 0;
    mem_src.at_eoi = false;
    mem_src.pub.bytes_in_buffer = 0;
    mem_src.pub.next_input_byte = NULL;
    cinfo.src = (struct jpeg_source_mgr*)&mem_src;

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    if ((uint32_t)cinfo.output_width != expected_width ||
        (uint32_t)cinfo.output_height != expected_height) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    /* Monochrome2 DICOM (expected_components=1) often has 3-component JPEG; convert to grayscale */
    if (expected_components == 1 && cinfo.output_components == 3) {
        rgb_to_gray = true;
    } else if ((uint32_t)cinfo.output_components != expected_components) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    } else {
        rgb_to_gray = false;
    }

    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, (JDIMENSION)row_stride, 1);
    for (uint32_t y = 0; y < expected_height; y++) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        if (rgb_to_gray) {
            const uint8_t* src = buffer[0];
            uint8_t* dst = out_pixels + y * expected_width;
            for (uint32_t x = 0; x < expected_width; x++) {
                int r = src[x * 3 + 0], g = src[x * 3 + 1], b = src[x * 3 + 2];
                int gray = (int)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
                dst[x] = (uint8_t)(gray < 0 ? 0 : (gray > 255 ? 255 : gray));
            }
        } else {
            memcpy(out_pixels + y * (size_t)row_stride, buffer[0], (size_t)row_stride);
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}
#endif /* HAVE_LIBJPEG */

/* Read next encapsulated pixel data item: (FFFE,E000) tag, length; returns length and leaves file at fragment data. */
static uint32_t read_encapsulated_item(FILE* f, long* fragment_start_out) {
    uint8_t buf[8];
    if (fread(buf, 1, 8, f) != 8)
        return 0;
    uint32_t tag = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (tag != TAG_ITEM)
        return 0; /* (FFFE,E000) */
    uint32_t item_len = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    if (fragment_start_out)
        *fragment_start_out = ftell(f);
    return item_len;
}

/* Read US (unsigned short) from element value */
static bool read_us(FILE* f, uint32_t length, bool big_endian, uint32_t* value) {
    uint8_t buf[2];
    if (length < 2 || fread(buf, 1, 2, f) != 2) {
        return false;
    }
    bool swap = big_endian != is_big_endian();
    *value = read_u16(buf, swap);
    if (length > 2 && fseek(f, length - 2, SEEK_CUR) != 0) {
        return false;
    }
    return true;
}

/* Read IS (Integer String) - parse first integer from value; default_value if empty/invalid */
static bool read_is(FILE* f, uint32_t length, uint32_t* value, uint32_t default_value) {
    char buf[32];
    if (length == 0 || length >= sizeof(buf)) {
        if (length > 0 && fseek(f, length, SEEK_CUR) != 0)
            return false;
        *value = default_value;
        return true;
    }
    if (fread(buf, 1, length, f) != length)
        return false;
    buf[length] = '\0';
    /* Trim trailing space/null */
    while (length > 0 && (buf[length - 1] == ' ' || buf[length - 1] == '\0'))
        buf[--length] = '\0';
    if (length == 0) {
        *value = default_value;
        return true;
    }
    char* end = NULL;
    unsigned long parsed = strtoul(buf, &end, 10);
    if (end == buf || parsed > 0xFFFFFFFFUL)
        *value = default_value;
    else
        *value = (uint32_t)parsed;
    return true;
}

/* Read CS (code string) - trim spaces and null, copy to buffer */
static bool read_cs(FILE* f, uint32_t length, char* buf, size_t buf_size) {
    if (length == 0 || buf_size == 0) {
        return false;
    }
    size_t to_read = length < (uint32_t)(buf_size - 1) ? length : (uint32_t)(buf_size - 1);
    if (fread(buf, 1, to_read, f) != to_read) {
        return false;
    }
    buf[to_read] = '\0';
    for (size_t i = to_read; i > 0 && (buf[i - 1] == ' ' || buf[i - 1] == '\0'); i--) {
        buf[i - 1] = '\0';
    }
    if (length > to_read && fseek(f, length - to_read, SEEK_CUR) != 0) {
        return false;
    }
    return true;
}

static DicomPhotometric parse_photometric(const char* cs) {
    if (!cs) {
        return DICOM_PHOTOMETRIC_UNKNOWN;
    }
    if (strncmp(cs, "MONOCHROME1", 11) == 0) {
        return DICOM_PHOTOMETRIC_MONOCHROME1;
    }
    if (strncmp(cs, "MONOCHROME2", 11) == 0) {
        return DICOM_PHOTOMETRIC_MONOCHROME2;
    }
    if (strncmp(cs, "RGB", 3) == 0) {
        return DICOM_PHOTOMETRIC_RGB;
    }
    if (strncmp(cs, "PALETTE", 7) == 0) {
        return DICOM_PHOTOMETRIC_PALETTE;
    }
    if (strncmp(cs, "YBR_FULL_422", 12) == 0) {
        return DICOM_PHOTOMETRIC_YBR_FULL_422;
    }
    if (strncmp(cs, "YBR_FULL", 8) == 0) {
        return DICOM_PHOTOMETRIC_YBR_FULL;
    }
    return DICOM_PHOTOMETRIC_UNKNOWN;
}

/* First pass: read meta and dataset until we have image info and pixel data offset.
 * File Meta Information (group 0002) is always Explicit VR Little Endian per DICOM Part 10.
 * If out_unsupported_compression is non-NULL and we fail due to encapsulated non-RLE (e.g. JPEG), set it to true. */
static bool dicom_parse_meta_and_image_info(FILE* f, DicomImageInfo* info, long* pixel_data_offset, uint32_t* pixel_data_length, bool* out_unsupported_compression) {
    char uid_buf[128];
    char cs_buf[64];
    uint8_t buf[8];
    uint32_t tag, len;
    uint32_t value_offset;
    bool have_transfer_syntax = false;
    bool use_meta_encoding;

    memset(info, 0, sizeof(DicomImageInfo));
    info->number_of_frames = 1;
    info->samples_per_pixel = 1;
    info->photometric = DICOM_PHOTOMETRIC_MONOCHROME2;
    info->explicit_vr = true;
    info->big_endian = false;

    /* Skip preamble and magic */
    if (fseek(f, DICOM_PREAMBLE_SIZE + DICOM_MAGIC_LEN, SEEK_SET) != 0) {
        return false;
    }

    for (;;) {
        use_meta_encoding = !have_transfer_syntax;
        len = read_element(f, use_meta_encoding ? true : info->explicit_vr, use_meta_encoding ? false : info->big_endian, &tag, &value_offset);
        /* len=0 can mean read error or a valid zero-length element (e.g. (0008,0008) Image Type) */
        if (len == 0 && tag != TAG_PIXEL_DATA) {
            if (feof(f)) {
                return false;
            }
            /* Zero-length element: skip 0 bytes and continue */
            skip_element_value(f, 0);
            continue;
        }

        if (tag == TAG_TRANSFER_SYNTAX_UID && len > 0 && len < sizeof(uid_buf)) {
            if (fread(uid_buf, 1, len, f) != len) {
                return false;
            }
            uid_buf[len] = '\0';
            /* DICOM pads UI to even length with space; trim trailing and leading so comparison matches */
            while (len > 0 && (uid_buf[len - 1] == ' ' || uid_buf[len - 1] == '\0')) {
                uid_buf[--len] = '\0';
            }
            {
                size_t lead = 0;
                while (uid_buf[lead] == ' ') {
                    lead++;
                }
                if (lead > 0 && lead <= len) {
                    memmove(uid_buf, uid_buf + lead, len - lead + 1);
                    len -= lead;
                }
            }
            parse_transfer_syntax(uid_buf, len, info);
            have_transfer_syntax = true;
            continue;
        }

        if (tag == TAG_ROWS && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->rows)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_COLUMNS && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->columns)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_SAMPLES_PER_PIXEL && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->samples_per_pixel)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_PHOTOMETRIC && len > 0) {
            if (!read_cs(f, len, cs_buf, sizeof(cs_buf))) {
                return false;
            }
            info->photometric = parse_photometric(cs_buf);
            continue;
        }
        if (tag == TAG_BITS_ALLOCATED && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->bits_allocated)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_BITS_STORED && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->bits_stored)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_HIGH_BIT && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->high_bit)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_PIXEL_REPRESENTATION && len >= 2) {
            if (!read_us(f, len, info->big_endian, &info->pixel_representation)) {
                return false;
            }
            continue;
        }
        if (tag == TAG_NUMBER_OF_FRAMES && len > 0) {
            /* (0028,0008) Number of Frames is IS (Integer String), not US */
            if (!read_is(f, len, &info->number_of_frames, 1)) {
                return false;
            }
            if (info->number_of_frames == 0) {
                info->number_of_frames = 1;
            }
            continue;
        }

        if (tag == TAG_PIXEL_DATA) {
            long pos = ftell(f);
            if (pos < 0) {
                return false; /* ftell failed (e.g. non-seekable stream) */
            }
            if (len == DICOM_UNDEFINED_LENGTH) {
                if (info->is_rle || info->is_jpeg) {
                    *pixel_data_offset = pos;
                    *pixel_data_length = 0; /* 0 = encapsulated, read items in load */
                    return true;
                }
                if (out_unsupported_compression) {
                    *out_unsupported_compression = true; /* other encapsulated not supported */
                }
                return false;
            }
            *pixel_data_offset = pos;
            *pixel_data_length = len;
            return true;
        }

        if (tag == TAG_RED_LUT_DESCRIPTOR && len >= 6) {
            uint32_t num_entries, first_val, bits;
            if (fread(buf, 1, 6, f) != 6) {
                (void)0;
            } else {
                bool swap_lut = info->big_endian != is_big_endian();
                num_entries = read_u16(buf + 0, swap_lut);
                first_val = read_u16(buf + 2, swap_lut);
                bits = read_u16(buf + 4, swap_lut);
                info->lut_num_entries = num_entries ? num_entries : 256;
                info->lut_first_value = first_val;
                info->lut_bits_per_entry = bits ? bits : 8;
            }
            if (len > 6)
                fseek(f, len - 6, SEEK_CUR);
            continue;
        }
        if (tag == TAG_GREEN_LUT_DESCRIPTOR && len >= 6) {
            if (fread(buf, 1, 6, f) != 6) {
                (void)0;
            }
            if (len > 6)
                fseek(f, len - 6, SEEK_CUR);
            continue;
        }
        if (tag == TAG_BLUE_LUT_DESCRIPTOR && len >= 6) {
            if (fread(buf, 1, 6, f) != 6) {
                (void)0;
            }
            if (len > 6)
                fseek(f, len - 6, SEEK_CUR);
            continue;
        }
        if (tag == TAG_RED_PALETTE && len > 0 && info->lut_num_entries > 0) {
            uint32_t n = info->lut_num_entries;
            if (n > 65536)
                n = 65536;
            info->red_lut = (uint16_t*)g_try_malloc(n * sizeof(uint16_t));
            if (info->red_lut && info->lut_bits_per_entry == 8) {
                for (uint32_t i = 0; i < n && (size_t)(i) < len; i++) {
                    uint8_t b;
                    if (fread(&b, 1, 1, f) != 1)
                        break;
                    info->red_lut[i] = (uint16_t)((b << 8) | b);
                }
                if (len > n)
                    fseek(f, len - n, SEEK_CUR);
            } else if (info->red_lut && info->lut_bits_per_entry == 16) {
                size_t to_read = (size_t)n * 2;
                if (to_read > len)
                    to_read = len;
                for (size_t i = 0; i + 2 <= to_read; i += 2) {
                    uint8_t tmp[2];
                    if (fread(tmp, 1, 2, f) != 2)
                        break;
                    info->red_lut[i / 2] = read_u16(tmp, info->big_endian != is_big_endian());
                }
                if (len > (uint32_t)to_read)
                    fseek(f, len - to_read, SEEK_CUR);
            }
            continue;
        }
        if (tag == TAG_GREEN_PALETTE && len > 0 && info->lut_num_entries > 0) {
            uint32_t n = info->lut_num_entries;
            if (n > 65536)
                n = 65536;
            info->green_lut = (uint16_t*)g_try_malloc(n * sizeof(uint16_t));
            if (info->green_lut && info->lut_bits_per_entry == 8) {
                for (uint32_t i = 0; i < n && (size_t)(i) < len; i++) {
                    uint8_t b;
                    if (fread(&b, 1, 1, f) != 1)
                        break;
                    info->green_lut[i] = (uint16_t)((b << 8) | b);
                }
                if (len > n)
                    fseek(f, len - n, SEEK_CUR);
            } else if (info->green_lut && info->lut_bits_per_entry == 16) {
                size_t to_read = (size_t)n * 2;
                if (to_read > len)
                    to_read = len;
                for (size_t i = 0; i + 2 <= to_read; i += 2) {
                    uint8_t tmp[2];
                    if (fread(tmp, 1, 2, f) != 2)
                        break;
                    info->green_lut[i / 2] = read_u16(tmp, info->big_endian != is_big_endian());
                }
                if (len > (uint32_t)to_read)
                    fseek(f, len - to_read, SEEK_CUR);
            }
            continue;
        }
        if (tag == TAG_BLUE_PALETTE && len > 0 && info->lut_num_entries > 0) {
            uint32_t n = info->lut_num_entries;
            if (n > 65536)
                n = 65536;
            info->blue_lut = (uint16_t*)g_try_malloc(n * sizeof(uint16_t));
            if (info->blue_lut && info->lut_bits_per_entry == 8) {
                for (uint32_t i = 0; i < n && (size_t)(i) < len; i++) {
                    uint8_t b;
                    if (fread(&b, 1, 1, f) != 1)
                        break;
                    info->blue_lut[i] = (uint16_t)((b << 8) | b);
                }
                if (len > n)
                    fseek(f, len - n, SEEK_CUR);
            } else if (info->blue_lut && info->lut_bits_per_entry == 16) {
                size_t to_read = (size_t)n * 2;
                if (to_read > len)
                    to_read = len;
                for (size_t i = 0; i + 2 <= to_read; i += 2) {
                    uint8_t tmp[2];
                    if (fread(tmp, 1, 2, f) != 2)
                        break;
                    info->blue_lut[i / 2] = read_u16(tmp, info->big_endian != is_big_endian());
                }
                if (len > (uint32_t)to_read)
                    fseek(f, len - to_read, SEEK_CUR);
            }
            continue;
        }

        /* Skip this element's value */
        if (!skip_element_value(f, len)) {
            return false;
        }
    }
}

/* Normalize pixel value to 0..255 for display (handles bits_stored, signed, window) */
static uint8_t dicom_sample_to_8bit(uint32_t raw, uint32_t bits_stored, uint32_t pixel_rep,
                                    uint32_t min_val, uint32_t max_val) {
    if (bits_stored > 16) {
        bits_stored = 16;
    }
    uint32_t mask = (1u << bits_stored) - 1;
    raw &= mask;

    int32_t s;
    if (pixel_rep) {
        /* Signed: interpret high bit as sign */
        uint32_t sign_bit = 1u << (bits_stored - 1);
        if (raw & sign_bit) {
            s = (int32_t)(raw | ~mask);
        } else {
            s = (int32_t)raw;
        }
    } else {
        s = (int32_t)raw;
    }

    /* Simple linear window: map [min_val, max_val] -> [0, 255] */
    if (max_val > min_val) {
        double t = (double)(s - (int32_t)min_val) / (double)((int32_t)max_val - (int32_t)min_val);
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        return (uint8_t)(t * 255.0 + 0.5);
    }
    return (uint8_t)(s & 0xFF);
}

static PluginError load_dicom(ImageDocument* doc, const char* filename) {
    FILE* f;
    DicomImageInfo info;
    long pixel_data_offset = 0;
    uint32_t pixel_data_length = 0;
    uint8_t* raw_pixels = NULL;
    uint32_t width, height, frames;
    uint32_t bytes_per_sample;
    uint32_t samples_per_pixel;
    uint32_t frame_size;
    ImageLayer* base_layer = NULL;
    cairo_surface_t* temp_surface;
    guchar* surface_data;
    int surface_stride;
    bool swap;
    uint32_t min_val = 0, max_val = 255;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    f = g_fopen(filename, "rb");
    if (!f) {
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    bool unsupported_compression = false;
    if (!dicom_parse_meta_and_image_info(f, &info, &pixel_data_offset, &pixel_data_length, &unsupported_compression)) {
        fclose(f);
        if (info.red_lut)
            g_free(info.red_lut);
        if (info.green_lut)
            g_free(info.green_lut);
        if (info.blue_lut)
            g_free(info.blue_lut);
        if (unsupported_compression) {
            return PLUGIN_ERROR_UNSUPPORTED_COMPRESSION;
        }
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

#ifdef HAVE_LIBJPEG
    (void)0;
#else
    if (info.is_jpeg) {
        fclose(f);
        if (info.red_lut)
            g_free(info.red_lut);
        if (info.green_lut)
            g_free(info.green_lut);
        if (info.blue_lut)
            g_free(info.blue_lut);
        return PLUGIN_ERROR_UNSUPPORTED_COMPRESSION;
    }
#endif

    width = info.columns;
    height = info.rows;
    frames = info.number_of_frames;
    samples_per_pixel = info.samples_per_pixel;

    /* DICOM defaults: Bits Stored = Bits Allocated, High Bit = Bits Allocated - 1 */
    if (info.bits_stored == 0 && info.bits_allocated > 0) {
        info.bits_stored = info.bits_allocated;
    }
    if (info.bits_allocated > 0 && info.high_bit == 0) {
        info.high_bit = info.bits_allocated - 1;
    }

    if (width == 0 || height == 0) {
        fclose(f);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    if (info.is_jpeg) {
        bytes_per_sample = 1; /* JPEG encapsulated is always 8-bit */
    } else if (info.bits_allocated <= 8) {
        bytes_per_sample = 1;
    } else if (info.bits_allocated <= 16) {
        bytes_per_sample = 2;
    } else {
        fclose(f);
        return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
    }

    frame_size = width * height * samples_per_pixel * bytes_per_sample;
    /* Sanity cap to avoid huge allocations from misparsed/malformed Number of Frames */
    if (frames > 4096) {
        frames = 4096;
    }
    size_t total_bytes = (size_t)frame_size * (size_t)frames;
    raw_pixels = g_try_malloc(total_bytes);
    if (!raw_pixels) {
        fclose(f);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    if (pixel_data_offset < 0) {
        g_free(raw_pixels);
        fclose(f);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    if (info.is_rle && pixel_data_length == 0) {
        /* Encapsulated RLE: read one item per frame (first item may be Basic Offset Table) */
        if (fseek(f, pixel_data_offset, SEEK_SET) != 0) {
            g_free(raw_pixels);
            fclose(f);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
        for (uint32_t frame = 0; frame < frames;) {
            long frag_start;
            uint32_t item_len = read_encapsulated_item(f, &frag_start);
            if (item_len == 0) {
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }
            /* First item may be Basic Offset Table (4 bytes per frame) */
            if (frame == 0 && item_len == frames * 4) {
                if (fseek(f, item_len, SEEK_CUR) != 0) {
                    g_free(raw_pixels);
                    fclose(f);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }
                continue; /* skip BOT, next item is frame 0 */
            }
            uint8_t* fragment = (uint8_t*)g_try_malloc(item_len);
            if (!fragment) {
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }
            size_t nread = fread(fragment, 1, item_len, f);
            if (nread != item_len) {
                g_free(fragment);
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }
            if (!rle_decode_frame(fragment, item_len, raw_pixels + (size_t)frame * frame_size,
                                  frame_size, height, width, samples_per_pixel, bytes_per_sample)) {
                g_free(fragment);
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }
            g_free(fragment);
            frame++;
        }
        fclose(f);
    }
#ifdef HAVE_LIBJPEG
    else if (info.is_jpeg && pixel_data_length == 0) {
        /* Encapsulated JPEG: one raw JPEG bitstream per frame (first item may be Basic Offset Table) */
        if (fseek(f, pixel_data_offset, SEEK_SET) != 0) {
            g_free(raw_pixels);
            fclose(f);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
        for (uint32_t frame = 0; frame < frames;) {
            long frag_start;
            uint32_t item_len = read_encapsulated_item(f, &frag_start);
            if (item_len == 0) {
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }
            if (frame == 0 && item_len == frames * 4) {
                if (fseek(f, item_len, SEEK_CUR) != 0) {
                    g_free(raw_pixels);
                    fclose(f);
                    return PLUGIN_ERROR_FILE_READ_ERROR;
                }
                continue;
            }
            uint8_t* fragment = (uint8_t*)g_try_malloc(item_len);
            if (!fragment) {
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_OUT_OF_MEMORY;
            }
            size_t nread = fread(fragment, 1, item_len, f);
            if (nread != item_len) {
                g_free(fragment);
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_FILE_READ_ERROR;
            }
            if (!jpeg_decode_from_memory(fragment, item_len, raw_pixels + (size_t)frame * frame_size,
                                         width, height, samples_per_pixel)) {
                g_free(fragment);
                g_free(raw_pixels);
                fclose(f);
                return PLUGIN_ERROR_CORRUPT_FILE;
            }
            g_free(fragment);
            frame++;
        }
        fclose(f);
    }
#endif
    else {
        /* Native (uncompressed) pixel data */
        if (total_bytes == 0 || (size_t)pixel_data_length < total_bytes) {
            g_free(raw_pixels);
            fclose(f);
            return PLUGIN_ERROR_CORRUPT_FILE;
        }
        if (fseek(f, pixel_data_offset, SEEK_SET) != 0) {
            g_free(raw_pixels);
            fclose(f);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
        size_t nread = fread(raw_pixels, 1, total_bytes, f);
        if (nread != total_bytes) {
            g_free(raw_pixels);
            fclose(f);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
        fclose(f);
    }

    swap = info.big_endian != is_big_endian();

    /* Compute min/max for grayscale windowing (use first frame sample) */
    if (samples_per_pixel == 1 && frames >= 1) {
        uint32_t count = width * height;
        uint32_t lo = UINT32_MAX, hi = 0;
        if (bytes_per_sample == 1) {
            for (uint32_t i = 0; i < count; i++) {
                uint32_t v = raw_pixels[i];
                if (v < lo)
                    lo = v;
                if (v > hi)
                    hi = v;
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                uint16_t v = read_u16(raw_pixels + i * 2, swap);
                if (v < lo)
                    lo = v;
                if (v > hi)
                    hi = v;
            }
        }
        min_val = lo;
        max_val = hi;
        if (max_val <= min_val) {
            max_val = min_val + 1;
        }
    }

    /* Set document to first frame dimensions */
    doc->width = width;
    doc->height = height;
    doc->channels = 4;
    doc->bit_depth = 8;
    doc->has_alpha = false;

    /* Free existing layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    for (uint32_t frame = 0; frame < frames; frame++) {
        const uint8_t* frame_data = raw_pixels + frame * frame_size;
        char layer_name[64];
        g_snprintf(layer_name, sizeof(layer_name), "Frame %u", frame + 1);

        base_layer = layer_new(layer_name, doc->width, doc->height, TRUE,
                               LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
        if (!base_layer) {
            g_free(raw_pixels);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        temp_surface = base_layer->surface;
        if (!temp_surface) {
            layer_free(base_layer);
            g_free(raw_pixels);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        cairo_surface_flush(temp_surface);
        surface_data = cairo_image_surface_get_data(temp_surface);
        surface_stride = cairo_image_surface_get_stride(temp_surface);
        if (!surface_data) {
            layer_free(base_layer);
            g_free(raw_pixels);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        for (uint32_t y = 0; y < height; y++) {
            guchar* row = surface_data + y * surface_stride;
            for (uint32_t x = 0; x < width; x++) {
                uint8_t r, g, b;
                if (info.photometric == DICOM_PHOTOMETRIC_PALETTE && info.red_lut && info.green_lut && info.blue_lut) {
                    uint32_t idx;
                    if (bytes_per_sample == 1) {
                        idx = frame_data[y * width + x];
                    } else {
                        idx = read_u16(frame_data + (y * width + x) * 2, swap);
                    }
                    uint32_t n_entries = info.lut_num_entries;
                    if (n_entries > 65536)
                        n_entries = 65536;
                    if (idx >= n_entries)
                        idx = n_entries - 1;
                    r = (uint8_t)((info.red_lut[idx] * 255) / 65535);
                    g = (uint8_t)((info.green_lut[idx] * 255) / 65535);
                    b = (uint8_t)((info.blue_lut[idx] * 255) / 65535);
                } else if (info.photometric == DICOM_PHOTOMETRIC_YBR_FULL_422 && samples_per_pixel == 3 && bytes_per_sample == 1) {
                    /* 4:2:2 packed: Y0 Cb Cr Y1 (Cb Cr shared for 2 pixels) */
                    uint32_t base = (y * width + x) / 2 * 4; /* 4 bytes per 2 pixels */
                    uint8_t Y, Cb, Cr;
                    if ((x & 1) == 0) {
                        Y = frame_data[base];
                        Cb = frame_data[base + 1];
                        Cr = frame_data[base + 2];
                    } else {
                        Y = frame_data[base + 3];
                        Cb = frame_data[base + 1];
                        Cr = frame_data[base + 2];
                    }
                    int y_ = (int)Y, cb_ = (int)Cb - 128, cr_ = (int)Cr - 128;
                    int R = y_ + (int)(1.402 * cr_);
                    int G = y_ - (int)(0.344 * cb_) - (int)(0.714 * cr_);
                    int B = y_ + (int)(1.772 * cb_);
                    r = (uint8_t)(R < 0 ? 0 : (R > 255 ? 255 : R));
                    g = (uint8_t)(G < 0 ? 0 : (G > 255 ? 255 : G));
                    b = (uint8_t)(B < 0 ? 0 : (B > 255 ? 255 : B));
                } else if (info.photometric == DICOM_PHOTOMETRIC_YBR_FULL && samples_per_pixel == 3 && bytes_per_sample == 1) {
                    uint32_t base_off = (y * width + x) * 3;
                    int y_ = (int)frame_data[base_off];
                    int cb_ = (int)frame_data[base_off + 1] - 128;
                    int cr_ = (int)frame_data[base_off + 2] - 128;
                    int R = y_ + (int)(1.402 * cr_);
                    int G = y_ - (int)(0.344 * cb_) - (int)(0.714 * cr_);
                    int B = y_ + (int)(1.772 * cb_);
                    r = (uint8_t)(R < 0 ? 0 : (R > 255 ? 255 : R));
                    g = (uint8_t)(G < 0 ? 0 : (G > 255 ? 255 : G));
                    b = (uint8_t)(B < 0 ? 0 : (B > 255 ? 255 : B));
                } else if (samples_per_pixel == 1) {
                    uint32_t raw;
                    if (bytes_per_sample == 1) {
                        raw = frame_data[y * width * bytes_per_sample + x];
                    } else {
                        raw = read_u16(frame_data + (y * width + x) * 2, swap);
                    }
                    r = g = b = dicom_sample_to_8bit(raw, info.bits_stored, info.pixel_representation, min_val, max_val);
                    if (info.photometric == DICOM_PHOTOMETRIC_MONOCHROME1) {
                        r = g = b = 255 - r;
                    }
                } else if (samples_per_pixel == 3) {
                    uint32_t base_off = (y * width + x) * samples_per_pixel * bytes_per_sample;
                    if (bytes_per_sample == 1) {
                        r = frame_data[base_off];
                        g = frame_data[base_off + 1];
                        b = frame_data[base_off + 2];
                    } else {
                        r = dicom_sample_to_8bit(read_u16(frame_data + base_off, swap), info.bits_stored, info.pixel_representation, 0, (1u << info.bits_stored) - 1);
                        g = dicom_sample_to_8bit(read_u16(frame_data + base_off + 2, swap), info.bits_stored, info.pixel_representation, 0, (1u << info.bits_stored) - 1);
                        b = dicom_sample_to_8bit(read_u16(frame_data + base_off + 4, swap), info.bits_stored, info.pixel_representation, 0, (1u << info.bits_stored) - 1);
                    }
                } else {
                    r = g = b = 0;
                }
                row[x * 4 + 0] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = 255;
            }
        }

        cairo_surface_mark_dirty(temp_surface);
        doc->layers = g_list_append(doc->layers, base_layer);
    }

    g_free(raw_pixels);
    if (info.red_lut)
        g_free(info.red_lut);
    if (info.green_lut)
        g_free(info.green_lut);
    if (info.blue_lut)
        g_free(info.blue_lut);

    document_render_composite(doc);
    return PLUGIN_ERROR_NONE;
}

bool plugin_init_dicom(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "DICOM - Digital Imaging and Communications in Medicine";
    out_plugin->format_info.extensions = "dcm,dicom";
    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.supports_hdr = false;
    out_plugin->format_info.priority = 70;

    out_plugin->callbacks.can_load = can_load_dicom;
    out_plugin->callbacks.load = load_dicom;
    out_plugin->callbacks.can_save = can_save_dicom;
    out_plugin->callbacks.save = NULL;
    out_plugin->callbacks.get_format_info = NULL;
    out_plugin->callbacks.get_save_options_size = NULL;
    out_plugin->callbacks.init_save_options = NULL;
    out_plugin->callbacks.cleanup = NULL;

    return true;
}
