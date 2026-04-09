/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_DICOM_H
#define PLUGIN_DICOM_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize DICOM (Digital Imaging and Communications in Medicine) plugin.
 * Supports loading native (uncompressed) and RLE-compressed DICOM images:
 * monochrome, RGB, palette color, YBR, and multi-frame.
 */
bool plugin_init_dicom(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_DICOM_H */
