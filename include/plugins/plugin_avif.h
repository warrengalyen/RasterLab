/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_AVIF_H
#define PLUGIN_AVIF_H

#include "image_format_plugin.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AVIF-specific save options
 * Use with SaveOptions.plugin_data when saving AVIF/AVIFS files.
 */
typedef struct {
    /* Quality (0-63, higher = better). Default 63. */
    int quality;
    uint32_t reserved[3];
} AVIFSaveOptions;

/**
 * Initialize AVIF plugin using libheif + libaom
 * Supports AVIF (AV1) load and save.
 */
bool plugin_init_avif(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_AVIF_H */
