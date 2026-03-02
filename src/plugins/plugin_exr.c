#ifdef HAVE_OPENEXR

#include "plugins/plugin_exr.h"
#include "app/settings.h"
#include "color_manager.h"
#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "tone_mapping.h"
#include "ui.h"
#include "ui/dialogs/hdr_image_dialog.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <openexr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OpenEXR file magic (big-endian): 20000630 = 0x01 0x31 0x2f 0x76 */
#define EXR_MAGIC_0 0x01
#define EXR_MAGIC_1 0x31
#define EXR_MAGIC_2 0x2f
#define EXR_MAGIC_3 0x76

static void exr_error_cb(exr_const_context_t ctxt, exr_result_t code, const char* msg) {
    (void)ctxt;
    g_warning("EXR plugin: %s (code %d)", msg ? msg : "unknown", code);
}

/** Custom I/O: use GLib's g_fopen so path handling matches the rest of the app (e.g. Windows UTF-8). */
typedef struct {
    FILE* fp;
    GMutex mutex;
} ExrFileStream;

static int64_t exr_custom_read(exr_const_context_t ctxt, void* userdata,
                               void* buffer, uint64_t sz, uint64_t offset,
                               exr_stream_error_func_ptr_t error_cb) {
    ExrFileStream* stream = (ExrFileStream*)userdata;
    (void)ctxt;
    (void)error_cb;
    if (!stream || !stream->fp || !buffer)
        return -1;
    g_mutex_lock(&stream->mutex);
    if (fseek(stream->fp, (long)offset, SEEK_SET) != 0) {
        g_mutex_unlock(&stream->mutex);
        return -1;
    }
    size_t n = fread(buffer, 1, (size_t)sz, stream->fp);
    g_mutex_unlock(&stream->mutex);
    return (int64_t)n;
}

static int64_t exr_custom_size(exr_const_context_t ctxt, void* userdata) {
    ExrFileStream* stream = (ExrFileStream*)userdata;
    (void)ctxt;
    if (!stream || !stream->fp)
        return -1;
    g_mutex_lock(&stream->mutex);
    if (fseek(stream->fp, 0, SEEK_END) != 0) {
        g_mutex_unlock(&stream->mutex);
        return -1;
    }
    long sz = ftell(stream->fp);
    g_mutex_unlock(&stream->mutex);
    return (int64_t)sz;
}

static void exr_custom_destroy(exr_const_context_t ctxt, void* userdata, int failed) {
    ExrFileStream* stream = (ExrFileStream*)userdata;
    (void)ctxt;
    (void)failed;
    if (stream) {
        if (stream->fp) {
            fclose(stream->fp);
            stream->fp = NULL;
        }
        g_mutex_clear(&stream->mutex);
        g_free(stream);
    }
}

/**
 * Check if file is EXR format by magic number
 */
static bool can_load_exr(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename;

    if (!header || header_size < 4) {
        return false;
    }
    if (header[0] == EXR_MAGIC_0 && header[1] == EXR_MAGIC_1 &&
        header[2] == EXR_MAGIC_2 && header[3] == EXR_MAGIC_3) {
        return true;
    }
    if (header[0] == EXR_MAGIC_3 && header[1] == EXR_MAGIC_2 &&
        header[2] == EXR_MAGIC_1 && header[3] == EXR_MAGIC_0) {
        return true;
    }
    return false;
}

static bool can_save_exr(const char* filename) {
    (void)filename;
    return false;
}

static void float_rgb_to_rgbe(float r, float g, float b,
                              uint8_t* out_r, uint8_t* out_g, uint8_t* out_b, uint8_t* out_e) {
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

/* Case-insensitive compare for channel names */
static bool channel_name_eq(const char* a, const char* b) {
    if (!a || !b)
        return false;
    for (; *a && *b; a++, b++) {
        char ca = (char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a);
        char cb = (char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);
        if (ca != cb)
            return false;
    }
    return *a == *b;
}

static int find_channel_index(exr_decode_pipeline_t* decoder, const char* name) {
    for (int i = 0; i < decoder->channel_count; i++) {
        if (decoder->channels[i].channel_name && channel_name_eq(decoder->channels[i].channel_name, name)) {
            return i;
        }
    }
    return -1;
}

/* Resolve R/G/B channel index (tries common names) */
static int find_rgb_channel_index(exr_decode_pipeline_t* decoder, const char* primary, const char* alt) {
    int i = find_channel_index(decoder, primary);
    if (i >= 0)
        return i;
    return find_channel_index(decoder, alt);
}

/* Simple nearest-neighbor upsample for subsampled chroma */
static void upsample_chroma(const float* src, float* dst, uint32_t src_w, uint32_t src_h,
                            uint32_t dst_w, uint32_t dst_h, int samp_x, int samp_y) {
    for (uint32_t dy = 0; dy < dst_h; dy++) {
        for (uint32_t dx = 0; dx < dst_w; dx++) {
            uint32_t sx = dx / samp_x;
            uint32_t sy = dy / samp_y;
            if (sx >= src_w)
                sx = src_w - 1;
            if (sy >= src_h)
                sy = src_h - 1;
            dst[dy * dst_w + dx] = src[sy * src_w + sx];
        }
    }
}

static PluginError load_exr(ImageDocument* doc, const char* filename) {
    exr_context_t ctxt = NULL;
    exr_context_initializer_t cinit = EXR_DEFAULT_CONTEXT_INITIALIZER;
    cinit.error_handler_fn = exr_error_cb;

    if (!filename || !*filename) {
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    FILE* fp = g_fopen(filename, "rb");
    if (!fp) {
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    ExrFileStream* stream = (ExrFileStream*)g_malloc(sizeof(ExrFileStream));
    if (!stream) {
        fclose(fp);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }
    stream->fp = fp;
    g_mutex_init(&stream->mutex);

    cinit.user_data = stream;
    cinit.read_fn = exr_custom_read;
    cinit.size_fn = exr_custom_size;
    cinit.destroy_fn = exr_custom_destroy;

    exr_result_t rv = exr_start_read(&ctxt, filename, &cinit);
    if (rv != EXR_ERR_SUCCESS || !ctxt) {
        if (ctxt)
            exr_finish(&ctxt);
        else
            exr_custom_destroy(NULL, stream, 0);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    exr_storage_t storage;
    rv = exr_get_storage(ctxt, 0, &storage);
    if (rv != EXR_ERR_SUCCESS || (storage != EXR_STORAGE_SCANLINE && storage != EXR_STORAGE_TILED)) {
        exr_finish(&ctxt);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    exr_attr_box2i_t data_window;
    rv = exr_get_data_window(ctxt, 0, &data_window);
    if (rv != EXR_ERR_SUCCESS) {
        exr_finish(&ctxt);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    int32_t w = data_window.max.x - data_window.min.x + 1;
    int32_t h = data_window.max.y - data_window.min.y + 1;
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) {
        exr_finish(&ctxt);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    uint32_t width = (uint32_t)w;
    uint32_t height = (uint32_t)h;
    size_t num_pixels = (size_t)width * (size_t)height;

    /* Check channel layout and subsampling */
    const exr_attr_chlist_t* chlist = NULL;
    rv = exr_get_channels(ctxt, 0, &chlist);
    if (rv != EXR_ERR_SUCCESS || !chlist) {
        exr_finish(&ctxt);
        return PLUGIN_ERROR_CORRUPT_FILE;
    }

    bool has_r = false, has_g = false, has_b = false;
    bool file_has_alpha = false;
    bool has_yryby = false;
    bool has_y_only = false;
    int y_samp_x = 1, y_samp_y = 1;
    int ry_samp_x = 1, ry_samp_y = 1;
    int by_samp_x = 1, by_samp_y = 1;

    for (int i = 0; i < chlist->num_channels; i++) {
        const char* name = chlist->entries[i].name.str;
        if (channel_name_eq(name, "R") || channel_name_eq(name, "red")) {
            has_r = true;
        } else if (channel_name_eq(name, "G") || channel_name_eq(name, "green")) {
            has_g = true;
        } else if (channel_name_eq(name, "B") || channel_name_eq(name, "blue")) {
            has_b = true;
        } else if (channel_name_eq(name, "A") || channel_name_eq(name, "alpha")) {
            file_has_alpha = true;
        } else if (channel_name_eq(name, "Y")) {
            y_samp_x = chlist->entries[i].x_sampling;
            y_samp_y = chlist->entries[i].y_sampling;
            has_y_only = true;
        } else if (channel_name_eq(name, "RY")) {
            ry_samp_x = chlist->entries[i].x_sampling;
            ry_samp_y = chlist->entries[i].y_sampling;
            has_yryby = true;
        } else if (channel_name_eq(name, "BY")) {
            by_samp_x = chlist->entries[i].x_sampling;
            by_samp_y = chlist->entries[i].y_sampling;
        }
    }

    bool has_rgb = has_r && has_g && has_b;

    /* Allocate final RGB buffer (planar: all R, then all G, then all B, then all A) */
    float* float_rgba = (float*)g_malloc(num_pixels * 4 * sizeof(float));
    if (!float_rgba) {
        exr_finish(&ctxt);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    float* r_plane = float_rgba + 0 * num_pixels;
    float* g_plane = float_rgba + 1 * num_pixels;
    float* b_plane = float_rgba + 2 * num_pixels;
    float* a_plane = float_rgba + 3 * num_pixels;

    /* Initialize in planar order so a_plane is fully opaque when no alpha channel is decoded */
    for (size_t i = 0; i < num_pixels; i++) {
        r_plane[i] = 0.f;
        g_plane[i] = 0.f;
        b_plane[i] = 0.f;
        a_plane[i] = 1.f;
    }

    bool decode_ok = true;

    if (has_rgb) {
        /* Standard RGB channels - use existing code */
        const int32_t line_stride_bytes = (int32_t)(width * sizeof(float));

        if (storage == EXR_STORAGE_SCANLINE) {
            int32_t y = data_window.min.y;
            while (y <= data_window.max.y && decode_ok) {
                exr_chunk_info_t cinfo;
                rv = exr_read_scanline_chunk_info(ctxt, 0, y, &cinfo);
                if (rv != EXR_ERR_SUCCESS) {
                    decode_ok = false;
                    break;
                }

                exr_decode_pipeline_t decoder = EXR_DECODE_PIPELINE_INITIALIZER;
                rv = exr_decoding_initialize(ctxt, 0, &cinfo, &decoder);
                if (rv != EXR_ERR_SUCCESS) {
                    decode_ok = false;
                    break;
                }

                int r_idx = find_rgb_channel_index(&decoder, "R", "red");
                int g_idx = find_rgb_channel_index(&decoder, "G", "green");
                int b_idx = find_rgb_channel_index(&decoder, "B", "blue");
                int a_idx = find_channel_index(&decoder, "A");
                if (a_idx < 0)
                    a_idx = find_channel_index(&decoder, "alpha");

                if (r_idx < 0 || g_idx < 0 || b_idx < 0) {
                    exr_decoding_destroy(ctxt, &decoder);
                    decode_ok = false;
                    break;
                }

                int32_t row_offset = cinfo.start_y - data_window.min.y;

                for (int c = 0; c < decoder.channel_count; c++) {
                    decoder.channels[c].decode_to_ptr = NULL;
                }

                decoder.channels[r_idx].decode_to_ptr = (uint8_t*)(r_plane + (size_t)row_offset * width);
                decoder.channels[r_idx].user_pixel_stride = 4;
                decoder.channels[r_idx].user_line_stride = line_stride_bytes;
                decoder.channels[r_idx].user_bytes_per_element = 4;
                decoder.channels[r_idx].user_data_type = EXR_PIXEL_FLOAT;

                decoder.channels[g_idx].decode_to_ptr = (uint8_t*)(g_plane + (size_t)row_offset * width);
                decoder.channels[g_idx].user_pixel_stride = 4;
                decoder.channels[g_idx].user_line_stride = line_stride_bytes;
                decoder.channels[g_idx].user_bytes_per_element = 4;
                decoder.channels[g_idx].user_data_type = EXR_PIXEL_FLOAT;

                decoder.channels[b_idx].decode_to_ptr = (uint8_t*)(b_plane + (size_t)row_offset * width);
                decoder.channels[b_idx].user_pixel_stride = 4;
                decoder.channels[b_idx].user_line_stride = line_stride_bytes;
                decoder.channels[b_idx].user_bytes_per_element = 4;
                decoder.channels[b_idx].user_data_type = EXR_PIXEL_FLOAT;

                if (a_idx >= 0) {
                    decoder.channels[a_idx].decode_to_ptr = (uint8_t*)(a_plane + (size_t)row_offset * width);
                    decoder.channels[a_idx].user_pixel_stride = 4;
                    decoder.channels[a_idx].user_line_stride = line_stride_bytes;
                    decoder.channels[a_idx].user_bytes_per_element = 4;
                    decoder.channels[a_idx].user_data_type = EXR_PIXEL_FLOAT;
                }

                rv = exr_decoding_choose_default_routines(ctxt, 0, &decoder);
                if (rv != EXR_ERR_SUCCESS) {
                    exr_decoding_destroy(ctxt, &decoder);
                    decode_ok = false;
                    break;
                }

                rv = exr_decoding_run(ctxt, 0, &decoder);
                exr_decoding_destroy(ctxt, &decoder);
                if (rv != EXR_ERR_SUCCESS) {
                    decode_ok = false;
                    break;
                }

                y = cinfo.start_y + cinfo.height;
            }
        } else {
            /* Tiled RGB - same as before */
            int32_t countx = 0, county = 0, tilew = 0, tileh = 0;
            exr_get_tile_counts(ctxt, 0, 0, 0, &countx, &county);
            exr_get_tile_sizes(ctxt, 0, 0, 0, &tilew, &tileh);

            for (int32_t tiley = 0; decode_ok && tiley < county; tiley++) {
                for (int32_t tilex = 0; decode_ok && tilex < countx; tilex++) {
                    exr_chunk_info_t cinfo;
                    rv = exr_read_tile_chunk_info(ctxt, 0, tilex, tiley, 0, 0, &cinfo);
                    if (rv != EXR_ERR_SUCCESS) {
                        decode_ok = false;
                        break;
                    }

                    exr_decode_pipeline_t decoder = EXR_DECODE_PIPELINE_INITIALIZER;
                    rv = exr_decoding_initialize(ctxt, 0, &cinfo, &decoder);
                    if (rv != EXR_ERR_SUCCESS) {
                        decode_ok = false;
                        break;
                    }

                    int r_idx = find_rgb_channel_index(&decoder, "R", "red");
                    int g_idx = find_rgb_channel_index(&decoder, "G", "green");
                    int b_idx = find_rgb_channel_index(&decoder, "B", "blue");
                    int a_idx = find_channel_index(&decoder, "A");
                    if (a_idx < 0)
                        a_idx = find_channel_index(&decoder, "alpha");

                    if (r_idx < 0 || g_idx < 0 || b_idx < 0) {
                        exr_decoding_destroy(ctxt, &decoder);
                        decode_ok = false;
                        break;
                    }

                    int32_t tw = cinfo.width;
                    int32_t th = cinfo.height;
                    size_t tile_pixels = (size_t)tw * (size_t)th;
                    float* tile_r = (float*)g_malloc(tile_pixels * 4 * sizeof(float));
                    if (!tile_r) {
                        exr_decoding_destroy(ctxt, &decoder);
                        decode_ok = false;
                        break;
                    }
                    float* tile_g = tile_r + tile_pixels;
                    float* tile_b = tile_r + tile_pixels * 2;
                    float* tile_a = tile_r + tile_pixels * 3;
                    int32_t tile_stride = (int32_t)(tw * sizeof(float));

                    for (int c = 0; c < decoder.channel_count; c++) {
                        decoder.channels[c].decode_to_ptr = NULL;
                    }

                    decoder.channels[r_idx].decode_to_ptr = (uint8_t*)tile_r;
                    decoder.channels[r_idx].user_pixel_stride = 4;
                    decoder.channels[r_idx].user_line_stride = tile_stride;
                    decoder.channels[r_idx].user_bytes_per_element = 4;
                    decoder.channels[r_idx].user_data_type = EXR_PIXEL_FLOAT;

                    if (g_idx >= 0) {
                        decoder.channels[g_idx].decode_to_ptr = (uint8_t*)tile_g;
                        decoder.channels[g_idx].user_pixel_stride = 4;
                        decoder.channels[g_idx].user_line_stride = tile_stride;
                        decoder.channels[g_idx].user_bytes_per_element = 4;
                        decoder.channels[g_idx].user_data_type = EXR_PIXEL_FLOAT;
                    }

                    if (b_idx >= 0) {
                        decoder.channels[b_idx].decode_to_ptr = (uint8_t*)tile_b;
                        decoder.channels[b_idx].user_pixel_stride = 4;
                        decoder.channels[b_idx].user_line_stride = tile_stride;
                        decoder.channels[b_idx].user_bytes_per_element = 4;
                        decoder.channels[b_idx].user_data_type = EXR_PIXEL_FLOAT;
                    }

                    if (a_idx >= 0) {
                        decoder.channels[a_idx].decode_to_ptr = (uint8_t*)tile_a;
                        decoder.channels[a_idx].user_pixel_stride = 4;
                        decoder.channels[a_idx].user_line_stride = tile_stride;
                        decoder.channels[a_idx].user_bytes_per_element = 4;
                        decoder.channels[a_idx].user_data_type = EXR_PIXEL_FLOAT;
                    }

                    rv = exr_decoding_choose_default_routines(ctxt, 0, &decoder);
                    if (rv != EXR_ERR_SUCCESS) {
                        exr_decoding_destroy(ctxt, &decoder);
                        g_free(tile_r);
                        decode_ok = false;
                        break;
                    }

                    rv = exr_decoding_run(ctxt, 0, &decoder);
                    exr_decoding_destroy(ctxt, &decoder);
                    if (rv != EXR_ERR_SUCCESS) {
                        g_free(tile_r);
                        decode_ok = false;
                        break;
                    }

                    int32_t tile_pixel_x = tilex * tilew;
                    int32_t tile_pixel_y = tiley * tileh;
                    for (int32_t ty = 0; ty < th; ty++) {
                        int32_t dst_y = tile_pixel_y + ty;
                        if (dst_y < 0 || dst_y >= (int32_t)height)
                            continue;
                        for (int32_t tx = 0; tx < tw; tx++) {
                            int32_t dst_x = tile_pixel_x + tx;
                            if (dst_x < 0 || dst_x >= (int32_t)width)
                                continue;
                            size_t src_idx = (size_t)ty * (size_t)tw + (size_t)tx;
                            size_t dst_idx = (size_t)dst_y * (size_t)width + (size_t)dst_x;
                            r_plane[dst_idx] = tile_r[src_idx];
                            g_plane[dst_idx] = (g_idx >= 0) ? tile_g[src_idx] : tile_r[src_idx];
                            b_plane[dst_idx] = (b_idx >= 0) ? tile_b[src_idx] : tile_r[src_idx];
                            a_plane[dst_idx] = (a_idx >= 0) ? tile_a[src_idx] : 1.f;
                        }
                    }
                    g_free(tile_r);
                }
            }
        }
    } else if (has_yryby || has_y_only) {
        /* Y/RY/BY or grayscale Y - decode to temp buffers at native resolution then upsample.
         * Use same dimensions as OpenEXR decoder: integer division for subsampled width/height. */
        uint32_t y_width = (y_samp_x > 0 && y_samp_y > 0)
                               ? (uint32_t)((int)width / y_samp_x)
                               : width;
        uint32_t y_height = (y_samp_x > 0 && y_samp_y > 0)
                                ? (uint32_t)((int)height / y_samp_y)
                                : height;
        if (y_width == 0)
            y_width = width;
        if (y_height == 0)
            y_height = height;
        uint32_t ry_width = has_yryby ? (uint32_t)((int)width / ry_samp_x) : 0;
        uint32_t ry_height = has_yryby ? (uint32_t)((int)height / ry_samp_y) : 0;
        uint32_t by_width = has_yryby ? (uint32_t)((int)width / by_samp_x) : 0;
        uint32_t by_height = has_yryby ? (uint32_t)((int)height / by_samp_y) : 0;
        if (ry_width == 0 && has_yryby)
            ry_width = 1;
        if (ry_height == 0 && has_yryby)
            ry_height = 1;
        if (by_width == 0 && has_yryby)
            by_width = 1;
        if (by_height == 0 && has_yryby)
            by_height = 1;

        float* y_buf = (float*)g_malloc((size_t)y_width * y_height * sizeof(float));
        float* ry_buf = has_yryby ? (float*)g_malloc((size_t)ry_width * ry_height * sizeof(float)) : NULL;
        float* by_buf = has_yryby ? (float*)g_malloc((size_t)by_width * by_height * sizeof(float)) : NULL;

        if (!y_buf || (has_yryby && (!ry_buf || !by_buf))) {
            g_free(y_buf);
            g_free(ry_buf);
            g_free(by_buf);
            g_free(float_rgba);
            exr_finish(&ctxt);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        memset(y_buf, 0, (size_t)y_width * y_height * sizeof(float));
        if (ry_buf)
            memset(ry_buf, 0, (size_t)ry_width * ry_height * sizeof(float));
        if (by_buf)
            memset(by_buf, 0, (size_t)by_width * by_height * sizeof(float));

        if (storage == EXR_STORAGE_SCANLINE) {
            /* Decode scanlines - Y/RY/BY are in separate buffers at their native resolutions */
            int32_t y_line = data_window.min.y;
            while (y_line <= data_window.max.y && decode_ok) {
                exr_chunk_info_t cinfo;
                rv = exr_read_scanline_chunk_info(ctxt, 0, y_line, &cinfo);
                if (rv != EXR_ERR_SUCCESS) {
                    decode_ok = false;
                    break;
                }

                exr_decode_pipeline_t decoder = EXR_DECODE_PIPELINE_INITIALIZER;
                rv = exr_decoding_initialize(ctxt, 0, &cinfo, &decoder);
                if (rv != EXR_ERR_SUCCESS) {
                    decode_ok = false;
                    break;
                }

                int y_idx = find_channel_index(&decoder, "Y");
                int ry_idx = has_yryby ? find_channel_index(&decoder, "RY") : -1;
                int by_idx = has_yryby ? find_channel_index(&decoder, "BY") : -1;

                if (y_idx < 0) {
                    exr_decoding_destroy(ctxt, &decoder);
                    decode_ok = false;
                    break;
                }

                for (int c = 0; c < decoder.channel_count; c++) {
                    decoder.channels[c].decode_to_ptr = NULL;
                }

                int32_t abs_y = cinfo.start_y - data_window.min.y;

                /* Decode Y if this scanline has it */
                if ((abs_y % y_samp_y) == 0) {
                    int32_t y_row = abs_y / y_samp_y;
                    decoder.channels[y_idx].decode_to_ptr = (uint8_t*)(y_buf + (size_t)y_row * y_width);
                    decoder.channels[y_idx].user_pixel_stride = 4;
                    decoder.channels[y_idx].user_line_stride = (int32_t)(y_width * sizeof(float));
                    decoder.channels[y_idx].user_bytes_per_element = 4;
                    decoder.channels[y_idx].user_data_type = EXR_PIXEL_FLOAT;
                }

                /* Decode RY/BY if this scanline has them */
                if (has_yryby && ry_idx >= 0 && (abs_y % ry_samp_y) == 0) {
                    int32_t ry_row = abs_y / ry_samp_y;
                    decoder.channels[ry_idx].decode_to_ptr = (uint8_t*)(ry_buf + (size_t)ry_row * ry_width);
                    decoder.channels[ry_idx].user_pixel_stride = 4;
                    decoder.channels[ry_idx].user_line_stride = (int32_t)(ry_width * sizeof(float));
                    decoder.channels[ry_idx].user_bytes_per_element = 4;
                    decoder.channels[ry_idx].user_data_type = EXR_PIXEL_FLOAT;
                }

                if (has_yryby && by_idx >= 0 && (abs_y % by_samp_y) == 0) {
                    int32_t by_row = abs_y / by_samp_y;
                    decoder.channels[by_idx].decode_to_ptr = (uint8_t*)(by_buf + (size_t)by_row * by_width);
                    decoder.channels[by_idx].user_pixel_stride = 4;
                    decoder.channels[by_idx].user_line_stride = (int32_t)(by_width * sizeof(float));
                    decoder.channels[by_idx].user_bytes_per_element = 4;
                    decoder.channels[by_idx].user_data_type = EXR_PIXEL_FLOAT;
                }

                rv = exr_decoding_choose_default_routines(ctxt, 0, &decoder);
                if (rv != EXR_ERR_SUCCESS) {
                    exr_decoding_destroy(ctxt, &decoder);
                    decode_ok = false;
                    break;
                }

                rv = exr_decoding_run(ctxt, 0, &decoder);
                exr_decoding_destroy(ctxt, &decoder);
                if (rv != EXR_ERR_SUCCESS) {
                    decode_ok = false;
                    break;
                }

                y_line = cinfo.start_y + cinfo.height;
            }
        } else {
            /* Tiled Y/RY/BY or Y-only - iterate tiles and decode into full buffers at sampled offsets */
            int32_t countx = 0, county = 0, tilew = 0, tileh = 0;
            exr_get_tile_counts(ctxt, 0, 0, 0, &countx, &county);
            exr_get_tile_sizes(ctxt, 0, 0, 0, &tilew, &tileh);

            for (int32_t tiley = 0; decode_ok && tiley < county; tiley++) {
                for (int32_t tilex = 0; decode_ok && tilex < countx; tilex++) {
                    exr_chunk_info_t cinfo;
                    rv = exr_read_tile_chunk_info(ctxt, 0, tilex, tiley, 0, 0, &cinfo);
                    if (rv != EXR_ERR_SUCCESS) {
                        decode_ok = false;
                        break;
                    }

                    exr_decode_pipeline_t decoder = EXR_DECODE_PIPELINE_INITIALIZER;
                    rv = exr_decoding_initialize(ctxt, 0, &cinfo, &decoder);
                    if (rv != EXR_ERR_SUCCESS) {
                        decode_ok = false;
                        break;
                    }

                    int y_idx = find_channel_index(&decoder, "Y");
                    int ry_idx = has_yryby ? find_channel_index(&decoder, "RY") : -1;
                    int by_idx = has_yryby ? find_channel_index(&decoder, "BY") : -1;

                    if (y_idx < 0) {
                        exr_decoding_destroy(ctxt, &decoder);
                        decode_ok = false;
                        break;
                    }

                    for (int c = 0; c < decoder.channel_count; c++) {
                        decoder.channels[c].decode_to_ptr = NULL;
                    }

                    /* For tiled, start_x/start_y are tile indices (tilex, tiley); use pixel position. */
                    int32_t base_x = tilex * tilew;
                    int32_t base_y = tiley * tileh;

                    if (decoder.channels[y_idx].height > 0) {
                        int32_t y_row0 = base_y / y_samp_y;
                        int32_t y_col0 = base_x / y_samp_x;
                        decoder.channels[y_idx].decode_to_ptr = (uint8_t*)(y_buf + (size_t)y_row0 * y_width + (size_t)y_col0);
                        decoder.channels[y_idx].user_pixel_stride = 4;
                        decoder.channels[y_idx].user_line_stride = (int32_t)(y_width * sizeof(float));
                        decoder.channels[y_idx].user_bytes_per_element = 4;
                        decoder.channels[y_idx].user_data_type = EXR_PIXEL_FLOAT;
                    }

                    if (has_yryby && ry_idx >= 0 && decoder.channels[ry_idx].height > 0) {
                        int32_t ry_row0 = base_y / ry_samp_y;
                        int32_t ry_col0 = base_x / ry_samp_x;
                        decoder.channels[ry_idx].decode_to_ptr = (uint8_t*)(ry_buf + (size_t)ry_row0 * ry_width + (size_t)ry_col0);
                        decoder.channels[ry_idx].user_pixel_stride = 4;
                        decoder.channels[ry_idx].user_line_stride = (int32_t)(ry_width * sizeof(float));
                        decoder.channels[ry_idx].user_bytes_per_element = 4;
                        decoder.channels[ry_idx].user_data_type = EXR_PIXEL_FLOAT;
                    }

                    if (has_yryby && by_idx >= 0 && decoder.channels[by_idx].height > 0) {
                        int32_t by_row0 = base_y / by_samp_y;
                        int32_t by_col0 = base_x / by_samp_x;
                        decoder.channels[by_idx].decode_to_ptr = (uint8_t*)(by_buf + (size_t)by_row0 * by_width + (size_t)by_col0);
                        decoder.channels[by_idx].user_pixel_stride = 4;
                        decoder.channels[by_idx].user_line_stride = (int32_t)(by_width * sizeof(float));
                        decoder.channels[by_idx].user_bytes_per_element = 4;
                        decoder.channels[by_idx].user_data_type = EXR_PIXEL_FLOAT;
                    }

                    rv = exr_decoding_choose_default_routines(ctxt, 0, &decoder);
                    if (rv != EXR_ERR_SUCCESS) {
                        exr_decoding_destroy(ctxt, &decoder);
                        decode_ok = false;
                        break;
                    }

                    rv = exr_decoding_run(ctxt, 0, &decoder);
                    exr_decoding_destroy(ctxt, &decoder);
                    if (rv != EXR_ERR_SUCCESS) {
                        decode_ok = false;
                        break;
                    }
                }
            }
        }

        if (decode_ok) {
            /* Upsample and convert to RGB */
            if (has_yryby) {
                /* Upsample RY and BY if needed */
                float* ry_full = r_plane; /* Reuse r_plane as temp */
                float* by_full = g_plane; /* Reuse g_plane as temp */
                float* y_full = b_plane;  /* Reuse b_plane as temp */

                if (y_samp_x > 1 || y_samp_y > 1) {
                    upsample_chroma(y_buf, y_full, y_width, y_height, width, height, y_samp_x, y_samp_y);
                } else {
                    memcpy(y_full, y_buf, num_pixels * sizeof(float));
                }

                if (ry_samp_x > 1 || ry_samp_y > 1) {
                    upsample_chroma(ry_buf, ry_full, ry_width, ry_height, width, height, ry_samp_x, ry_samp_y);
                } else {
                    memcpy(ry_full, ry_buf, num_pixels * sizeof(float));
                }

                if (by_samp_x > 1 || by_samp_y > 1) {
                    upsample_chroma(by_buf, by_full, by_width, by_height, width, height, by_samp_x, by_samp_y);
                } else {
                    memcpy(by_full, by_buf, num_pixels * sizeof(float));
                }

                /* Convert Y/RY/BY to RGB using OpenEXR YCA formula (ImfRgbaYca.h):
                 * RY = (R-Y)/Y, BY = (B-Y)/Y  =>  R = Y*(1+RY), B = Y*(1+BY),
                 * G = (Y - R*yw.x - B*yw.z) / yw.y.  Use Rec.709 weights (default in OpenEXR). */
                const float yw_x = 0.2126f;
                const float yw_y = 0.7152f;
                const float yw_z = 0.0722f;
                for (size_t i = 0; i < num_pixels; i++) {
                    float Y = y_full[i];
                    float RY = ry_full[i];
                    float BY = by_full[i];
                    float r, g, b;
                    if (Y <= 0.f || (RY == 0.f && BY == 0.f)) {
                        r = g = b = (Y > 0.f) ? Y : 0.f;
                    } else {
                        r = Y * (1.f + RY);
                        b = Y * (1.f + BY);
                        g = (Y - r * yw_x - b * yw_z) / yw_y;
                    }
                    r_plane[i] = r < 0.f ? 0.f : (r > 1.f ? 1.f : r);
                    g_plane[i] = g < 0.f ? 0.f : (g > 1.f ? 1.f : g);
                    b_plane[i] = b < 0.f ? 0.f : (b > 1.f ? 1.f : b);
                }
            } else {
                /* Grayscale Y only */
                if (y_samp_x > 1 || y_samp_y > 1) {
                    upsample_chroma(y_buf, r_plane, y_width, y_height, width, height, y_samp_x, y_samp_y);
                } else {
                    memcpy(r_plane, y_buf, num_pixels * sizeof(float));
                }
                memcpy(g_plane, r_plane, num_pixels * sizeof(float));
                memcpy(b_plane, r_plane, num_pixels * sizeof(float));
            }
            /* Y/RY/BY has no alpha channel; ensure full opacity so no transparency is shown */
            for (size_t i = 0; i < num_pixels; i++) {
                a_plane[i] = 1.f;
            }
        }

        g_free(y_buf);
        g_free(ry_buf);
        g_free(by_buf);
    } else {
        decode_ok = false;
    }

    if (!decode_ok) {
        g_free(float_rgba);
        exr_finish(&ctxt);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Detect source color space from chromaticities (before closing context). If none, assume Linear Rec.709. */
    bool have_chromaticities = false;
    float chroma_white_x = 0.3127f, chroma_white_y = 0.3290f; /* D65 default */
    float chroma_red_x = 0.64f, chroma_red_y = 0.33f;
    float chroma_green_x = 0.30f, chroma_green_y = 0.60f;
    float chroma_blue_x = 0.15f, chroma_blue_y = 0.06f;
    {
        exr_attr_chromaticities_t chroma;
        if (exr_attr_get_chromaticities(ctxt, 0, "chromaticities", &chroma) == EXR_ERR_SUCCESS) {
            have_chromaticities = true;
            chroma_white_x = chroma.white_x;
            chroma_white_y = chroma.white_y;
            chroma_red_x = chroma.red_x;
            chroma_red_y = chroma.red_y;
            chroma_green_x = chroma.green_x;
            chroma_green_y = chroma.green_y;
            chroma_blue_x = chroma.blue_x;
            chroma_blue_y = chroma.blue_y;
        }
    }

    exr_finish(&ctxt);

#if HAVE_LCMS2
    /* Convert linear float from source primaries to linear sRGB before tone mapping (tone mapping must run in linear sRGB) */
    {
        ColorProfile* src_profile = NULL;
        if (have_chromaticities) {
            src_profile = cm_profile_create_linear_from_primaries(
                chroma_white_x, chroma_white_y,
                chroma_red_x, chroma_red_y,
                chroma_green_x, chroma_green_y,
                chroma_blue_x, chroma_blue_y);
        }
        if (src_profile) {
            size_t num_pixels_rgb = num_pixels * 3;
            float* interleaved = (float*)g_malloc(num_pixels_rgb * sizeof(float));
            if (interleaved) {
                for (size_t i = 0; i < num_pixels; i++) {
                    interleaved[i * 3 + 0] = r_plane[i];
                    interleaved[i * 3 + 1] = g_plane[i];
                    interleaved[i * 3 + 2] = b_plane[i];
                }
                cm_convert_hdr_linear_to_linear_srgb_from_profile(interleaved, num_pixels, src_profile);
                for (size_t i = 0; i < num_pixels; i++) {
                    r_plane[i] = interleaved[i * 3 + 0];
                    g_plane[i] = interleaved[i * 3 + 1];
                    b_plane[i] = interleaved[i * 3 + 2];
                }
                g_free(interleaved);
            }
            cm_profile_destroy(src_profile);
        }
        /* If no chromaticities or profile creation failed: assume Linear Rec.709, no conversion */
    }
#endif

    /* Build RGBE buffer for HDR tone mapping dialog */
    size_t rgbe_size = width * height * 4;
    uint8_t* rgbe_data = (uint8_t*)g_malloc(rgbe_size);
    if (!rgbe_data) {
        g_free(float_rgba);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t py = 0; py < height; py++) {
        for (uint32_t px = 0; px < width; px++) {
            size_t idx = (size_t)py * width + px;
            float r = r_plane[idx];
            float g = g_plane[idx];
            float b = b_plane[idx];
            uint8_t* out = rgbe_data + (idx * 4);
            float_rgb_to_rgbe(r, g, b, &out[0], &out[1], &out[2], &out[3]);
        }
    }

    /* Tone mapping parameters and dialog */
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
    if (ctx && ctx->window) {
        parent_window = GTK_WINDOW(ctx->window);
    }

    gboolean auto_apply = FALSE;
    gint dialog_response = GTK_RESPONSE_OK;
    if (!auto_apply_enabled) {
        dialog_response = hdr_image_dialog_show(parent_window, &tone_params, &auto_apply,
                                                rgbe_data, width, height, settings, app_dir);
        if (dialog_response != GTK_RESPONSE_OK) {
            g_free(rgbe_data);
            g_free(float_rgba);
            return PLUGIN_ERROR_USER_CANCELLED;
        }
    }

    bool has_alpha = false;
    size_t a_near_zero_count = 0;
    for (size_t i = 0; i < num_pixels; i++) {
        float a = a_plane[i];
        if (a < 0.999f)
            has_alpha = true;
        if (a < 0.01f)
            a_near_zero_count++;
    }

    /* If file has alpha but decoded alpha is almost all zeros, treat as invalid/unintended and force opaque */
    if (file_has_alpha && num_pixels > 0 && a_near_zero_count > (size_t)(0.95 * (double)num_pixels)) {
        for (size_t i = 0; i < num_pixels; i++)
            a_plane[i] = 1.f;
        has_alpha = false;
    }

    doc->width = width;
    doc->height = height;
    doc->channels = has_alpha ? 4 : 3;
    doc->bit_depth = 8;
    doc->has_alpha = has_alpha;

    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Always request ARGB32 so we can write 4 bytes per pixel (B,G,R,A); RGB24 would mismatch our write loop */
    ImageLayer* base_layer = layer_new("Background", doc->width, doc->height, TRUE,
                                       LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!base_layer) {
        g_free(rgbe_data);
        g_free(float_rgba);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_t* temp_surface = base_layer->surface;
    if (!temp_surface) {
        layer_free(base_layer);
        g_free(rgbe_data);
        g_free(float_rgba);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    cairo_surface_flush(temp_surface);
    guchar* surface_data = cairo_image_surface_get_data(temp_surface);
    int surface_stride = cairo_image_surface_get_stride(temp_surface);
    if (!surface_data) {
        layer_free(base_layer);
        g_free(rgbe_data);
        g_free(float_rgba);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t py = 0; py < height; py++) {
        guchar* dst_row = surface_data + (size_t)py * surface_stride;
        for (uint32_t px = 0; px < width; px++) {
            size_t idx = (size_t)py * width + px;
            float r = r_plane[idx];
            float g = g_plane[idx];
            float b = b_plane[idx];
            float a = a_plane[idx];

            uint8_t r8, g8, b8;
            tone_map_rgb(r, g, b, &tone_params, &r8, &g8, &b8);
            uint8_t a8 = has_alpha ? (uint8_t)(a * 255.f) : 255;

            /* Premultiply for Cairo blending */
            dst_row[px * 4 + 0] = (guchar)((b8 * (uint32_t)a8 + 127) / 255);
            dst_row[px * 4 + 1] = (guchar)((g8 * (uint32_t)a8 + 127) / 255);
            dst_row[px * 4 + 2] = (guchar)((r8 * (uint32_t)a8 + 127) / 255);
            dst_row[px * 4 + 3] = (guchar)a8;
        }
    }

    cairo_surface_mark_dirty(temp_surface);

    g_free(rgbe_data);
    g_free(float_rgba);

    doc->layers = g_list_append(doc->layers, base_layer);
    document_render_composite(doc);

    return PLUGIN_ERROR_NONE;
}

bool plugin_init_exr(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
    (void)host;

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "EXR - OpenEXR";
    out_plugin->format_info.extensions = "exr";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.supports_hdr = true;
    out_plugin->format_info.priority = 75;

    out_plugin->callbacks.can_load = can_load_exr;
    out_plugin->callbacks.load = load_exr;
    out_plugin->callbacks.can_save = can_save_exr;
    out_plugin->callbacks.save = NULL;

    return true;
}

#endif /* HAVE_OPENEXR */