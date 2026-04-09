/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PLUGIN_RLI_H
#define PLUGIN_RLI_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize RLI (Rasterlab Image) plugin
 *
 * The RLI format is a multi-layer, chunk-based, lossless image file format
 * that preserves pixel layers, text layers, ICC color profiles, and metadata.
 * Pixel data is tile-encoded (64x64 tiles) with QOI-inspired delta/run-length
 * ops and LZ4 compression.
 */
bool plugin_init_rli(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

/* -------- RLI Format Version -------- */

#define RLI_FORMAT_VERSION 1

/* -------- Chunk Type IDs (little-endian 4CC) -------- */

#define RLI_CHUNK_RLIB 0x42494C52u /* "RLIB" - file header */
#define RLI_CHUNK_ICCP 0x50434349u /* "ICCP" - ICC color profile */
#define RLI_CHUNK_EXIF 0x46495845u /* "EXIF" - EXIF metadata */
#define RLI_CHUNK_XMP 0x20504D58u /* "XMP " - XMP metadata */
#define RLI_CHUNK_LAYR 0x5259414Cu /* "LAYR" - layer header */
#define RLI_CHUNK_LPIX 0x5849504Cu /* "LPIX" - raster pixel data */
#define RLI_CHUNK_LTXT 0x5458544Cu /* "LTXT" - text layer data */
#define RLI_CHUNK_REND 0x444E4552u /* "REND" - end-of-file marker */

/* -------- Pixel Format Tags (stored in LPIX header byte) -------- */

#define RLI_PIXFMT_BGRA_PREMUL 0x03 /* Cairo ARGB32 native: BGRA premultiplied */

/* -------- Layer Type IDs (matches LayerType enum in document.h) -------- */

#define RLI_LAYER_TYPE_RASTER 0
#define RLI_LAYER_TYPE_TEXT 1

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_RLI_H */
