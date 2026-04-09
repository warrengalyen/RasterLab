/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_EXR_H
#define PLUGIN_EXR_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the EXR image format plugin (OpenEXR).
 * Only available when built with HAVE_OPENEXR.
 *
 * @param host Host API (can be NULL)
 * @param out_plugin Output plugin structure to fill
 * @return true if initialization succeeded, false otherwise
 */
bool plugin_init_exr(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_EXR_H */
