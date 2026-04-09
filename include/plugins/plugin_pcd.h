/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_PCD_H
#define PLUGIN_PCD_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize PCD plugin (Kodak Photo CD format)
 * 
 * Features:
 * - All 6 PhotoCD resolutions (Overview through 16Base)
 * - Automatic orientation detection and correction
 * - Huffman decoding for higher resolutions
 * - Resolution selection dialog
 */
bool plugin_init_pcd(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_PCD_H */
