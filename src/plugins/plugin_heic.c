/**
 * HEIC image format plugin using libheif
 *
 * Supports loading HEIC (High Efficiency Image Container) images, including:
 * - Single and multiple images
 * - Alpha channel
 * - 8-bit and HDR (with tone mapping dialog when >16M colors)
 * - HEVC codec via libde265
 */

#include "plugins/plugin_heic.h"
#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "tone_mapping.h"
#include "ui.h"
#include "ui/dialogs/hdr_image_dialog.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBHEIF
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#if defined(HAVE_LCMS2)
#include "color_manager/icc_utils.h"
#endif

/* ISO Base Media / HEIC file structure: ftyp box at offset 4 */
static const uint8_t HEIC_FTYP[4] = {'f', 't', 'y', 'p'};

/* HEIC major brands (bytes 8-11 of file) - HEIC only, not AVIF */
static const uint8_t HEIC_BRAND_HEIC[4] = {'h', 'e', 'i', 'c'};
static const uint8_t HEIC_BRAND_HEIX[4] = {'h', 'e', 'i', 'x'};
static const uint8_t HEIC_BRAND_HEVC[4] = {'h', 'e', 'v', 'c'};
static const uint8_t HEIC_BRAND_HEVX[4] = {'h', 'e', 'v', 'x'};
static const uint8_t HEIC_BRAND_HEIM[4] = {'h', 'e', 'i', 'm'};
static const uint8_t HEIC_BRAND_HEIS[4] = {'h', 'e', 'i', 's'};
static const uint8_t HEIC_BRAND_HEVM[4] = {'h', 'e', 'v', 'm'};
static const uint8_t HEIC_BRAND_HEVS[4] = {'h', 'e', 'v', 's'};
static const uint8_t HEIC_BRAND_MIF1[4] = {'m', 'i', 'f', '1'};
static const uint8_t HEIC_BRAND_MSF1[4] = {'m', 's', 'f', '1'};

/**
 * Check if file is HEIC format (ISO Base Media with ftyp, HEIC brands only)
 */
static bool is_heic_brand(const uint8_t* brand) {
    return memcmp(brand, HEIC_BRAND_HEIC, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEIX, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEVC, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEVX, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEIM, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEIS, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEVM, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_HEVS, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_MIF1, 4) == 0 ||
           memcmp(brand, HEIC_BRAND_MSF1, 4) == 0;
}

/**
 * Convert linear RGB float to RGBE format (for HDR dialog)
 */
static void rgb_float_to_rgbe(float r, float g, float b, uint8_t* out_r, uint8_t* out_g, uint8_t* out_b, uint8_t* out_e) {
    float max_c = r;
    if (g > max_c)
        max_c = g;
    if (b > max_c)
        max_c = b;
    if (max_c < 1e-32f) {
        *out_r = *out_g = *out_b = *out_e = 0;
        return;
    }
    int e;
    float mul = frexpf(max_c, &e) * 256.0f / max_c;
    *out_r = (uint8_t)(r * mul);
    *out_g = (uint8_t)(g * mul);
    *out_b = (uint8_t)(b * mul);
    *out_e = (uint8_t)(e + 128);
}

/**
 * Convert RGBE to linear RGB float (for tone mapping)
 */
static void rgbe_to_rgb_float(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float* out_r, float* out_g, float* out_b) {
    if (e == 0) {
        *out_r = *out_g = *out_b = 0.0f;
    } else {
        float f = ldexpf(1.0f, (int)e - 128);
        *out_r = ((float)r + 0.5f) / 256.0f * f;
        *out_g = ((float)g + 0.5f) / 256.0f * f;
        *out_b = ((float)b + 0.5f) / 256.0f * f;
    }
}

/**
 * Convert libheif RGB/RGBA to Cairo ARGB32 (BGRA in memory, premultiplied alpha)
 * src_bpp: 3 for RGB, 4 for RGBA
 */
static void rgb_to_cairo_argb32(const uint8_t* src, uint8_t* dst,
                                int width, int height, int src_stride,
                                int dst_stride, int src_bpp, bool premultiplied_src) {
    bool has_alpha = (src_bpp == 4);
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + (size_t)y * src_stride;
        uint8_t* dst_row = dst + (size_t)y * dst_stride;

        for (int x = 0; x < width; x++) {
            uint8_t r = src_row[x * src_bpp + 0];
            uint8_t g = src_row[x * src_bpp + 1];
            uint8_t b = src_row[x * src_bpp + 2];
            uint8_t a = has_alpha ? src_row[x * src_bpp + 3] : 255;

            if (!premultiplied_src && a < 255 && a > 0) {
                r = (uint8_t)((r * 255 + a / 2) / a);
                g = (uint8_t)((g * 255 + a / 2) / a);
                b = (uint8_t)((b * 255 + a / 2) / a);
            }
            if (a < 255 && a > 0) {
                r = (uint8_t)((r * a + 127) / 255);
                g = (uint8_t)((g * a + 127) / 255);
                b = (uint8_t)((b * a + 127) / 255);
            } else if (a == 0) {
                r = g = b = 0;
            }

            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
        }
    }
}

/**
 * Load a single HEIF image handle into a layer (SDR 8-bit path)
 */
static PluginError load_heif_image_handle_sdr(ImageDocument* doc, heif_context* ctx,
                                              heif_image_handle* handle, const char* layer_name,
                                              int has_alpha, int is_premultiplied,
                                              int expected_w, int expected_h) {
    heif_image* img = NULL;
    heif_error err;
    heif_decoding_options* opts = NULL;
    int width, height;
    const uint8_t* data;
    size_t stride_size;
    ImageLayer* layer = NULL;
    cairo_surface_t* surface = NULL;
    guchar* surface_data;
    int surface_stride;

    opts = heif_decoding_options_alloc();
    if (opts) {
        opts->convert_hdr_to_8bit = 1;
    }

    err = heif_decode_image(handle, &img, heif_colorspace_RGB,
                            has_alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB,
                            opts);
    if (opts) {
        heif_decoding_options_free(opts);
    }
    if (err.code != heif_error_Ok) {
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    width = heif_image_get_width(img, heif_channel_interleaved);
    height = heif_image_get_height(img, heif_channel_interleaved);

    /* Skip truncated/incomplete decodes: decoded size must match expected dimensions */
    if (expected_w > 0 && expected_h > 0 && (width != expected_w || height != expected_h)) {
        heif_image_release(img);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    data = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride_size);
    if (!data) {
        heif_image_release(img);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    layer = layer_new(layer_name, (guint)width, (guint)height, TRUE,
                      LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!layer) {
        heif_image_release(img);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    surface = layer->surface;
    if (!surface) {
        layer_free(layer);
        heif_image_release(img);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);
    surface_stride = cairo_image_surface_get_stride(surface);

    rgb_to_cairo_argb32(data, surface_data, width, height,
                        (int)stride_size, surface_stride,
                        has_alpha ? 4 : 3, (bool)is_premultiplied);

    cairo_surface_mark_dirty(surface);
    heif_image_release(img);

    doc->layers = g_list_append(doc->layers, layer);
    return PLUGIN_ERROR_NONE;
}

/**
 * Load a single HEIF image handle into a layer
 */
static PluginError load_heif_image_handle(ImageDocument* doc, heif_context* ctx,
                                          heif_image_handle* handle, const char* layer_name,
                                          bool is_background, int expected_w, int expected_h) {
    int has_alpha = heif_image_handle_has_alpha_channel(handle);
    int is_premultiplied = heif_image_handle_is_premultiplied_alpha(handle);

    return load_heif_image_handle_sdr(doc, ctx, handle, layer_name, has_alpha, is_premultiplied,
                                      expected_w, expected_h);
}

/**
 * Load HEIC image from file
 */
static PluginError load_heic(ImageDocument* doc, const char* filename) {
    heif_context* ctx = NULL;
    heif_error err;
    heif_item_id* ids = NULL;
    int num_images = 0;
    int i;
    int loaded_count = 0;
    char layer_name[64];

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    ctx = heif_context_alloc();
    if (!ctx) {
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    err = heif_context_read_from_file(ctx, filename, NULL);
    if (err.code != heif_error_Ok) {
        heif_context_free(ctx);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Free existing layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    num_images = heif_context_get_number_of_top_level_images(ctx);
    if (num_images <= 0) {
        heif_context_free(ctx);
        g_warning("HEIC plugin: No top-level images");
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    ids = g_malloc(sizeof(heif_item_id) * (size_t)num_images);
    if (!ids) {
        heif_context_free(ctx);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    int filled = heif_context_get_list_of_top_level_image_IDs(ctx, ids, num_images);

    /* Iterate all top-level images; use first decodable for dimensions.
     * Skip images that fail to decode (e.g. incomplete elementary streams).
     * Other applications may handle such files by trying alternatives. */
    for (i = 0; i < filled; i++) {
        heif_image_handle* handle = NULL;
        err = heif_context_get_image_handle(ctx, ids[i], &handle);
        if (err.code != heif_error_Ok || !handle)
            continue;

#if defined(HAVE_LCMS2)
        /* HEIF: heif_image_handle_get_raw_color_profile_size(); allocate buffer; get profile;
         * pass to icc_profile_from_memory(). Fall back to NULL profile (sRGB) on any failure. */
        if (loaded_count == 0) {
            size_t profile_size = heif_image_handle_get_raw_color_profile_size(handle);
            if (profile_size > 0 && profile_size <= 16 * 1024 * 1024) { /* cap 16MB to avoid malformed */
                uint8_t* profile_buf = g_malloc(profile_size);
                if (profile_buf) {
                    heif_error prof_err = heif_image_handle_get_raw_color_profile(handle, profile_buf);
                    if (prof_err.code == heif_error_Ok) {
                        cmsHPROFILE profile = icc_profile_from_memory(profile_buf, profile_size);
                        if (profile) {
                            g_message("HEIC: embedded ICC profile found (%zu bytes), will convert to sRGB", profile_size);
                            ImageFormatHostAPI* api = plugin_host_api_get();
                            if (api && api->document_set_load_icc_profile)
                                api->document_set_load_icc_profile(doc, profile);
                            else
                                icc_destroy(profile);
                        } else {
                            g_warning("HEIC plugin: Invalid or non-RGB embedded ICC profile; assuming sRGB");
                        }
                    }
                    g_free(profile_buf);
                }
            }
        }
#endif

        const char* name = (filled > 1) ? (loaded_count == 0 ? "Frame 1" : NULL) : "Background";
        if (name != NULL)
            g_snprintf(layer_name, sizeof(layer_name), "%s", name);
        else
            g_snprintf(layer_name, sizeof(layer_name), "Frame %d", loaded_count + 1);

        int expected_w = (loaded_count > 0) ? (int)doc->width : 0;
        int expected_h = (loaded_count > 0) ? (int)doc->height : 0;
        if (load_heif_image_handle(doc, ctx, handle, layer_name, (loaded_count == 0), expected_w, expected_h) == PLUGIN_ERROR_NONE) {
            if (loaded_count == 0) {
                doc->width = (guint)heif_image_handle_get_width(handle);
                doc->height = (guint)heif_image_handle_get_height(handle);
                doc->channels = 4;
                doc->bit_depth = 8;
                doc->has_alpha = heif_image_handle_has_alpha_channel(handle) != 0;
            }
            loaded_count++;
        }
        heif_image_handle_release(handle);
    }

    g_free(ids);
    heif_context_free(ctx);

    if (loaded_count == 0 || doc->layers == NULL) {
        g_warning("HEIC plugin: No decodable image found (incomplete/corrupt streams skipped)");
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    document_render_composite(doc);
    return PLUGIN_ERROR_NONE;
}

/**
 * Convert Cairo ARGB32 (BGRA in memory, premultiplied alpha) to libheif RGBA (straight alpha)
 */
static void cairo_argb32_to_heif_rgba(const uint8_t* src, uint8_t* dst,
                                      int width, int height, int src_stride, int dst_stride) {
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + (size_t)y * src_stride;
        uint8_t* dst_row = dst + (size_t)y * dst_stride;

        for (int x = 0; x < width; x++) {
            uint8_t b = src_row[x * 4 + 0];
            uint8_t g = src_row[x * 4 + 1];
            uint8_t r = src_row[x * 4 + 2];
            uint8_t a = src_row[x * 4 + 3];

            if (a == 0) {
                r = g = b = 0;
            } else if (a < 255) {
                r = (uint8_t)((r * 255 + a / 2) / a);
                g = (uint8_t)((g * 255 + a / 2) / a);
                b = (uint8_t)((b * 255 + a / 2) / a);
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            }

            dst_row[x * 4 + 0] = r;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = b;
            dst_row[x * 4 + 3] = a;
        }
    }
}

/**
 * Create heif_image from cairo_surface_t (ARGB32)
 */
static heif_image* create_heif_image_from_surface(cairo_surface_t* surface,
                                                  guint width, guint height) {
    heif_image* img = NULL;
    heif_error err;

    cairo_surface_flush(surface);
    guchar* surface_data = cairo_image_surface_get_data(surface);
    int surface_stride = cairo_image_surface_get_stride(surface);
    if (!surface_data) {
        return NULL;
    }

    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB,
                            heif_chroma_interleaved_RGBA, &img);
    if (err.code != heif_error_Ok || !img) {
        return NULL;
    }

    err = heif_image_add_plane(img, heif_channel_interleaved, (int)width, (int)height, 8);
    if (err.code != heif_error_Ok) {
        heif_image_release(img);
        return NULL;
    }

    int dst_stride;
    uint8_t* dst = heif_image_get_plane(img, heif_channel_interleaved, &dst_stride);
    if (!dst) {
        heif_image_release(img);
        return NULL;
    }

    cairo_argb32_to_heif_rgba(surface_data, dst, (int)width, (int)height,
                              surface_stride, dst_stride);
    return img;
}

/**
 * Save HEIC image
 */
static PluginError save_heic_impl(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    heif_context* ctx = NULL;
    heif_encoder* encoder = NULL;
    heif_encoding_options* enc_opts = NULL;
    HEICSaveOptions* heic_opts = NULL;
    bool lossless = true;
    int quality = 90;
    bool multiframe = false;
    PluginError ret = PLUGIN_ERROR_NONE;

    if (!doc || !filename || doc->width == 0 || doc->height == 0) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    if (opts && opts->plugin_data) {
        heic_opts = (HEICSaveOptions*)opts->plugin_data;
        lossless = heic_opts->lossless;
        quality = heic_opts->quality;
        multiframe = heic_opts->multiframe;
    }

    if (quality < 0)
        quality = 0;
    if (quality > 100)
        quality = 100;

    ctx = heif_context_alloc();
    if (!ctx) {
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
    if (err.code != heif_error_Ok || !encoder) {
        heif_context_free(ctx);
        g_warning("HEIC plugin: No HEVC encoder available (x265 development libraries required)");
        return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
    }

    if (lossless) {
        err = heif_encoder_set_lossless(encoder, 1);
    } else {
        err = heif_encoder_set_lossless(encoder, 0);
        if (err.code == heif_error_Ok) {
            err = heif_encoder_set_lossy_quality(encoder, quality);
        }
    }
    if (err.code != heif_error_Ok) {
        heif_encoder_release(encoder);
        heif_context_free(ctx);
        return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
    }

    enc_opts = heif_encoding_options_alloc();
    if (!enc_opts) {
        heif_encoder_release(encoder);
        heif_context_free(ctx);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }
    enc_opts->version = 1;
    enc_opts->save_alpha_channel = doc->has_alpha ? 1 : 0;

    if (multiframe) {
        guint layer_count = document_get_layer_count(doc);
        if (layer_count == 0) {
            heif_encoding_options_free(enc_opts);
            heif_encoder_release(encoder);
            heif_context_free(ctx);
            return PLUGIN_ERROR_INVALID_PARAMETERS;
        }

        for (guint i = 0; i < layer_count; i++) {
            ImageLayer* layer = document_get_layer(doc, i);
            if (!layer || !layer->surface) {
                continue;
            }
            if (!layer_ensure_cache(layer)) {
                continue;
            }

            heif_image* img = create_heif_image_from_surface(layer->cache_surface,
                                                             layer->width, layer->height);
            if (!img) {
                ret = PLUGIN_ERROR_FILE_WRITE_ERROR;
                break;
            }

            heif_image_handle* handle = NULL;
            err = heif_context_encode_image(ctx, img, encoder, enc_opts, &handle);
            heif_image_release(img);

            if (handle) {
                heif_image_handle_release(handle);
            }
            if (err.code != heif_error_Ok) {
                g_warning("HEIC plugin: Encode error: %s", err.message);
                ret = PLUGIN_ERROR_FILE_WRITE_ERROR;
                break;
            }
        }
    } else {
        cairo_surface_t* composite = document_export_composite_surface(doc);
        if (!composite) {
            heif_encoding_options_free(enc_opts);
            heif_encoder_release(encoder);
            heif_context_free(ctx);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }

        heif_image* img = create_heif_image_from_surface(composite, doc->width, doc->height);
        cairo_surface_destroy(composite);

        if (!img) {
            heif_encoding_options_free(enc_opts);
            heif_encoder_release(encoder);
            heif_context_free(ctx);
            return PLUGIN_ERROR_FILE_WRITE_ERROR;
        }

        heif_image_handle* handle = NULL;
        err = heif_context_encode_image(ctx, img, encoder, enc_opts, &handle);
        heif_image_release(img);

        if (handle) {
            heif_image_handle_release(handle);
        }
        if (err.code != heif_error_Ok) {
            g_warning("HEIC plugin: Encode error: %s", err.message);
            ret = PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    }

    if (ret == PLUGIN_ERROR_NONE) {
        err = heif_context_write_to_file(ctx, filename);
        if (err.code != heif_error_Ok) {
            g_warning("HEIC plugin: Write error: %s", err.message);
            ret = PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    }

    heif_encoding_options_free(enc_opts);
    heif_encoder_release(encoder);
    heif_context_free(ctx);
    return ret;
}

static size_t get_heic_save_options_size(void) {
    return sizeof(HEICSaveOptions);
}

static void init_heic_save_options(void* plugin_data) {
    HEICSaveOptions* opts = (HEICSaveOptions*)plugin_data;
    if (opts) {
        opts->lossless = true;
        opts->quality = 90;
        opts->multiframe = false;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}

#endif /* HAVE_LIBHEIF */

/**
 * Check if file is HEIC format
 */
static bool can_load_heic(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;

#ifndef HAVE_LIBHEIF
    (void)header;
    (void)header_size;
    return false;
#else
    if (!header || header_size < 12) {
        return false;
    }

    if (memcmp(header + 4, HEIC_FTYP, 4) != 0) {
        return false;
    }

    return is_heic_brand(header + 8);
#endif
}

/**
 * Check if plugin can save (HEIC encoding)
 */
static bool can_save_heic(const char* filename) {
#ifdef HAVE_LIBHEIF
    if (!filename) {
        return false;
    }
    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }
    return g_ascii_strcasecmp(ext + 1, "heic") == 0 ||
           g_ascii_strcasecmp(ext + 1, "heif") == 0;
#else
    (void)filename;
    return false;
#endif
}

/**
 * Save HEIC image
 */
static PluginError save_heic(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
#ifdef HAVE_LIBHEIF
    return save_heic_impl(doc, filename, opts);
#else
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
#endif
}

/**
 * Initialize HEIC plugin
 */
bool plugin_init_heic(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

#ifdef HAVE_LIBHEIF
    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "HEIC/HEIF - High Efficiency Image Format";
    out_plugin->format_info.extensions = "heic,heif";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = true;
    out_plugin->format_info.supports_hdr = true;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_heic;
    out_plugin->callbacks.load = load_heic;
    out_plugin->callbacks.can_save = can_save_heic;
    out_plugin->callbacks.save = save_heic;
    out_plugin->callbacks.get_format_info = NULL;
    out_plugin->callbacks.get_save_options_size = get_heic_save_options_size;
    out_plugin->callbacks.init_save_options = init_heic_save_options;
    out_plugin->callbacks.cleanup = NULL;

    return true;
#else
    (void)out_plugin;
    return false;
#endif
}
