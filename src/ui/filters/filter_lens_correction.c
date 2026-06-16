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
#include "filters.h"
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include "debug_logger.h"
#include "lensfun.h"

gboolean filter_lens_correction_apply(ImageLayer* layer,
                                      const LensCorrectionParams* params,
                                      const gchar* app_dir) {
    cairo_surface_t* surface;
    gint width, height, stride;
    guchar* pixels;
    lfDatabase* db = NULL;
    const lfCamera* camera = NULL;
    const lfLens* lens = NULL;
    lfModifier* modifier = NULL;
    gfloat crop_factor = 1.0f;
    gchar* db_path = NULL;
    gboolean success = FALSE;

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

    /* Load database */
    db = lf_db_create();
    if (!db) {
        debug_log("ERR", "Lens Correction: failed to create lensfun database");
        return FALSE;
    }

    db_path = g_build_filename(app_dir, "LensProfiles", NULL);
    if (lf_db_load_path(db, db_path) != LF_NO_ERROR) {
        debug_log("WRN", "Lens Correction: failed to load database from %s, trying default", db_path);
        if (lf_db_load(db) != LF_NO_ERROR) {
            debug_log("ERR", "Lens Correction: failed to load any database");
            g_free(db_path);
            lf_db_destroy(db);
            return FALSE;
        }
    }
    g_free(db_path);

    /* Resolve camera */
    if (params->camera_index >= 0) {
        const lfCamera* const* cameras = lf_db_get_cameras(db);
        gint cam_count = 0;
        if (cameras) {
            while (cameras[cam_count]) cam_count++;
        }

        /* The camera_index from dialog is sorted; we need to re-sort to find the right one.
           For the actual apply, we rebuild the sorted mapping. */
        if (cam_count > 0) {
            gint* sort_idx = g_new(gint, cam_count);
            gchar** names = g_new(gchar*, cam_count);
            for (gint i = 0; i < cam_count; i++) {
                const gchar* maker = cameras[i]->Maker ? cameras[i]->Maker : "";
                const gchar* model = cameras[i]->Model ? cameras[i]->Model : "";
                names[i] = g_strdup_printf("%s %s", maker, model);
                sort_idx[i] = i;
            }

            /* Simple insertion sort for stability */
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

            for (gint i = 0; i < cam_count; i++) g_free(names[i]);
            g_free(names);
            g_free(sort_idx);
        }
    }

    /* Resolve lens */
    if (params->lens_index >= 0) {
        const lfLens* const* lenses = lf_db_get_lenses(db);
        gint lens_count = 0;
        if (lenses) {
            while (lenses[lens_count]) lens_count++;
        }
        if (params->lens_index < lens_count) {
            lens = lenses[params->lens_index];
        }
    }

    if (!lens && !camera) {
        debug_log("WRN", "Lens Correction: no camera or lens selected");
        lf_db_destroy(db);
        return FALSE;
    }

    /* If no lens selected but camera is, we can't apply lens-specific corrections.
       Only apply sensor crop scaling, which is a no-op without a modifier. */
    if (!lens) {
        debug_log("INF", "Lens Correction: no lens selected, skipping corrections");
        lf_db_destroy(db);
        return TRUE;
    }

    /* Create modifier */
    modifier = lf_modifier_create(lens, params->focal_length, crop_factor,
                                   width, height, LF_PF_U8, FALSE);
    if (!modifier) {
        debug_log("ERR", "Lens Correction: failed to create modifier");
        lf_db_destroy(db);
        return FALSE;
    }

    /* Enable requested corrections */
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

    /*
     * Apply corrections in the order recommended by lensfun:
     * 1. Color modification (vignetting) — modifies pixel values in-place
     * 2. Coordinate distortion (distortion + TCA) — requires coordinate remapping
     */

    /* Step 1: Vignetting (color modification) — operates on ARGB32 directly */
    if (params->vignetting) {
        /* Cairo ARGB32 layout on little-endian: B, G, R, A per pixel (4 bytes) */
        for (gint y = 0; y < height; y++) {
            lf_modifier_apply_color_modification(modifier,
                                                  pixels + (gsize)y * stride,
                                                  0.0f, (float)y, width, 1,
                                                  LF_CR_4(BLUE, GREEN, RED, UNKNOWN),
                                                  stride);
        }
    }

    /* Step 2: Geometry correction (distortion, TCA, scaling) — coordinate remapping */
    if (params->distortion || params->tca || params->scale_to_fit) {
        /* Allocate source copy for remapping */
        gsize buf_size = (gsize)height * (gsize)stride;
        guchar* src_pixels = (guchar*)g_malloc(buf_size);
        if (!src_pixels) {
            debug_log("ERR", "Lens Correction: failed to allocate source buffer");
            lf_modifier_destroy(modifier);
            lf_db_destroy(db);
            return FALSE;
        }
        memcpy(src_pixels, pixels, buf_size);

        if (params->tca) {
            /* SubpixelDistortion: returns float[width*2*3] (x,y for R,G,B channels) */
            gfloat* subpixel_coords = (gfloat*)g_malloc((gsize)width * 2 * 3 * sizeof(gfloat));
            if (!subpixel_coords) {
                g_free(src_pixels);
                lf_modifier_destroy(modifier);
                lf_db_destroy(db);
                return FALSE;
            }

            /* Clear destination */
            memset(pixels, 0, buf_size);

            for (gint y = 0; y < height; y++) {
                if (!lf_modifier_apply_subpixel_distortion(modifier, 0.0f, (float)y,
                                                            width, 1, subpixel_coords)) {
                    continue;
                }

                for (gint x = 0; x < width; x++) {
                    gint dest_offset = y * stride + x * 4;
                    gfloat* coords = subpixel_coords + x * 2 * 3;

                    /* Sample each channel from source at remapped coordinates */
                    guchar r = 0, g = 0, b = 0, a = 255;

                    /* Red channel */
                    gint rx = (gint)(coords[0] + 0.5f);
                    gint ry = (gint)(coords[1] + 0.5f);
                    if (rx >= 0 && rx < width && ry >= 0 && ry < height) {
                        gint src_off = ry * stride + rx * 4;
                        r = src_pixels[src_off + 2]; /* R in ARGB32 (little-endian BGRA) */
                    }

                    /* Green channel */
                    gint gx = (gint)(coords[2] + 0.5f);
                    gint gy = (gint)(coords[3] + 0.5f);
                    if (gx >= 0 && gx < width && gy >= 0 && gy < height) {
                        gint src_off = gy * stride + gx * 4;
                        g = src_pixels[src_off + 1]; /* G in ARGB32 */
                    }

                    /* Blue channel */
                    gint bx = (gint)(coords[4] + 0.5f);
                    gint by = (gint)(coords[5] + 0.5f);
                    if (bx >= 0 && bx < width && by >= 0 && by < height) {
                        gint src_off = by * stride + bx * 4;
                        b = src_pixels[src_off + 0]; /* B in ARGB32 */
                    }

                    /* Alpha: take from center (green) coordinate */
                    if (gx >= 0 && gx < width && gy >= 0 && gy < height) {
                        gint src_off = gy * stride + gx * 4;
                        a = src_pixels[src_off + 3]; /* A in ARGB32 */
                    }

                    pixels[dest_offset + 0] = b;
                    pixels[dest_offset + 1] = g;
                    pixels[dest_offset + 2] = r;
                    pixels[dest_offset + 3] = a;
                }
            }

            g_free(subpixel_coords);
        } else {
            /* GeometryDistortion only (no TCA): returns float[width*2] (x,y) */
            gfloat* coords = (gfloat*)g_malloc((gsize)width * 2 * sizeof(gfloat));
            if (!coords) {
                g_free(src_pixels);
                lf_modifier_destroy(modifier);
                lf_db_destroy(db);
                return FALSE;
            }

            /* Clear destination */
            memset(pixels, 0, buf_size);

            for (gint y = 0; y < height; y++) {
                if (!lf_modifier_apply_geometry_distortion(modifier, 0.0f, (float)y,
                                                            width, 1, coords)) {
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
    lf_db_destroy(db);

    return success;
}

#else /* !HAVE_LENSFUN */

#include "ui/filters/filter_lens_correction.h"
#include "debug_logger.h"

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
