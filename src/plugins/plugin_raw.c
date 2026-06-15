/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "debug_logger.h"
#ifdef HAVE_LIBRAW

#include "app/settings.h"
#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_raw.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "tone_mapping.h"
#include "ui.h"
#include "ui/dialogs/hdr_image_dialog.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LibRaw C API */
#include "libraw/libraw.h"

/* -------------------------------------------------------------------------
 * Magic-byte signatures for the major camera RAW format families.
 * (Extension matching in format_registry pre-filters files; these checks
 * add a secondary sanity gate so we don't misidentify a stray file.)
 * ---------------------------------------------------------------------- */

/* TIFF little-endian: covers CR2, NEF, ARW, DNG, ORF, PEF, RW2, SRW, IIQ … */
#define RAW_TIFF_LE_0 0x49 /* 'I' */
#define RAW_TIFF_LE_1 0x49 /* 'I' */
#define RAW_TIFF_LE_2 0x2A /* '*' */
#define RAW_TIFF_LE_3 0x00

/* TIFF big-endian: same families, big-endian byte order */
#define RAW_TIFF_BE_0 0x4D /* 'M' */
#define RAW_TIFF_BE_1 0x4D /* 'M' */
#define RAW_TIFF_BE_2 0x00
#define RAW_TIFF_BE_3 0x2A /* '*' */

/* Fuji RAF */
static const uint8_t RAF_MAGIC[] = {'F', 'U', 'J', 'I', 'F', 'I', 'L', 'M', 'C', 'C', 'D', '-', 'R', 'A', 'W'};
#define RAF_MAGIC_LEN 15

/* Canon CRW / CIFF */
static const uint8_t CRW_MAGIC[] = {0x49, 0x49, 0x1A, 0x00, 0x00, 0x00, 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R'};
#define CRW_MAGIC_LEN 14

/* Sigma X3F */
static const uint8_t X3F_MAGIC[] = {'F', 'O', 'V', 'b'};
#define X3F_MAGIC_LEN 4

/* -------------------------------------------------------------------------
 * Pack linear float RGB into RGBE (Radiance shared-exponent) for the HDR
 * tone-mapping dialog preview.  Duplicated from plugin_exr.c (static local).
 * ---------------------------------------------------------------------- */
static void raw_float_rgb_to_rgbe(float r, float g, float b,
                                  uint8_t* out_r, uint8_t* out_g,
                                  uint8_t* out_b, uint8_t* out_e) {
    float max_c = r;
    if (g > max_c)
        max_c = g;
    if (b > max_c)
        max_c = b;

    if (max_c <= 1e-32f) {
        *out_r = *out_g = *out_b = *out_e = 0;
        return;
    }

    int exp_val;
    (void)frexp(max_c, &exp_val);
    float scale = (float)(256.0 * ldexp(1.0, -exp_val));
    int ir = (int)(r * scale + 0.5f);
    int ig = (int)(g * scale + 0.5f);
    int ib = (int)(b * scale + 0.5f);
    *out_r = (uint8_t)(ir < 0 ? 0 : (ir > 255 ? 255 : ir));
    *out_g = (uint8_t)(ig < 0 ? 0 : (ig > 255 ? 255 : ig));
    *out_b = (uint8_t)(ib < 0 ? 0 : (ib > 255 ? 255 : ib));
    *out_e = (uint8_t)(exp_val + 128);
}

/* -------------------------------------------------------------------------
 * can_load
 * ---------------------------------------------------------------------- */
static bool can_load_raw(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;

    if (!header || header_size < 4)
        return false;

    /* TIFF little-endian (covers most RAW formats) */
    if (header[0] == RAW_TIFF_LE_0 && header[1] == RAW_TIFF_LE_1 &&
        header[2] == RAW_TIFF_LE_2 && header[3] == RAW_TIFF_LE_3)
        return true;

    /* TIFF big-endian */
    if (header[0] == RAW_TIFF_BE_0 && header[1] == RAW_TIFF_BE_1 &&
        header[2] == RAW_TIFF_BE_2 && header[3] == RAW_TIFF_BE_3)
        return true;

    /* Fuji RAF */
    if (header_size >= RAF_MAGIC_LEN &&
        memcmp(header, RAF_MAGIC, RAF_MAGIC_LEN) == 0)
        return true;

    /* Canon CRW / CIFF */
    if (header_size >= CRW_MAGIC_LEN &&
        memcmp(header, CRW_MAGIC, CRW_MAGIC_LEN) == 0)
        return true;

    /* Sigma X3F */
    if (header_size >= X3F_MAGIC_LEN &&
        memcmp(header, X3F_MAGIC, X3F_MAGIC_LEN) == 0)
        return true;

    return false;
}

/* -------------------------------------------------------------------------
 * can_save — read-only plugin
 * ---------------------------------------------------------------------- */
static bool can_save_raw(const char* filename) {
    (void)filename;
    return false;
}

/* -------------------------------------------------------------------------
 * load_raw
 * ---------------------------------------------------------------------- */
static PluginError load_raw(ImageDocument* doc, const char* filename) {
    if (!doc || !filename)
        return PLUGIN_ERROR_INVALID_PARAMETERS;

    debug_log("DBG", "RAW plugin: loading '%s'", filename);

    /* --- 1. Initialise LibRaw ------------------------------------------ */
    libraw_data_t* raw = libraw_init(0);
    if (!raw) {
        debug_log("ERR", "RAW plugin: libraw_init failed");
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* --- 2. Open file ----------------------------------------------------- */
    int ret = libraw_open_file(raw, filename);
    if (ret != LIBRAW_SUCCESS) {
        debug_log("ERR", "RAW plugin: libraw_open_file failed: %s", libraw_strerror(ret));
        libraw_close(raw);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* --- 3. Processing parameters ---------------------------------------- */
    raw->params.output_bps = 16;   /* 16-bit per channel for best precision */
    raw->params.use_camera_wb = 1; /* honour embedded white balance */
    raw->params.use_auto_wb = 0;
    raw->params.output_color = 1;   /* sRGB output colour space */
    raw->params.no_auto_bright = 1; /* keep exposure as-shot */
    raw->params.gamm[0] = 1.0;      /* linear gamma (no encoding curve) */
    raw->params.gamm[1] = 1.0;
    raw->params.no_interpolation = 0;

    /* --- 4. Unpack raw sensor data ---------------------------------------- */
    ret = libraw_unpack(raw);
    if (ret != LIBRAW_SUCCESS) {
        debug_log("ERR", "RAW plugin: libraw_unpack failed: %s", libraw_strerror(ret));
        libraw_close(raw);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* --- 5. dcraw-style post-processing ----------------------------------- */
    ret = libraw_dcraw_process(raw);
    if (ret != LIBRAW_SUCCESS && LIBRAW_FATAL_ERROR(ret)) {
        debug_log("ERR", "RAW plugin: libraw_dcraw_process failed: %s", libraw_strerror(ret));
        libraw_close(raw);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    /* --- 6. Extract processed image into memory --------------------------- */
    int mem_err = 0;
    libraw_processed_image_t* img = libraw_dcraw_make_mem_image(raw, &mem_err);
    if (!img || mem_err != LIBRAW_SUCCESS) {
        debug_log("ERR", "RAW plugin: libraw_dcraw_make_mem_image failed: %s",
                  libraw_strerror(mem_err));
        libraw_close(raw);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    if (img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
        debug_log("ERR", "RAW plugin: unexpected image type or channel count");
        libraw_dcraw_clear_mem(img);
        libraw_close(raw);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    uint32_t width = img->width;
    uint32_t height = img->height;
    uint32_t bits = img->bits; /* 16 */

    debug_log("DBG", "RAW plugin: decoded %ux%u, %u-bit, %u channels",
              width, height, bits, img->colors);

    /* --- 7. Convert 16-bit RGB → normalised linear float ------------------ */
    size_t num_pixels = (size_t)width * height;
    float* linear_rgb = (float*)g_malloc(num_pixels * 3 * sizeof(float));
    if (!linear_rgb) {
        libraw_dcraw_clear_mem(img);
        libraw_close(raw);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    float scale = (bits == 16) ? 65535.0f : 255.0f;

    if (bits == 16) {
        const unsigned short* pixels = (const unsigned short*)img->data;
        for (size_t i = 0; i < num_pixels; i++) {
            linear_rgb[i * 3 + 0] = pixels[i * 3 + 0] / scale;
            linear_rgb[i * 3 + 1] = pixels[i * 3 + 1] / scale;
            linear_rgb[i * 3 + 2] = pixels[i * 3 + 2] / scale;
        }
    } else {
        /* 8-bit fallback (should not normally occur with output_bps=16) */
        const uint8_t* pixels = img->data;
        for (size_t i = 0; i < num_pixels; i++) {
            linear_rgb[i * 3 + 0] = pixels[i * 3 + 0] / scale;
            linear_rgb[i * 3 + 1] = pixels[i * 3 + 1] / scale;
            linear_rgb[i * 3 + 2] = pixels[i * 3 + 2] / scale;
        }
    }

    libraw_dcraw_clear_mem(img);
    img = NULL;
    libraw_close(raw);
    raw = NULL;

    /* --- 8. Pack into RGBE buffer for tone-mapping dialog preview --------- */
    size_t rgbe_size = num_pixels * 4;
    uint8_t* rgbe_data = (uint8_t*)g_malloc(rgbe_size);
    if (!rgbe_data) {
        g_free(linear_rgb);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < num_pixels; i++) {
        uint8_t* out = rgbe_data + i * 4;
        raw_float_rgb_to_rgbe(linear_rgb[i * 3 + 0],
                              linear_rgb[i * 3 + 1],
                              linear_rgb[i * 3 + 2],
                              &out[0], &out[1], &out[2], &out[3]);
    }

    /* --- 9. Tone-mapping parameters / dialog ------------------------------ */
    ToneMapParams tone_params;
    tone_map_params_init(&tone_params);

    AppContext* ctx = NULL;
    Settings* settings = NULL;
    const char* app_dir = NULL;
    gboolean auto_apply_enabled = FALSE;

    if (doc && doc->drawing_area) {
        ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
        if (ctx) {
            settings = ctx->settings;
            app_dir = ctx->app_dir;
            if (settings) {
                auto_apply_enabled = settings_get_tone_map_auto_apply(settings);
                tone_params.operator=(ToneMapOperator) settings_get_tone_map_operator(settings);
                tone_params.normalize = (ToneMapNormalize)settings_get_tone_map_normalize(settings);
                tone_params.gamma = (float)settings_get_tone_map_gamma(settings);
                tone_params.exposure = (float)settings_get_tone_map_exposure(settings);
                tone_params.white_point = (float)settings_get_tone_map_white_point(settings);
                tone_params.intensity = (float)settings_get_tone_map_intensity(settings);
                tone_params.adaptation = (float)settings_get_tone_map_adaptation(settings);
                tone_params.color_correction = (float)settings_get_tone_map_color_correction(settings);
            }
        }
    }

    GtkWindow* parent_window = NULL;
    if (ctx && ctx->window)
        parent_window = GTK_WINDOW(ctx->window);

    gboolean auto_apply = FALSE;
    gint dialog_response = GTK_RESPONSE_OK;

    if (!auto_apply_enabled) {
        dialog_response = hdr_image_dialog_show(parent_window, &tone_params, &auto_apply,
                                                rgbe_data, width, height, settings, app_dir);
        if (dialog_response != GTK_RESPONSE_OK) {
            g_free(rgbe_data);
            g_free(linear_rgb);
            return PLUGIN_ERROR_USER_CANCELLED;
        }
    } else {
        debug_log("DBG", "RAW plugin: auto-apply enabled, using saved tone-mapping settings");
    }

    /* If auto_apply was checked in dialog, persist the new default */
    if (auto_apply && settings)
        settings_set_tone_map_auto_apply(settings, TRUE);

    /* --- 10. Create / update document ------------------------------------- */
    doc->width = width;
    doc->height = height;
    doc->channels = 3;
    doc->bit_depth = 8;
    doc->has_alpha = false;

    for (GList* iter = doc->layers; iter; iter = iter->next)
        layer_free((ImageLayer*)iter->data);
    g_list_free(doc->layers);
    doc->layers = NULL;

    ImageLayer* base_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                                       LAYER_BACKGROUND_TRANSPARENT,
                                       LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        g_free(rgbe_data);
        g_free(linear_rgb);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_t* temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        g_free(rgbe_data);
        g_free(linear_rgb);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    guchar* surface_data = cairo_image_surface_get_data(temp_surface);
    int surface_stride = cairo_image_surface_get_stride(temp_surface);

    if (!surface_data) {
        layer_free(base_layer);
        g_free(rgbe_data);
        g_free(linear_rgb);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* --- 11. Tone-map to 8-bit Cairo ARGB32 (premultiplied) --------------- */
    for (uint32_t y = 0; y < height; y++) {
        guchar* dst_row = surface_data + (size_t)y * surface_stride;
        for (uint32_t x = 0; x < width; x++) {
            size_t idx = (size_t)y * width + x;
            float r_f = linear_rgb[idx * 3 + 0];
            float g_f = linear_rgb[idx * 3 + 1];
            float b_f = linear_rgb[idx * 3 + 2];

            uint8_t r, g, b;
            tone_map_rgb(r_f, g_f, b_f, &tone_params, &r, &g, &b);
            uint8_t a = 255;

            /* Cairo ARGB32: BGRA in memory, premultiplied */
            dst_row[x * 4 + 0] = (guchar)((b * (uint32_t)a + 127) / 255);
            dst_row[x * 4 + 1] = (guchar)((g * (uint32_t)a + 127) / 255);
            dst_row[x * 4 + 2] = (guchar)((r * (uint32_t)a + 127) / 255);
            dst_row[x * 4 + 3] = a;
        }
    }

    cairo_surface_mark_dirty(temp_surface);

    g_free(linear_rgb);
    g_free(rgbe_data);

    doc->layers = g_list_append(doc->layers, base_layer);
    document_render_composite(doc);

    debug_log("DBG", "RAW plugin: successfully loaded '%s' (%ux%u)", filename, width, height);
    return PLUGIN_ERROR_NONE;
}

/* -------------------------------------------------------------------------
 * plugin_init_raw
 * ---------------------------------------------------------------------- */
bool plugin_init_raw(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

    if (!out_plugin)
        return false;

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "Camera RAW";
    /*
     * Single consolidated file filter covering all LibRaw-supported extensions.
     * format_registry builds one GTK filter from this comma-separated list.
     */
    out_plugin->format_info.extensions =
        "cr2,cr3,crw," /* Canon */
        "nef,nrw,"     /* Nikon */
        "arw,srf,sr2," /* Sony */
        "raf,"         /* Fujifilm */
        "orf,"         /* Olympus / OM System */
        "rw2,"         /* Panasonic / Leica */
        "pef,ptx,"     /* Pentax */
        "srw,"         /* Samsung */
        "x3f,"         /* Sigma */
        "3fr,fff,"     /* Hasselblad */
        "dng,"         /* Adobe / Leica / others */
        "dcr,kdc,dcs," /* Kodak */
        "iiq,"         /* Phase One */
        "mef,"         /* Mamiya */
        "mos,"         /* Leaf */
        "mrw,"         /* Minolta */
        "rwl,"         /* Leica (older) */
        "erf,"         /* Epson */
        "raw";         /* Panasonic / generic RAW */

    out_plugin->format_info.supports_alpha = false;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.supports_hdr = true;
    out_plugin->format_info.priority = 65; /* below EXR(75) and HDR(70) */

    out_plugin->callbacks.can_load = can_load_raw;
    out_plugin->callbacks.load = load_raw;
    out_plugin->callbacks.can_save = can_save_raw;
    out_plugin->callbacks.save = NULL;

    return true;
}

#else /* !HAVE_LIBRAW */

#include "image_format_plugin.h"
#include "plugins/plugin_raw.h"
#include <stdbool.h>
#include <string.h>

bool plugin_init_raw(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;
    (void)out_plugin;
    return false;
}

#endif /* HAVE_LIBRAW */
