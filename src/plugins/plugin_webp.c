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

#ifdef HAVE_LIBWEBP
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>
#if defined(HAVE_LCMS2)
#include "color_manager/icc_utils.h"
#endif
#if defined(HAVE_LCMS2)
#include "color_manager.h"
#include "color_manager/icc_utils.h"
#include "debug_logger.h"
#endif

/**
 * WebP compression method
 */
typedef enum {
    WEBP_COMPRESSION_FAST = 0,     /* Fast compression (method 0) */
    WEBP_COMPRESSION_BALANCED = 1, /* Balanced compression (method 3) */
    WEBP_COMPRESSION_BEST = 2      /* Best compression (method 6) */
} WebPCompressionMethod;

/**
 * WebP-specific save options
 */
typedef struct {
    /* Image type hint (WebPImageHint parameter) */
    WebPImageHint image_hint;

    /* Quality setting (0-100, where 0 = lowest quality, 100 = highest quality) */
    int32_t quality;

    /* Compression method (fast, balanced, best) */
    WebPCompressionMethod compression_method;

    /* Reserved for future use */
    uint32_t reserved[2];
} WebPSaveOptions;

/* WebP file signature: "RIFF" followed by file size and "WEBP" */
static const uint8_t WEBP_RIFF_SIGNATURE[4] = {'R', 'I', 'F', 'F'};
static const uint8_t WEBP_WEBP_SIGNATURE[4] = {'W', 'E', 'B', 'P'};

#else
/* WebP file signature (for can_load even when libwebp not available) */
static const uint8_t WEBP_RIFF_SIGNATURE[4] = {'R', 'I', 'F', 'F'};
static const uint8_t WEBP_WEBP_SIGNATURE[4] = {'W', 'E', 'B', 'P'};
#endif

/**
 * Check if file is WebP format
 */
static bool can_load_webp(const char* filename, const uint8_t* header, size_t header_size) {
    (void)filename; /* Unused */

    if (!header || header_size < 12) {
        return false;
    }

    /* Check for RIFF signature */
    if (memcmp(header, WEBP_RIFF_SIGNATURE, 4) != 0) {
        return false;
    }

    /* Check for WEBP signature at offset 8 */
    if (memcmp(header + 8, WEBP_WEBP_SIGNATURE, 4) != 0) {
        return false;
    }

    return true;
}

#ifdef HAVE_LIBWEBP
/**
 * Helper function to convert RGBA data to Cairo ARGB32 format
 * and copy it to a layer surface
 */
static bool convert_rgba_to_layer(uint8_t* rgba_data, int rgba_width, int rgba_height,
                                  cairo_surface_t* surface, int surface_stride) {
    guchar* surface_data = cairo_image_surface_get_data(surface);
    if (!surface_data) {
        return false;
    }

    /* Convert RGBA to ARGB32 (Cairo format) */
    /* WebP: R, G, B, A (byte order) */
    /* Cairo ARGB32: A, R, G, B (byte order, pre-multiplied alpha) */
    for (int y = 0; y < rgba_height; y++) {
        uint8_t* webp_row = rgba_data + y * rgba_width * 4;
        uint32_t* cairo_row = (uint32_t*)(surface_data + y * surface_stride);

        for (int x = 0; x < rgba_width; x++) {
            uint8_t r = webp_row[x * 4 + 0];
            uint8_t g = webp_row[x * 4 + 1];
            uint8_t b = webp_row[x * 4 + 2];
            uint8_t a = webp_row[x * 4 + 3];

            /* Pre-multiply alpha for Cairo */
            if (a == 255) {
                /* No pre-multiplication needed for opaque pixels */
                cairo_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            } else if (a == 0) {
                /* Fully transparent */
                cairo_row[x] = 0;
            } else {
                /* Pre-multiply alpha */
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
                cairo_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    cairo_surface_mark_dirty(surface);
    return true;
}

/**
 * Load WebP image using libwebp (supports both static and animated)
 */
static PluginError load_webp(ImageDocument* doc, const char* filename) {
    FILE* infile;
    uint8_t* file_data = NULL;
    size_t file_size;
    size_t bytes_read;
    WebPDemuxer* demux = NULL;
    WebPData webp_data;
    uint32_t canvas_width, canvas_height;
    uint32_t frame_count;
    WebPIterator iter;
    int frame_num;
    ImageLayer* layer = NULL;
    char layer_name[64];
    bool is_animated = false;

    if (!doc || !filename) {
        g_warning("WebP plugin: Invalid parameters (doc=%p, filename=%p)", doc, filename);
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Open WebP file */
    infile = g_fopen(filename, "rb");
    if (!infile) {
        g_warning("WebP plugin: Failed to open file: %s", filename);
        return PLUGIN_ERROR_FILE_NOT_FOUND;
    }

    /* Get file size */
    fseek(infile, 0, SEEK_END);
    file_size = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    if (file_size == 0 || file_size > 100 * 1024 * 1024) { /* Max 100MB */
        g_warning("WebP plugin: Invalid file size: %zu", file_size);
        fclose(infile);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Read entire file into memory */
    file_data = g_malloc(file_size);
    if (!file_data) {
        g_warning("WebP plugin: Failed to allocate memory for file data");
        fclose(infile);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    bytes_read = fread(file_data, 1, file_size, infile);
    fclose(infile);

    if (bytes_read != file_size) {
        g_warning("WebP plugin: Failed to read entire file (read %zu of %zu bytes)", bytes_read, file_size);
        g_free(file_data);
        return PLUGIN_ERROR_FILE_READ_ERROR;
    }

    /* Set up WebPData structure */
    webp_data.bytes = file_data;
    webp_data.size = file_size;

    /* Create demuxer to check if file is animated */
    demux = WebPDemux(&webp_data);
    if (!demux) {
        g_warning("WebP plugin: Failed to create demuxer");
        g_free(file_data);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Get canvas dimensions */
    canvas_width = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    canvas_height = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    frame_count = WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);

#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
    /* WebP: libwebp demux API. Extract ICC chunk from container (ICCP chunk).
     * Pass raw data to icc_profile_from_memory(). Fall back to NULL profile (sRGB) on any failure. */
    {
        WebPChunkIterator chunk_iter;
        if (WebPDemuxGetChunk(demux, "ICCP", 1, &chunk_iter)) {
            size_t icc_size = (size_t)chunk_iter.chunk.size;
            const void* icc_bytes = chunk_iter.chunk.bytes;
            if (icc_bytes != NULL && icc_size > 0) {
                cmsHPROFILE profile = icc_profile_from_memory(icc_bytes, icc_size);
                WebPDemuxReleaseChunkIterator(&chunk_iter);
                if (profile) {
                    ImageFormatHostAPI* api = plugin_host_api_get();
                    if (api && api->document_set_load_icc_profile) {
                        if (api->get_use_embedded_icc && !api->get_use_embedded_icc())
                            icc_destroy(profile);
                        else {
                            debug_log("DBG", "WebP: embedded ICC profile found, will convert to sRGB");
                            api->document_set_load_icc_profile(doc, profile);
                        }
                    } else {
                        icc_destroy(profile);
                    }
                } else {
                    g_warning("WebP plugin: Invalid or non-RGB embedded ICC profile; assuming sRGB");
                }
            } else {
                WebPDemuxReleaseChunkIterator(&chunk_iter);
            }
        }
        /* No ICCP chunk or malformed */
    }
#endif

    if (canvas_width <= 0 || canvas_height <= 0 || canvas_width > 65535 || canvas_height > 65535) {
        g_warning("WebP plugin: Invalid canvas dimensions: %ux%u", canvas_width, canvas_height);
        WebPDemuxDelete(demux);
        g_free(file_data);
        return PLUGIN_ERROR_UNSUPPORTED_FORMAT;
    }

    /* Check if animated (more than 1 frame) */
    is_animated = (frame_count > 1);

    /* Set document metadata */
    doc->width = canvas_width;
    doc->height = canvas_height;
    doc->channels = 4; /* RGBA */
    doc->bit_depth = 8;
    doc->has_alpha = true; /* WebP always supports alpha */

    /* Free old layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    if (is_animated) {
        /* Load animated WebP - create a layer for each frame */
        frame_num = 1;
        if (WebPDemuxGetFrame(demux, frame_num, &iter)) {
            do {
                uint8_t* decoded_data = NULL;
                int frame_width, frame_height;
                cairo_surface_t* temp_surface;
                int surface_stride;

                /* Decode frame */
                decoded_data = WebPDecodeRGBA(iter.fragment.bytes, iter.fragment.size, &frame_width, &frame_height);
                if (!decoded_data) {
                    g_warning("WebP plugin: Failed to decode frame %d", frame_num);
                    continue;
                }

                /* Create layer name */
                g_snprintf(layer_name, sizeof(layer_name), "Frame %d", frame_num);

                /* Create layer with canvas dimensions (frames may be smaller) */
                layer = layer_new(layer_name, doc->width, doc->height, TRUE,
                                  LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
                if (!layer) {
                    g_warning("WebP plugin: Failed to create layer for frame %d", frame_num);
                    WebPFree(decoded_data);
                    continue;
                }

                /* Get surface data */
                temp_surface = layer->surface;
                if (!temp_surface) {
                    g_warning("WebP plugin: layer->surface is NULL for frame %d", frame_num);
                    layer_free(layer);
                    WebPFree(decoded_data);
                    continue;
                }

                cairo_surface_flush(temp_surface);
                surface_stride = cairo_image_surface_get_stride(temp_surface);

                /* If frame has offset or different size, we need to handle it */
                if (iter.x_offset == 0 && iter.y_offset == 0 &&
                    frame_width == (int)doc->width && frame_height == (int)doc->height) {
                    /* Frame matches canvas exactly - simple copy */
                    if (!convert_rgba_to_layer(decoded_data, frame_width, frame_height,
                                               temp_surface, surface_stride)) {
                        g_warning("WebP plugin: Failed to convert frame %d", frame_num);
                        layer_free(layer);
                        WebPFree(decoded_data);
                        continue;
                    }
                } else {
                    /* Frame has offset or different size - need to composite */
                    /* Convert RGBA to ARGB32 first */
                    uint8_t* argb_data = g_malloc(frame_width * frame_height * 4);
                    if (argb_data) {
                        for (int y = 0; y < frame_height; y++) {
                            uint8_t* rgba_row = decoded_data + y * frame_width * 4;
                            uint32_t* argb_row = (uint32_t*)(argb_data + y * frame_width * 4);
                            for (int x = 0; x < frame_width; x++) {
                                uint8_t r = rgba_row[x * 4 + 0];
                                uint8_t g = rgba_row[x * 4 + 1];
                                uint8_t b = rgba_row[x * 4 + 2];
                                uint8_t a = rgba_row[x * 4 + 3];
                                if (a == 255) {
                                    argb_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
                                } else if (a == 0) {
                                    argb_row[x] = 0;
                                } else {
                                    r = (r * a + 127) / 255;
                                    g = (g * a + 127) / 255;
                                    b = (b * a + 127) / 255;
                                    argb_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
                                }
                            }
                        }
                        /* Create a temporary surface for the frame */
                        cairo_surface_t* frame_surface = cairo_image_surface_create_for_data(
                            argb_data, CAIRO_FORMAT_ARGB32, frame_width, frame_height,
                            frame_width * 4);
                        if (frame_surface && cairo_surface_status(frame_surface) == CAIRO_STATUS_SUCCESS) {
                            cairo_t* cr = cairo_create(temp_surface);
                            if (cr) {
                                /* Clear to transparent */
                                cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                                cairo_paint(cr);
                                /* Draw frame at offset */
                                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                                cairo_set_source_surface(cr, frame_surface, iter.x_offset, iter.y_offset);
                                cairo_paint(cr);
                                cairo_destroy(cr);
                            }
                            cairo_surface_destroy(frame_surface);
                        } else {
                            if (frame_surface) {
                                cairo_surface_destroy(frame_surface);
                            }
                        }
                        g_free(argb_data);
                    }
                }

                /* Add layer to document */
                doc->layers = g_list_append(doc->layers, layer);
                layer = NULL; /* Don't free it, it's in the list now */

                WebPFree(decoded_data);
                frame_num++;
            } while (WebPDemuxNextFrame(&iter));

            WebPDemuxReleaseIterator(&iter);
        }

        if (doc->layers == NULL) {
            g_warning("WebP plugin: Failed to load any frames from animated WebP");
            WebPDemuxDelete(demux);
            g_free(file_data);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }
    } else {
        /* Load static WebP - single frame */
        uint8_t* decoded_data = NULL;
        int width, height;

        /* Decode WebP image to RGBA */
        decoded_data = WebPDecodeRGBA(file_data, file_size, &width, &height);
        if (!decoded_data) {
            g_warning("WebP plugin: Failed to decode WebP image");
            WebPDemuxDelete(demux);
            g_free(file_data);
            return PLUGIN_ERROR_FILE_READ_ERROR;
        }

        /* Create base layer */
        layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                          LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
        if (!layer) {
            g_warning("WebP plugin: layer_new returned NULL for %ux%u layer", doc->width, doc->height);
            WebPFree(decoded_data);
            WebPDemuxDelete(demux);
            g_free(file_data);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Get surface data */
        cairo_surface_t* temp_surface = layer->surface;
        if (!temp_surface) {
            g_warning("WebP plugin: base_layer->surface is NULL");
            layer_free(layer);
            WebPFree(decoded_data);
            WebPDemuxDelete(demux);
            g_free(file_data);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        cairo_surface_flush(temp_surface);
        int surface_stride = cairo_image_surface_get_stride(temp_surface);

        /* Convert and copy RGBA data to layer */
        if (!convert_rgba_to_layer(decoded_data, width, height, temp_surface, surface_stride)) {
            g_warning("WebP plugin: Failed to convert RGBA to layer");
            layer_free(layer);
            WebPFree(decoded_data);
            WebPDemuxDelete(demux);
            g_free(file_data);
            return PLUGIN_ERROR_OUT_OF_MEMORY;
        }

        /* Add layer to document */
        doc->layers = g_list_append(doc->layers, layer);

        WebPFree(decoded_data);
    }

    /* Cleanup */
    WebPDemuxDelete(demux);
    g_free(file_data);

    return PLUGIN_ERROR_NONE;
}

/**
 * Save WebP image using libwebp
 */
static PluginError save_webp(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    cairo_surface_t* composite;
    guchar* surface_data;
    int surface_stride;
    uint8_t* rgba_data = NULL;
    uint8_t* webp_data = NULL;
    size_t webp_size;
    FILE* outfile;
    WebPSaveOptions* webp_opts = NULL;
    WebPConfig config;
    WebPPicture picture;
    int width, height;
    int result;
#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
    WebPData assembled_webp = {NULL, 0};
#endif

    if (!doc || !filename) {
        return PLUGIN_ERROR_INVALID_PARAMETERS;
    }

    /* Get composite surface */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    cairo_surface_flush(composite);
    surface_data = cairo_image_surface_get_data(composite);
    surface_stride = cairo_image_surface_get_stride(composite);

#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
    if (opts && opts->preserve_icc_profile && doc->original_icc_data && doc->original_icc_size > 0) {
        int cms_intent = plugin_host_api_get_cm_rendering_intent();
        bool cms_bpc = plugin_host_api_get_cm_bpc();
        if (surface_stride == (int)(doc->width * 4)) {
            cm_convert_srgb_argb32_to_profile(surface_data, (size_t)(doc->width * doc->height),
                                               doc->original_icc_data, doc->original_icc_size,
                                               cms_intent, cms_bpc);
        } else {
            for (guint y = 0; y < doc->height; y++) {
                uint8_t* row = surface_data + y * surface_stride;
                cm_convert_srgb_argb32_to_profile(row, doc->width,
                                                   doc->original_icc_data, doc->original_icc_size,
                                                   cms_intent, cms_bpc);
            }
        }
    }
#endif
    width = cairo_image_surface_get_width(composite);
    height = cairo_image_surface_get_height(composite);

    if (!surface_data || width <= 0 || height <= 0) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Get WebP-specific options */
    if (opts && opts->plugin_data) {
        webp_opts = (WebPSaveOptions*)opts->plugin_data;
    }

    /* Initialize WebP config with defaults */
    if (!WebPConfigInit(&config)) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_UNKNOWN;
    }

    /* Set quality */
    if (webp_opts && webp_opts->quality >= 0 && webp_opts->quality <= 100) {
        config.quality = (float)webp_opts->quality;
    } else if (opts && opts->quality >= 0 && opts->quality <= 100) {
        config.quality = (float)opts->quality;
    } else {
        config.quality = 75.0f; /* Default quality */
    }

    /* Set compression method (maps to WebPConfig.method) */
    if (webp_opts) {
        switch (webp_opts->compression_method) {
            case WEBP_COMPRESSION_FAST:
                config.method = 0;
                break;
            case WEBP_COMPRESSION_BALANCED:
                config.method = 3;
                break;
            case WEBP_COMPRESSION_BEST:
                config.method = 6;
                break;
            default:
                config.method = 3; /* Default to balanced */
                break;
        }
    } else {
        config.method = 3; /* Default to balanced */
    }

    /* Set image hint */
    if (webp_opts) {
        config.image_hint = webp_opts->image_hint;
    } else {
        config.image_hint = WEBP_HINT_DEFAULT;
    }

    /* Validate config */
    if (!WebPValidateConfig(&config)) {
        g_warning("WebP plugin: Invalid WebP config");
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_UNKNOWN;
    }

    /* Allocate RGBA buffer */
    rgba_data = g_malloc(width * height * 4);
    if (!rgba_data) {
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Convert ARGB32 (Cairo format) to RGBA (WebP format) */
    /* Cairo ARGB32: A, R, G, B (byte order, pre-multiplied alpha) */
    /* WebP: R, G, B, A (byte order) */
    for (int y = 0; y < height; y++) {
        uint32_t* cairo_row = (uint32_t*)(surface_data + y * surface_stride);
        uint8_t* rgba_row = rgba_data + y * width * 4;

        for (int x = 0; x < width; x++) {
            uint32_t pixel = cairo_row[x];
            uint8_t a = (pixel >> 24) & 0xFF;
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;

            /* Un-pre-multiply alpha if needed */
            if (a != 0 && a != 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
            }

            rgba_row[x * 4 + 0] = r;
            rgba_row[x * 4 + 1] = g;
            rgba_row[x * 4 + 2] = b;
            rgba_row[x * 4 + 3] = a;
        }
    }

    /* Initialize WebP picture */
    if (!WebPPictureInit(&picture)) {
        g_free(rgba_data);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_UNKNOWN;
    }

    picture.width = width;
    picture.height = height;
    picture.use_argb = 0; /* Use RGBA format */

    /* Import RGBA data */
    if (!WebPPictureImportRGBA(&picture, rgba_data, width * 4)) {
        g_warning("WebP plugin: WebPPictureImportRGBA failed");
        g_free(rgba_data);
        WebPPictureFree(&picture);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_UNKNOWN;
    }

    /* Encode WebP image */
    webp_size = 0;
    webp_data = NULL;

    /* Use memory writer to get encoded data */
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    if (!WebPEncode(&config, &picture)) {
        g_warning("WebP plugin: WebPEncode failed (error code: %d)", picture.error_code);
        WebPMemoryWriterClear(&writer);
        g_free(rgba_data);
        WebPPictureFree(&picture);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    webp_data = writer.mem;
    webp_size = writer.size;

#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
    {
        const void* embed_icc = NULL;
        size_t embed_icc_size = 0;
        void* alloc_icc = NULL;
        if (opts && opts->preserve_icc_profile && doc->original_icc_data && doc->original_icc_size > 0) {
            embed_icc = doc->original_icc_data;
            embed_icc_size = doc->original_icc_size;
        } else {
            cmsHPROFILE srgb = icc_create_srgb_profile();
            if (srgb && icc_profile_to_memory(srgb, &alloc_icc, &embed_icc_size) && alloc_icc && embed_icc_size > 0) {
                icc_destroy(srgb);
                embed_icc = alloc_icc;
            } else {
                if (srgb) icc_destroy(srgb);
                if (alloc_icc) free(alloc_icc);
                embed_icc_size = 0;
            }
        }
        if (embed_icc && embed_icc_size > 0) {
            WebPData encoded = {webp_data, webp_size};
            WebPMux* mux = WebPMuxCreate(&encoded, 0);
            if (mux) {
                WebPData icc = {(const uint8_t*)embed_icc, embed_icc_size};
                if (WebPMuxSetChunk(mux, "ICCP", &icc, 1) == WEBP_MUX_OK) {
                    if (WebPMuxAssemble(mux, &assembled_webp) == WEBP_MUX_OK) {
                        webp_data = assembled_webp.bytes;
                        webp_size = assembled_webp.size;
                    }
                }
                WebPMuxDelete(mux);
            }
        }
        if (alloc_icc) free(alloc_icc);
    }
#endif

    /* Write to file */
    outfile = g_fopen(filename, "wb");
    if (!outfile) {
        g_warning("WebP plugin: Failed to open file for writing: %s", filename);
#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
        if (assembled_webp.bytes)
            WebPDataClear(&assembled_webp);
        else
#endif
            WebPMemoryWriterClear(&writer);
        g_free(rgba_data);
        WebPPictureFree(&picture);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    result = fwrite(webp_data, 1, webp_size, outfile);
    fclose(outfile);

    if (result != (int)webp_size) {
        g_warning("WebP plugin: Failed to write entire file (wrote %d of %zu bytes)", result, webp_size);
#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
        if (assembled_webp.bytes)
            WebPDataClear(&assembled_webp);
        else
#endif
            WebPMemoryWriterClear(&writer);
        g_free(rgba_data);
        WebPPictureFree(&picture);
        cairo_surface_destroy(composite);
        return PLUGIN_ERROR_FILE_WRITE_ERROR;
    }

    /* Cleanup */
#if defined(HAVE_LIBWEBP) && defined(HAVE_LCMS2)
    if (assembled_webp.bytes)
        WebPDataClear(&assembled_webp);
    else
#endif
        WebPMemoryWriterClear(&writer);
    g_free(rgba_data);
    WebPPictureFree(&picture);
    cairo_surface_destroy(composite);

    /* Note: Animated WebP support will be added at a later time */
    return PLUGIN_ERROR_NONE;
}

#else
/**
 * Load WebP image (stub when libwebp is not available)
 */
static PluginError load_webp(ImageDocument* doc, const char* filename) {
    (void)doc;
    (void)filename;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}

/**
 * Save WebP image (stub when libwebp is not available)
 */
static PluginError save_webp(ImageDocument* doc, const char* filename, const SaveOptions* opts) {
    (void)doc;
    (void)filename;
    (void)opts;
    return PLUGIN_ERROR_UNSUPPORTED_FEATURE;
}
#endif

/**
 * Check if we can save as WebP
 */
static bool can_save_webp(const char* filename) {
    if (!filename) {
        return false;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return g_ascii_strcasecmp(ext + 1, "webp") == 0;
}

#ifdef HAVE_LIBWEBP
/**
 * Get size of WebP-specific save options structure
 */
static size_t get_webp_save_options_size(void) {
    return sizeof(WebPSaveOptions);
}

/**
 * Initialize WebP-specific save options with defaults
 */
static void init_webp_save_options(void* plugin_data) {
    WebPSaveOptions* opts = (WebPSaveOptions*)plugin_data;
    if (opts) {
        opts->image_hint = WEBP_HINT_DEFAULT;
        opts->quality = 100;                                  /* Default quality */
        opts->compression_method = WEBP_COMPRESSION_BALANCED; /* Default balanced */
        memset(opts->reserved, 0, sizeof(opts->reserved));
    }
}
#else
/**
 * Get size of WebP-specific save options structure (stub when libwebp not available)
 */
static size_t get_webp_save_options_size(void) {
    return 0;
}

/**
 * Initialize WebP-specific save options with defaults (stub when libwebp not available)
 */
static void init_webp_save_options(void* plugin_data) {
    (void)plugin_data;
}
#endif

/**
 * WebP plugin initialization
 */
bool plugin_init_webp(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin) {
#ifdef HAVE_LIBWEBP
    (void)host; /* Host API not needed for this simple plugin */

    if (!out_plugin) {
        return false;
    }

    memset(out_plugin, 0, sizeof(ImageFormatPlugin));

    out_plugin->plugin_version = 1;
    out_plugin->format_info.name = "WebP - Google WebP Image";
    out_plugin->format_info.extensions = "webp";
    out_plugin->format_info.supports_alpha = true;
    out_plugin->format_info.supports_layers = false;
    out_plugin->format_info.priority = 100;

    out_plugin->callbacks.can_load = can_load_webp;
    out_plugin->callbacks.load = load_webp;
    out_plugin->callbacks.can_save = can_save_webp;
    out_plugin->callbacks.save = save_webp;
    out_plugin->callbacks.get_save_options_size = get_webp_save_options_size;
    out_plugin->callbacks.init_save_options = init_webp_save_options;

    return true;
#else
    (void)host;
    (void)out_plugin;
    return false;
#endif
}
