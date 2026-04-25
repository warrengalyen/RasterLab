/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "filters/filter_lut3d.h"
#include "debug_logger.h"
#include "filters.h"
#include "io/lut3d.h"
#include "io/lut3d_io.h"
#include "render/blend.h"
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void unpremul_bgra(guchar b, guchar g, guchar r, guchar a, guchar* outR, guchar* outG, guchar* outB) {
    if (a == 0) {
        *outR = *outG = *outB = 0;
        return;
    }
    *outR = (guchar)((r * 255 + a / 2) / a);
    *outG = (guchar)((g * 255 + a / 2) / a);
    *outB = (guchar)((b * 255 + a / 2) / a);
    if (*outR > 255)
        *outR = 255;
    if (*outG > 255)
        *outG = 255;
    if (*outB > 255)
        *outB = 255;
}

/* Pack premultiplied straight RGBA into 0xAARRGGBB (matches blend.c expectations). */
static guint32 pack_premul_argb(guint8 r, guint8 g, guint8 b, guint8 a) {
    guint8 pr, pg, pb;
    if (a < 255) {
        pr = (guint8)((r * a + 127) / 255);
        pg = (guint8)((g * a + 127) / 255);
        pb = (guint8)((b * a + 127) / 255);
    } else {
        pr = r;
        pg = g;
        pb = b;
    }
    return ((guint32)a << 24) | ((guint32)pr << 16) | ((guint32)pg << 8) | (guint32)pb;
}

static gdouble map_to_lut_01(const ColorLut3D* L, guchar c, int ch) {
    gdouble t = (gdouble)c / 255.0;
    gdouble d0 = L->domain_min[ch];
    gdouble d1 = L->domain_max[ch];
    gdouble span = d1 - d0;
    if (span > 1e-12) {
        t = (t - d0) / span;
    } else {
        t = 0.0;
    }
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    return t;
}

static void sample_lut3d_trilinear(const ColorLut3D* L, gdouble tr, gdouble tg, gdouble tb,
                                   gfloat* oR, gfloat* oG, gfloat* oB) {
    int n, r0, r1, g0, g1, b0, b1;
    gdouble fr, fg, fb, w000, w100, w010, w110, w001, w101, w011, w111;
    const gfloat* c000;
    const gfloat* c100;
    const gfloat* c010;
    const gfloat* c110;
    const gfloat* c001;
    const gfloat* c101;
    const gfloat* c011;
    const gfloat* c111;
    gfloat cr, cg, cb;
    gdouble t;

    n = L->size;
    if (n < 2) {
        *oR = *oG = *oB = 0.0f;
        return;
    }
    t = (gdouble)tr;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    tr = t;
    t = (gdouble)tg;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    tg = t;
    t = (gdouble)tb;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    tb = t;

    fr = tr * (gdouble)(n - 1);
    fg = tg * (gdouble)(n - 1);
    fb = tb * (gdouble)(n - 1);
    r0 = (int)floor(fr);
    r1 = r0 + 1;
    if (r1 > n - 1)
        r1 = n - 1;
    if (r0 < 0)
        r0 = 0;
    g0 = (int)floor(fg);
    g1 = g0 + 1;
    if (g1 > n - 1)
        g1 = n - 1;
    if (g0 < 0)
        g0 = 0;
    b0 = (int)floor(fb);
    b1 = b0 + 1;
    if (b1 > n - 1)
        b1 = n - 1;
    if (b0 < 0)
        b0 = 0;
    fr -= (gdouble)r0;
    fg -= (gdouble)g0;
    fb -= (gdouble)b0;

    c000 = L->rgb + lut3d_cell_index(r0, g0, b0, n);
    c100 = L->rgb + lut3d_cell_index(r1, g0, b0, n);
    c010 = L->rgb + lut3d_cell_index(r0, g1, b0, n);
    c110 = L->rgb + lut3d_cell_index(r1, g1, b0, n);
    c001 = L->rgb + lut3d_cell_index(r0, g0, b1, n);
    c101 = L->rgb + lut3d_cell_index(r1, g0, b1, n);
    c011 = L->rgb + lut3d_cell_index(r0, g1, b1, n);
    c111 = L->rgb + lut3d_cell_index(r1, g1, b1, n);

    w000 = (1.0 - fr) * (1.0 - fg) * (1.0 - fb);
    w100 = (fr) * (1.0 - fg) * (1.0 - fb);
    w010 = (1.0 - fr) * (fg) * (1.0 - fb);
    w110 = (fr) * (fg) * (1.0 - fb);
    w001 = (1.0 - fr) * (1.0 - fg) * (fb);
    w101 = (fr) * (1.0 - fg) * (fb);
    w011 = (1.0 - fr) * (fg) * (fb);
    w111 = (fr) * (fg) * (fb);

    cr = (gfloat)(w000 * c000[0] + w100 * c100[0] + w010 * c010[0] + w110 * c110[0] + w001 * c001[0] + w101 * c101[0] + w011 * c011[0] + w111 * c111[0]);
    cg = (gfloat)(w000 * c000[1] + w100 * c100[1] + w010 * c010[1] + w110 * c110[1] + w001 * c001[1] + w101 * c101[1] + w011 * c011[1] + w111 * c111[1]);
    cb = (gfloat)(w000 * c000[2] + w100 * c100[2] + w010 * c010[2] + w110 * c110[2] + w001 * c001[2] + w101 * c101[2] + w011 * c011[2] + w111 * c111[2]);
    *oR = cr;
    *oG = cg;
    *oB = cb;
}

/* LUT output is in [0,1] (may exceed; clamp to display range). */
static guchar clamp_u8_f(gfloat v) {
    if (v <= 0.0f)
        return 0;
    if (v >= 1.0f)
        return 255;
    return (guchar)(v * 255.0f + 0.5f);
}

gboolean filter_apply_3d_lut(
    cairo_surface_t* surface,
    const char* lut_file_path,
    gint intensity,
    BlendMode blend_mode) {
    gint width, height, stride, x, y;
    guchar* surface_data;
    Lut3DIOError ioerr;
    ColorLut3D* lut;
    gfloat fr, fg, fb;
    guint32* src_buf;
    guint32* dst_buf;
    gdouble tmix;
    guint8 sra, sga, sba, a;
    gfloat lutR, lutG, lutB;
    guint8 uR, uG, uB;
    guint8 origR, origG, origB, outR, outG, outB;
    guint32 pdst;
    guint8 br, bg, bb, oar, oag, oab, oa;

    if (intensity <= 0) {
        return TRUE;
    }
    if (intensity > 100) {
        intensity = 100;
    }
    if (!surface || !lut_file_path) {
        return FALSE;
    }
    if (blend_mode < 0 || blend_mode >= BLEND_MODE_COUNT) {
        return FALSE;
    }
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    tmix = (gdouble)intensity / 100.0;
    if (tmix > 1.0)
        tmix = 1.0;

    ioerr = LUT3D_IO_ERROR_NONE;
    lut = lut3d_io_load(lut_file_path, &ioerr);
    if (!lut) {
        debug_log("ERR", "filter_apply_3d_lut: %s", lut3d_io_get_error_message(ioerr, lut_file_path));
        return FALSE;
    }

    src_buf = (guint32*)malloc((size_t)width * sizeof(guint32) * 2U);
    if (!src_buf) {
        lut3d_free(lut);
        return FALSE;
    }
    dst_buf = src_buf + width;

    stride = cairo_image_surface_get_stride(surface);
    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);

    for (y = 0; y < height; y++) {
        guchar* row = surface_data + y * stride;
        for (x = 0; x < width; x++) {
            guchar* px = row + x * 4U;
            a = px[3];
            unpremul_bgra(px[0], px[1], px[2], a, &uR, &uG, &uB);

            if (a == 0) {
                src_buf[x] = 0U;
            } else {
                fr = (gfloat)map_to_lut_01(lut, uR, 0);
                fg = (gfloat)map_to_lut_01(lut, uG, 1);
                fb = (gfloat)map_to_lut_01(lut, uB, 2);
                sample_lut3d_trilinear(lut, (gdouble)fr, (gdouble)fg, (gdouble)fb, &lutR, &lutG, &lutB);
                sra = clamp_u8_f(lutR);
                sga = clamp_u8_f(lutG);
                sba = clamp_u8_f(lutB);
                /* Source for blend: LUT color with same alpha as original (straight → premul in pack). */
                src_buf[x] = pack_premul_argb(sra, sga, sba, a);
            }
            /* Backdrop: copy of original (straight → premul pack). */
            dst_buf[x] = pack_premul_argb(uR, uG, uB, a);
        }
        blend_composite_row((const guint32*)src_buf, dst_buf, width, 0, y, 255, blend_mode);
        for (x = 0; x < width; x++) {
            guchar* oxp = row + x * 4U;
            a = oxp[3];
            /* Original straight (surface row unchanged until we write). */
            unpremul_bgra(oxp[0], oxp[1], oxp[2], a, &origR, &origG, &origB);
            pdst = dst_buf[x];
            oa = (guint8)((pdst >> 24) & 0xFF);
            oar = (guint8)((pdst >> 16) & 0xFF);
            oag = (guint8)((pdst >> 8) & 0xFF);
            oab = (guint8)(pdst & 0xFF);
            if (oa < 255 && oa > 0) {
                br = (guint8)((oar * 255 + oa / 2) / oa);
                bg = (guint8)((oag * 255 + oa / 2) / oa);
                bb = (guint8)((oab * 255 + oa / 2) / oa);
            } else if (oa == 0) {
                br = bg = bb = 0;
            } else {
                br = oar;
                bg = oag;
                bb = oab;
            }
            if (a == 0) {
                continue;
            }
            outR = (guint8)((1.0 - tmix) * (gdouble)origR + tmix * (gdouble)br + 0.5);
            outG = (guint8)((1.0 - tmix) * (gdouble)origG + tmix * (gdouble)bg + 0.5);
            outB = (guint8)((1.0 - tmix) * (gdouble)origB + tmix * (gdouble)bb + 0.5);
            if (a < 255) {
                outR = (guint8)((outR * a + 127) / 255);
                outG = (guint8)((outG * a + 127) / 255);
                outB = (guint8)((outB * a + 127) / 255);
            }
            oxp[0] = outB;
            oxp[1] = outG;
            oxp[2] = outR;
            oxp[3] = a;
        }
    }

    cairo_surface_mark_dirty(surface);
    free(src_buf);
    lut3d_free(lut);
    return TRUE;
}

/* ImageMagick / Quelsolaar Hald: square S×S with S = L³, LUT size n = L² per axis. */
static gint hald_level_from_side(gint S) {
    gint L, c;
    for (L = 1; L <= 64; L++) {
        c = L * L * L;
        if (c == S) {
            return L;
        }
        if (c > S) {
            return 0;
        }
    }
    return 0;
}

static void bgra_to_straight_float01(guchar b, guchar g, guchar r, guchar a, gfloat* oR, gfloat* oG, gfloat* oB) {
    guchar uR, uG, uB;
    unpremul_bgra(b, g, r, a, &uR, &uG, &uB);
    *oR = (gfloat)uR / 255.0f;
    *oG = (gfloat)uG / 255.0f;
    *oB = (gfloat)uB / 255.0f;
}

/*
 * Voxel-bin nearest-color LUT builder (single reference image).
 *
 * Algorithm
 *
 *   Phase 1 – Bucket all opaque pixels of the reference image into a coarse
 *             VOXEL_RES³ color-space grid, accumulating the average sRGB color
 *             for each occupied bin.
 *
 *   Phase 2 – Collect occupied bins into a compact list.
 *
 *   Phase 3 – For every LUT grid vertex (whose "identity" color is
 *             ir/(n-1), ig/(n-1), ib/(n-1) in [0,1]) scan the occupied-bin list
 *             and pick the bin whose centre is nearest in Euclidean RGB distance.
 *             Store that bin's average color as the LUT output.
 *
 *             Rationale: for a vertex that represents, say, vivid red, the nearest
 *             bin in a desaturated reference image will be a muted pinkish-red –
 *             so the LUT maps vivid red → muted pink, reproducing the desaturation
 *             when applied to any other image.  No "before" image is required.
 *
 * VOXEL_RES = 32 → 32³ = 32768 bins.  For a 17³ LUT (4913 vertices) the inner
 * loop is ≤ 4913 × 32768 ≈ 161 M float ops — fast enough for a save operation.
 */
#define VOXEL_RES 32
#define VOXEL_TOTAL (VOXEL_RES * VOXEL_RES * VOXEL_RES)

typedef struct {
    double sr, sg, sb;
    int cnt;
} VoxBin;
typedef struct {
    float avg_r, avg_g, avg_b, ctr_r, ctr_g, ctr_b;
} OccBin;

gboolean filter_save_3d_lut_from_image(
    cairo_surface_t* surface,
    const char* lut_file_path,
    const char* file_description,
    const char* file_copyright,
    gint grid_points) {
    gint w, h, stride, x, y, ir, ig, ib, n, idx, k, vr, vg, vb, vidx, nocc;
    double den;
    VoxBin* vox = NULL;
    OccBin* occ = NULL;
    ColorLut3D* lut = NULL;
    Lut3DIOError ioerr;
    guchar* data;
    gboolean ok;

    if (!surface || !lut_file_path)
        return FALSE;
    if (grid_points < FILTER_LUT3D_PHOTO_GRID_MIN || grid_points > FILTER_LUT3D_PHOTO_GRID_MAX)
        return FALSE;
    if (!lut3d_io_is_supported(lut_file_path)) {
        debug_log("WRN", "filter_save_3d_lut_from_image: use .cube or .look");
        return FALSE;
    }
    if (!adjustments_validate_surface(surface, &w, &h))
        return FALSE;

    n = (int)grid_points;
    den = (n > 1) ? (double)(n - 1) : 1.0;

    /* Phase 1: accumulate pixels into voxel bins */
    vox = (VoxBin*)g_malloc0((gsize)VOXEL_TOTAL * sizeof(VoxBin));
    if (!vox)
        return FALSE;

    stride = cairo_image_surface_get_stride(surface);
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);

    for (y = 0; y < h; y++) {
        guchar* row = data + (gsize)y * (gsize)stride;
        for (x = 0; x < w; x++) {
            guchar* px = row + (gsize)x * 4U;
            guchar a = px[3];
            guchar uR, uG, uB;

            if (a == 0)
                continue;
            unpremul_bgra(px[0], px[1], px[2], a, &uR, &uG, &uB);

            vr = (int)((double)uR / 255.0 * (VOXEL_RES - 1) + 0.5);
            vg = (int)((double)uG / 255.0 * (VOXEL_RES - 1) + 0.5);
            vb = (int)((double)uB / 255.0 * (VOXEL_RES - 1) + 0.5);
            /* clamp (should never be needed but be safe) */
            if (vr < 0)
                vr = 0;
            else if (vr >= VOXEL_RES)
                vr = VOXEL_RES - 1;
            if (vg < 0)
                vg = 0;
            else if (vg >= VOXEL_RES)
                vg = VOXEL_RES - 1;
            if (vb < 0)
                vb = 0;
            else if (vb >= VOXEL_RES)
                vb = VOXEL_RES - 1;

            vidx = vb * VOXEL_RES * VOXEL_RES + vg * VOXEL_RES + vr;
            vox[vidx].sr += (double)uR / 255.0;
            vox[vidx].sg += (double)uG / 255.0;
            vox[vidx].sb += (double)uB / 255.0;
            vox[vidx].cnt++;
        }
    }

    /* Phase 2: build compact occupied-bin list */
    occ = (OccBin*)g_malloc((gsize)VOXEL_TOTAL * sizeof(OccBin));
    if (!occ) {
        g_free(vox);
        return FALSE;
    }
    nocc = 0;
    for (vb = 0; vb < VOXEL_RES; vb++) {
        for (vg = 0; vg < VOXEL_RES; vg++) {
            for (vr = 0; vr < VOXEL_RES; vr++) {
                vidx = vb * VOXEL_RES * VOXEL_RES + vg * VOXEL_RES + vr;
                if (vox[vidx].cnt > 0) {
                    double c = (double)vox[vidx].cnt;
                    occ[nocc].avg_r = (float)(vox[vidx].sr / c);
                    occ[nocc].avg_g = (float)(vox[vidx].sg / c);
                    occ[nocc].avg_b = (float)(vox[vidx].sb / c);
                    occ[nocc].ctr_r = (float)(vr / (double)(VOXEL_RES - 1));
                    occ[nocc].ctr_g = (float)(vg / (double)(VOXEL_RES - 1));
                    occ[nocc].ctr_b = (float)(vb / (double)(VOXEL_RES - 1));
                    nocc++;
                }
            }
        }
    }
    g_free(vox);
    vox = NULL;

    if (nocc == 0) {
        g_free(occ);
        debug_log("WRN", "filter_save_3d_lut_from_image: no opaque pixels in reference");
        return FALSE;
    }

    /* Phase 3: for each LUT vertex (identity color), find nearest occupied bin */
    lut = lut3d_new(n);
    if (!lut) {
        g_free(occ);
        return FALSE;
    }
    g_free(lut->title);
    g_free(lut->copyright);
    lut->title = g_strdup(file_description && file_description[0]
                              ? file_description
                              : "RasterLab color lookup");
    lut->copyright = (file_copyright && file_copyright[0])
                         ? g_strdup(file_copyright)
                         : NULL;

    for (ib = 0; ib < n; ib++) {
        for (ig = 0; ig < n; ig++) {
            for (ir = 0; ir < n; ir++) {
                float Li = (float)(ir / den); /* identity R in [0,1] */
                float Gi = (float)(ig / den); /* identity G */
                float Bi = (float)(ib / den); /* identity B */

                float best = 1e30f;
                int bk = 0;
                for (k = 0; k < nocc; k++) {
                    float dr = occ[k].ctr_r - Li;
                    float dg = occ[k].ctr_g - Gi;
                    float db = occ[k].ctr_b - Bi;
                    float d = dr * dr + dg * dg + db * db;
                    if (d < best) {
                        best = d;
                        bk = k;
                    }
                }

                idx = ib * n * n + ig * n + ir;
                lut->rgb[idx * 3 + 0] = occ[bk].avg_r;
                lut->rgb[idx * 3 + 1] = occ[bk].avg_g;
                lut->rgb[idx * 3 + 2] = occ[bk].avg_b;
            }
        }
    }
    g_free(occ);

    /* Phase 4: save */
    ioerr = LUT3D_IO_ERROR_NONE;
    ok = lut3d_io_save(lut, lut_file_path, &ioerr);
    if (!ok) {
        debug_log("ERR", "filter_save_3d_lut_from_image: %s",
                  lut3d_io_get_error_message(ioerr, lut_file_path));
    }
    lut3d_free(lut);
    return ok;
}

/*
 * Two-surface LUT builder.
 *
 * For every opaque pixel pair at the same (x,y), the BASE color is quantised
 * to the nearest LUT grid vertex and the MOD color is accumulated there.
 * Vertices that receive no samples fall back to the identity map.  A flood-fill
 * diffusion then propagates sampled transforms into unsampled vertices so the
 * LUT is spatially smooth even when the image does not cover the whole color cube.
 */
gboolean filter_save_3d_lut_from_two_surfaces(
    cairo_surface_t* base_surface,
    cairo_surface_t* mod_surface,
    const char* lut_file_path,
    const char* file_description,
    const char* file_copyright,
    gint grid_points) {
    gint bw, bh, mw, mh, stride_b, stride_m, scan_w, scan_h;
    int n, total, pass, max_passes;
    int x, y, ir, ig, ib, idx;
    double den;
    double* sum_r = NULL;
    double* sum_g = NULL;
    double* sum_b = NULL;
    int* cnt = NULL;
    guint8* sampled = NULL;
    ColorLut3D* lut = NULL;
    Lut3DIOError ioerr;
    guchar* base_data;
    guchar* mod_data;
    gboolean changed;
    gboolean ok;

    if (!base_surface || !mod_surface || !lut_file_path)
        return FALSE;
    if (grid_points < FILTER_LUT3D_PHOTO_GRID_MIN || grid_points > FILTER_LUT3D_PHOTO_GRID_MAX)
        return FALSE;
    if (!lut3d_io_is_supported(lut_file_path)) {
        debug_log("WRN", "filter_save_3d_lut_from_two_surfaces: use .cube or .look");
        return FALSE;
    }
    if (!adjustments_validate_surface(base_surface, &bw, &bh))
        return FALSE;
    if (!adjustments_validate_surface(mod_surface, &mw, &mh))
        return FALSE;

    n = (int)grid_points;
    total = n * n * n;
    scan_w = (bw < mw) ? bw : mw;
    scan_h = (bh < mh) ? bh : mh;
    den = (n > 1) ? (double)(n - 1) : 1.0;

    sum_r = (double*)g_malloc0((gsize)total * sizeof(double));
    sum_g = (double*)g_malloc0((gsize)total * sizeof(double));
    sum_b = (double*)g_malloc0((gsize)total * sizeof(double));
    cnt = (int*)g_malloc0((gsize)total * sizeof(int));
    sampled = (guint8*)g_malloc0((gsize)total * sizeof(guint8));
    if (!sum_r || !sum_g || !sum_b || !cnt || !sampled) {
        goto fail;
    }

    stride_b = cairo_image_surface_get_stride(base_surface);
    stride_m = cairo_image_surface_get_stride(mod_surface);
    cairo_surface_flush(base_surface);
    cairo_surface_flush(mod_surface);
    base_data = cairo_image_surface_get_data(base_surface);
    mod_data = cairo_image_surface_get_data(mod_surface);

    /* Phase 1 – accumulate per-pixel color correspondences */
    for (y = 0; y < scan_h; y++) {
        guchar* brow = base_data + (gsize)y * (gsize)stride_b;
        guchar* mrow = mod_data + (gsize)y * (gsize)stride_m;
        for (x = 0; x < scan_w; x++) {
            guchar* bpx = brow + (gsize)x * 4U;
            guchar* mpx = mrow + (gsize)x * 4U;
            guchar ba = bpx[3];
            guchar ma = mpx[3];
            guchar bR, bG, bB, mR, mG, mB;

            if (ba == 0 || ma == 0)
                continue;

            unpremul_bgra(bpx[0], bpx[1], bpx[2], ba, &bR, &bG, &bB);
            unpremul_bgra(mpx[0], mpx[1], mpx[2], ma, &mR, &mG, &mB);

            /* Nearest grid vertex for the base (original) color */
            ir = (int)((double)bR / 255.0 * den + 0.5);
            ig = (int)((double)bG / 255.0 * den + 0.5);
            ib = (int)((double)bB / 255.0 * den + 0.5);
            if (ir < 0)
                ir = 0;
            else if (ir >= n)
                ir = n - 1;
            if (ig < 0)
                ig = 0;
            else if (ig >= n)
                ig = n - 1;
            if (ib < 0)
                ib = 0;
            else if (ib >= n)
                ib = n - 1;

            idx = ib * n * n + ig * n + ir;
            sum_r[idx] += (double)mR / 255.0;
            sum_g[idx] += (double)mG / 255.0;
            sum_b[idx] += (double)mB / 255.0;
            cnt[idx]++;
        }
    }

    /* Phase 2 – build initial LUT: sampled → average, unsampled → identity */
    lut = lut3d_new(n);
    if (!lut)
        goto fail;
    g_free(lut->title);
    g_free(lut->copyright);
    lut->title = g_strdup(file_description && file_description[0] ? file_description : "RasterLab color lookup");
    lut->copyright = (file_copyright && file_copyright[0]) ? g_strdup(file_copyright) : NULL;

    for (ib = 0; ib < n; ib++) {
        for (ig = 0; ig < n; ig++) {
            for (ir = 0; ir < n; ir++) {
                idx = ib * n * n + ig * n + ir;
                if (cnt[idx] > 0) {
                    lut->rgb[idx * 3 + 0] = (gfloat)(sum_r[idx] / (double)cnt[idx]);
                    lut->rgb[idx * 3 + 1] = (gfloat)(sum_g[idx] / (double)cnt[idx]);
                    lut->rgb[idx * 3 + 2] = (gfloat)(sum_b[idx] / (double)cnt[idx]);
                    sampled[idx] = 1;
                } else {
                    /* Identity: output = input */
                    lut->rgb[idx * 3 + 0] = (gfloat)(ir / den);
                    lut->rgb[idx * 3 + 1] = (gfloat)(ig / den);
                    lut->rgb[idx * 3 + 2] = (gfloat)(ib / den);
                    sampled[idx] = 0;
                }
            }
        }
    }

    /* Phase 3 – flood-fill diffusion: spread sampled transform into unsampled vertices.
       Each pass expands the "frontier" of known values by one step in the 6-connected
       neighbourhood.  We need at most n-1 passes to cover the entire n³ cube. */
    max_passes = n;
    for (pass = 0; pass < max_passes; pass++) {
        changed = FALSE;
        for (ib = 0; ib < n; ib++) {
            for (ig = 0; ig < n; ig++) {
                for (ir = 0; ir < n; ir++) {
                    static const int offs[6][3] = {
                        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    double nr = 0, ng = 0, nb = 0;
                    int nc = 0, d;
                    int nr2, ng2, nb2, nidx;

                    idx = ib * n * n + ig * n + ir;
                    if (sampled[idx])
                        continue;

                    for (d = 0; d < 6; d++) {
                        nr2 = ir + offs[d][0];
                        ng2 = ig + offs[d][1];
                        nb2 = ib + offs[d][2];
                        if (nr2 < 0 || nr2 >= n || ng2 < 0 || ng2 >= n || nb2 < 0 || nb2 >= n)
                            continue;
                        nidx = nb2 * n * n + ng2 * n + nr2;
                        if (!sampled[nidx])
                            continue;
                        nr += (double)lut->rgb[nidx * 3 + 0];
                        ng += (double)lut->rgb[nidx * 3 + 1];
                        nb += (double)lut->rgb[nidx * 3 + 2];
                        nc++;
                    }
                    if (nc > 0) {
                        lut->rgb[idx * 3 + 0] = (gfloat)(nr / (double)nc);
                        lut->rgb[idx * 3 + 1] = (gfloat)(ng / (double)nc);
                        lut->rgb[idx * 3 + 2] = (gfloat)(nb / (double)nc);
                        sampled[idx] = 1;
                        changed = TRUE;
                    }
                }
            }
        }
        if (!changed)
            break;
    }

    /* Phase 4 – save */
    ioerr = LUT3D_IO_ERROR_NONE;
    ok = lut3d_io_save(lut, lut_file_path, &ioerr);
    if (!ok) {
        debug_log("ERR", "filter_save_3d_lut_from_two_surfaces: %s",
                  lut3d_io_get_error_message(ioerr, lut_file_path));
    }

    lut3d_free(lut);
    g_free(sum_r);
    g_free(sum_g);
    g_free(sum_b);
    g_free(cnt);
    g_free(sampled);
    return ok;

fail:
    lut3d_free(lut);
    g_free(sum_r);
    g_free(sum_g);
    g_free(sum_b);
    g_free(cnt);
    g_free(sampled);
    return FALSE;
}

gboolean filter_save_3d_lut_from_hald_image(cairo_surface_t* surface, const char* lut_file_path) {
    gint width, height, stride, S, L, n, y, x, r, g, b, idx;
    guchar* data;
    guchar* row;
    guchar* px;
    ColorLut3D* lut;
    Lut3DIOError ioerr;
    gfloat fr, fg, fb;

    if (!surface || !lut_file_path) {
        return FALSE;
    }
    if (!lut3d_io_is_supported(lut_file_path)) {
        debug_log("WRN", "filter_save_3d_lut_from_hald_image: use .cube or .look");
        return FALSE;
    }
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }
    if (width != height) {
        debug_log("WRN", "filter_save_3d_lut_from_hald_image: image must be square (Hald CLUT)");
        return FALSE;
    }
    S = width;
    L = hald_level_from_side(S);
    if (L < 2) {
        debug_log("WRN", "filter_save_3d_lut_from_hald_image: side %d is not L³ (e.g. 8, 512, 4096)", (int)S);
        return FALSE;
    }
    n = L * L;
    if (n < LUT3D_SIZE_MIN || n > LUT3D_SIZE_MAX) {
        debug_log("WRN", "filter_save_3d_lut_from_hald_image: implied LUT_3D_SIZE %d is out of range", (int)n);
        return FALSE;
    }

    lut = lut3d_new(n);
    if (!lut) {
        return FALSE;
    }
    g_free(lut->title);
    lut->title = g_strdup("HALD image");
    lut->domain_min[0] = lut->domain_min[1] = lut->domain_min[2] = 0.0;
    lut->domain_max[0] = lut->domain_max[1] = lut->domain_max[2] = 1.0;

    stride = cairo_image_surface_get_stride(surface);
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);

    for (y = 0; y < S; y++) {
        row = data + (size_t)y * (size_t)stride;
        for (x = 0; x < S; x++) {
            px = row + (size_t)x * 4U;
            bgra_to_straight_float01(px[0], px[1], px[2], px[3], &fr, &fg, &fb);
            r = (gint)(x % n);
            g = (gint)(x / n + (y % L) * L);
            b = (gint)(y / L);
            if (r < 0 || r >= n || g < 0 || g >= n || b < 0 || b >= n) {
                debug_log("ERR", "filter_save_3d_lut_from_hald_image: bad Hald (x,y)=(%d,%d)", (int)x, (int)y);
                lut3d_free(lut);
                return FALSE;
            }
            idx = b * n * n + g * n + r;
            lut->rgb[3U * (size_t)idx + 0U] = fr;
            lut->rgb[3U * (size_t)idx + 1U] = fg;
            lut->rgb[3U * (size_t)idx + 2U] = fb;
        }
    }

    ioerr = LUT3D_IO_ERROR_NONE;
    if (!lut3d_io_save(lut, lut_file_path, &ioerr)) {
        debug_log("ERR", "filter_save_3d_lut_from_hald_image: %s", lut3d_io_get_error_message(ioerr, lut_file_path));
        lut3d_free(lut);
        return FALSE;
    }
    lut3d_free(lut);
    return TRUE;
}
