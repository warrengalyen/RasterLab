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
 * RasterLab Binary Gradient (.rgr) reader and writer.
 *
 * Binary layout (all multi-byte integers are little-endian):
 *
 *   File header (12 bytes):
 *     [4]  magic          "RGRX"  (0x52 0x47 0x52 0x58)
 *     [2]  version        1       (uint16)
 *     [2]  reserved       0       (uint16)
 *     [4]  gradient_count         (uint32)
 *
 *   Per gradient:
 *     [4]  name_len   byte length of name including NUL  (uint32; 0 = unnamed)
 *     [N]  name       UTF-8 NUL-terminated (present only when name_len > 0)
 *     [4]  num_segments            (uint32)
 *     [4]  num_transparency_stops  (uint32)
 *     [4]  smoothness              (int32)
 *
 *   Per segment (104 bytes each):
 *     11 × double (8 bytes, little-endian IEEE 754):
 *       left_pos, midpoint, right_pos,
 *       left_r, left_g, left_b, left_a,
 *       right_r, right_g, right_b, right_a
 *     4 × int32:
 *       blend_mode, color_space, left_type, right_type
 *
 *   Per transparency stop (24 bytes each):
 *     3 × double (8 bytes, little-endian IEEE 754):
 *       position, midpoint, opacity
 */

#include "io/gradient_rgr.h"
#include "debug_logger.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RGR_MAGIC   "RGRX"
#define RGR_VERSION 1

/* Maximum sanity limits to guard against corrupt/malicious files */
#define RGR_MAX_GRADIENTS       65536u
#define RGR_MAX_SEGMENTS        65536u
#define RGR_MAX_TRANS_STOPS     65536u
#define RGR_MAX_NAME_LEN        65536u

/* -------------------------------------------------------------------------
 * Low-level portable I/O helpers (little-endian)
 * ---------------------------------------------------------------------- */

static int read_u16(FILE* fp, uint16_t* out) {
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    *out = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
    return 1;
}

static int write_u16(FILE* fp, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFFu), (uint8_t)((v >> 8) & 0xFFu) };
    return fwrite(b, 1, 2, fp) == 2;
}

static int read_u32(FILE* fp, uint32_t* out) {
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    *out = (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
    return 1;
}

static int write_u32(FILE* fp, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v & 0xFFu),
        (uint8_t)((v >> 8)  & 0xFFu),
        (uint8_t)((v >> 16) & 0xFFu),
        (uint8_t)((v >> 24) & 0xFFu)
    };
    return fwrite(b, 1, 4, fp) == 4;
}

static int read_i32(FILE* fp, int32_t* out) {
    uint32_t u;
    if (!read_u32(fp, &u)) return 0;
    *out = (int32_t)u;
    return 1;
}

static int write_i32(FILE* fp, int32_t v) {
    return write_u32(fp, (uint32_t)v);
}

/*
 * Double I/O: reinterpret as uint64_t via memcpy so the byte layout is
 * well-defined regardless of platform endianness, then read/write
 * those bytes in little-endian order.
 */
static int read_f64(FILE* fp, double* out) {
    uint8_t b[8];
    if (fread(b, 1, 8, fp) != 8) return 0;
    uint64_t u = (uint64_t)b[0]
               | ((uint64_t)b[1] << 8)
               | ((uint64_t)b[2] << 16)
               | ((uint64_t)b[3] << 24)
               | ((uint64_t)b[4] << 32)
               | ((uint64_t)b[5] << 40)
               | ((uint64_t)b[6] << 48)
               | ((uint64_t)b[7] << 56);
    memcpy(out, &u, 8);
    return 1;
}

static int write_f64(FILE* fp, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    uint8_t b[8] = {
        (uint8_t)(u & 0xFFu),
        (uint8_t)((u >> 8)  & 0xFFu),
        (uint8_t)((u >> 16) & 0xFFu),
        (uint8_t)((u >> 24) & 0xFFu),
        (uint8_t)((u >> 32) & 0xFFu),
        (uint8_t)((u >> 40) & 0xFFu),
        (uint8_t)((u >> 48) & 0xFFu),
        (uint8_t)((u >> 56) & 0xFFu)
    };
    return fwrite(b, 1, 8, fp) == 8;
}

/* -------------------------------------------------------------------------
 * Loader
 * ---------------------------------------------------------------------- */

#define SET_ERR(e)  do { if (error_out) *error_out = (e); } while (0)
#define FAIL(e)     do { SET_ERR(e); goto cleanup; } while (0)

GradientSet* gradient_rgr_load(const char* filename, GradientRgrError* error_out) {
    FILE*        fp      = NULL;
    GradientSet* set     = NULL;
    char*        name_buf = NULL;

    SET_ERR(GRADIENT_RGR_ERROR_NONE);

    if (!filename) {
        debug_log("WRN", "gradient_rgr_load: NULL filename");
        SET_ERR(GRADIENT_RGR_ERROR_INVALID_PARAMETERS);
        return NULL;
    }

    fp = g_fopen(filename, "rb");
    if (!fp) {
        debug_log("WRN", "gradient_rgr_load: cannot open '%s'", filename);
        SET_ERR(GRADIENT_RGR_ERROR_FILE_NOT_FOUND);
        return NULL;
    }

    /* --- File header --- */
    char magic[4];
    if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, RGR_MAGIC, 4) != 0) {
        debug_log("WRN", "gradient_rgr_load: bad magic in '%s'", filename);
        FAIL(GRADIENT_RGR_ERROR_CORRUPT_FILE);
    }

    uint16_t version = 0, reserved = 0;
    if (!read_u16(fp, &version) || !read_u16(fp, &reserved)) {
        FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
    }
    if (version != RGR_VERSION) {
        debug_log("WRN", "gradient_rgr_load: unsupported version %u in '%s'", version, filename);
        FAIL(GRADIENT_RGR_ERROR_CORRUPT_FILE);
    }

    uint32_t gradient_count = 0;
    if (!read_u32(fp, &gradient_count)) {
        FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
    }
    if (gradient_count > RGR_MAX_GRADIENTS) {
        debug_log("WRN", "gradient_rgr_load: unreasonable gradient count %u in '%s'",
                  gradient_count, filename);
        FAIL(GRADIENT_RGR_ERROR_CORRUPT_FILE);
    }

    set = gradient_set_new((int)gradient_count);
    if (!set) {
        FAIL(GRADIENT_RGR_ERROR_OUT_OF_MEMORY);
    }

    /* --- Gradients --- */
    for (uint32_t gi = 0; gi < gradient_count; gi++) {
        GradientDef* def = &set->gradients[gi];

        /* Name */
        uint32_t name_len = 0;
        if (!read_u32(fp, &name_len)) {
            FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
        }
        if (name_len > RGR_MAX_NAME_LEN) {
            debug_log("WRN", "gradient_rgr_load: unreasonable name_len %u", name_len);
            FAIL(GRADIENT_RGR_ERROR_CORRUPT_FILE);
        }
        if (name_len > 0) {
            name_buf = (char*)g_malloc(name_len + 1);
            if (!name_buf) {
                FAIL(GRADIENT_RGR_ERROR_OUT_OF_MEMORY);
            }
            if (fread(name_buf, 1, name_len, fp) != name_len) {
                g_free(name_buf);
                name_buf = NULL;
                FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
            }
            name_buf[name_len - 1] = '\0'; /* ensure NUL even if file is missing it */
            def->name = name_buf;
            name_buf = NULL;
        }

        /* Counts and smoothness */
        uint32_t num_segments = 0, num_trans = 0;
        int32_t  smoothness   = 0;
        if (!read_u32(fp, &num_segments) ||
            !read_u32(fp, &num_trans)    ||
            !read_i32(fp, &smoothness)) {
            FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
        }
        if (num_segments > RGR_MAX_SEGMENTS || num_trans > RGR_MAX_TRANS_STOPS) {
            debug_log("WRN", "gradient_rgr_load: unreasonable segment/trans counts in gradient %u", gi);
            FAIL(GRADIENT_RGR_ERROR_CORRUPT_FILE);
        }

        def->smoothness = smoothness;

        /* Segments */
        if (num_segments > 0) {
            def->segments = (GradientSegment*)calloc(num_segments, sizeof(GradientSegment));
            if (!def->segments) {
                FAIL(GRADIENT_RGR_ERROR_OUT_OF_MEMORY);
            }
            def->num_segments = (int)num_segments;

            for (uint32_t si = 0; si < num_segments; si++) {
                GradientSegment* seg = &def->segments[si];
                int32_t bm = 0, cs = 0, lt = 0, rt = 0;

                if (!read_f64(fp, &seg->left_pos)  ||
                    !read_f64(fp, &seg->midpoint)   ||
                    !read_f64(fp, &seg->right_pos)  ||
                    !read_f64(fp, &seg->left_r)     ||
                    !read_f64(fp, &seg->left_g)     ||
                    !read_f64(fp, &seg->left_b)     ||
                    !read_f64(fp, &seg->left_a)     ||
                    !read_f64(fp, &seg->right_r)    ||
                    !read_f64(fp, &seg->right_g)    ||
                    !read_f64(fp, &seg->right_b)    ||
                    !read_f64(fp, &seg->right_a)    ||
                    !read_i32(fp, &bm)              ||
                    !read_i32(fp, &cs)              ||
                    !read_i32(fp, &lt)              ||
                    !read_i32(fp, &rt)) {
                    FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
                }

                seg->blend_mode  = (GradientBlendMode)bm;
                seg->color_space = (GradientColorSpace)cs;
                seg->left_type   = (GradientEndpointType)lt;
                seg->right_type  = (GradientEndpointType)rt;
            }
        }

        /* Transparency stops */
        if (num_trans > 0) {
            def->transparency_stops = (GradientTransparencyStop*)
                calloc(num_trans, sizeof(GradientTransparencyStop));
            if (!def->transparency_stops) {
                FAIL(GRADIENT_RGR_ERROR_OUT_OF_MEMORY);
            }
            def->num_transparency_stops = (int)num_trans;

            for (uint32_t ti = 0; ti < num_trans; ti++) {
                GradientTransparencyStop* ts = &def->transparency_stops[ti];
                if (!read_f64(fp, &ts->position) ||
                    !read_f64(fp, &ts->midpoint)  ||
                    !read_f64(fp, &ts->opacity)) {
                    FAIL(GRADIENT_RGR_ERROR_FILE_READ_ERROR);
                }
            }
        }
    }

    fclose(fp);
    return set;

cleanup:
    g_free(name_buf);
    if (fp)  fclose(fp);
    if (set) gradient_set_free(set);
    return NULL;
}

#undef SET_ERR
#undef FAIL

/* -------------------------------------------------------------------------
 * Writer
 * ---------------------------------------------------------------------- */

#define SET_ERR(e)  do { if (error_out) *error_out = (e); } while (0)
#define FAIL(e)     do { SET_ERR(e); goto cleanup; } while (0)

gboolean gradient_rgr_save(const GradientSet* set, const char* filename,
                             GradientRgrError* error_out) {
    FILE* fp = NULL;

    SET_ERR(GRADIENT_RGR_ERROR_NONE);

    if (!set || !filename) {
        debug_log("WRN", "gradient_rgr_save: NULL argument");
        SET_ERR(GRADIENT_RGR_ERROR_INVALID_PARAMETERS);
        return FALSE;
    }

    fp = g_fopen(filename, "wb");
    if (!fp) {
        debug_log("WRN", "gradient_rgr_save: cannot open '%s' for writing", filename);
        SET_ERR(GRADIENT_RGR_ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    /* --- File header --- */
    if (fwrite(RGR_MAGIC, 1, 4, fp) != 4) {
        FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
    }
    if (!write_u16(fp, (uint16_t)RGR_VERSION) ||
        !write_u16(fp, 0u)) {
        FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
    }
    if (!write_u32(fp, (uint32_t)set->num_gradients)) {
        FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
    }

    /* --- Gradients --- */
    for (int gi = 0; gi < set->num_gradients; gi++) {
        const GradientDef* def = &set->gradients[gi];

        /* Name */
        uint32_t name_len = 0;
        if (def->name && def->name[0] != '\0') {
            name_len = (uint32_t)(strlen(def->name) + 1); /* include NUL */
        }
        if (!write_u32(fp, name_len)) {
            FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
        }
        if (name_len > 0) {
            if (fwrite(def->name, 1, name_len, fp) != name_len) {
                FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
            }
        }

        /* Counts and smoothness */
        if (!write_u32(fp, (uint32_t)def->num_segments)            ||
            !write_u32(fp, (uint32_t)def->num_transparency_stops)  ||
            !write_i32(fp, (int32_t)def->smoothness)) {
            FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
        }

        /* Segments */
        for (int si = 0; si < def->num_segments; si++) {
            const GradientSegment* seg = &def->segments[si];
            if (!write_f64(fp, seg->left_pos)  ||
                !write_f64(fp, seg->midpoint)   ||
                !write_f64(fp, seg->right_pos)  ||
                !write_f64(fp, seg->left_r)     ||
                !write_f64(fp, seg->left_g)     ||
                !write_f64(fp, seg->left_b)     ||
                !write_f64(fp, seg->left_a)     ||
                !write_f64(fp, seg->right_r)    ||
                !write_f64(fp, seg->right_g)    ||
                !write_f64(fp, seg->right_b)    ||
                !write_f64(fp, seg->right_a)    ||
                !write_i32(fp, (int32_t)seg->blend_mode)  ||
                !write_i32(fp, (int32_t)seg->color_space) ||
                !write_i32(fp, (int32_t)seg->left_type)   ||
                !write_i32(fp, (int32_t)seg->right_type)) {
                FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
            }
        }

        /* Transparency stops */
        for (int ti = 0; ti < def->num_transparency_stops; ti++) {
            const GradientTransparencyStop* ts = &def->transparency_stops[ti];
            if (!write_f64(fp, ts->position) ||
                !write_f64(fp, ts->midpoint)  ||
                !write_f64(fp, ts->opacity)) {
                FAIL(GRADIENT_RGR_ERROR_FILE_WRITE_ERROR);
            }
        }
    }

    fclose(fp);
    return TRUE;

cleanup:
    if (fp) fclose(fp);
    return FALSE;
}

#undef SET_ERR
#undef FAIL
