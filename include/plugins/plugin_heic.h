#ifndef PLUGIN_HEIC_H
#define PLUGIN_HEIC_H

#include "image_format_plugin.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HEIC-specific save options
 * Use with SaveOptions.plugin_data when saving HEIC/HEIF files.
 */
typedef struct {
    /* Quality: lossless or lossy. Default true (lossless). */
    bool lossless;

    /* Quality for lossy mode (0-100). Default 90. */
    int quality;

    /* Image format: false = single frame (composited), true = multiframe (one frame per layer) */
    bool multiframe;

    uint32_t reserved[2];
} HEICSaveOptions;

/**
 * Initialize HEIC plugin using libheif
 * Supports HEIC (HEVC)
 */
bool plugin_init_heic(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_HEIC_H */
