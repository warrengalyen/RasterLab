/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef BLEND_H
#define BLEND_H

/**
 * @file blend.h
 * @brief SIMD-optimized blend mode implementations for tile compositing
 * 
 * This module provides high-performance blend mode functions using SSE2 SIMD
 * instructions via the SIMDe library for cross-platform compatibility.
 * 
 * Supports all 27 Photoshop-compatible blend modes:
 * 
 * Normal modes:
 *   - Normal (OVER): out = src + dst * (1 - src_a)
 *   - Dissolve: random dithering based on opacity
 * 
 * Darken modes:
 *   - Darken: blend = min(src, dst)
 *   - Multiply: blend = src * dst / 255
 *   - Color Burn: blend = 255 - min(255, (255-dst)*255/src)
 *   - Linear Burn: blend = max(0, src + dst - 255)
 *   - Darker Color: compare luminance, pick darker pixel
 * 
 * Lighten modes:
 *   - Lighten: blend = max(src, dst)
 *   - Screen: blend = src + dst - src * dst / 255
 *   - Color Dodge: blend = min(255, dst*255/(255-src))
 *   - Linear Dodge (Add): blend = min(255, src + dst)
 *   - Lighter Color: compare luminance, pick lighter pixel
 * 
 * Contrast modes:
 *   - Overlay: 2*src*dst/255 or 255-2*(255-src)*(255-dst)/255
 *   - Soft Light: Pegtop formula approximation
 *   - Hard Light: Overlay with src/dst swapped
 *   - Vivid Light: Color Burn or Color Dodge based on src
 *   - Linear Light: Linear Burn or Linear Dodge based on src
 *   - Pin Light: Darken or Lighten based on src
 *   - Hard Mix: threshold at src + dst >= 255
 * 
 * Inversion modes:
 *   - Difference: blend = |src - dst|
 *   - Exclusion: blend = src + dst - 2*src*dst/255
 * 
 * Cancellation modes:
 *   - Subtract: blend = max(0, dst - src)
 *   - Divide: blend = min(255, dst*255/src)
 * 
 * Component (HSL) modes:
 *   - Hue: take hue from src, sat/lum from dst
 *   - Saturation: take saturation from src, hue/lum from dst
 *   - Color: take hue+sat from src, lum from dst
 *   - Luminosity: take luminosity from src, hue+sat from dst
 * 
 * SIMD path processes 4 ARGB32 pixels at once using 128-bit SSE2 registers.
 * Scalar fallback handles remaining pixels and complex HSL calculations.
 * Pixel format: 0xAARRGGBB (premultiplied alpha)
 */

#include "document.h"
#include <glib.h>

G_BEGIN_DECLS

/**
 * Composite a row of pixels using SIMD with blend mode support
 * Processes 4 pixels at a time, with scalar fallback for remaining pixels
 * 
 * @param src_row Source pixel row (layer pixels)
 * @param dst_row Destination pixel row (tile pixels)
 * @param width Number of pixels to composite
 * @param row_x X coordinate of first pixel in document space (for Dissolve)
 * @param row_y Y coordinate of this row in document space (for Dissolve)
 * @param layer_opacity Layer opacity (0-255)
 * @param blend_mode Layer blend mode (from BlendMode enum)
 */
void blend_composite_row(const guint32* src_row, guint32* dst_row,
                         gint width, gint row_x, gint row_y, guint8 layer_opacity, BlendMode blend_mode);

/**
 * Composite a row of pixels using SIMD (OVER blend only)
 * Optimized fast path for Normal blend mode
 * Processes 4 pixels at a time, with scalar fallback for remaining pixels
 * 
 * @param src_row Source pixel row (layer pixels)
 * @param dst_row Destination pixel row (tile pixels)
 * @param width Number of pixels to composite
 * @param layer_opacity Layer opacity (0-255)
 */
void blend_composite_row_over(const guint32* src_row, guint32* dst_row,
                              gint width, guint8 layer_opacity);

G_END_DECLS

#endif /* BLEND_H */
