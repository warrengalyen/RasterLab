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
 * Photoshop Gradient (.grd) reader and writer.
 *
 * TWO FORMAT VERSIONS
 * -------------------
 * Both are big-endian binary.  File starts with the 4-byte signature "8BGR"
 * followed by a 2-byte (int16) version number.
 *
 * Version 3 (Photoshop 5 and earlier):
 *   - Flat binary layout: Pascal-string name, color stops, transparency stops.
 *   - One color model field per stop (RGB, HSV, CMYK, Lab, Grayscale).
 *
 * Version 5 (Photoshop 6+):
 *   - The remainder of the file after the 10-byte preamble is a Photoshop
 *     Descriptor tree (same format used in Action files).
 *   - Top-level descriptor key "GrdL" holds a VlLs (list) of gradient descriptors.
 *   - Key encoding rule: if the stored 4-byte length is 0, the actual key string
 *     is the following 4 bytes verbatim.
 *   - Strings: uint32_t UTF-16 code-unit count + UTF-16BE data.
 *   - Color stop type: "Clrt", transparency stop type: "TrnS".
 *   - Noise gradients (GrdF = "ClNs") are skipped on load; not written on save.
 *
 * INTERNAL MODEL MAPPING
 * ----------------------
 * GRD stores separate color stops and transparency stops.  These are kept
 * separate in GradientDef (segments[] + transparency_stops[]) to allow lossless
 * round-tripping.  The segments array is built by pairing adjacent color stops;
 * transparency stops are stored verbatim.
 */

#include "io/gradient_grd.h"
#include "debug_logger.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Big-endian read helpers
 * ---------------------------------------------------------------------- */

static int read_u8(FILE* fp, uint8_t* out) {
    int c = fgetc(fp);
    if (c == EOF) return 0;
    *out = (uint8_t)c;
    return 1;
}

static int read_be_u16(FILE* fp, uint16_t* out) {
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    *out = (uint16_t)((b[0] << 8) | b[1]);
    return 1;
}

static int read_be_i16(FILE* fp, int16_t* out) {
    uint16_t u;
    if (!read_be_u16(fp, &u)) return 0;
    *out = (int16_t)u;
    return 1;
}

static int read_be_u32(FILE* fp, uint32_t* out) {
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
    return 1;
}

static int read_be_i32(FILE* fp, int32_t* out) {
    uint32_t u;
    if (!read_be_u32(fp, &u)) return 0;
    *out = (int32_t)u;
    return 1;
}

static int read_be_double(FILE* fp, double* out) {
    uint8_t b[8];
    if (fread(b, 1, 8, fp) != 8) return 0;
    /* Reconstruct IEEE-754 big-endian double */
    uint64_t u = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
                 ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
                 ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                 ((uint64_t)b[6] <<  8) |  (uint64_t)b[7];
    memcpy(out, &u, 8);
    return 1;
}

/* Read a 4-byte tag into a 5-byte null-terminated buffer */
static int read_tag(FILE* fp, char tag[5]) {
    if (fread(tag, 1, 4, fp) != 4) return 0;
    tag[4] = '\0';
    return 1;
}

/* -------------------------------------------------------------------------
 * Big-endian write helpers
 * ---------------------------------------------------------------------- */

static int write_be_u16(FILE* fp, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return (int)fwrite(b, 1, 2, fp) == 2;
}

static int write_be_u32(FILE* fp, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 8),  (uint8_t)v };
    return (int)fwrite(b, 1, 4, fp) == 4;
}

static int write_be_i32(FILE* fp, int32_t v) {
    return write_be_u32(fp, (uint32_t)v);
}

static int write_be_double(FILE* fp, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    uint8_t b[8];
    for (int i = 7; i >= 0; i--) { b[i] = (uint8_t)(u & 0xFF); u >>= 8; }
    return (int)fwrite(b, 1, 8, fp) == 8;
}

/* Write a 4-byte tag string (no null terminator in file) */
static int write_tag(FILE* fp, const char* tag) {
    return (int)fwrite(tag, 1, 4, fp) == 4;
}

/* Write a descriptor key using the "short key" convention:
 * 4-byte zero length followed by the 4-character key. */
static int write_key(FILE* fp, const char* key) {
    return write_be_u32(fp, 0) && write_tag(fp, key);
}

/* Write a UTF-16BE string preceded by a uint32_t code-unit count.
 * Input is UTF-8; conversion via GLib. */
static int write_utf16_string(FILE* fp, const char* utf8) {
    if (!utf8) utf8 = "";
    glong n_units = 0;
    gunichar2* utf16 = g_utf8_to_utf16(utf8, -1, NULL, &n_units, NULL);
    if (!utf16) {
        /* Fallback: write empty string */
        return write_be_u32(fp, 0);
    }
    int ok = write_be_u32(fp, (uint32_t)n_units);
    for (glong i = 0; i < n_units && ok; i++) {
        uint8_t b[2] = { (uint8_t)(utf16[i] >> 8), (uint8_t)(utf16[i] & 0xFF) };
        ok = (int)fwrite(b, 1, 2, fp) == 2;
    }
    g_free(utf16);
    return ok;
}

/* Read a UTF-16BE string (uint32_t count + UTF-16 data) and return UTF-8. */
static char* read_utf16_string(FILE* fp) {
    uint32_t n_units = 0;
    if (!read_be_u32(fp, &n_units)) return NULL;
    if (n_units == 0) return g_strdup("");
    if (n_units > 65535) return NULL; /* sanity guard */
    gunichar2* buf = g_malloc((n_units + 1) * sizeof(gunichar2));
    if (!buf) return NULL;
    for (uint32_t i = 0; i < n_units; i++) {
        uint8_t b[2];
        if (fread(b, 1, 2, fp) != 2) { g_free(buf); return NULL; }
        buf[i] = (gunichar2)((b[0] << 8) | b[1]);
    }
    buf[n_units] = 0;
    char* utf8 = g_utf16_to_utf8(buf, (glong)n_units, NULL, NULL, NULL);
    g_free(buf);
    return utf8 ? utf8 : g_strdup("");
}

/* -------------------------------------------------------------------------
 * Color conversion helpers
 * ---------------------------------------------------------------------- */

/* Convert CMYK (0-100%) to RGB (0-1) */
static void cmyk_to_rgb(double c, double m, double y, double k,
                         double* r, double* g, double* b) {
    double k1 = 1.0 - k / 100.0;
    *r = (1.0 - c / 100.0) * k1;
    *g = (1.0 - m / 100.0) * k1;
    *b = (1.0 - y / 100.0) * k1;
}

/* Very rough Lab to RGB (via XYZ, D65, sRGB).
 * Used for GRD Lab color stops; precision is sufficient for gradient preview. */
static void lab_to_rgb(double L, double a, double b_val,
                        double* r, double* g, double* b) {
    /* Lab -> XYZ */
    double fy = (L + 16.0) / 116.0;
    double fx = a / 500.0 + fy;
    double fz = fy - b_val / 200.0;
    double eps = 0.008856;
    double kappa = 903.3;
    double xn = 0.95047, yn = 1.00000, zn = 1.08883;
    double x = (fx * fx * fx > eps) ? fx * fx * fx : (116.0 * fx - 16.0) / kappa;
    double y = (L > kappa * eps)     ? fy * fy * fy : L / kappa;
    double z = (fz * fz * fz > eps) ? fz * fz * fz : (116.0 * fz - 16.0) / kappa;
    x *= xn; y *= yn; z *= zn;

    /* XYZ -> linear sRGB */
    double lr =  3.2406 * x - 1.5372 * y - 0.4986 * z;
    double lg = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    double lb =  0.0557 * x - 0.2040 * y + 1.0570 * z;

    /* Gamma */
#define SRGB_GAMMA(c) ((c) <= 0.0031308 ? 12.92*(c) : 1.055*pow((c),1.0/2.4)-0.055)
    *r = SRGB_GAMMA(lr < 0.0 ? 0.0 : lr > 1.0 ? 1.0 : lr);
    *g = SRGB_GAMMA(lg < 0.0 ? 0.0 : lg > 1.0 ? 1.0 : lg);
    *b = SRGB_GAMMA(lb < 0.0 ? 0.0 : lb > 1.0 ? 1.0 : lb);
#undef SRGB_GAMMA
    if (*r > 1.0) *r = 1.0; if (*r < 0.0) *r = 0.0;
    if (*g > 1.0) *g = 1.0; if (*g < 0.0) *g = 0.0;
    if (*b > 1.0) *b = 1.0; if (*b < 0.0) *b = 0.0;
}

/* HSV (h:0-360°, s:0-100%, v:0-100%) -> RGB [0-1] */
static void hsv_to_rgb_pct(double h, double s, double v,
                             double* r, double* g, double* b) {
    h /= 360.0; s /= 100.0; v /= 100.0;
    if (s <= 0.0) { *r = *g = *b = v; return; }
    double hh = h * 6.0;
    if (hh >= 6.0) hh = 0.0;
    int i = (int)hh;
    double f  = hh - (double)i;
    double p  = v * (1.0 - s);
    double q  = v * (1.0 - s * f);
    double t2 = v * (1.0 - s * (1.0 - f));
    switch (i) {
        case 0: *r = v; *g = t2; *b = p; break;
        case 1: *r = q; *g = v;  *b = p; break;
        case 2: *r = p; *g = v;  *b = t2; break;
        case 3: *r = p; *g = q;  *b = v; break;
        case 4: *r = t2; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

/* =========================================================================
 * VERSION 3 LOADER
 * ====================================================================== */

/* Read a Pascal-string (1 byte length, then N ASCII bytes) into a g_malloc'd string. */
static char* read_pascal_string(FILE* fp) {
    uint8_t len = 0;
    if (!read_u8(fp, &len)) return NULL;
    char* buf = g_malloc((gsize)len + 1);
    if (!buf) return NULL;
    if (len > 0 && fread(buf, 1, len, fp) != len) { g_free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

/* Raw GRD v3 color stop (before conversion) */
typedef struct {
    int32_t  offset;       /* [0, 4096] */
    int32_t  midpoint;     /* percentage [0, 100] */
    int16_t  color_model;  /* 0=RGB,1=HSV,2=CMYK,7=Lab,8=Gray */
    int16_t  channels[4];  /* raw channel values */
    int16_t  color_type;   /* 0=user,1=foreground,2=background */
} GrdV3ColorStop;

/* Raw GRD v3 transparency stop */
typedef struct {
    int32_t offset;    /* [0, 4096] */
    int32_t midpoint;  /* percentage [0, 100] */
    int16_t opacity;   /* [0, 255] */
} GrdV3TransStop;

/* Decode a v3 color stop's raw channel values into normalized RGB. */
static void decode_v3_color(int16_t color_model, const int16_t channels[4],
                              double* r, double* g, double* b) {
    switch (color_model) {
        case 0: /* RGB: channels in [0,255] */
            *r = channels[0] / 255.0;
            *g = channels[1] / 255.0;
            *b = channels[2] / 255.0;
            break;
        case 1: /* HSV: H[0-65535] maps to degrees via /182.04, S/V via /655.35 */
            hsv_to_rgb_pct(channels[0] / 182.04,
                           channels[1] / 655.35,
                           channels[2] / 655.35, r, g, b);
            break;
        case 2: /* CMYK: channels [0,65535] mapped to [0,100%] */
            cmyk_to_rgb(channels[0] / 655.35,
                        channels[1] / 655.35,
                        channels[2] / 655.35,
                        channels[3] / 655.35, r, g, b);
            break;
        case 7: /* Lab */
            lab_to_rgb((double)channels[0],
                       (double)channels[1],
                       (double)channels[2], r, g, b);
            break;
        case 8: /* Grayscale [0,10000] */
            *r = *g = *b = channels[0] / 10000.0;
            break;
        default:
            *r = *g = *b = 0.0;
            break;
    }
}

static gboolean load_v3_gradient(FILE* fp, GradientDef* def, GradientGrdError* err) {
    int16_t n_color = 0, n_trans = 0;

    def->name = read_pascal_string(fp);
    if (!def->name) { *err = GRADIENT_GRD_ERROR_CORRUPT_FILE; return FALSE; }

    if (!read_be_i16(fp, &n_color)) goto corrupt;
    if (n_color < 0 || n_color > 1024) goto corrupt;

    GrdV3ColorStop* cstops = calloc((size_t)n_color, sizeof(GrdV3ColorStop));
    if (n_color > 0 && !cstops) { *err = GRADIENT_GRD_ERROR_OUT_OF_MEMORY; return FALSE; }

    for (int16_t i = 0; i < n_color; i++) {
        GrdV3ColorStop* cs = &cstops[i];
        if (!read_be_i32(fp, &cs->offset))    goto corrupt_free;
        if (!read_be_i32(fp, &cs->midpoint))  goto corrupt_free;
        if (!read_be_i16(fp, &cs->color_model)) goto corrupt_free;
        for (int j = 0; j < 4; j++) {
            if (!read_be_i16(fp, &cs->channels[j])) goto corrupt_free;
        }
        if (!read_be_i16(fp, &cs->color_type)) goto corrupt_free;
    }

    if (!read_be_i16(fp, &n_trans)) goto corrupt_free;
    if (n_trans < 0 || n_trans > 1024) goto corrupt_free;

    GrdV3TransStop* tstops = calloc((size_t)n_trans, sizeof(GrdV3TransStop));
    if (n_trans > 0 && !tstops) {
        free(cstops);
        *err = GRADIENT_GRD_ERROR_OUT_OF_MEMORY;
        return FALSE;
    }

    for (int16_t i = 0; i < n_trans; i++) {
        GrdV3TransStop* ts = &tstops[i];
        if (!read_be_i32(fp, &ts->offset))   { free(tstops); goto corrupt_free; }
        if (!read_be_i32(fp, &ts->midpoint)) { free(tstops); goto corrupt_free; }
        if (!read_be_i16(fp, &ts->opacity))  { free(tstops); goto corrupt_free; }
        /* Skip 6 reserved bytes */
        uint8_t reserved[6];
        if (fread(reserved, 1, 6, fp) != 6)  { free(tstops); goto corrupt_free; }
    }

    /* Build segments from color stops */
    int n_segs = (n_color > 1) ? n_color - 1 : (n_color == 1 ? 1 : 0);
    if (n_segs > 0) {
        def->segments = calloc((size_t)n_segs, sizeof(GradientSegment));
        if (!def->segments) {
            free(cstops); free(tstops);
            *err = GRADIENT_GRD_ERROR_OUT_OF_MEMORY;
            return FALSE;
        }
        def->num_segments = n_segs;
    }

    for (int i = 0; i < n_segs; i++) {
        GradientSegment* s = &def->segments[i];
        GrdV3ColorStop* lcs = &cstops[i];
        GrdV3ColorStop* rcs = &cstops[i + 1];

        s->left_pos  = (double)lcs->offset / 4096.0;
        s->right_pos = (double)rcs->offset / 4096.0;

        /* Midpoint of left stop biases blend to next stop */
        double mid_pct = (double)lcs->midpoint / 100.0;
        s->midpoint = s->left_pos + mid_pct * (s->right_pos - s->left_pos);

        s->blend_mode  = GRADIENT_BLEND_LINEAR;
        s->color_space = GRADIENT_COLOR_RGB;

        decode_v3_color(lcs->color_model, lcs->channels, &s->left_r,  &s->left_g,  &s->left_b);
        decode_v3_color(rcs->color_model, rcs->channels, &s->right_r, &s->right_g, &s->right_b);
        s->left_a = s->right_a = 1.0;

        /* Endpoint types */
        s->left_type  = (lcs->color_type == 1) ? GRADIENT_ENDPOINT_FOREGROUND
                       :(lcs->color_type == 2) ? GRADIENT_ENDPOINT_BACKGROUND
                       : GRADIENT_ENDPOINT_FIXED;
        s->right_type = (rcs->color_type == 1) ? GRADIENT_ENDPOINT_FOREGROUND
                       :(rcs->color_type == 2) ? GRADIENT_ENDPOINT_BACKGROUND
                       : GRADIENT_ENDPOINT_FIXED;
    }

    /* Transparency stops */
    if (n_trans > 0) {
        def->transparency_stops = calloc((size_t)n_trans, sizeof(GradientTransparencyStop));
        if (!def->transparency_stops) {
            free(cstops); free(tstops);
            *err = GRADIENT_GRD_ERROR_OUT_OF_MEMORY;
            return FALSE;
        }
        def->num_transparency_stops = n_trans;
        for (int i = 0; i < n_trans; i++) {
            double pos = tstops[i].offset / 4096.0;
            double mid_pct = tstops[i].midpoint / 100.0;
            def->transparency_stops[i].position = pos;
            def->transparency_stops[i].opacity  = tstops[i].opacity / 255.0;
            /* Store midpoint as absolute position between this and next stop */
            if (i + 1 < n_trans) {
                double next_pos = tstops[i + 1].offset / 4096.0;
                def->transparency_stops[i].midpoint = pos + mid_pct * (next_pos - pos);
            } else {
                def->transparency_stops[i].midpoint = pos + mid_pct * (1.0 - pos);
            }
        }
    }

    free(cstops);
    free(tstops);
    return TRUE;

corrupt_free:
    free(cstops);
corrupt:
    *err = GRADIENT_GRD_ERROR_CORRUPT_FILE;
    return FALSE;
}

/* =========================================================================
 * VERSION 5 DESCRIPTOR LOADER
 * ====================================================================== */

/*
 * OSType tags used in v5 descriptor trees.
 * All 4-character codes that appear as types or keys.
 */
#define OST_OBJC "Objc"  /* Descriptor */
#define OST_VLLS "VlLs"  /* List */
#define OST_TEXT "TEXT"  /* Unicode string */
#define OST_DOUB "doub"  /* double */
#define OST_UNTF "UntF"  /* Unit float */
#define OST_LONG "long"  /* int32 */
#define OST_BOOL "bool"  /* boolean */
#define OST_ENUM "enum"  /* enumerated */
#define OST_TDTA "tdta"  /* raw data */
#define OST_PTHS "Pths"  /* path list (skip) */

/* Skip n bytes */
static int skip_bytes(FILE* fp, size_t n) {
    return fseek(fp, (long)n, SEEK_CUR) == 0;
}

/* Read a variable-length key or type string.
 * If the 4-byte length field is 0, the key is the next 4 bytes verbatim.
 * Otherwise read exactly length bytes. Returns g_malloc'd string or NULL. */
static char* read_var_string(FILE* fp) {
    uint32_t len = 0;
    if (!read_be_u32(fp, &len)) return NULL;
    if (len == 0) {
        char* buf = g_malloc(5);
        if (!buf) return NULL;
        if (fread(buf, 1, 4, fp) != 4) { g_free(buf); return NULL; }
        buf[4] = '\0';
        return buf;
    }
    if (len > 4096) return NULL; /* sanity */
    char* buf = g_malloc(len + 1);
    if (!buf) return NULL;
    if (fread(buf, 1, len, fp) != len) { g_free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

/* Forward declaration for recursive descriptor parsing */
static gboolean skip_descriptor_value(FILE* fp, const char* type_tag);

/* Skip over a single item value of the given OSType tag. */
static gboolean skip_descriptor_value(FILE* fp, const char* type_tag) {
    if (strcmp(type_tag, OST_DOUB) == 0) return skip_bytes(fp, 8);
    if (strcmp(type_tag, OST_UNTF) == 0) return skip_bytes(fp, 12); /* 4-char unit + 8 double */
    if (strcmp(type_tag, OST_LONG) == 0) return skip_bytes(fp, 4);
    if (strcmp(type_tag, OST_BOOL) == 0) return skip_bytes(fp, 1);
    if (strcmp(type_tag, OST_TEXT) == 0) {
        uint32_t n = 0;
        if (!read_be_u32(fp, &n)) return FALSE;
        return skip_bytes(fp, n * 2);
    }
    if (strcmp(type_tag, OST_ENUM) == 0) {
        char* a = read_var_string(fp);
        char* b = read_var_string(fp);
        g_free(a); g_free(b);
        return a && b;
    }
    if (strcmp(type_tag, OST_TDTA) == 0) {
        uint32_t n = 0;
        if (!read_be_u32(fp, &n)) return FALSE;
        return skip_bytes(fp, n);
    }
    if (strcmp(type_tag, OST_OBJC) == 0) {
        /* Unicode name */
        uint32_t name_len = 0;
        if (!read_be_u32(fp, &name_len)) return FALSE;
        if (!skip_bytes(fp, name_len * 2)) return FALSE;
        /* classID */
        char* cid = read_var_string(fp);
        g_free(cid);
        if (!cid) return FALSE;
        /* item count */
        uint32_t count = 0;
        if (!read_be_u32(fp, &count)) return FALSE;
        for (uint32_t i = 0; i < count; i++) {
            char* key = read_var_string(fp);
            char type[5];
            gboolean ok = key && read_tag(fp, type);
            g_free(key);
            if (!ok) return FALSE;
            if (!skip_descriptor_value(fp, type)) return FALSE;
        }
        return TRUE;
    }
    if (strcmp(type_tag, OST_VLLS) == 0) {
        uint32_t count = 0;
        if (!read_be_u32(fp, &count)) return FALSE;
        for (uint32_t i = 0; i < count; i++) {
            char type[5];
            if (!read_tag(fp, type)) return FALSE;
            if (!skip_descriptor_value(fp, type)) return FALSE;
        }
        return TRUE;
    }
    /* Unknown type: cannot determine size; bail */
    debug_log("WRN", "grd_v5: unknown OSType '%s', cannot skip", type_tag);
    return FALSE;
}

/* --- Color descriptor parsing ------------------------------------------ */

/* Parse a "Clr " color descriptor (Objc with classID RGBC/HSBC/CMYC/LbCl/Grsc).
 * Returns TRUE on success, sets r/g/b in [0,1]. */
static gboolean parse_color_objc(FILE* fp, double* r, double* g, double* b) {
    /* We are positioned right after the type tag "Objc" was consumed by caller.
     * Layout: uint32 name_len, name_utf16, classID var-string, uint32 item_count,
     *         then key/type/value triples. */
    uint32_t name_len = 0;
    if (!read_be_u32(fp, &name_len)) return FALSE;
    if (!skip_bytes(fp, name_len * 2)) return FALSE;

    char* classid = read_var_string(fp);
    if (!classid) return FALSE;

    uint32_t count = 0;
    if (!read_be_u32(fp, &count)) { g_free(classid); return FALSE; }

    /* Channel accumulators */
    double ch[4] = { 0.0, 0.0, 0.0, 0.0 };
    char ch_key[4][5] = { "", "", "", "" };
    int ch_count = 0;

    gboolean ok = TRUE;
    for (uint32_t i = 0; i < count && ok; i++) {
        char* key = read_var_string(fp);
        char type[5];
        ok = key && read_tag(fp, type);
        if (!ok) { g_free(key); break; }

        if (strcmp(type, OST_DOUB) == 0) {
            double val = 0.0;
            ok = read_be_double(fp, &val);
            if (ok && ch_count < 4) {
                g_strlcpy(ch_key[ch_count], key, 5);
                ch[ch_count++] = val;
            }
        } else if (strcmp(type, OST_UNTF) == 0) {
            char unit[5];
            double val = 0.0;
            ok = read_tag(fp, unit) && read_be_double(fp, &val);
            if (ok && ch_count < 4) {
                g_strlcpy(ch_key[ch_count], key, 5);
                ch[ch_count++] = val;
            }
        } else {
            ok = skip_descriptor_value(fp, type);
        }
        g_free(key);
    }
    if (!ok) { g_free(classid); return FALSE; }

    /* Convert based on classID */
    if (strcmp(classid, "RGBC") == 0) {
        /* Rd  , Grn , Bl   all in [0,255] */
        *r = ch[0] / 255.0;
        *g = ch[1] / 255.0;
        *b = ch[2] / 255.0;
    } else if (strcmp(classid, "HSBC") == 0) {
        /* H   = degrees [0,360], Strt/Brgh [0,100] */
        hsv_to_rgb_pct(ch[0], ch[1], ch[2], r, g, b);
    } else if (strcmp(classid, "CMYC") == 0) {
        /* Cyn /Mgnt/Ylw /Blck in [0,100%] */
        cmyk_to_rgb(ch[0], ch[1], ch[2], ch[3], r, g, b);
    } else if (strcmp(classid, "LbCl") == 0) {
        /* Lmnc, A   , B    */
        lab_to_rgb(ch[0], ch[1], ch[2], r, g, b);
    } else if (strcmp(classid, "Grsc") == 0) {
        /* Gry  [0,100%] */
        *r = *g = *b = ch[0] / 100.0;
    } else {
        /* Unknown color space — default black */
        debug_log("WRN", "grd_v5: unknown color classID '%s'", classid);
        *r = *g = *b = 0.0;
    }
    g_free(classid);

    /* Clamp */
    if (*r < 0.0) *r = 0.0; if (*r > 1.0) *r = 1.0;
    if (*g < 0.0) *g = 0.0; if (*g > 1.0) *g = 1.0;
    if (*b < 0.0) *b = 0.0; if (*b > 1.0) *b = 1.0;
    return TRUE;
}

/* Full parse of a v5 gradient descriptor: reads "Clrs" and "Trns" lists. */
static gboolean parse_v5_gradient(FILE* fp, GradientDef* def, GradientGrdError* err) {
    /* Positioned just after the outer "Objc" type tag was consumed by the list
     * iterator.  Read the descriptor header. */
    uint32_t name_len = 0;
    if (!read_be_u32(fp, &name_len)) { *err = GRADIENT_GRD_ERROR_CORRUPT_FILE; return FALSE; }
    if (!skip_bytes(fp, name_len * 2)) { *err = GRADIENT_GRD_ERROR_CORRUPT_FILE; return FALSE; }

    char* classid = read_var_string(fp); /* "Grdn" */
    g_free(classid);
    if (!classid) { *err = GRADIENT_GRD_ERROR_CORRUPT_FILE; return FALSE; }

    uint32_t top_count = 0;
    if (!read_be_u32(fp, &top_count)) { *err = GRADIENT_GRD_ERROR_CORRUPT_FILE; return FALSE; }

    /* Some GRD v5 files wrap the actual gradient keys inside an extra
     * "Grad" → Objc layer (one key at the top level whose value contains the
     * real Nm /GrdF/Clrs/Trns keys).  Detect and transparently unwrap it so
     * the main loop below sees the inner keys directly. */
    if (top_count == 1) {
        long saved_pos = ftell(fp);
        char* peek_key  = read_var_string(fp);
        char  peek_type[5] = {0};
        gboolean peeked = (peek_key != NULL) && read_tag(fp, peek_type);
        if (peeked && strcmp(peek_key, "Grad") == 0
                   && strcmp(peek_type, OST_OBJC) == 0) {
            /* Step into the inner Objc: skip name + classID, read inner count */
            uint32_t inl = 0;
            gboolean unwrapped = read_be_u32(fp, &inl)
                              && skip_bytes(fp, (size_t)inl * 2);
            if (unwrapped) {
                char* icid = read_var_string(fp);
                unwrapped  = (icid != NULL);
                g_free(icid);
            }
            if (unwrapped) unwrapped = read_be_u32(fp, &top_count);
            if (!unwrapped) {
                g_free(peek_key);
                *err = GRADIENT_GRD_ERROR_CORRUPT_FILE;
                return FALSE;
            }
            /* fp is now positioned at the first inner key; top_count updated */
        } else {
            fseek(fp, saved_pos, SEEK_SET); /* not a wrapper — restore */
        }
        g_free(peek_key);
    }

    /* Dynamic arrays for color/transparency stops */
    int n_cstops = 0, cap_cstops = 0;
    int n_tstops = 0, cap_tstops = 0;

    typedef struct { double r,g,b; int color_type; double pos; double midpoint; } CStop;
    typedef struct { double pos; double midpoint; double opacity; } TStop;

    CStop* cstops = NULL;
    TStop* tstops = NULL;

    gboolean ok = TRUE;
    gboolean is_noise = FALSE;
    int smoothness = 0;
    char* grad_name = NULL;

    for (uint32_t ti = 0; ti < top_count && ok; ti++) {
        char* key = read_var_string(fp);
        char type[5];
        ok = key && read_tag(fp, type);
        if (!ok) { g_free(key); break; }

        if (strcmp(key, "Nm  ") == 0 && strcmp(type, OST_TEXT) == 0) {
            grad_name = read_utf16_string(fp);
            ok = grad_name != NULL;
        } else if (strcmp(key, "GrdF") == 0 && strcmp(type, OST_ENUM) == 0) {
            char* tid = read_var_string(fp);
            char* val = read_var_string(fp);
            if (val && strcmp(val, "ClNs") == 0) is_noise = TRUE;
            g_free(tid); g_free(val);
            ok = tid && val;
        } else if (strcmp(key, "Smth") == 0 && strcmp(type, OST_LONG) == 0) {
            int32_t sm = 0;
            ok = read_be_i32(fp, &sm);
            smoothness = (int)sm;
        } else if (strcmp(key, "Clrs") == 0 && strcmp(type, OST_VLLS) == 0) {
            /* List of color stop descriptors */
            uint32_t count = 0;
            ok = read_be_u32(fp, &count);
            for (uint32_t ci = 0; ci < count && ok; ci++) {
                char item_type[5];
                ok = read_tag(fp, item_type);
                if (!ok) break;

                if (strcmp(item_type, OST_OBJC) != 0) {
                    ok = skip_descriptor_value(fp, item_type);
                    continue;
                }

                /* Parse the color stop descriptor header ourselves */
                uint32_t inl = 0;
                ok = read_be_u32(fp, &inl);
                if (!ok) break;
                ok = skip_bytes(fp, inl * 2); /* name */
                if (!ok) break;
                char* cid = read_var_string(fp);
                g_free(cid);
                uint32_t ic = 0;
                ok = read_be_u32(fp, &ic);
                if (!ok) break;

                CStop cs = { 0.0, 0.0, 0.0, 0, 0.0, 50.0 };
                for (uint32_t ii = 0; ii < ic && ok; ii++) {
                    char* ik = read_var_string(fp);
                    char it[5];
                    ok = ik && read_tag(fp, it);
                    if (!ok) { g_free(ik); break; }

                    if (strcmp(ik, "Type") == 0 && strcmp(it, OST_ENUM) == 0) {
                        char* etid = read_var_string(fp);
                        char* eval = read_var_string(fp);
                        if (eval) {
                            if (strcmp(eval, "FrgC") == 0)      cs.color_type = 1;
                            else if (strcmp(eval, "BkgC") == 0) cs.color_type = 2;
                        }
                        ok = etid && eval;
                        g_free(etid); g_free(eval);
                    } else if (strcmp(ik, "Clr ") == 0 && strcmp(it, OST_OBJC) == 0) {
                        ok = parse_color_objc(fp, &cs.r, &cs.g, &cs.b);
                    } else if (strcmp(ik, "Lctn") == 0 && strcmp(it, OST_LONG) == 0) {
                        int32_t loc = 0;
                        ok = read_be_i32(fp, &loc);
                        cs.pos = (double)loc;
                    } else if (strcmp(ik, "Mdpn") == 0 && strcmp(it, OST_LONG) == 0) {
                        int32_t mdp = 0;
                        ok = read_be_i32(fp, &mdp);
                        cs.midpoint = (double)mdp;
                    } else {
                        ok = skip_descriptor_value(fp, it);
                    }
                    g_free(ik);
                }

                if (!ok) break;

                /* Append to cstops array */
                if (n_cstops >= cap_cstops) {
                    cap_cstops = cap_cstops ? cap_cstops * 2 : 8;
                    CStop* tmp = realloc(cstops, (size_t)cap_cstops * sizeof(CStop));
                    if (!tmp) { ok = FALSE; break; }
                    cstops = tmp;
                }
                cstops[n_cstops++] = cs;
            }
        } else if (strcmp(key, "Trns") == 0 && strcmp(type, OST_VLLS) == 0) {
            /* List of transparency stop descriptors */
            uint32_t count = 0;
            ok = read_be_u32(fp, &count);
            for (uint32_t ti2 = 0; ti2 < count && ok; ti2++) {
                char item_type[5];
                ok = read_tag(fp, item_type);
                if (!ok) break;

                if (strcmp(item_type, OST_OBJC) != 0) {
                    ok = skip_descriptor_value(fp, item_type);
                    continue;
                }

                uint32_t inl = 0;
                ok = read_be_u32(fp, &inl);
                if (!ok) break;
                ok = skip_bytes(fp, inl * 2);
                if (!ok) break;
                char* cid = read_var_string(fp);
                g_free(cid);
                uint32_t ic = 0;
                ok = read_be_u32(fp, &ic);
                if (!ok) break;

                TStop ts = { 0.0, 50.0, 100.0 };
                for (uint32_t ii = 0; ii < ic && ok; ii++) {
                    char* ik = read_var_string(fp);
                    char it[5];
                    ok = ik && read_tag(fp, it);
                    if (!ok) { g_free(ik); break; }

                    if (strcmp(ik, "Opct") == 0 && strcmp(it, OST_UNTF) == 0) {
                        char unit[5];
                        double val = 0.0;
                        ok = read_tag(fp, unit) && read_be_double(fp, &val);
                        ts.opacity = val; /* percentage [0,100] */
                    } else if (strcmp(ik, "Lctn") == 0 && strcmp(it, OST_LONG) == 0) {
                        int32_t loc = 0;
                        ok = read_be_i32(fp, &loc);
                        ts.pos = (double)loc;
                    } else if (strcmp(ik, "Mdpn") == 0 && strcmp(it, OST_LONG) == 0) {
                        int32_t mdp = 0;
                        ok = read_be_i32(fp, &mdp);
                        ts.midpoint = (double)mdp;
                    } else {
                        ok = skip_descriptor_value(fp, it);
                    }
                    g_free(ik);
                }

                if (!ok) break;

                if (n_tstops >= cap_tstops) {
                    cap_tstops = cap_tstops ? cap_tstops * 2 : 8;
                    TStop* tmp = realloc(tstops, (size_t)cap_tstops * sizeof(TStop));
                    if (!tmp) { ok = FALSE; break; }
                    tstops = tmp;
                }
                tstops[n_tstops++] = ts;
            }
        } else {
            ok = skip_descriptor_value(fp, type);
        }
        g_free(key);
    }

    if (!ok) {
        g_free(grad_name); free(cstops); free(tstops);
        *err = GRADIENT_GRD_ERROR_CORRUPT_FILE;
        return FALSE;
    }

    if (is_noise) {
        debug_log("WRN", "grd_v5: noise gradient '%s' skipped (not supported)",
                  grad_name ? grad_name : "(unnamed)");
        g_free(grad_name); free(cstops); free(tstops);
        /* Signal to caller that gradient should be skipped, not a hard error */
        return FALSE;
    }

    def->name       = grad_name ? grad_name : g_strdup("Unnamed Gradient");
    def->smoothness = smoothness;

    /* Build segments from color stops */
    int n_segs = (n_cstops > 1) ? n_cstops - 1 : (n_cstops == 1 ? 1 : 0);
    if (n_segs > 0) {
        def->segments = calloc((size_t)n_segs, sizeof(GradientSegment));
        if (!def->segments) {
            free(cstops); free(tstops);
            *err = GRADIENT_GRD_ERROR_OUT_OF_MEMORY;
            return FALSE;
        }
        def->num_segments = n_segs;
    }

    for (int i = 0; i < n_segs; i++) {
        GradientSegment* s = &def->segments[i];
        CStop* lcs = &cstops[i];
        CStop* rcs = &cstops[i + 1];

        s->left_pos  = lcs->pos / 4096.0;
        s->right_pos = rcs->pos / 4096.0;
        /* Midpoint of left stop, as fraction of segment, then absolute position */
        double mid_frac = lcs->midpoint / 100.0;
        s->midpoint = s->left_pos + mid_frac * (s->right_pos - s->left_pos);

        s->left_r  = lcs->r; s->left_g  = lcs->g; s->left_b  = lcs->b;
        s->right_r = rcs->r; s->right_g = rcs->g; s->right_b = rcs->b;
        s->left_a = s->right_a = 1.0; /* alpha from transparency stops */

        s->blend_mode  = GRADIENT_BLEND_LINEAR;
        s->color_space = GRADIENT_COLOR_RGB;

        s->left_type  = (lcs->color_type == 1) ? GRADIENT_ENDPOINT_FOREGROUND
                       :(lcs->color_type == 2) ? GRADIENT_ENDPOINT_BACKGROUND
                       : GRADIENT_ENDPOINT_FIXED;
        s->right_type = (rcs->color_type == 1) ? GRADIENT_ENDPOINT_FOREGROUND
                       :(rcs->color_type == 2) ? GRADIENT_ENDPOINT_BACKGROUND
                       : GRADIENT_ENDPOINT_FIXED;
    }

    /* Build transparency stops */
    if (n_tstops > 0) {
        def->transparency_stops = calloc((size_t)n_tstops, sizeof(GradientTransparencyStop));
        if (!def->transparency_stops) {
            free(cstops); free(tstops);
            *err = GRADIENT_GRD_ERROR_OUT_OF_MEMORY;
            return FALSE;
        }
        def->num_transparency_stops = n_tstops;
        for (int i = 0; i < n_tstops; i++) {
            double pos = tstops[i].pos / 4096.0;
            double mid_frac = tstops[i].midpoint / 100.0;
            def->transparency_stops[i].position = pos;
            def->transparency_stops[i].opacity  = tstops[i].opacity / 100.0;
            if (i + 1 < n_tstops) {
                double next_pos = tstops[i + 1].pos / 4096.0;
                def->transparency_stops[i].midpoint = pos + mid_frac * (next_pos - pos);
            } else {
                def->transparency_stops[i].midpoint = pos + mid_frac * (1.0 - pos);
            }
        }
    }

    free(cstops);
    free(tstops);
    return TRUE;
}

/* =========================================================================
 * PUBLIC LOADER
 * ====================================================================== */

GradientSet* gradient_grd_load(const char* filename, GradientGrdError* error_out) {
    FILE* fp = NULL;
    GradientSet* set = NULL;

#define SET_ERR(e) do { if (error_out) *error_out = (e); } while (0)
#define FAIL(e)    do { SET_ERR(e); goto cleanup; } while (0)

    SET_ERR(GRADIENT_GRD_ERROR_NONE);

    if (!filename) FAIL(GRADIENT_GRD_ERROR_INVALID_PARAMETERS);

    fp = g_fopen(filename, "rb");
    if (!fp) {
        debug_log("WRN", "gradient_grd_load: cannot open '%s'", filename);
        FAIL(GRADIENT_GRD_ERROR_FILE_NOT_FOUND);
    }

    /* Signature "8BGR" */
    char sig[5];
    if (!read_tag(fp, sig) || strcmp(sig, "8BGR") != 0) {
        debug_log("WRN", "gradient_grd_load: bad signature in '%s'", filename);
        FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);
    }

    int16_t version = 0;
    if (!read_be_i16(fp, &version)) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);

    if (version == 3) {
        /* Flat binary layout */
        int16_t n_gradients = 0;
        if (!read_be_i16(fp, &n_gradients) || n_gradients <= 0)
            FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);

        set = gradient_set_new((int)n_gradients);
        if (!set) FAIL(GRADIENT_GRD_ERROR_OUT_OF_MEMORY);

        for (int16_t i = 0; i < n_gradients; i++) {
            GradientGrdError sub_err = GRADIENT_GRD_ERROR_NONE;
            if (!load_v3_gradient(fp, &set->gradients[i], &sub_err)) {
                SET_ERR(sub_err);
                goto cleanup;
            }
        }
    } else if (version == 5) {
        /* Skip the 4-byte "00 00 00 10" PS 6.0 marker */
        uint32_t marker = 0;
        if (!read_be_u32(fp, &marker)) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);

        /* The file is now positioned at the top-level descriptor.
         * Layout: uint32 name_len, name_utf16, classID, uint32 item_count, items. */
        uint32_t root_name_len = 0;
        if (!read_be_u32(fp, &root_name_len)) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);
        if (!skip_bytes(fp, root_name_len * 2)) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);

        char* root_cid = read_var_string(fp);
        g_free(root_cid);
        if (!root_cid) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);

        uint32_t root_count = 0;
        if (!read_be_u32(fp, &root_count)) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);

        /* Collect all gradients from "GrdL" */
        GradientDef* temp_grads   = NULL;
        int          n_temp       = 0;
        int          cap_temp     = 0;
        gboolean     found_list   = FALSE;
        gboolean     ok           = TRUE;

        for (uint32_t ri = 0; ri < root_count && ok; ri++) {
            char* rkey = read_var_string(fp);
            char rtype[5];
            ok = rkey && read_tag(fp, rtype);
            if (!ok) { g_free(rkey); break; }

            if (strcmp(rkey, "GrdL") == 0 && strcmp(rtype, OST_VLLS) == 0) {
                found_list = TRUE;
                uint32_t list_count = 0;
                ok = read_be_u32(fp, &list_count);

                for (uint32_t li = 0; li < list_count && ok; li++) {
                    char item_type[5];
                    ok = read_tag(fp, item_type);
                    if (!ok) break;

                    if (strcmp(item_type, OST_OBJC) != 0) {
                        ok = skip_descriptor_value(fp, item_type);
                        continue;
                    }

                    /* Grow temp array */
                    if (n_temp >= cap_temp) {
                        cap_temp = cap_temp ? cap_temp * 2 : 8;
                        GradientDef* tmp = realloc(temp_grads,
                                                   (size_t)cap_temp * sizeof(GradientDef));
                        if (!tmp) { ok = FALSE; break; }
                        /* Zero out new slots */
                        memset(tmp + n_temp, 0,
                               (size_t)(cap_temp - n_temp) * sizeof(GradientDef));
                        temp_grads = tmp;
                    }

                    GradientDef* def = &temp_grads[n_temp];
                    memset(def, 0, sizeof(GradientDef));

                    GradientGrdError sub_err = GRADIENT_GRD_ERROR_NONE;
                    if (parse_v5_gradient(fp, def, &sub_err)) {
                        n_temp++;
                    } else {
                        gradient_def_free(def);
                        if (sub_err == GRADIENT_GRD_ERROR_CORRUPT_FILE) {
                            ok = FALSE;
                            SET_ERR(sub_err);
                        }
                        /* Otherwise it's a skipped noise gradient — continue */
                    }
                }
            } else {
                ok = skip_descriptor_value(fp, rtype);
            }
            g_free(rkey);
        }

        if (!ok || !found_list) {
            for (int i = 0; i < n_temp; i++) gradient_def_free(&temp_grads[i]);
            free(temp_grads);
            if (ok) FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);
            goto cleanup;
        }

        if (n_temp == 0) {
            free(temp_grads);
            debug_log("WRN", "gradient_grd_load: no usable gradients in '%s'", filename);
            FAIL(GRADIENT_GRD_ERROR_CORRUPT_FILE);
        }

        set = calloc(1, sizeof(GradientSet));
        if (!set) {
            for (int i = 0; i < n_temp; i++) gradient_def_free(&temp_grads[i]);
            free(temp_grads);
            FAIL(GRADIENT_GRD_ERROR_OUT_OF_MEMORY);
        }
        set->gradients    = temp_grads;
        set->num_gradients = n_temp;
    } else {
        debug_log("WRN", "gradient_grd_load: unsupported .grd version %d in '%s'",
                  (int)version, filename);
        FAIL(GRADIENT_GRD_ERROR_UNSUPPORTED_FORMAT);
    }

    fclose(fp);
    debug_log("DBG", "gradient_grd_load: loaded '%s' (%d gradient(s))",
              filename, set ? set->num_gradients : 0);
    return set;

cleanup:
    if (fp)  fclose(fp);
    if (set) gradient_set_free(set);
    return NULL;

#undef SET_ERR
#undef FAIL
}

/* =========================================================================
 * VERSION 5 WRITER
 *
 * Writes the GradientSet in Photoshop .grd v5 (descriptor) format.
 * Color stops are derived from segment endpoints; alpha channel from
 * transparency_stops when present, otherwise written as 100% opaque.
 * ====================================================================== */

/* Write an Objc descriptor header (name + classID + item count).
 * name_utf8 may be NULL (writes empty string).
 * classid must be exactly 4 chars.
 * Caller writes item_count items immediately after. */
static gboolean write_objc_header(FILE* fp, const char* name_utf8,
                                   const char* classid, uint32_t item_count) {
    return write_utf16_string(fp, name_utf8 ? name_utf8 : "")
        && write_key(fp, classid)
        && write_be_u32(fp, item_count);
}

/* Write a "long" (int32) key/value pair */
static gboolean write_kv_long(FILE* fp, const char* key, int32_t val) {
    return write_key(fp, key)
        && write_tag(fp, OST_LONG)
        && write_be_i32(fp, val);
}

/* Write a "doub" (double) key/value pair */
static gboolean write_kv_doub(FILE* fp, const char* key, double val) {
    return write_key(fp, key)
        && write_tag(fp, OST_DOUB)
        && write_be_double(fp, val);
}

/* Write a "UntF" (unit float) key/value pair */
static gboolean write_kv_untf(FILE* fp, const char* key,
                                const char* unit, double val) {
    return write_key(fp, key)
        && write_tag(fp, OST_UNTF)
        && write_tag(fp, unit)
        && write_be_double(fp, val);
}

/* Write an enumerated key/value pair */
static gboolean write_kv_enum(FILE* fp, const char* key,
                               const char* type_id, const char* value) {
    return write_key(fp, key)
        && write_tag(fp, OST_ENUM)
        && write_key(fp, type_id)
        && write_key(fp, value);
}

/* Write a TEXT (Unicode string) key/value pair */
static gboolean write_kv_text(FILE* fp, const char* key, const char* utf8_val) {
    if (!write_key(fp, key)) return FALSE;
    if (!write_tag(fp, OST_TEXT)) return FALSE;
    return write_utf16_string(fp, utf8_val);
}

/* Write an RGB color stop descriptor.
 * r,g,b in [0,1]; location [0,4096]; midpoint [0,100]. */
static gboolean write_color_stop(FILE* fp, double r, double g, double b,
                                  int color_type, int32_t location, int32_t midpoint) {
    /* Descriptor: 4 items (Type, Clr , Lctn, Mdpn) */
    if (!write_tag(fp, OST_OBJC)) return FALSE;
    if (!write_objc_header(fp, "", "Clrt", 4)) return FALSE;

    /* Type enum */
    const char* type_val = (color_type == 1) ? "FrgC"
                          :(color_type == 2) ? "BkgC" : "UsrS";
    if (!write_kv_enum(fp, "Type", "Clry", type_val)) return FALSE;

    /* Color as RGBC */
    if (!write_key(fp, "Clr ")) return FALSE;
    if (!write_tag(fp, OST_OBJC)) return FALSE;
    if (!write_objc_header(fp, "", "RGBC", 3)) return FALSE;
    if (!write_kv_doub(fp, "Rd  ", r * 255.0)) return FALSE;
    if (!write_kv_doub(fp, "Grn ", g * 255.0)) return FALSE;
    if (!write_kv_doub(fp, "Bl  ", b * 255.0)) return FALSE;

    if (!write_kv_long(fp, "Lctn", location))  return FALSE;
    if (!write_kv_long(fp, "Mdpn", midpoint))  return FALSE;
    return TRUE;
}

/* Write a transparency stop descriptor.
 * opacity [0,100%]; location [0,4096]; midpoint [0,100]. */
static gboolean write_trans_stop(FILE* fp, double opacity_pct,
                                  int32_t location, int32_t midpoint) {
    if (!write_tag(fp, OST_OBJC)) return FALSE;
    if (!write_objc_header(fp, "", "TrnS", 3)) return FALSE;
    if (!write_kv_untf(fp, "Opct", "#Prc", opacity_pct)) return FALSE;
    if (!write_kv_long(fp, "Lctn", location))  return FALSE;
    if (!write_kv_long(fp, "Mdpn", midpoint))  return FALSE;
    return TRUE;
}

gboolean gradient_grd_save(const GradientSet* set, const char* filename,
                             GradientGrdError* error_out) {
    FILE* fp = NULL;

#define SET_ERR(e) do { if (error_out) *error_out = (e); } while (0)
#define FAIL(e)    do { SET_ERR(e); goto cleanup; } while (0)

    SET_ERR(GRADIENT_GRD_ERROR_NONE);

    if (!set || !filename) FAIL(GRADIENT_GRD_ERROR_INVALID_PARAMETERS);

    fp = g_fopen(filename, "wb");
    if (!fp) {
        debug_log("WRN", "gradient_grd_save: cannot open '%s' for writing", filename);
        FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
    }

    /* File header: "8BGR" + version 5 + PS6 marker */
    if (fwrite("8BGR", 1, 4, fp) != 4) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
    if (!write_be_u16(fp, 5)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
    if (!write_be_u32(fp, 0x00000010)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

    /* Root descriptor: empty name, null classID (4 zero bytes), 1 item */
    if (!write_be_u32(fp, 0)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR); /* name len = 0 */
    /* classID: zero-length prefix means use next 4 bytes; write null classID */
    if (!write_be_u32(fp, 0)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
    if (!write_be_u32(fp, 0)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR); /* null classID */
    if (!write_be_u32(fp, 1)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR); /* 1 top-level item */

    /* Key "GrdL", type VlLs */
    if (!write_key(fp, "GrdL")) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
    if (!write_tag(fp, OST_VLLS)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
    if (!write_be_u32(fp, (uint32_t)set->num_gradients))
        FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

    /* Write each gradient */
    for (int gi = 0; gi < set->num_gradients; gi++) {
        const GradientDef* def = &set->gradients[gi];

        /* Determine color stop count: one stop per segment endpoint, collapsed */
        int n_cstops = (def->num_segments > 0) ? def->num_segments + 1 : 0;
        int n_tstops = def->num_transparency_stops;
        if (n_tstops == 0) n_tstops = 2; /* always write at least two (0%=opaque, 100%=opaque) */

        /* Gradient descriptor: items: Nm  , GrdF, Smth, Clrs, Trns */
        if (!write_tag(fp, OST_OBJC)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
        if (!write_objc_header(fp, "", "Grdn", 5)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

        /* Name */
        if (!write_kv_text(fp, "Nm  ", def->name ? def->name : "Gradient"))
            FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

        /* GrdF = CstS (solid gradient) */
        if (!write_kv_enum(fp, "GrdF", "GrdF", "CstS")) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

        /* Smoothness */
        if (!write_kv_long(fp, "Smth", (int32_t)def->smoothness)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

        /* Color stop list */
        if (!write_key(fp, "Clrs")) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
        if (!write_tag(fp, OST_VLLS)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
        if (!write_be_u32(fp, (uint32_t)n_cstops)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

        for (int si = 0; si <= def->num_segments && si < n_cstops; si++) {
            double r, g, b;
            int32_t location;
            int32_t midpoint_pct = 50;
            int color_type = 0;

            if (def->num_segments == 0) {
                r = g = b = 0.0; location = 0;
            } else if (si < def->num_segments) {
                const GradientSegment* s = &def->segments[si];
                r = s->left_r; g = s->left_g; b = s->left_b;
                location = (int32_t)(s->left_pos * 4096.0 + 0.5);
                /* Midpoint as percentage between this stop and next */
                double span = s->right_pos - s->left_pos;
                double mid_abs = s->midpoint - s->left_pos;
                midpoint_pct = (span > 0.0) ? (int32_t)(mid_abs / span * 100.0 + 0.5) : 50;
                color_type = (s->left_type == GRADIENT_ENDPOINT_FOREGROUND) ? 1
                            :(s->left_type == GRADIENT_ENDPOINT_BACKGROUND)  ? 2 : 0;
            } else {
                /* Last stop = right endpoint of last segment */
                const GradientSegment* s = &def->segments[def->num_segments - 1];
                r = s->right_r; g = s->right_g; b = s->right_b;
                location = (int32_t)(s->right_pos * 4096.0 + 0.5);
                midpoint_pct = 50;
                color_type = (s->right_type == GRADIENT_ENDPOINT_FOREGROUND) ? 1
                            :(s->right_type == GRADIENT_ENDPOINT_BACKGROUND)  ? 2 : 0;
            }

            if (!write_color_stop(fp, r, g, b, color_type, location, midpoint_pct))
                FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
        }

        /* Transparency stop list */
        if (!write_key(fp, "Trns")) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
        if (!write_tag(fp, OST_VLLS)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);

        if (def->num_transparency_stops > 0) {
            if (!write_be_u32(fp, (uint32_t)def->num_transparency_stops))
                FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
            for (int ti = 0; ti < def->num_transparency_stops; ti++) {
                const GradientTransparencyStop* ts = &def->transparency_stops[ti];
                int32_t loc = (int32_t)(ts->position * 4096.0 + 0.5);
                /* Midpoint as pct between this and next stop */
                int32_t midp;
                if (ti + 1 < def->num_transparency_stops) {
                    double span = def->transparency_stops[ti + 1].position - ts->position;
                    double mid_abs = ts->midpoint - ts->position;
                    midp = (span > 0.0) ? (int32_t)(mid_abs / span * 100.0 + 0.5) : 50;
                } else {
                    midp = 50;
                }
                if (!write_trans_stop(fp, ts->opacity * 100.0, loc, midp))
                    FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
            }
        } else {
            /* Write two fully-opaque stops at 0 and 4096 */
            if (!write_be_u32(fp, 2)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
            if (!write_trans_stop(fp, 100.0, 0,    50)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
            if (!write_trans_stop(fp, 100.0, 4096, 50)) FAIL(GRADIENT_GRD_ERROR_FILE_WRITE_ERROR);
        }
    }

    fclose(fp);
    debug_log("DBG", "gradient_grd_save: saved '%s' (%d gradient(s))",
              filename, set->num_gradients);
    return TRUE;

cleanup:
    if (fp) fclose(fp);
    return FALSE;

#undef SET_ERR
#undef FAIL
}
