/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifdef HAVE_LENSFUN

#include "ui/filters/filter_lens_correction.h"
#include "debug_logger.h"
#include "filters.h"
#include "lensfun.h"
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <string.h>

gboolean filter_lens_correction_apply(ImageLayer* layer,
                                      const LensCorrectionParams* params,
                                      const gchar* app_dir) {
    cairo_surface_t* surface;
    gint width, height, stride;
    guchar* pixels;
    lfDatabase* db = NULL;
    const lfCamera* camera = NULL;
    const lfLens* lens = NULL;
    lfLens* manual_lens = NULL;
    lfModifier* modifier = NULL;
    gfloat crop_factor = 1.0f;
    gchar* db_path = NULL;
    gboolean success = FALSE;
    gboolean db_loaded = FALSE;

    if (!layer || !layer->surface || !params) {
        return FALSE;
    }

    surface = layer->surface;
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    cairo_surface_flush(surface);
    pixels = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);

    if (!pixels || stride <= 0) {
        return FALSE;
    }

    /* Try to load database for camera/lens lookup (optional — manual mode
       works without it) */
    db = lf_db_create();
    if (db) {
        db_path = g_build_filename(app_dir, "LensProfiles", NULL);
        if (lf_db_load_path(db, db_path) == LF_NO_ERROR) {
            db_loaded = TRUE;
        } else if (lf_db_load(db) == LF_NO_ERROR) {
            db_loaded = TRUE;
        }
        g_free(db_path);
    }

    /* Resolve camera from database */
    if (db_loaded && params->camera_index >= 0) {
        const lfCamera* const* cameras = lf_db_get_cameras(db);
        gint cam_count = 0;
        if (cameras) {
            while (cameras[cam_count])
                cam_count++;
        }

        if (cam_count > 0) {
            gint* sort_idx = g_new(gint, cam_count);
            gchar** names = g_new(gchar*, cam_count);
            for (gint i = 0; i < cam_count; i++) {
                const gchar* maker = cameras[i]->Maker ? cameras[i]->Maker : "";
                const gchar* model = cameras[i]->Model ? cameras[i]->Model : "";
                names[i] = g_strdup_printf("%s %s", maker, model);
                sort_idx[i] = i;
            }

            for (gint i = 1; i < cam_count; i++) {
                gint key = sort_idx[i];
                gint j = i - 1;
                while (j >= 0 && g_ascii_strcasecmp(names[sort_idx[j]], names[key]) > 0) {
                    sort_idx[j + 1] = sort_idx[j];
                    j--;
                }
                sort_idx[j + 1] = key;
            }

            if (params->camera_index < cam_count) {
                camera = cameras[sort_idx[params->camera_index]];
                crop_factor = camera->CropFactor;
            }

            for (gint i = 0; i < cam_count; i++)
                g_free(names[i]);
            g_free(names);
            g_free(sort_idx);
        }
    }

    /* Resolve lens from database */
    if (db_loaded && params->lens_index >= 0) {
        const lfLens* const* lenses = lf_db_get_lenses(db);
        gint lens_count = 0;
        if (lenses) {
            while (lenses[lens_count])
                lens_count++;
        }
        if (params->lens_index < lens_count) {
            lens = lenses[params->lens_index];
        }
    }

    /* If no database lens selected, create a synthetic lens populated with
       generic calibration data derived from the user's slider parameters.
       Without calibration entries, lensfun's enable_* functions silently return
       0 (not registered), so no correction would occur.  Adding synthetic
       entries gives the modifier something to work with:
         Distortion: POLY3 k1 = (focal − 50) × 0.0003
           → negative at short focal (barrel correction),
             positive at long focal (pincushion correction),
             zero at 50 mm (neutral).
         Vignetting: PA model k1 = −0.4 / aperture
           → stronger correction at wide apertures, weaker at small. */
    if (!lens) {
        lfLensCalibAttributes calib_attr;
        memset(&calib_attr, 0, sizeof(calib_attr));
        calib_attr.CropFactor = 1.0f;   /* full-frame reference */
        calib_attr.AspectRatio = 1.5f;  /* 3:2 generic aspect */

        manual_lens = lf_lens_create();
        if (!manual_lens) {
            debug_log("ERR", "Lens Correction: failed to create manual lens object");
            if (db) lf_db_destroy(db);
            return FALSE;
        }

        if (params->distortion) {
            lfLensCalibDistortion dist;
            memset(&dist, 0, sizeof(dist));
            dist.Model = LF_DIST_MODEL_POLY3;
            dist.Focal = params->focal_length;
            dist.RealFocal = params->focal_length;
            dist.RealFocalMeasured = FALSE;
            /* k1 > 0 at long focal (pincushion model), k1 < 0 at short focal
               (barrel model).  lensfun inverts the model to produce the
               correction, so the output straightens the corresponding
               distortion type. */
            dist.Terms[0] = (params->focal_length - 50.0f) * 0.0003f;
            dist.CalibAttr = calib_attr;
            lf_lens_add_calib_distortion(manual_lens, &dist);
        }

        if (params->vignetting) {
            lfLensCalibVignetting vgn;
            memset(&vgn, 0, sizeof(vgn));
            vgn.Model = LF_VIGNETTING_MODEL_PA;
            vgn.Focal = params->focal_length;
            vgn.Aperture = params->aperture;
            vgn.Distance = params->focal_distance;
            /* PA model: k1 < 0 brightens edges (corrects darkening vignette).
               Use aperture to scale: wider aperture → stronger vignette → more
               correction needed. */
            vgn.Terms[0] = -0.4f / params->aperture;
            vgn.Terms[1] = 0.0f;
            vgn.Terms[2] = 0.0f;
            vgn.CalibAttr = calib_attr;
            lf_lens_add_calib_vignetting(manual_lens, &vgn);
        }

        lens = manual_lens;
    }

    /* Create modifier */
    modifier = lf_modifier_create(lens, params->focal_length, crop_factor,
                                  width, height, LF_PF_U8, FALSE);
    if (!modifier) {
        debug_log("ERR", "Lens Correction: failed to create modifier");
        if (manual_lens) lf_lens_destroy(manual_lens);
        if (db) lf_db_destroy(db);
        return FALSE;
    }

    /* Enable requested corrections and track what was actually enabled */
    if (params->distortion) {
        lf_modifier_enable_distortion_correction(modifier);
    }

    if (params->vignetting) {
        lf_modifier_enable_vignetting_correction(modifier,
                                                 params->aperture,
                                                 params->focal_distance);
    }

    if (params->tca) {
        lf_modifier_enable_tca_correction(modifier);
    }

    if (params->scale_to_fit) {
        gfloat scale = lf_modifier_get_auto_scale(modifier, FALSE);
        if (scale > 0.0f) {
            lf_modifier_enable_scaling(modifier, scale);
        }
    }

    /* Check which modifications were actually enabled by lensfun (calibration
       data may be absent for a blank/manual lens, causing enable calls to
       silently skip registration). */
    gint mod_flags = lf_modifier_get_mod_flags(modifier);
    gboolean has_color_mod = (mod_flags & LF_MODIFY_VIGNETTING) != 0;
    gboolean has_geom_mod = (mod_flags & (LF_MODIFY_DISTORTION | LF_MODIFY_TCA |
                                           LF_MODIFY_SCALE | LF_MODIFY_GEOMETRY)) != 0;

    /*
     * Apply corrections in the order recommended by lensfun:
     * 1. Color modification (vignetting) — modifies pixel values in-place
     * 2. Coordinate distortion (distortion + TCA) — requires coordinate remapping
     */

    /* Step 1: Vignetting (color modification) — operates on ARGB32 directly */
    if (has_color_mod) {
        for (gint y = 0; y < height; y++) {
            lf_modifier_apply_color_modification(modifier,
                                                 pixels + (gsize)y * stride,
                                                 0.0f, (float)y, width, 1,
                                                 LF_CR_4(BLUE, GREEN, RED, UNKNOWN),
                                                 stride);
        }
    }

    /* Step 2: Geometry correction (distortion, TCA, scaling) — coordinate remapping.
       Only perform if lensfun actually registered geometry callbacks. */
    if (has_geom_mod) {
        gsize buf_size = (gsize)height * (gsize)stride;
        guchar* src_pixels = (guchar*)g_malloc(buf_size);
        if (!src_pixels) {
            debug_log("ERR", "Lens Correction: failed to allocate source buffer");
            lf_modifier_destroy(modifier);
            if (manual_lens) lf_lens_destroy(manual_lens);
            if (db) lf_db_destroy(db);
            return FALSE;
        }
        memcpy(src_pixels, pixels, buf_size);

        if (mod_flags & LF_MODIFY_TCA) {
            gfloat* subpixel_coords = (gfloat*)g_malloc((gsize)width * 2 * 3 * sizeof(gfloat));
            if (!subpixel_coords) {
                g_free(src_pixels);
                lf_modifier_destroy(modifier);
                if (manual_lens) lf_lens_destroy(manual_lens);
                if (db) lf_db_destroy(db);
                return FALSE;
            }

            memset(pixels, 0, buf_size);

            for (gint y = 0; y < height; y++) {
                if (!lf_modifier_apply_subpixel_distortion(modifier, 0.0f, (float)y,
                                                           width, 1, subpixel_coords)) {
                    /* No modification for this row — copy source pixels through */
                    memcpy(pixels + (gsize)y * stride, src_pixels + (gsize)y * stride,
                           (gsize)width * 4);
                    continue;
                }

                for (gint x = 0; x < width; x++) {
                    gint dest_offset = y * stride + x * 4;
                    gfloat* coords = subpixel_coords + x * 2 * 3;

                    guchar r = 0, g = 0, b = 0, a = 255;

                    gint rx = (gint)(coords[0] + 0.5f);
                    gint ry = (gint)(coords[1] + 0.5f);
                    if (rx >= 0 && rx < width && ry >= 0 && ry < height) {
                        gint src_off = ry * stride + rx * 4;
                        r = src_pixels[src_off + 2];
                    }

                    gint gx = (gint)(coords[2] + 0.5f);
                    gint gy = (gint)(coords[3] + 0.5f);
                    if (gx >= 0 && gx < width && gy >= 0 && gy < height) {
                        gint src_off = gy * stride + gx * 4;
                        g = src_pixels[src_off + 1];
                    }

                    gint bx = (gint)(coords[4] + 0.5f);
                    gint by = (gint)(coords[5] + 0.5f);
                    if (bx >= 0 && bx < width && by >= 0 && by < height) {
                        gint src_off = by * stride + bx * 4;
                        b = src_pixels[src_off + 0];
                    }

                    if (gx >= 0 && gx < width && gy >= 0 && gy < height) {
                        gint src_off = gy * stride + gx * 4;
                        a = src_pixels[src_off + 3];
                    }

                    pixels[dest_offset + 0] = b;
                    pixels[dest_offset + 1] = g;
                    pixels[dest_offset + 2] = r;
                    pixels[dest_offset + 3] = a;
                }
            }

            g_free(subpixel_coords);
        } else {
            gfloat* coords = (gfloat*)g_malloc((gsize)width * 2 * sizeof(gfloat));
            if (!coords) {
                g_free(src_pixels);
                lf_modifier_destroy(modifier);
                if (manual_lens) lf_lens_destroy(manual_lens);
                if (db) lf_db_destroy(db);
                return FALSE;
            }

            memset(pixels, 0, buf_size);

            for (gint y = 0; y < height; y++) {
                if (!lf_modifier_apply_geometry_distortion(modifier, 0.0f, (float)y,
                                                           width, 1, coords)) {
                    /* No modification for this row — copy source pixels through */
                    memcpy(pixels + (gsize)y * stride, src_pixels + (gsize)y * stride,
                           (gsize)width * 4);
                    continue;
                }

                for (gint x = 0; x < width; x++) {
                    gfloat sx = coords[x * 2];
                    gfloat sy = coords[x * 2 + 1];

                    gint ix = (gint)(sx + 0.5f);
                    gint iy = (gint)(sy + 0.5f);

                    if (ix >= 0 && ix < width && iy >= 0 && iy < height) {
                        gint src_off = iy * stride + ix * 4;
                        gint dest_off = y * stride + x * 4;
                        pixels[dest_off + 0] = src_pixels[src_off + 0];
                        pixels[dest_off + 1] = src_pixels[src_off + 1];
                        pixels[dest_off + 2] = src_pixels[src_off + 2];
                        pixels[dest_off + 3] = src_pixels[src_off + 3];
                    }
                }
            }

            g_free(coords);
        }

        g_free(src_pixels);
    }

    cairo_surface_mark_dirty(surface);

    success = TRUE;

    lf_modifier_destroy(modifier);
    if (manual_lens) lf_lens_destroy(manual_lens);
    if (db) lf_db_destroy(db);

    return success;
}

#else /* !HAVE_LENSFUN */

#include "debug_logger.h"
#include "ui/filters/filter_lens_correction.h"

gboolean filter_lens_correction_apply(ImageLayer* layer,
                                      const LensCorrectionParams* params,
                                      const gchar* app_dir) {
    (void)layer;
    (void)params;
    (void)app_dir;
    debug_log("WRN", "Lens Correction not available (HAVE_LENSFUN not defined)");
    return FALSE;
}

#endif /* HAVE_LENSFUN */
