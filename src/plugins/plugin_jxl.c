/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "plugins/plugin_jxl.h"
#include "debug_logger.h"
#include "document.h"
#include "i18n.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_LIBJXL) && defined(HAVE_LCMS2)
#include "color_manager.h"
#include "color_manager/icc_utils.h"
#endif

#ifdef HAVE_LIBJXL
#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#endif

/* JPEG XL container signature: first 12 bytes */
static const uint8_t JXL_CONTAINER_SIG[12] = {
    0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20,
    0x0D, 0x0A, 0x87, 0x0A};
/* JPEG XL bare codestream signature */
static const uint8_t JXL_CODESTREAM_SIG[2] = {0xFF, 0x0A};

/**
 * Check if file is JPEG XL format by inspecting the header bytes
 */
static bool can_load_jxl(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;

    if (!header || header_size < 2) {
        return false;
    }

    /* Check bare codestream signature */
    if (header_size >= 2 && memcmp(header, JXL_CODESTREAM_SIG, 2) == 0) {
        return true;
    }

    /* Check container (ISOBMFF) signature */
    if (header_size >= 12 && memcmp(header, JXL_CONTAINER_SIG, 12) == 0) {
        return true;
    }

    return false;
}

/**
 * Check if we can save as JPEG XL
 */
static bool can_save_jxl(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "jxl") == 0;
}

#ifdef HAVE_LIBJXL

/**
 * Load JPEG XL image using libjxl
 */
static PluginError load_jxl(ImageDocument* doc, const char* filename) {
    FILE* infile = NULL;
    uint8_t* file_data = NULL;
    size_t file_size = 0;
    size_t bytes_read = 0;
    JxlDecoder* dec = NULL;
    void* runner = NULL;
    JxlBasicInfo info;
    JxlPixelFormat pixel_format;
    JxlDecoderStatus status;
    uint8_t* pixels = NULL;
    size_t pixels_size = 0;
    ImageLayer* layer = NULL;
    PluginError result = PLUGIN_ERROR_UNKNOWN;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    infile = g_fopen(filename, "rb");
    if (!infile) {
        debug_log("WRN", "JXL plugin: Failed to open file: %s", filename);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    fseek(infile, 0, SEEK_END);
    file_size = (size_t)ftell(infile);
    fseek(infile, 0, SEEK_SET);

    if (file_size == 0 || file_size > 512 * 1024 * 1024) { /* Max 512 MB */
        debug_log("WRN", "JXL plugin: Invalid file size: %zu", file_size);
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    file_data = (uint8_t*)g_malloc(file_size);
    if (!file_data) {
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    bytes_read = fread(file_data, 1, file_size, infile);
    fclose(infile);

    if (bytes_read != file_size) {
        debug_log("WRN", "JXL plugin: Read %zu of %zu bytes", bytes_read, file_size);
        g_free(file_data);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    runner = JxlThreadParallelRunnerCreate(NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads());
    if (!runner) {
        debug_log("WRN", "JXL plugin: Failed to create thread runner");
        g_free(file_data);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    dec = JxlDecoderCreate(NULL);
    if (!dec) {
        debug_log("WRN", "JXL plugin: Failed to create decoder");
        JxlThreadParallelRunnerDestroy(runner);
        g_free(file_data);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    if (JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner) != JXL_DEC_SUCCESS) {
        debug_log("WRN", "JXL plugin: Failed to set parallel runner");
        result = PLUGIN_ERROR_UNKNOWN;
        goto cleanup;
    }

    if (JxlDecoderSubscribeEvents(dec,
                                  JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
        debug_log("WRN", "JXL plugin: Failed to subscribe to events");
        result = PLUGIN_ERROR_UNKNOWN;
        goto cleanup;
    }

    JxlDecoderSetInput(dec, file_data, file_size);
    JxlDecoderCloseInput(dec);

    /* Decode loop */
    memset(&info, 0, sizeof(info));
    memset(&pixel_format, 0, sizeof(pixel_format));
    pixel_format.num_channels = 4; /* RGBA */
    pixel_format.data_type = JXL_TYPE_UINT8;
    pixel_format.endianness = JXL_NATIVE_ENDIAN;
    pixel_format.align = 0;

    while (1) {
        status = JxlDecoderProcessInput(dec);

        if (status == JXL_DEC_ERROR) {
            debug_log("WRN", "JXL plugin: Decoder error");
            result = PLUGIN_ERROR_UNSUPPORTED_FORMAT;
            goto cleanup;
        }

        if (status == JXL_DEC_NEED_MORE_INPUT) {
            debug_log("WRN", "JXL plugin: Unexpected need-more-input");
            result = PLUGIN_ERROR_FILE_READ_ERROR;
            goto cleanup;
        }

        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
                debug_log("WRN", "JXL plugin: Failed to get basic info");
                result = PLUGIN_ERROR_UNSUPPORTED_FORMAT;
                goto cleanup;
            }

            if (info.xsize == 0 || info.ysize == 0 ||
                info.xsize > 65535 || info.ysize > 65535) {
                debug_log("WRN", "JXL plugin: Invalid image dimensions %ux%u", info.xsize, info.ysize);
                result = PLUGIN_ERROR_UNSUPPORTED_FORMAT;
                goto cleanup;
            }

            doc->width = info.xsize;
            doc->height = info.ysize;
            doc->channels = 4;
            doc->bit_depth = 8;
            doc->has_alpha = true;
            continue;
        }

        if (status == JXL_DEC_COLOR_ENCODING) {
#if defined(HAVE_LIBJXL) && defined(HAVE_LCMS2)
            /* Try to extract the embedded ICC profile from the original color metadata */
            size_t icc_size = 0;
            if (JxlDecoderGetICCProfileSize(dec, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &icc_size) == JXL_DEC_SUCCESS && icc_size > 0) {
                uint8_t* icc_buf = (uint8_t*)g_malloc(icc_size);
                if (icc_buf) {
                    if (JxlDecoderGetColorAsICCProfile(dec, JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                                                       icc_buf, icc_size) == JXL_DEC_SUCCESS) {
                        ImageFormatHostAPI* api = plugin_host_api_get();
                        gboolean use_icc = !api || !api->get_use_embedded_icc || api->get_use_embedded_icc();
                        if (use_icc) {
                            cmsHPROFILE profile = icc_profile_from_memory(icc_buf, icc_size);
                            if (profile) {
                                if (api && api->document_set_load_icc_profile) {
                                    debug_log("DBG", "JXL: embedded ICC profile found, will convert to sRGB");
                                    api->document_set_load_icc_profile(doc, profile);
                                } else {
                                    icc_destroy(profile);
                                }
                            } else {
                                debug_log("WRN", "JXL plugin: Embedded ICC profile is invalid or non-RGB; assuming sRGB");
                            }
                        }
                    }
                    g_free(icc_buf);
                }
            }
#endif
            continue;
        }

        if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (JxlDecoderImageOutBufferSize(dec, &pixel_format, &pixels_size) != JXL_DEC_SUCCESS) {
                debug_log("WRN", "JXL plugin: Failed to get output buffer size");
                result = PLUGIN_ERROR_UNKNOWN;
                goto cleanup;
            }

            pixels = (uint8_t*)g_malloc(pixels_size);
            if (!pixels) {
                result = PLUGIN_ERROR_OUT_OF_MEMORY;
                goto cleanup;
            }

            if (JxlDecoderSetImageOutBuffer(dec, &pixel_format, pixels, pixels_size) != JXL_DEC_SUCCESS) {
                debug_log("WRN", "JXL plugin: Failed to set output buffer");
                result = PLUGIN_ERROR_UNKNOWN;
                goto cleanup;
            }
            continue;
        }

        if (status == JXL_DEC_FULL_IMAGE) {
            /* Image decoded successfully, create layer */
            break;
        }

        if (status == JXL_DEC_SUCCESS) {
            break;
        }
    }

    if (!pixels) {
        debug_log("WRN", "JXL plugin: No pixel data decoded");
        result = PLUGIN_ERROR_FILE_READ_ERROR;
        goto cleanup;
    }

    /* Free existing layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                      LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!layer) {
        debug_log("WRN", "JXL plugin: Failed to create layer");
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    cairo_surface_t* surface = layer->surface;
    if (!surface) {
        debug_log("WRN", "JXL plugin: Layer surface is NULL");
        layer_free(layer);
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    cairo_surface_flush(surface);
    guchar* surface_data = cairo_image_surface_get_data(surface);
    int surface_stride = cairo_image_surface_get_stride(surface);

    if (!surface_data) {
        debug_log("WRN", "JXL plugin: Failed to get surface data");
        layer_free(layer);
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    /* Convert RGBA (JXL output) to ARGB32 pre-multiplied (Cairo format) */
    for (guint y = 0; y < doc->height; y++) {
        const uint8_t* src_row = pixels + y * doc->width * 4;
        uint32_t* dst_row = (uint32_t*)(surface_data + y * surface_stride);

        for (guint x = 0; x < doc->width; x++) {
            uint8_t r = src_row[x * 4 + 0];
            uint8_t g = src_row[x * 4 + 1];
            uint8_t b = src_row[x * 4 + 2];
            uint8_t a = src_row[x * 4 + 3];

            if (a == 255) {
                dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else if (a == 0) {
                dst_row[x] = 0;
            } else {
                r = (uint8_t)((r * a + 127) / 255);
                g = (uint8_t)((g * a + 127) / 255);
                b = (uint8_t)((b * a + 127) / 255);
                dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    cairo_surface_mark_dirty(surface);
    doc->layers = g_list_append(doc->layers, layer);
    result = PLUGIN_ERROR_NONE;

cleanup:
    g_free(pixels);
    JxlDecoderDestroy(dec);
    JxlThreadParallelRunnerDestroy(runner);
    g_free(file_data);
    return result;
}

/**
 * Save JPEG XL image using libjxl
 */
static PluginError save_jxl(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite = NULL;
    guchar* surface_data = NULL;
    int surface_stride = 0;
    int width = 0, height = 0;
    uint8_t* rgba_data = NULL;
    JxlEncoder* enc = NULL;
    void* runner = NULL;
    JxlEncoderFrameSettings* frame_settings = NULL;
    uint8_t* compressed = NULL;
    size_t compressed_alloc = 0;
    size_t compressed_size = 0;
    FILE* outfile = NULL;
    PluginError result = PLUGIN_ERROR_UNKNOWN;
    const JXLSaveOptions* jxl_opts = NULL;
    bool lossless = true;
    int quality = 90;
    int effort = 7;

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Read options */
    if (opts && opts->plugin_data) {
        jxl_opts = (const JXLSaveOptions*)opts->plugin_data;
        lossless = jxl_opts->lossless;
        quality = jxl_opts->quality;
        effort = jxl_opts->effort;
    }

    /* Clamp */
    if (quality < 0)
        quality = 0;
    if (quality > 100)
        quality = 100;
    if (effort < 1)
        effort = 1;
    if (effort > 9)
        effort = 9;

    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    cairo_surface_flush(composite);
    surface_data = cairo_image_surface_get_data(composite);
    surface_stride = cairo_image_surface_get_stride(composite);
    width = cairo_image_surface_get_width(composite);
    height = cairo_image_surface_get_height(composite);

    if (!surface_data || width <= 0 || height <= 0) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert ARGB32 pre-multiplied (Cairo) → RGBA (libjxl input) */
    rgba_data = (uint8_t*)g_malloc((size_t)width * (size_t)height * 4);
    if (!rgba_data) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    for (int y = 0; y < height; y++) {
        const uint32_t* src_row = (const uint32_t*)(surface_data + y * surface_stride);
        uint8_t* dst_row = rgba_data + y * width * 4;

        for (int x = 0; x < width; x++) {
            uint32_t pixel = src_row[x];
            uint8_t a = (uint8_t)((pixel >> 24) & 0xFF);
            uint8_t r = (uint8_t)((pixel >> 16) & 0xFF);
            uint8_t g = (uint8_t)((pixel >> 8) & 0xFF);
            uint8_t b = (uint8_t)(pixel & 0xFF);

            /* Un-pre-multiply alpha */
            if (a != 0 && a != 255) {
                r = (uint8_t)((r * 255 + a / 2) / a);
                g = (uint8_t)((g * 255 + a / 2) / a);
                b = (uint8_t)((b * 255 + a / 2) / a);
            }

            dst_row[x * 4 + 0] = r;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = b;
            dst_row[x * 4 + 3] = a;
        }
    }

    runner = JxlThreadParallelRunnerCreate(NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads());
    if (!runner) {
        debug_log("WRN", "JXL plugin: Failed to create thread runner");
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    enc = JxlEncoderCreate(NULL);
    if (!enc) {
        debug_log("WRN", "JXL plugin: Failed to create encoder");
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    if (JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner) != JXL_ENC_SUCCESS) {
        debug_log("WRN", "JXL plugin: Failed to set parallel runner");
        result = PLUGIN_ERROR_UNKNOWN;
        goto cleanup;
    }

    /* Decide up front whether to embed the original ICC profile */
#if defined(HAVE_LIBJXL) && defined(HAVE_LCMS2)
    bool embed_icc = (opts && opts->preserve_icc_profile && doc->original_icc_data && doc->original_icc_size > 0);
#else
    bool embed_icc = false;
#endif

    /* Set basic image info */
    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = (uint32_t)width;
    basic_info.ysize = (uint32_t)height;
    basic_info.bits_per_sample = 8;
    basic_info.exponent_bits_per_sample = 0;
    basic_info.alpha_bits = 8;
    basic_info.alpha_exponent_bits = 0;
    basic_info.num_color_channels = 3;
    basic_info.num_extra_channels = 1; /* alpha */
    /* uses_original_profile must be JXL_TRUE for lossless or when embedding a custom ICC profile */
    basic_info.uses_original_profile = (lossless || embed_icc) ? JXL_TRUE : JXL_FALSE;

    if (JxlEncoderSetBasicInfo(enc, &basic_info) != JXL_ENC_SUCCESS) {
        debug_log("WRN", "JXL plugin: Failed to set basic info");
        result = PLUGIN_ERROR_UNKNOWN;
        goto cleanup;
    }

#if defined(HAVE_LIBJXL) && defined(HAVE_LCMS2)
    if (embed_icc) {
        /* Convert pixel data from sRGB back to the original color profile before encoding */
        int cms_intent = plugin_host_api_get_cm_rendering_intent();
        bool cms_bpc = plugin_host_api_get_cm_bpc();
        if (surface_stride == width * 4) {
            cm_convert_srgb_argb32_to_profile(rgba_data, (size_t)(width * height),
                                              doc->original_icc_data, doc->original_icc_size,
                                              cms_intent, cms_bpc);
        } else {
            for (int y = 0; y < height; y++) {
                uint8_t* row = rgba_data + y * width * 4;
                cm_convert_srgb_argb32_to_profile(row, (size_t)width,
                                                  doc->original_icc_data, doc->original_icc_size,
                                                  cms_intent, cms_bpc);
            }
        }
        if (JxlEncoderSetICCProfile(enc,
                                    (const uint8_t*)doc->original_icc_data, doc->original_icc_size) != JXL_ENC_SUCCESS) {
            debug_log("WRN", "JXL plugin: Failed to embed original ICC profile");
            result = PLUGIN_ERROR_UNKNOWN;
            goto cleanup;
        }
    } else {
        JxlColorEncoding color_encoding;
        JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
        if (JxlEncoderSetColorEncoding(enc, &color_encoding) != JXL_ENC_SUCCESS) {
            debug_log("WRN", "JXL plugin: Failed to set sRGB color encoding");
            result = PLUGIN_ERROR_UNKNOWN;
            goto cleanup;
        }
    }
#else
    {
        JxlColorEncoding color_encoding;
        JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
        if (JxlEncoderSetColorEncoding(enc, &color_encoding) != JXL_ENC_SUCCESS) {
            debug_log("WRN", "JXL plugin: Failed to set color encoding");
            result = PLUGIN_ERROR_UNKNOWN;
            goto cleanup;
        }
    }
#endif

    frame_settings = JxlEncoderFrameSettingsCreate(enc, NULL);
    if (!frame_settings) {
        debug_log("WRN", "JXL plugin: Failed to create frame settings");
        result = PLUGIN_ERROR_UNKNOWN;
        goto cleanup;
    }

    if (lossless) {
        JxlEncoderSetFrameLossless(frame_settings, JXL_TRUE);
        JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, effort);
    } else {
        JxlEncoderSetFrameLossless(frame_settings, JXL_FALSE);
        JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, effort);
        /* Map quality 0-100 → distance 15.0-0.0 (libjxl uses "distance" for lossy quality) */
        float distance = (float)(100 - quality) * 15.0f / 100.0f;
        if (distance < 0.0f)
            distance = 0.0f;
        if (distance > 15.0f)
            distance = 15.0f;
        JxlEncoderSetFrameDistance(frame_settings, distance);
    }

    /* Submit frame */
    JxlPixelFormat pixel_format;
    pixel_format.num_channels = 4;
    pixel_format.data_type = JXL_TYPE_UINT8;
    pixel_format.endianness = JXL_NATIVE_ENDIAN;
    pixel_format.align = 0;

    if (JxlEncoderAddImageFrame(frame_settings, &pixel_format,
                                rgba_data, (size_t)width * (size_t)height * 4) != JXL_ENC_SUCCESS) {
        debug_log("WRN", "JXL plugin: Failed to add image frame");
        result = PLUGIN_ERROR_FILE_WRITE_ERROR;
        goto cleanup;
    }

    JxlEncoderCloseInput(enc);

    /* Collect output */
    compressed_alloc = (size_t)width * (size_t)height * 4 + 4096; /* initial guess */
    compressed = (uint8_t*)g_malloc(compressed_alloc);
    if (!compressed) {
        result = PLUGIN_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    {
        uint8_t* next_out = compressed;
        size_t avail_out = compressed_alloc;
        JxlEncoderStatus enc_status;

        while (1) {
            enc_status = JxlEncoderProcessOutput(enc, &next_out, &avail_out);

            if (enc_status == JXL_ENC_SUCCESS) {
                break;
            }

            if (enc_status == JXL_ENC_NEED_MORE_OUTPUT) {
                size_t bytes_written = compressed_alloc - avail_out;
                compressed_alloc *= 2;
                uint8_t* new_buf = (uint8_t*)g_realloc(compressed, compressed_alloc);
                if (!new_buf) {
                    result = PLUGIN_ERROR_OUT_OF_MEMORY;
                    goto cleanup;
                }
                compressed = new_buf;
                next_out = compressed + bytes_written;
                avail_out = compressed_alloc - bytes_written;
                continue;
            }

            debug_log("WRN", "JXL plugin: Encoder error status %d", (int)enc_status);
            result = PLUGIN_ERROR_FILE_WRITE_ERROR;
            goto cleanup;
        }

        compressed_size = compressed_alloc - avail_out;
    }

    outfile = g_fopen(filename, "wb");
    if (!outfile) {
        debug_log("WRN", "JXL plugin: Failed to open output file: %s", filename);
        result = PLUGIN_ERROR_FILE_WRITE_ERROR;
        goto cleanup;
    }

    if (fwrite(compressed, 1, compressed_size, outfile) != compressed_size) {
        debug_log("WRN", "JXL plugin: Failed to write output file");
        fclose(outfile);
        result = PLUGIN_ERROR_FILE_WRITE_ERROR;
        goto cleanup;
    }

    fclose(outfile);
    result = PLUGIN_ERROR_NONE;

cleanup:
    g_free(compressed);
    if (enc)
        JxlEncoderDestroy(enc);
    if (runner)
        JxlThreadParallelRunnerDestroy(runner);
    g_free(rgba_data);
    cairo_surface_destroy(composite);
    return result;
}

/**
 * Get size of JXL-specific save options structure
 */
static size_t get_jxl_save_options_size(void) {
    return sizeof(JXLSaveOptions);
}

/**
 * Initialize JXL-specific save options with defaults
 */
static void init_jxl_save_options(void* plugin_data) {
    JXLSaveOptions* opts = (JXLSaveOptions*)plugin_data;
    if (opts) {
        opts->lossless = true;
        opts->quality = 90;
        opts->effort = 7;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}

#else /* HAVE_LIBJXL not defined */

static PluginError load_jxl(ImageDocument* doc, const char* filename) {
    (void)doc;
    (void)filename;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}

static PluginError save_jxl(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}

static size_t get_jxl_save_options_size(void) {
    return 0;
}

static void init_jxl_save_options(void* plugin_data) {
    (void)plugin_data;
}

#endif /* HAVE_LIBJXL */

/**
 * JPEG XL plugin initialization
 */
bool plugin_init_jxl(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
#ifdef HAVE_LIBJXL
    (void)host;

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "JXL - JPEG XL";
    out_plugin->format_info.extensions = "jxl";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_jxl;
    out_plugin->callbacks.load = load_jxl;
    out_plugin->callbacks.can_save = can_save_jxl;
    out_plugin->callbacks.save = save_jxl;
    out_plugin->callbacks.get_save_options_size = get_jxl_save_options_size;
    out_plugin->callbacks.init_save_options = init_jxl_save_options;

    return true;
#else
    (void)host;
    (void)out_plugin;
    return false;
#endif
}
