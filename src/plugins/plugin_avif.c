/**
 * AVIF image format plugin using libheif + libaom
 *
 * Supports loading and saving AVIF images (AV1 codec via libaom).
 */

#include "plugins/plugin_avif.h"
#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#if HAVE_LIBHEIF && HAVE_LIBAOM
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#if defined(HAVE_LCMS2)
#include "color_manager/icc_utils.h"
#endif
#endif

/* ISO Base Media / AVIF file structure: ftyp box at offset 4 */
static const uint8_t HEIC_FTYP[4] = {'f', 't', 'y', 'p'};

/* AVIF brands (bytes 8-11 of file) */
static const uint8_t AVIF_BRAND_AVIF[4] = {'a', 'v', 'i', 'f'};
static const uint8_t AVIF_BRAND_AVIS[4] = {'a', 'v', 'i', 's'};

static bool is_avif_brand(const uint8_t* brand) {
    return memcmp(brand, AVIF_BRAND_AVIF, 4) == 0 ||
           memcmp(brand, AVIF_BRAND_AVIS, 4) == 0;
}

/**
 * Convert libheif RGB/RGBA to Cairo ARGB32 (BGRA in memory, premultiplied alpha)
 */
static void rgb_to_cairo_argb32(const uint8_t* src, uint8_t* dst,
                                int width, int height, int src_stride,
                                int dst_stride, int src_bpp, bool premultiplied_src) {
    bool has_alpha = (src_bpp == 4);
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + (size_t)y * src_stride;
        uint32_t* dst_row = (uint32_t*)(dst + (size_t)y * dst_stride);
        for (int x = 0; x < width; x++) {
            uint8_t r = src_row[x * src_bpp + 0];
            uint8_t g = src_row[x * src_bpp + 1];
            uint8_t b = src_row[x * src_bpp + 2];
            uint8_t a = has_alpha ? src_row[x * src_bpp + 3] : 255;
            if (has_alpha && !premultiplied_src && a != 0 && a != 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
            }
            if (a == 255) {
                dst_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            } else if (a == 0) {
                dst_row[x] = 0;
            } else {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
                dst_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
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
 * Create heif_image from cairo_surface_t (ARGB32).
 * icc_data/icc_size: optional sRGB ICC to embed; NULL/0 to skip.
 */
static heif_image* create_heif_image_from_surface(cairo_surface_t* surface,
                                                  guint width, guint height,
                                                  const void* icc_data, size_t icc_size) {
    heif_image* img = NULL;
    heif_error err;

    cairo_surface_flush(surface);
    uint8_t* surface_data = cairo_image_surface_get_data(surface);
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

#if defined(HAVE_LCMS2)
    if (icc_data != NULL && icc_size > 0) {
        heif_error prof_err = heif_image_set_raw_color_profile(img, "prof", icc_data, icc_size);
        if (prof_err.code != heif_error_Ok) {
            /* Non-fatal; continue without embedded profile */
        }
    }
#endif

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

static PluginError load_avif_image_handle_sdr(ImageDocument* doc, heif_context* ctx,
                                              heif_image_handle* handle, const char* layer_name,
                                              int has_alpha, int is_premultiplied,
                                              int expected_w, int expected_h) {
    heif_image* img = NULL;
    heif_error err;
    ImageLayer* layer;
    cairo_surface_t* surface;
    uint8_t* surface_data;
    int surface_stride;
    const uint8_t* data;
    int width, height;
    size_t stride_size;

    (void)ctx;

    err = heif_decode_image(handle, &img,
                            heif_colorspace_RGB,
                            has_alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB,
                            NULL);
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

static PluginError load_avif_image_handle(ImageDocument* doc, heif_context* ctx,
                                          heif_image_handle* handle, const char* layer_name,
                                          int expected_w, int expected_h) {
    int has_alpha = heif_image_handle_has_alpha_channel(handle);
    int is_premultiplied = heif_image_handle_is_premultiplied_alpha(handle);
    return load_avif_image_handle_sdr(doc, ctx, handle, layer_name, has_alpha, is_premultiplied,
                                      expected_w, expected_h);
}

static PluginError load_avif(ImageDocument* doc, const char* filename) {
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

    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    num_images = heif_context_get_number_of_top_level_images(ctx);
    if (num_images <= 0) {
        heif_context_free(ctx);
        g_warning("AVIF plugin: No top-level images");
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    ids = (heif_item_id*)calloc((size_t)num_images, sizeof(heif_item_id));
    if (!ids) {
        heif_context_free(ctx);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    int n = heif_context_get_list_of_top_level_image_IDs(ctx, ids, num_images);

    /* Iterate all top-level images; use first decodable for dimensions.
     * Skip images that fail to decode (e.g. incomplete elementary streams).
     * Other applications may handle such files by trying alternatives. */
    for (i = 0; i < n; i++) {
        heif_image_handle* h = NULL;
        err = heif_context_get_image_handle(ctx, ids[i], &h);
        if (err.code != heif_error_Ok || !h)
            continue;

#if defined(HAVE_LCMS2)
        /* AVIF: heif_image_handle_get_raw_color_profile_size(); get profile;
         * pass to icc_profile_from_memory(). Fall back to NULL profile (sRGB) on any failure. */
        if (loaded_count == 0) {
            size_t profile_size = heif_image_handle_get_raw_color_profile_size(h);
            if (profile_size > 0 && profile_size <= 16 * 1024 * 1024) {
                uint8_t* profile_buf = g_malloc(profile_size);
                if (profile_buf) {
                    heif_error prof_err = heif_image_handle_get_raw_color_profile(h, profile_buf);
                    if (prof_err.code == heif_error_Ok) {
                        cmsHPROFILE profile = icc_profile_from_memory(profile_buf, profile_size);
                        if (profile) {
                            ImageFormatHostAPI* api = plugin_host_api_get();
                            if (api && api->document_set_load_icc_profile)
                                api->document_set_load_icc_profile(doc, profile);
                            else
                                icc_destroy(profile);
                        } else {
                            g_warning("AVIF plugin: Invalid or non-RGB embedded ICC profile; assuming sRGB");
                        }
                    }
                    g_free(profile_buf);
                }
            }
        }
#endif

        const char* name = (n > 1) ? (loaded_count == 0 ? "Frame 1" : NULL) : "Background";
        if (name == NULL)
            g_snprintf(layer_name, sizeof(layer_name), "Frame %d", loaded_count + 1);
        else
            g_snprintf(layer_name, sizeof(layer_name), "%s", name);

        int expected_w = (loaded_count > 0) ? (int)doc->width : 0;
        int expected_h = (loaded_count > 0) ? (int)doc->height : 0;
        if (load_avif_image_handle(doc, ctx, h, layer_name, expected_w, expected_h) == PLUGIN_ERROR_NONE) {
            if (loaded_count == 0) {
                doc->width = (guint)heif_image_handle_get_width(h);
                doc->height = (guint)heif_image_handle_get_height(h);
                doc->channels = 4;
                doc->bit_depth = 8;
                doc->has_alpha = heif_image_handle_has_alpha_channel(h) != 0;
            }
            loaded_count++;
        }
        heif_image_handle_release(h);
    }

    free(ids);
    heif_context_free(ctx);

    if (loaded_count == 0) {
        g_warning("AVIF plugin: No decodable image found (incomplete/corrupt streams skipped)");
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    document_render_composite(doc);
    return PLUGIN_ERROR_NONE;
}

static bool can_load_avif(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;

#if !HAVE_LIBHEIF || !HAVE_LIBAOM
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
    return is_avif_brand(header + 8);
#endif
}

static bool can_save_avif(const char* filename) {
#if !HAVE_LIBHEIF || !HAVE_LIBAOM
    (void)filename;
    return false;
#else
    if (!filename)
        return false;
    const char* ext = strrchr(filename, '.');
    if (!ext)
        return false;
    return g_ascii_strcasecmp(ext + 1, "avif") == 0 ||
           g_ascii_strcasecmp(ext + 1, "avifs") == 0;
#endif
}

static PluginError save_avif_impl(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    heif_context* ctx = NULL;
    heif_encoder* encoder = NULL;
    heif_encoding_options* enc_opts = NULL;
    AVIFSaveOptions* avif_opts = NULL;
    int quality = 63;
    PluginError ret = PLUGIN_ERROR_NONE;
#if defined(HAVE_LCMS2)
    void* icc_data = NULL;
    size_t icc_size = 0;
    cmsHPROFILE srgb = icc_create_srgb_profile();
    if (srgb && icc_profile_to_memory(srgb, &icc_data, &icc_size)) {
        icc_destroy(srgb);
    } else {
        if (srgb) icc_destroy(srgb);
        icc_data = NULL;
        icc_size = 0;
    }
#endif

    if (!doc || !filename || doc->width == 0 || doc->height == 0) {
#if defined(HAVE_LCMS2)
        if (icc_data) free(icc_data);
#endif
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    if (opts && opts->plugin_data) {
        avif_opts = (AVIFSaveOptions*)opts->plugin_data;
        quality = avif_opts->quality;
    }

    if (quality < 0)
        quality = 0;
    if (quality > 63)
        quality = 63;

    ctx = heif_context_alloc();
    if (!ctx) {
#if defined(HAVE_LCMS2)
        if (icc_data) free(icc_data);
#endif
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &encoder);
    if (err.code != heif_error_Ok || !encoder) {
        heif_context_free(ctx);
#if defined(HAVE_LCMS2)
        if (icc_data) free(icc_data);
#endif
        g_warning("AVIF plugin: No AV1 encoder available (libaom required)");
        return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
    }

    err = heif_encoder_set_lossy_quality(encoder, quality);
    if (err.code != heif_error_Ok) {
        heif_encoder_release(encoder);
        heif_context_free(ctx);
#if defined(HAVE_LCMS2)
        if (icc_data) free(icc_data);
#endif
        return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
    }

    enc_opts = heif_encoding_options_alloc();
    if (!enc_opts) {
        heif_encoder_release(encoder);
        heif_context_free(ctx);
#if defined(HAVE_LCMS2)
        if (icc_data) free(icc_data);
#endif
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }
    enc_opts->version = 1;
    enc_opts->save_alpha_channel = doc->has_alpha ? 1 : 0;

    cairo_surface_t* composite = document_export_composite_surface(doc);
    if (!composite) {
        heif_encoding_options_free(enc_opts);
        heif_encoder_release(encoder);
        heif_context_free(ctx);
#if defined(HAVE_LCMS2)
        if (icc_data) free(icc_data);
#endif
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    heif_image* img = create_heif_image_from_surface(composite, doc->width, doc->height,
#if defined(HAVE_LCMS2)
                                                     icc_data, icc_size
#else
                                                     NULL, 0
#endif
    );
#if defined(HAVE_LCMS2)
    if (icc_data) free(icc_data);
#endif
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
        g_warning("AVIF plugin: Encode error: %s", err.message);
        ret = PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    if (ret == PLUGIN_ERROR_NONE) {
        err = heif_context_write_to_file(ctx, filename);
        if (err.code != heif_error_Ok) {
            g_warning("AVIF plugin: Write error: %s", err.message);
            ret = PLUGIN_ERROR_FILE_WRITE_ERROR;
        }
    }

    heif_encoding_options_free(enc_opts);
    heif_encoder_release(encoder);
    heif_context_free(ctx);
    return ret;
}

static size_t get_avif_save_options_size(void) {
    return sizeof(AVIFSaveOptions);
}

static void init_avif_save_options(void* plugin_data) {
    AVIFSaveOptions* opts = (AVIFSaveOptions*)plugin_data;
    if (opts) {
        opts->quality = 63;
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}

static PluginError save_avif(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
#if HAVE_LIBHEIF && HAVE_LIBAOM
    return save_avif_impl(doc, filename, opts);
#else
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
#endif
}

bool plugin_init_avif(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

#if HAVE_LIBHEIF && HAVE_LIBAOM
    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "AVIF - AV1 Image File Format";
    out_plugin->format_info.extensions = "avif,avifs";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = true;
    out_plugin->format_info.supports_hdr = true;
    out_plugin->format_info.priority = 50;

    out_plugin->callbacks.can_load = can_load_avif;
    out_plugin->callbacks.load = load_avif;
    out_plugin->callbacks.can_save = can_save_avif;
    out_plugin->callbacks.save = save_avif;
    out_plugin->callbacks.get_format_info = NULL;
    out_plugin->callbacks.get_save_options_size = get_avif_save_options_size;
    out_plugin->callbacks.init_save_options = init_avif_save_options;
    out_plugin->callbacks.cleanup = NULL;

    return true;
#else
    (void)out_plugin;
    return false;
#endif
}
