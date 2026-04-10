/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_JXL_H
#define PLUGIN_JXL_H

#include "image_format_plugin.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * JPEG XL-specific save options.
 * Use with SaveOptions.plugin_data when saving .jxl files.
 */
typedef struct {
    /* Quality mode: true = lossless, false = lossy. Default true. */
    bool lossless;

    /* Quality for lossy mode (0-100). Default 90. */
    int quality;

    /* Compression effort for lossless mode (1-9). Default 7. */
    int effort;

    uint32_t reserved[2];
} JXLSaveOptions;

/**
 * Initialize the JPEG XL plugin using libjxl.
 */
bool plugin_init_jxl(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_JXL_H */
