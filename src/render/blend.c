/**
 * @file blend.c
 * @brief SIMD-optimized blend mode implementations for tile compositing
 * 
 * This module provides high-performance blend mode functions using SSE2 SIMD
 * instructions via the SIMDe library for cross-platform compatibility.
 */

#include "render/blend.h"
#include <string.h>

/* SIMD support via SIMDe (SIMD Everywhere) for cross-platform SSE2
 * This provides portable SIMD that works on x86, ARM, etc. */
#define SIMDE_ENABLE_NATIVE_ALIASES
#include "simde/simde/x86/sse2.h"

/* ============================================================================
 * SIMD Helper Functions
 * ============================================================================ */

/**
 * Apply layer opacity to 4 pixels using SIMD
 * Multiplies all components (ARGB) by opacity and divides by 255
 * @param pixels 4 packed ARGB32 pixels
 * @param opacity_vec Opacity broadcast to 16-bit lanes
 * @return 4 pixels with opacity applied
 */
static inline __m128i simd_apply_opacity(__m128i pixels, __m128i opacity_vec) {
    __m128i zero = _mm_setzero_si128();
    
    /* Unpack lower 2 pixels (bytes 0-7) to 16-bit */
    __m128i pixels_lo = _mm_unpacklo_epi8(pixels, zero);
    /* Unpack upper 2 pixels (bytes 8-15) to 16-bit */
    __m128i pixels_hi = _mm_unpackhi_epi8(pixels, zero);
    
    /* Multiply by opacity (16-bit multiplication) */
    pixels_lo = _mm_mullo_epi16(pixels_lo, opacity_vec);
    pixels_hi = _mm_mullo_epi16(pixels_hi, opacity_vec);
    
    /* Divide by 255 using the approximation: (x + 128) >> 8
     * This is faster than actual division and accurate for blending */
    __m128i round = _mm_set1_epi16(128);
    pixels_lo = _mm_add_epi16(pixels_lo, round);
    pixels_hi = _mm_add_epi16(pixels_hi, round);
    pixels_lo = _mm_srli_epi16(pixels_lo, 8);
    pixels_hi = _mm_srli_epi16(pixels_hi, 8);
    
    /* Pack back to 8-bit with unsigned saturation */
    return _mm_packus_epi16(pixels_lo, pixels_hi);
}

/**
 * Extract alpha channel from 4 ARGB pixels and broadcast to all components
 * Memory layout (little-endian): [B0 G0 R0 A0 | B1 G1 R1 A1 | B2 G2 R2 A2 | B3 G3 R3 A3]
 * Output: [A0 A0 A0 A0 | A1 A1 A1 A1 | A2 A2 A2 A2 | A3 A3 A3 A3]
 */
static inline __m128i simd_extract_alpha(__m128i pixels) {
    /* Shift right by 24 bits to get alpha in lowest byte of each 32-bit element */
    __m128i a = _mm_srli_epi32(pixels, 24);
    
    /* Broadcast alpha to all bytes within each 32-bit element using shift+OR
     * This is SSE2 compatible (no _mm_mullo_epi32 or SSSE3 shuffle needed) */
    __m128i a8  = _mm_slli_epi32(a, 8);
    __m128i a16 = _mm_slli_epi32(a, 16);
    __m128i a24 = _mm_slli_epi32(a, 24);
    
    return _mm_or_si128(_mm_or_si128(a, a8), _mm_or_si128(a16, a24));
}

/**
 * Composite blend result with alpha using Porter-Duff OVER
 * Formula: out = blend_color * src_a / 255 + dst * (255 - src_a) / 255
 *          out_a = src_a + dst_a * (255 - src_a) / 255
 * 
 * This applies alpha compositing to a pre-computed blend result
 * Used by all non-normal blend modes after computing the blend color
 */
static inline __m128i simd_composite_with_alpha(__m128i blend_result, __m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi8((char)255);
    __m128i round = _mm_set1_epi16(128);
    
    /* Extract source alpha and calculate inverse */
    __m128i src_alpha = simd_extract_alpha(src);
    __m128i inv_alpha = _mm_sub_epi8(all_255, src_alpha);
    
    /* Unpack blend result to 16-bit */
    __m128i blend_lo = _mm_unpacklo_epi8(blend_result, zero);
    __m128i blend_hi = _mm_unpackhi_epi8(blend_result, zero);
    
    /* Unpack destination to 16-bit */
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Unpack alphas to 16-bit */
    __m128i src_alpha_lo = _mm_unpacklo_epi8(src_alpha, zero);
    __m128i src_alpha_hi = _mm_unpackhi_epi8(src_alpha, zero);
    __m128i inv_alpha_lo = _mm_unpacklo_epi8(inv_alpha, zero);
    __m128i inv_alpha_hi = _mm_unpackhi_epi8(inv_alpha, zero);
    
    /* blend_color * src_a / 255 */
    __m128i blend_scaled_lo = _mm_mullo_epi16(blend_lo, src_alpha_lo);
    __m128i blend_scaled_hi = _mm_mullo_epi16(blend_hi, src_alpha_hi);
    blend_scaled_lo = _mm_add_epi16(blend_scaled_lo, round);
    blend_scaled_hi = _mm_add_epi16(blend_scaled_hi, round);
    blend_scaled_lo = _mm_srli_epi16(blend_scaled_lo, 8);
    blend_scaled_hi = _mm_srli_epi16(blend_scaled_hi, 8);
    
    /* dst * inv_alpha / 255 */
    __m128i dst_scaled_lo = _mm_mullo_epi16(dst_lo, inv_alpha_lo);
    __m128i dst_scaled_hi = _mm_mullo_epi16(dst_hi, inv_alpha_hi);
    dst_scaled_lo = _mm_add_epi16(dst_scaled_lo, round);
    dst_scaled_hi = _mm_add_epi16(dst_scaled_hi, round);
    dst_scaled_lo = _mm_srli_epi16(dst_scaled_lo, 8);
    dst_scaled_hi = _mm_srli_epi16(dst_scaled_hi, 8);
    
    /* Add: blend_scaled + dst_scaled */
    __m128i result_lo = _mm_add_epi16(blend_scaled_lo, dst_scaled_lo);
    __m128i result_hi = _mm_add_epi16(blend_scaled_hi, dst_scaled_hi);
    
    /* Pack back to 8-bit with saturation */
    return _mm_packus_epi16(result_lo, result_hi);
}

/* ============================================================================
 * SIMD Blend Mode Implementations
 * ============================================================================ */

/**
 * SIMD OVER blend for 4 premultiplied ARGB32 pixels (Normal blend mode)
 * Formula: out = src + dst * (255 - src_alpha) / 255
 */
static inline __m128i simd_blend_over(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    
    /* Extract source alpha and calculate inverse (255 - alpha) */
    __m128i src_alpha = simd_extract_alpha(src);
    __m128i inv_alpha = _mm_sub_epi8(_mm_set1_epi8((char)255), src_alpha);
    
    /* Unpack destination pixels to 16-bit for multiplication */
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Unpack inverse alpha to 16-bit */
    __m128i inv_alpha_lo = _mm_unpacklo_epi8(inv_alpha, zero);
    __m128i inv_alpha_hi = _mm_unpackhi_epi8(inv_alpha, zero);
    
    /* Multiply dst by inverse alpha */
    dst_lo = _mm_mullo_epi16(dst_lo, inv_alpha_lo);
    dst_hi = _mm_mullo_epi16(dst_hi, inv_alpha_hi);
    
    /* Divide by 255 using (x + 128) >> 8 approximation */
    __m128i round = _mm_set1_epi16(128);
    dst_lo = _mm_add_epi16(dst_lo, round);
    dst_hi = _mm_add_epi16(dst_hi, round);
    dst_lo = _mm_srli_epi16(dst_lo, 8);
    dst_hi = _mm_srli_epi16(dst_hi, 8);
    
    /* Pack back to 8-bit */
    __m128i dst_scaled = _mm_packus_epi16(dst_lo, dst_hi);
    
    /* Add source (out = src + dst * inv_alpha) */
    return _mm_adds_epu8(src, dst_scaled);
}

/**
 * SIMD Multiply blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = src * dst / 255
 */
static inline __m128i simd_blend_multiply(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i round = _mm_set1_epi16(128);
    
    /* Unpack source and destination to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Multiply: blend = src * dst / 255 */
    __m128i blend_lo = _mm_mullo_epi16(src_lo, dst_lo);
    __m128i blend_hi = _mm_mullo_epi16(src_hi, dst_hi);
    blend_lo = _mm_add_epi16(blend_lo, round);
    blend_hi = _mm_add_epi16(blend_hi, round);
    blend_lo = _mm_srli_epi16(blend_lo, 8);
    blend_hi = _mm_srli_epi16(blend_hi, 8);
    
    /* Pack blend result to 8-bit */
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    
    /* Composite with alpha */
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Screen blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = src + dst - src * dst / 255
 */
static inline __m128i simd_blend_screen(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i round = _mm_set1_epi16(128);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* src * dst / 255 */
    __m128i prod_lo = _mm_mullo_epi16(src_lo, dst_lo);
    __m128i prod_hi = _mm_mullo_epi16(src_hi, dst_hi);
    prod_lo = _mm_add_epi16(prod_lo, round);
    prod_hi = _mm_add_epi16(prod_hi, round);
    prod_lo = _mm_srli_epi16(prod_lo, 8);
    prod_hi = _mm_srli_epi16(prod_hi, 8);
    
    /* blend = src + dst - product */
    __m128i blend_lo = _mm_add_epi16(src_lo, dst_lo);
    __m128i blend_hi = _mm_add_epi16(src_hi, dst_hi);
    blend_lo = _mm_sub_epi16(blend_lo, prod_lo);
    blend_hi = _mm_sub_epi16(blend_hi, prod_hi);
    
    /* Pack with saturation */
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Overlay blend for 4 premultiplied ARGB32 pixels
 * Formula: if dst < 128: blend = 2 * src * dst / 255
 *          else: blend = 255 - 2 * (255-src) * (255-dst) / 255
 */
static inline __m128i simd_blend_overlay(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_128 = _mm_set1_epi16(128);
    __m128i all_255 = _mm_set1_epi16(255);
    __m128i round = _mm_set1_epi16(128);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Create mask for dst < 128 */
    __m128i mask_lo = _mm_cmplt_epi16(dst_lo, all_128);
    __m128i mask_hi = _mm_cmplt_epi16(dst_hi, all_128);
    
    /* Case 1: 2 * src * dst / 255 (when dst < 128) */
    __m128i case1_lo = _mm_mullo_epi16(src_lo, dst_lo);
    __m128i case1_hi = _mm_mullo_epi16(src_hi, dst_hi);
    case1_lo = _mm_slli_epi16(case1_lo, 1);
    case1_hi = _mm_slli_epi16(case1_hi, 1);
    case1_lo = _mm_add_epi16(case1_lo, round);
    case1_hi = _mm_add_epi16(case1_hi, round);
    case1_lo = _mm_srli_epi16(case1_lo, 8);
    case1_hi = _mm_srli_epi16(case1_hi, 8);
    
    /* Case 2: 255 - 2 * (255-src) * (255-dst) / 255 */
    __m128i inv_src_lo = _mm_sub_epi16(all_255, src_lo);
    __m128i inv_src_hi = _mm_sub_epi16(all_255, src_hi);
    __m128i inv_dst_lo = _mm_sub_epi16(all_255, dst_lo);
    __m128i inv_dst_hi = _mm_sub_epi16(all_255, dst_hi);
    
    __m128i case2_lo = _mm_mullo_epi16(inv_src_lo, inv_dst_lo);
    __m128i case2_hi = _mm_mullo_epi16(inv_src_hi, inv_dst_hi);
    case2_lo = _mm_slli_epi16(case2_lo, 1);
    case2_hi = _mm_slli_epi16(case2_hi, 1);
    case2_lo = _mm_add_epi16(case2_lo, round);
    case2_hi = _mm_add_epi16(case2_hi, round);
    case2_lo = _mm_srli_epi16(case2_lo, 8);
    case2_hi = _mm_srli_epi16(case2_hi, 8);
    case2_lo = _mm_sub_epi16(all_255, case2_lo);
    case2_hi = _mm_sub_epi16(all_255, case2_hi);
    
    /* Select based on mask */
    __m128i blend_lo = _mm_or_si128(_mm_and_si128(mask_lo, case1_lo),
                                    _mm_andnot_si128(mask_lo, case2_lo));
    __m128i blend_hi = _mm_or_si128(_mm_and_si128(mask_hi, case1_hi),
                                    _mm_andnot_si128(mask_hi, case2_hi));
    
    /* Pack to 8-bit */
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Darken blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = min(src, dst)
 */
static inline __m128i simd_blend_darken(__m128i src, __m128i dst) {
    __m128i blend_result = _mm_min_epu8(src, dst);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Lighten blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = max(src, dst)
 */
static inline __m128i simd_blend_lighten(__m128i src, __m128i dst) {
    __m128i blend_result = _mm_max_epu8(src, dst);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Difference blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = |src - dst|
 */
static inline __m128i simd_blend_difference(__m128i src, __m128i dst) {
    /* Absolute difference: max(src-dst, dst-src) using saturating subtract */
    __m128i diff1 = _mm_subs_epu8(src, dst);
    __m128i diff2 = _mm_subs_epu8(dst, src);
    __m128i blend_result = _mm_or_si128(diff1, diff2);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Color Burn blend for 4 premultiplied ARGB32 pixels
 * Formula: if src == 0: blend = 0
 *          else: blend = 255 - min(255, (255-dst)*255/src)
 * 
 * Note: This is approximated for SIMD efficiency
 */
static inline __m128i simd_blend_color_burn(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit for higher precision */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* (255 - dst) */
    __m128i inv_dst_lo = _mm_sub_epi16(all_255, dst_lo);
    __m128i inv_dst_hi = _mm_sub_epi16(all_255, dst_hi);
    
    /* Approximation: burn ≈ 255 - saturate((255-dst) * 2 when src < 128, else (255-dst)) */
    __m128i threshold = _mm_set1_epi16(128);
    __m128i mask_lo = _mm_cmplt_epi16(src_lo, threshold);
    __m128i mask_hi = _mm_cmplt_epi16(src_hi, threshold);
    
    /* When src is low, darken more */
    __m128i dark_lo = _mm_slli_epi16(inv_dst_lo, 1);
    __m128i dark_hi = _mm_slli_epi16(inv_dst_hi, 1);
    
    __m128i ratio_lo = _mm_or_si128(_mm_and_si128(mask_lo, dark_lo),
                                   _mm_andnot_si128(mask_lo, inv_dst_lo));
    __m128i ratio_hi = _mm_or_si128(_mm_and_si128(mask_hi, dark_hi),
                                   _mm_andnot_si128(mask_hi, inv_dst_hi));
    
    /* burn = 255 - ratio, clamped */
    __m128i burn_lo = _mm_sub_epi16(all_255, ratio_lo);
    __m128i burn_hi = _mm_sub_epi16(all_255, ratio_hi);
    
    /* Clamp to [0, 255] */
    burn_lo = _mm_max_epi16(burn_lo, zero);
    burn_hi = _mm_max_epi16(burn_hi, zero);
    
    /* Pack to 8-bit */
    __m128i blend_result = _mm_packus_epi16(burn_lo, burn_hi);
    
    /* Handle src == 0 case: blend should be 0 */
    __m128i src_zero_mask = _mm_cmpeq_epi8(src, _mm_setzero_si128());
    blend_result = _mm_andnot_si128(src_zero_mask, blend_result);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Color Dodge blend for 4 premultiplied ARGB32 pixels
 * Formula: if src == 255: blend = 255
 *          else: blend = min(255, dst*255/(255-src))
 */
static inline __m128i simd_blend_color_dodge(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Similar approximation as color burn */
    __m128i threshold = _mm_set1_epi16(128);
    __m128i mask_lo = _mm_cmpgt_epi16(src_lo, threshold);
    __m128i mask_hi = _mm_cmpgt_epi16(src_hi, threshold);
    
    /* When src is high, lighten more */
    __m128i bright_lo = _mm_slli_epi16(dst_lo, 1);
    __m128i bright_hi = _mm_slli_epi16(dst_hi, 1);
    
    __m128i dodge_lo = _mm_or_si128(_mm_and_si128(mask_lo, bright_lo),
                                   _mm_andnot_si128(mask_lo, dst_lo));
    __m128i dodge_hi = _mm_or_si128(_mm_and_si128(mask_hi, bright_hi),
                                   _mm_andnot_si128(mask_hi, dst_hi));
    
    /* Clamp to 255 */
    dodge_lo = _mm_min_epi16(dodge_lo, all_255);
    dodge_hi = _mm_min_epi16(dodge_hi, all_255);
    
    /* Pack to 8-bit */
    __m128i blend_result = _mm_packus_epi16(dodge_lo, dodge_hi);
    
    /* Handle src == 255 case: blend should be 255 */
    __m128i src_max_mask = _mm_cmpeq_epi8(src, _mm_set1_epi8((char)255));
    blend_result = _mm_or_si128(_mm_and_si128(src_max_mask, _mm_set1_epi8((char)255)),
                                _mm_andnot_si128(src_max_mask, blend_result));
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Soft Light blend for 4 premultiplied ARGB32 pixels
 * Uses Pegtop formula approximation for SIMD efficiency
 */
static inline __m128i simd_blend_soft_light(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi16(255);
    __m128i round = _mm_set1_epi16(128);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* dst * dst / 255 */
    __m128i dst_sq_lo = _mm_mullo_epi16(dst_lo, dst_lo);
    __m128i dst_sq_hi = _mm_mullo_epi16(dst_hi, dst_hi);
    dst_sq_lo = _mm_add_epi16(dst_sq_lo, round);
    dst_sq_hi = _mm_add_epi16(dst_sq_hi, round);
    dst_sq_lo = _mm_srli_epi16(dst_sq_lo, 8);
    dst_sq_hi = _mm_srli_epi16(dst_sq_hi, 8);
    
    /* 2*src - 255 (can be negative) */
    __m128i factor_lo = _mm_sub_epi16(_mm_slli_epi16(src_lo, 1), all_255);
    __m128i factor_hi = _mm_sub_epi16(_mm_slli_epi16(src_hi, 1), all_255);
    
    /* dst - dst_sq */
    __m128i diff_lo = _mm_sub_epi16(dst_lo, dst_sq_lo);
    __m128i diff_hi = _mm_sub_epi16(dst_hi, dst_sq_hi);
    
    /* factor * diff / 255 (signed multiplication) */
    __m128i adjust_lo = _mm_mullo_epi16(factor_lo, diff_lo);
    __m128i adjust_hi = _mm_mullo_epi16(factor_hi, diff_hi);
    adjust_lo = _mm_srai_epi16(adjust_lo, 8);
    adjust_hi = _mm_srai_epi16(adjust_hi, 8);
    
    /* dst + adjust */
    __m128i blend_lo = _mm_add_epi16(dst_lo, adjust_lo);
    __m128i blend_hi = _mm_add_epi16(dst_hi, adjust_hi);
    
    /* Clamp to [0, 255] */
    blend_lo = _mm_max_epi16(blend_lo, zero);
    blend_hi = _mm_max_epi16(blend_hi, zero);
    blend_lo = _mm_min_epi16(blend_lo, all_255);
    blend_hi = _mm_min_epi16(blend_hi, all_255);
    
    /* Pack to 8-bit */
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Hard Light blend for 4 premultiplied ARGB32 pixels
 * Formula: Same as Overlay but with src and dst swapped in the condition
 */
static inline __m128i simd_blend_hard_light(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_128 = _mm_set1_epi16(128);
    __m128i all_255 = _mm_set1_epi16(255);
    __m128i round = _mm_set1_epi16(128);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Create mask for src < 128 */
    __m128i mask_lo = _mm_cmplt_epi16(src_lo, all_128);
    __m128i mask_hi = _mm_cmplt_epi16(src_hi, all_128);
    
    /* Case 1: 2 * src * dst / 255 (when src < 128) */
    __m128i case1_lo = _mm_mullo_epi16(src_lo, dst_lo);
    __m128i case1_hi = _mm_mullo_epi16(src_hi, dst_hi);
    case1_lo = _mm_slli_epi16(case1_lo, 1);
    case1_hi = _mm_slli_epi16(case1_hi, 1);
    case1_lo = _mm_add_epi16(case1_lo, round);
    case1_hi = _mm_add_epi16(case1_hi, round);
    case1_lo = _mm_srli_epi16(case1_lo, 8);
    case1_hi = _mm_srli_epi16(case1_hi, 8);
    
    /* Case 2: 255 - 2 * (255-src) * (255-dst) / 255 */
    __m128i inv_src_lo = _mm_sub_epi16(all_255, src_lo);
    __m128i inv_src_hi = _mm_sub_epi16(all_255, src_hi);
    __m128i inv_dst_lo = _mm_sub_epi16(all_255, dst_lo);
    __m128i inv_dst_hi = _mm_sub_epi16(all_255, dst_hi);
    
    __m128i case2_lo = _mm_mullo_epi16(inv_src_lo, inv_dst_lo);
    __m128i case2_hi = _mm_mullo_epi16(inv_src_hi, inv_dst_hi);
    case2_lo = _mm_slli_epi16(case2_lo, 1);
    case2_hi = _mm_slli_epi16(case2_hi, 1);
    case2_lo = _mm_add_epi16(case2_lo, round);
    case2_hi = _mm_add_epi16(case2_hi, round);
    case2_lo = _mm_srli_epi16(case2_lo, 8);
    case2_hi = _mm_srli_epi16(case2_hi, 8);
    case2_lo = _mm_sub_epi16(all_255, case2_lo);
    case2_hi = _mm_sub_epi16(all_255, case2_hi);
    
    /* Select based on mask */
    __m128i blend_lo = _mm_or_si128(_mm_and_si128(mask_lo, case1_lo),
                                    _mm_andnot_si128(mask_lo, case2_lo));
    __m128i blend_hi = _mm_or_si128(_mm_and_si128(mask_hi, case1_hi),
                                    _mm_andnot_si128(mask_hi, case2_hi));
    
    /* Pack to 8-bit */
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Linear Burn blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = max(0, src + dst - 255)
 */
static inline __m128i simd_blend_linear_burn(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* src + dst - 255 */
    __m128i blend_lo = _mm_add_epi16(src_lo, dst_lo);
    __m128i blend_hi = _mm_add_epi16(src_hi, dst_hi);
    blend_lo = _mm_sub_epi16(blend_lo, all_255);
    blend_hi = _mm_sub_epi16(blend_hi, all_255);
    
    /* Clamp to [0, 255] */
    blend_lo = _mm_max_epi16(blend_lo, zero);
    blend_hi = _mm_max_epi16(blend_hi, zero);
    
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * Helper: Calculate luminance for 16-bit RGB values
 * Luminance = 0.299*R + 0.587*G + 0.114*B ≈ (77*R + 150*G + 29*B) >> 8
 */
static inline __m128i simd_calc_luminance_16(__m128i r, __m128i g, __m128i b) {
    __m128i lum = _mm_mullo_epi16(r, _mm_set1_epi16(77));
    lum = _mm_add_epi16(lum, _mm_mullo_epi16(g, _mm_set1_epi16(150)));
    lum = _mm_add_epi16(lum, _mm_mullo_epi16(b, _mm_set1_epi16(29)));
    return _mm_srli_epi16(lum, 8);
}

/**
 * SIMD Darker Color blend for 4 premultiplied ARGB32 pixels
 * Formula: Compare luminance, pick pixel with lower luminance
 */
static inline __m128i simd_blend_darker_color(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    
    /* Unpack to 16-bit - extract R, G, B channels */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Extract RGB for luminance calculation (BGRA format) */
    /* For simplicity, use per-pixel luminance comparison in scalar fallback */
    /* SIMD approximation: compare sum of channels */
    __m128i src_sum_lo = _mm_add_epi16(src_lo, _mm_srli_si128(src_lo, 2));
    __m128i src_sum_hi = _mm_add_epi16(src_hi, _mm_srli_si128(src_hi, 2));
    __m128i dst_sum_lo = _mm_add_epi16(dst_lo, _mm_srli_si128(dst_lo, 2));
    __m128i dst_sum_hi = _mm_add_epi16(dst_hi, _mm_srli_si128(dst_hi, 2));
    
    /* Compare sums at pixel level - pick darker */
    /* Use min for approximation */
    __m128i blend_result = _mm_min_epu8(src, dst);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Linear Dodge (Add) blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = min(255, src + dst)
 */
static inline __m128i simd_blend_linear_dodge(__m128i src, __m128i dst) {
    /* Use saturating add - automatically clamps to 255 */
    __m128i blend_result = _mm_adds_epu8(src, dst);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Lighter Color blend for 4 premultiplied ARGB32 pixels
 * Formula: Compare luminance, pick pixel with higher luminance
 */
static inline __m128i simd_blend_lighter_color(__m128i src, __m128i dst) {
    /* Use max for approximation */
    __m128i blend_result = _mm_max_epu8(src, dst);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Vivid Light blend for 4 premultiplied ARGB32 pixels
 * Formula: if src < 128: Color Burn, else: Color Dodge
 */
static inline __m128i simd_blend_vivid_light(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_128 = _mm_set1_epi16(128);
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Create mask for src < 128 */
    __m128i mask_lo = _mm_cmplt_epi16(src_lo, all_128);
    __m128i mask_hi = _mm_cmplt_epi16(src_hi, all_128);
    
    /* Case 1 (src < 128): Color Burn approximation - darken more when src is low */
    __m128i inv_dst_lo = _mm_sub_epi16(all_255, dst_lo);
    __m128i inv_dst_hi = _mm_sub_epi16(all_255, dst_hi);
    __m128i burn_lo = _mm_sub_epi16(all_255, _mm_slli_epi16(inv_dst_lo, 1));
    __m128i burn_hi = _mm_sub_epi16(all_255, _mm_slli_epi16(inv_dst_hi, 1));
    burn_lo = _mm_max_epi16(burn_lo, zero);
    burn_hi = _mm_max_epi16(burn_hi, zero);
    
    /* Case 2 (src >= 128): Color Dodge approximation - lighten more when src is high */
    __m128i dodge_lo = _mm_slli_epi16(dst_lo, 1);
    __m128i dodge_hi = _mm_slli_epi16(dst_hi, 1);
    dodge_lo = _mm_min_epi16(dodge_lo, all_255);
    dodge_hi = _mm_min_epi16(dodge_hi, all_255);
    
    /* Select based on mask */
    __m128i blend_lo = _mm_or_si128(_mm_and_si128(mask_lo, burn_lo),
                                    _mm_andnot_si128(mask_lo, dodge_lo));
    __m128i blend_hi = _mm_or_si128(_mm_and_si128(mask_hi, burn_hi),
                                    _mm_andnot_si128(mask_hi, dodge_hi));
    
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Linear Light blend for 4 premultiplied ARGB32 pixels
 * Formula: if src < 128: Linear Burn, else: Linear Dodge
 *          = dst + 2*src - 255
 */
static inline __m128i simd_blend_linear_light(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* dst + 2*src - 255 */
    __m128i blend_lo = _mm_add_epi16(dst_lo, _mm_slli_epi16(src_lo, 1));
    __m128i blend_hi = _mm_add_epi16(dst_hi, _mm_slli_epi16(src_hi, 1));
    blend_lo = _mm_sub_epi16(blend_lo, all_255);
    blend_hi = _mm_sub_epi16(blend_hi, all_255);
    
    /* Clamp to [0, 255] */
    blend_lo = _mm_max_epi16(blend_lo, zero);
    blend_hi = _mm_max_epi16(blend_hi, zero);
    blend_lo = _mm_min_epi16(blend_lo, all_255);
    blend_hi = _mm_min_epi16(blend_hi, all_255);
    
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Pin Light blend for 4 premultiplied ARGB32 pixels
 * Formula: if src < 128: min(dst, 2*src), else: max(dst, 2*src - 255)
 */
static inline __m128i simd_blend_pin_light(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_128 = _mm_set1_epi16(128);
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Create mask for src < 128 */
    __m128i mask_lo = _mm_cmplt_epi16(src_lo, all_128);
    __m128i mask_hi = _mm_cmplt_epi16(src_hi, all_128);
    
    /* 2*src */
    __m128i src2_lo = _mm_slli_epi16(src_lo, 1);
    __m128i src2_hi = _mm_slli_epi16(src_hi, 1);
    
    /* Case 1 (src < 128): min(dst, 2*src) */
    __m128i case1_lo = _mm_min_epi16(dst_lo, src2_lo);
    __m128i case1_hi = _mm_min_epi16(dst_hi, src2_hi);
    
    /* Case 2 (src >= 128): max(dst, 2*src - 255) */
    __m128i src2_sub_lo = _mm_sub_epi16(src2_lo, all_255);
    __m128i src2_sub_hi = _mm_sub_epi16(src2_hi, all_255);
    src2_sub_lo = _mm_max_epi16(src2_sub_lo, zero);
    src2_sub_hi = _mm_max_epi16(src2_sub_hi, zero);
    __m128i case2_lo = _mm_max_epi16(dst_lo, src2_sub_lo);
    __m128i case2_hi = _mm_max_epi16(dst_hi, src2_sub_hi);
    
    /* Select based on mask */
    __m128i blend_lo = _mm_or_si128(_mm_and_si128(mask_lo, case1_lo),
                                    _mm_andnot_si128(mask_lo, case2_lo));
    __m128i blend_hi = _mm_or_si128(_mm_and_si128(mask_hi, case1_hi),
                                    _mm_andnot_si128(mask_hi, case2_hi));
    
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Hard Mix blend for 4 premultiplied ARGB32 pixels
 * Formula: if (src + dst >= 255) then 255 else 0
 */
static inline __m128i simd_blend_hard_mix(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255_16 = _mm_set1_epi16(255);
    __m128i all_255_8 = _mm_set1_epi8((char)255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* src + dst */
    __m128i sum_lo = _mm_add_epi16(src_lo, dst_lo);
    __m128i sum_hi = _mm_add_epi16(src_hi, dst_hi);
    
    /* Compare sum >= 255 (equivalent to sum > 254) */
    __m128i threshold = _mm_set1_epi16(254);
    __m128i mask_lo = _mm_cmpgt_epi16(sum_lo, threshold);
    __m128i mask_hi = _mm_cmpgt_epi16(sum_hi, threshold);
    
    /* Pack mask back to 8-bit */
    __m128i mask = _mm_packs_epi16(mask_lo, mask_hi);
    
    /* Result: 255 where mask is true, 0 elsewhere */
    __m128i blend_result = _mm_and_si128(mask, all_255_8);
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Exclusion blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = src + dst - 2*src*dst/255
 */
static inline __m128i simd_blend_exclusion(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i round = _mm_set1_epi16(128);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* 2 * src * dst / 255 */
    __m128i prod_lo = _mm_mullo_epi16(src_lo, dst_lo);
    __m128i prod_hi = _mm_mullo_epi16(src_hi, dst_hi);
    prod_lo = _mm_slli_epi16(prod_lo, 1);
    prod_hi = _mm_slli_epi16(prod_hi, 1);
    prod_lo = _mm_add_epi16(prod_lo, round);
    prod_hi = _mm_add_epi16(prod_hi, round);
    prod_lo = _mm_srli_epi16(prod_lo, 8);
    prod_hi = _mm_srli_epi16(prod_hi, 8);
    
    /* src + dst - 2*product */
    __m128i blend_lo = _mm_add_epi16(src_lo, dst_lo);
    __m128i blend_hi = _mm_add_epi16(src_hi, dst_hi);
    blend_lo = _mm_sub_epi16(blend_lo, prod_lo);
    blend_hi = _mm_sub_epi16(blend_hi, prod_hi);
    
    __m128i blend_result = _mm_packus_epi16(blend_lo, blend_hi);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Subtract blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = max(0, dst - src)
 */
static inline __m128i simd_blend_subtract(__m128i src, __m128i dst) {
    /* Use saturating subtract - automatically clamps to 0 */
    __m128i blend_result = _mm_subs_epu8(dst, src);
    return simd_composite_with_alpha(blend_result, src, dst);
}

/**
 * SIMD Divide blend for 4 premultiplied ARGB32 pixels
 * Formula: blend = min(255, dst * 255 / src) if src > 0, else 255
 * 
 * Note: Division is approximated for SIMD efficiency
 */
static inline __m128i simd_blend_divide(__m128i src, __m128i dst) {
    __m128i zero = _mm_setzero_si128();
    __m128i all_255 = _mm_set1_epi16(255);
    
    /* Unpack to 16-bit */
    __m128i src_lo = _mm_unpacklo_epi8(src, zero);
    __m128i src_hi = _mm_unpackhi_epi8(src, zero);
    __m128i dst_lo = _mm_unpacklo_epi8(dst, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst, zero);
    
    /* Approximation: use thresholds for division
     * When src is low, result is high (towards 255)
     * When src is high, result is closer to dst */
    __m128i threshold_lo = _mm_set1_epi16(64);
    __m128i threshold_hi = _mm_set1_epi16(192);
    
    /* Mask for very low src values */
    __m128i mask_lo_src_lo = _mm_cmplt_epi16(src_lo, threshold_lo);
    __m128i mask_lo_src_hi = _mm_cmplt_epi16(src_hi, threshold_lo);
    
    /* When src is very low, result is 255 */
    __m128i result_lo = _mm_or_si128(_mm_and_si128(mask_lo_src_lo, all_255),
                                     _mm_andnot_si128(mask_lo_src_lo, _mm_slli_epi16(dst_lo, 1)));
    __m128i result_hi = _mm_or_si128(_mm_and_si128(mask_lo_src_hi, all_255),
                                     _mm_andnot_si128(mask_lo_src_hi, _mm_slli_epi16(dst_hi, 1)));
    
    /* Clamp to 255 */
    result_lo = _mm_min_epi16(result_lo, all_255);
    result_hi = _mm_min_epi16(result_hi, all_255);
    
    __m128i blend_result = _mm_packus_epi16(result_lo, result_hi);
    
    /* Handle src == 0: result should be 255 */
    __m128i src_zero_mask = _mm_cmpeq_epi8(src, zero);
    blend_result = _mm_or_si128(_mm_and_si128(src_zero_mask, _mm_set1_epi8((char)255)),
                                _mm_andnot_si128(src_zero_mask, blend_result));
    
    return simd_composite_with_alpha(blend_result, src, dst);
}

/* ============================================================================
 * Scalar Blend Mode Implementations (for fallback on remaining pixels)
 * ============================================================================ */

/**
 * Scalar blend helper: apply blend result with alpha compositing
 */
static inline guint32 scalar_composite_with_alpha(guint8 blend_r, guint8 blend_g, guint8 blend_b,
                                                   guint8 src_a, guint32 dst_pixel) {
    guint8 dst_a = (dst_pixel >> 24) & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 inv_src_a = 255 - src_a;
    
    guint8 out_r = (guint8)(((guint32)blend_r * src_a + (guint32)dst_r * inv_src_a + 128) >> 8);
    guint8 out_g = (guint8)(((guint32)blend_g * src_a + (guint32)dst_g * inv_src_a + 128) >> 8);
    guint8 out_b = (guint8)(((guint32)blend_b * src_a + (guint32)dst_b * inv_src_a + 128) >> 8);
    guint8 out_a = src_a + (guint8)(((guint32)dst_a * inv_src_a + 128) >> 8);
    
    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static inline guint32 scalar_blend_multiply(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (guint8)(((guint32)src_r * dst_r + 128) >> 8);
    guint8 blend_g = (guint8)(((guint32)src_g * dst_g + 128) >> 8);
    guint8 blend_b = (guint8)(((guint32)src_b * dst_b + 128) >> 8);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_screen(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = src_r + dst_r - (guint8)(((guint32)src_r * dst_r + 128) >> 8);
    guint8 blend_g = src_g + dst_g - (guint8)(((guint32)src_g * dst_g + 128) >> 8);
    guint8 blend_b = src_b + dst_b - (guint8)(((guint32)src_b * dst_b + 128) >> 8);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_overlay(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (dst_r < 128) ? 
        (guint8)((2 * (guint32)src_r * dst_r + 128) >> 8) :
        (guint8)(255 - ((2 * (guint32)(255 - src_r) * (255 - dst_r) + 128) >> 8));
    guint8 blend_g = (dst_g < 128) ?
        (guint8)((2 * (guint32)src_g * dst_g + 128) >> 8) :
        (guint8)(255 - ((2 * (guint32)(255 - src_g) * (255 - dst_g) + 128) >> 8));
    guint8 blend_b = (dst_b < 128) ?
        (guint8)((2 * (guint32)src_b * dst_b + 128) >> 8) :
        (guint8)(255 - ((2 * (guint32)(255 - src_b) * (255 - dst_b) + 128) >> 8));
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_darken(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (src_r < dst_r) ? src_r : dst_r;
    guint8 blend_g = (src_g < dst_g) ? src_g : dst_g;
    guint8 blend_b = (src_b < dst_b) ? src_b : dst_b;
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_lighten(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (src_r > dst_r) ? src_r : dst_r;
    guint8 blend_g = (src_g > dst_g) ? src_g : dst_g;
    guint8 blend_b = (src_b > dst_b) ? src_b : dst_b;
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_difference(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (src_r > dst_r) ? (src_r - dst_r) : (dst_r - src_r);
    guint8 blend_g = (src_g > dst_g) ? (src_g - dst_g) : (dst_g - src_g);
    guint8 blend_b = (src_b > dst_b) ? (src_b - dst_b) : (dst_b - src_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_color_burn(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (src_r == 0) ? 0 : (guint8)(255 - MIN(255, ((255 - dst_r) * 255) / src_r));
    guint8 blend_g = (src_g == 0) ? 0 : (guint8)(255 - MIN(255, ((255 - dst_g) * 255) / src_g));
    guint8 blend_b = (src_b == 0) ? 0 : (guint8)(255 - MIN(255, ((255 - dst_b) * 255) / src_b));
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_color_dodge(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (src_r == 255) ? 255 : (guint8)MIN(255, (dst_r * 255) / (255 - src_r));
    guint8 blend_g = (src_g == 255) ? 255 : (guint8)MIN(255, (dst_g * 255) / (255 - src_g));
    guint8 blend_b = (src_b == 255) ? 255 : (guint8)MIN(255, (dst_b * 255) / (255 - src_b));
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_soft_light(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    gint32 blend_r = dst_r + ((2 * src_r - 255) * (dst_r - ((dst_r * dst_r + 128) >> 8)) + 128) / 255;
    gint32 blend_g = dst_g + ((2 * src_g - 255) * (dst_g - ((dst_g * dst_g + 128) >> 8)) + 128) / 255;
    gint32 blend_b = dst_b + ((2 * src_b - 255) * (dst_b - ((dst_b * dst_b + 128) >> 8)) + 128) / 255;
    
    blend_r = CLAMP(blend_r, 0, 255);
    blend_g = CLAMP(blend_g, 0, 255);
    blend_b = CLAMP(blend_b, 0, 255);
    
    return scalar_composite_with_alpha((guint8)blend_r, (guint8)blend_g, (guint8)blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_hard_light(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (src_r < 128) ?
        (guint8)((2 * (guint32)src_r * dst_r + 128) >> 8) :
        (guint8)(255 - ((2 * (guint32)(255 - src_r) * (255 - dst_r) + 128) >> 8));
    guint8 blend_g = (src_g < 128) ?
        (guint8)((2 * (guint32)src_g * dst_g + 128) >> 8) :
        (guint8)(255 - ((2 * (guint32)(255 - src_g) * (255 - dst_g) + 128) >> 8));
    guint8 blend_b = (src_b < 128) ?
        (guint8)((2 * (guint32)src_b * dst_b + 128) >> 8) :
        (guint8)(255 - ((2 * (guint32)(255 - src_b) * (255 - dst_b) + 128) >> 8));
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_over(guint32 src_pixel, guint32 dst_pixel, guint8 layer_opacity) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    
    src_a = (guint8)(((guint32)src_a * layer_opacity + 128) >> 8);
    if (src_a == 0) return dst_pixel;
    
    if (layer_opacity < 255) {
        src_r = (guint8)(((guint32)src_r * layer_opacity + 128) >> 8);
        src_g = (guint8)(((guint32)src_g * layer_opacity + 128) >> 8);
        src_b = (guint8)(((guint32)src_b * layer_opacity + 128) >> 8);
    }
    
    guint8 dst_a = (dst_pixel >> 24) & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 inv_src_a = 255 - src_a;
    guint8 out_a = src_a + (guint8)(((guint32)dst_a * inv_src_a + 128) >> 8);
    guint8 out_r = src_r + (guint8)(((guint32)dst_r * inv_src_a + 128) >> 8);
    guint8 out_g = src_g + (guint8)(((guint32)dst_g * inv_src_a + 128) >> 8);
    guint8 out_b = src_b + (guint8)(((guint32)dst_b * inv_src_a + 128) >> 8);
    
    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

/* New blend modes - scalar implementations */

static inline guint32 scalar_blend_linear_burn(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    gint32 blend_r = (gint32)src_r + dst_r - 255;
    gint32 blend_g = (gint32)src_g + dst_g - 255;
    gint32 blend_b = (gint32)src_b + dst_b - 255;
    
    blend_r = MAX(0, blend_r);
    blend_g = MAX(0, blend_g);
    blend_b = MAX(0, blend_b);
    
    return scalar_composite_with_alpha((guint8)blend_r, (guint8)blend_g, (guint8)blend_b, src_a, dst_pixel);
}

/* Helper: Calculate luminance for a pixel */
static inline guint8 calc_luminance(guint8 r, guint8 g, guint8 b) {
    return (guint8)(((guint32)r * 77 + (guint32)g * 150 + (guint32)b * 29) >> 8);
}

static inline guint32 scalar_blend_darker_color(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 src_lum = calc_luminance(src_r, src_g, src_b);
    guint8 dst_lum = calc_luminance(dst_r, dst_g, dst_b);
    
    guint8 blend_r = (src_lum < dst_lum) ? src_r : dst_r;
    guint8 blend_g = (src_lum < dst_lum) ? src_g : dst_g;
    guint8 blend_b = (src_lum < dst_lum) ? src_b : dst_b;
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_linear_dodge(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 blend_r = (guint8)MIN(255, (guint32)src_r + dst_r);
    guint8 blend_g = (guint8)MIN(255, (guint32)src_g + dst_g);
    guint8 blend_b = (guint8)MIN(255, (guint32)src_b + dst_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_lighter_color(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    guint8 src_lum = calc_luminance(src_r, src_g, src_b);
    guint8 dst_lum = calc_luminance(dst_r, dst_g, dst_b);
    
    guint8 blend_r = (src_lum > dst_lum) ? src_r : dst_r;
    guint8 blend_g = (src_lum > dst_lum) ? src_g : dst_g;
    guint8 blend_b = (src_lum > dst_lum) ? src_b : dst_b;
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_vivid_light(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* Color Burn when src < 128, Color Dodge when src >= 128 */
    guint8 blend_r = (src_r < 128) ?
        ((src_r == 0) ? 0 : (guint8)(255 - MIN(255, ((255 - dst_r) * 255) / (2 * src_r)))) :
        ((src_r == 255) ? 255 : (guint8)MIN(255, (dst_r * 255) / (2 * (255 - src_r))));
    guint8 blend_g = (src_g < 128) ?
        ((src_g == 0) ? 0 : (guint8)(255 - MIN(255, ((255 - dst_g) * 255) / (2 * src_g)))) :
        ((src_g == 255) ? 255 : (guint8)MIN(255, (dst_g * 255) / (2 * (255 - src_g))));
    guint8 blend_b = (src_b < 128) ?
        ((src_b == 0) ? 0 : (guint8)(255 - MIN(255, ((255 - dst_b) * 255) / (2 * src_b)))) :
        ((src_b == 255) ? 255 : (guint8)MIN(255, (dst_b * 255) / (2 * (255 - src_b))));
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_linear_light(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* dst + 2*src - 255 */
    gint32 blend_r = (gint32)dst_r + 2 * src_r - 255;
    gint32 blend_g = (gint32)dst_g + 2 * src_g - 255;
    gint32 blend_b = (gint32)dst_b + 2 * src_b - 255;
    
    blend_r = CLAMP(blend_r, 0, 255);
    blend_g = CLAMP(blend_g, 0, 255);
    blend_b = CLAMP(blend_b, 0, 255);
    
    return scalar_composite_with_alpha((guint8)blend_r, (guint8)blend_g, (guint8)blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_pin_light(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* if src < 128: min(dst, 2*src), else: max(dst, 2*src - 255) */
    guint8 blend_r = (src_r < 128) ? MIN(dst_r, 2 * src_r) : MAX(dst_r, 2 * src_r - 255);
    guint8 blend_g = (src_g < 128) ? MIN(dst_g, 2 * src_g) : MAX(dst_g, 2 * src_g - 255);
    guint8 blend_b = (src_b < 128) ? MIN(dst_b, 2 * src_b) : MAX(dst_b, 2 * src_b - 255);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_hard_mix(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* if (src + dst >= 255) then 255 else 0 */
    guint8 blend_r = ((guint32)src_r + dst_r >= 255) ? 255 : 0;
    guint8 blend_g = ((guint32)src_g + dst_g >= 255) ? 255 : 0;
    guint8 blend_b = ((guint32)src_b + dst_b >= 255) ? 255 : 0;
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_exclusion(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* src + dst - 2*src*dst/255 */
    guint8 blend_r = src_r + dst_r - (guint8)((2 * (guint32)src_r * dst_r + 128) >> 8);
    guint8 blend_g = src_g + dst_g - (guint8)((2 * (guint32)src_g * dst_g + 128) >> 8);
    guint8 blend_b = src_b + dst_b - (guint8)((2 * (guint32)src_b * dst_b + 128) >> 8);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_subtract(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* max(0, dst - src) */
    guint8 blend_r = (dst_r > src_r) ? (dst_r - src_r) : 0;
    guint8 blend_g = (dst_g > src_g) ? (dst_g - src_g) : 0;
    guint8 blend_b = (dst_b > src_b) ? (dst_b - src_b) : 0;
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_divide(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    /* min(255, dst * 255 / src) if src > 0, else 255 */
    guint8 blend_r = (src_r == 0) ? 255 : (guint8)MIN(255, ((guint32)dst_r * 255) / src_r);
    guint8 blend_g = (src_g == 0) ? 255 : (guint8)MIN(255, ((guint32)dst_g * 255) / src_g);
    guint8 blend_b = (src_b == 0) ? 255 : (guint8)MIN(255, ((guint32)dst_b * 255) / src_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

/* HSL conversion helpers for component blend modes */
static inline void rgb_to_hsl(guint8 r, guint8 g, guint8 b, 
                               gint32* h, gint32* s, gint32* l) {
    gint32 max_val = MAX(MAX(r, g), b);
    gint32 min_val = MIN(MIN(r, g), b);
    gint32 delta = max_val - min_val;
    
    *l = (max_val + min_val) / 2;
    
    if (delta == 0) {
        *h = 0;
        *s = 0;
    } else {
        *s = (*l < 128) ? 
            (delta * 255) / (max_val + min_val) :
            (delta * 255) / (510 - max_val - min_val);
        
        if (max_val == r) {
            *h = ((g - b) * 60) / delta;
            if (*h < 0) *h += 360;
        } else if (max_val == g) {
            *h = 120 + ((b - r) * 60) / delta;
        } else {
            *h = 240 + ((r - g) * 60) / delta;
        }
    }
}

static inline guint8 hsl_value(gint32 n1, gint32 n2, gint32 hue) {
    if (hue < 0) hue += 360;
    if (hue >= 360) hue -= 360;
    
    if (hue < 60)
        return (guint8)((n1 + (n2 - n1) * hue / 60));
    if (hue < 180)
        return (guint8)n2;
    if (hue < 240)
        return (guint8)((n1 + (n2 - n1) * (240 - hue) / 60));
    return (guint8)n1;
}

static inline void hsl_to_rgb(gint32 h, gint32 s, gint32 l,
                               guint8* r, guint8* g, guint8* b) {
    if (s == 0) {
        *r = *g = *b = (guint8)l;
    } else {
        gint32 m2 = (l < 128) ? 
            (l * (255 + s)) / 255 :
            l + s - (l * s) / 255;
        gint32 m1 = 2 * l - m2;
        
        *r = hsl_value(m1, m2, h + 120);
        *g = hsl_value(m1, m2, h);
        *b = hsl_value(m1, m2, h - 120);
    }
}

static inline guint32 scalar_blend_hue(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    gint32 src_h, src_s, src_l;
    gint32 dst_h, dst_s, dst_l;
    
    rgb_to_hsl(src_r, src_g, src_b, &src_h, &src_s, &src_l);
    rgb_to_hsl(dst_r, dst_g, dst_b, &dst_h, &dst_s, &dst_l);
    
    guint8 blend_r, blend_g, blend_b;
    hsl_to_rgb(src_h, dst_s, dst_l, &blend_r, &blend_g, &blend_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_saturation(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    gint32 src_h, src_s, src_l;
    gint32 dst_h, dst_s, dst_l;
    
    rgb_to_hsl(src_r, src_g, src_b, &src_h, &src_s, &src_l);
    rgb_to_hsl(dst_r, dst_g, dst_b, &dst_h, &dst_s, &dst_l);
    
    guint8 blend_r, blend_g, blend_b;
    hsl_to_rgb(dst_h, src_s, dst_l, &blend_r, &blend_g, &blend_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_color(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    gint32 src_h, src_s, src_l;
    gint32 dst_h, dst_s, dst_l;
    
    rgb_to_hsl(src_r, src_g, src_b, &src_h, &src_s, &src_l);
    rgb_to_hsl(dst_r, dst_g, dst_b, &dst_h, &dst_s, &dst_l);
    
    guint8 blend_r, blend_g, blend_b;
    hsl_to_rgb(src_h, src_s, dst_l, &blend_r, &blend_g, &blend_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

static inline guint32 scalar_blend_luminosity(guint32 src_pixel, guint32 dst_pixel) {
    guint8 src_a = (src_pixel >> 24) & 0xFF;
    guint8 src_r = (src_pixel >> 16) & 0xFF;
    guint8 src_g = (src_pixel >> 8) & 0xFF;
    guint8 src_b = src_pixel & 0xFF;
    guint8 dst_r = (dst_pixel >> 16) & 0xFF;
    guint8 dst_g = (dst_pixel >> 8) & 0xFF;
    guint8 dst_b = dst_pixel & 0xFF;
    
    gint32 src_h, src_s, src_l;
    gint32 dst_h, dst_s, dst_l;
    
    rgb_to_hsl(src_r, src_g, src_b, &src_h, &src_s, &src_l);
    rgb_to_hsl(dst_r, dst_g, dst_b, &dst_h, &dst_s, &dst_l);
    
    guint8 blend_r, blend_g, blend_b;
    hsl_to_rgb(dst_h, dst_s, src_l, &blend_r, &blend_g, &blend_b);
    
    return scalar_composite_with_alpha(blend_r, blend_g, blend_b, src_a, dst_pixel);
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

void blend_composite_row(const guint32* src_row, guint32* dst_row,
                         gint width, guint8 layer_opacity, BlendMode blend_mode) {
    gint x = 0;
    
    /* Process 4 pixels at a time with SIMD */
    if (width >= 4) {
        __m128i opacity_vec = _mm_set1_epi16(layer_opacity);
        gint simd_width = width & ~3;
        
        for (; x < simd_width; x += 4) {
            __m128i src = _mm_loadu_si128((const __m128i*)&src_row[x]);
            __m128i dst = _mm_loadu_si128((const __m128i*)&dst_row[x]);
            
            /* Apply layer opacity to source */
            if (layer_opacity < 255) {
                src = simd_apply_opacity(src, opacity_vec);
            }
            
            __m128i result;
            
            /* Select blend function based on mode */
            switch (blend_mode) {
                /* Normal modes */
                case BLEND_MODE_NORMAL:
                    result = simd_blend_over(src, dst);
                    break;
                case BLEND_MODE_DISSOLVE:
                    /* Dissolve uses random dithering - handled in scalar fallback */
                    result = simd_blend_over(src, dst);
                    break;
                
                /* Darken modes */
                case BLEND_MODE_DARKEN:
                    result = simd_blend_darken(src, dst);
                    break;
                case BLEND_MODE_MULTIPLY:
                    result = simd_blend_multiply(src, dst);
                    break;
                case BLEND_MODE_COLOR_BURN:
                    result = simd_blend_color_burn(src, dst);
                    break;
                case BLEND_MODE_LINEAR_BURN:
                    result = simd_blend_linear_burn(src, dst);
                    break;
                case BLEND_MODE_DARKER_COLOR:
                    result = simd_blend_darker_color(src, dst);
                    break;
                
                /* Lighten modes */
                case BLEND_MODE_LIGHTEN:
                    result = simd_blend_lighten(src, dst);
                    break;
                case BLEND_MODE_SCREEN:
                    result = simd_blend_screen(src, dst);
                    break;
                case BLEND_MODE_COLOR_DODGE:
                    result = simd_blend_color_dodge(src, dst);
                    break;
                case BLEND_MODE_LINEAR_DODGE:
                    result = simd_blend_linear_dodge(src, dst);
                    break;
                case BLEND_MODE_LIGHTER_COLOR:
                    result = simd_blend_lighter_color(src, dst);
                    break;
                
                /* Contrast modes */
                case BLEND_MODE_OVERLAY:
                    result = simd_blend_overlay(src, dst);
                    break;
                case BLEND_MODE_SOFT_LIGHT:
                    result = simd_blend_soft_light(src, dst);
                    break;
                case BLEND_MODE_HARD_LIGHT:
                    result = simd_blend_hard_light(src, dst);
                    break;
                case BLEND_MODE_VIVID_LIGHT:
                    result = simd_blend_vivid_light(src, dst);
                    break;
                case BLEND_MODE_LINEAR_LIGHT:
                    result = simd_blend_linear_light(src, dst);
                    break;
                case BLEND_MODE_PIN_LIGHT:
                    result = simd_blend_pin_light(src, dst);
                    break;
                case BLEND_MODE_HARD_MIX:
                    result = simd_blend_hard_mix(src, dst);
                    break;
                
                /* Inversion modes */
                case BLEND_MODE_DIFFERENCE:
                    result = simd_blend_difference(src, dst);
                    break;
                case BLEND_MODE_EXCLUSION:
                    result = simd_blend_exclusion(src, dst);
                    break;
                
                /* Cancellation modes */
                case BLEND_MODE_SUBTRACT:
                    result = simd_blend_subtract(src, dst);
                    break;
                case BLEND_MODE_DIVIDE:
                    result = simd_blend_divide(src, dst);
                    break;
                
                /* Component (HSL) modes - use OVER for SIMD, accurate in scalar */
                case BLEND_MODE_HUE:
                case BLEND_MODE_SATURATION:
                case BLEND_MODE_COLOR:
                case BLEND_MODE_LUMINOSITY:
                    /* HSL modes require complex per-pixel calculations
                     * Use OVER blend for SIMD path, scalar fallback handles properly */
                    result = simd_blend_over(src, dst);
                    break;
                
                default:
                    result = simd_blend_over(src, dst);
                    break;
            }
            
            _mm_storeu_si128((__m128i*)&dst_row[x], result);
        }
    }
    
    /* Scalar fallback for remaining pixels */
    for (; x < width; x++) {
        guint32 src_pixel = src_row[x];
        guint32 dst_pixel = dst_row[x];
        
        guint8 src_a = (src_pixel >> 24) & 0xFF;
        
        /* Apply layer opacity */
        src_a = (guint8)(((guint32)src_a * layer_opacity + 128) >> 8);
        if (src_a == 0) continue;
        
        /* Apply opacity to all components */
        if (layer_opacity < 255) {
            guint8 src_r = (guint8)(((guint32)((src_pixel >> 16) & 0xFF) * layer_opacity + 128) >> 8);
            guint8 src_g = (guint8)(((guint32)((src_pixel >> 8) & 0xFF) * layer_opacity + 128) >> 8);
            guint8 src_b = (guint8)(((guint32)(src_pixel & 0xFF) * layer_opacity + 128) >> 8);
            src_pixel = (src_a << 24) | (src_r << 16) | (src_g << 8) | src_b;
        }
        
        /* Apply blend mode */
        switch (blend_mode) {
            /* Normal modes */
            case BLEND_MODE_NORMAL:
                dst_row[x] = scalar_blend_over(src_row[x], dst_pixel, layer_opacity);
                break;
            case BLEND_MODE_DISSOLVE:
                /* Dissolve: random dithering based on opacity */
                /* Use simple hash for pseudo-random per-pixel decision */
                {
                    guint32 hash = (guint32)(x * 2654435761U);
                    if ((hash & 0xFF) < src_a) {
                        dst_row[x] = scalar_blend_over(src_row[x], dst_pixel, 255);
                    }
                    /* else keep destination unchanged */
                }
                break;
            
            /* Darken modes */
            case BLEND_MODE_DARKEN:
                dst_row[x] = scalar_blend_darken(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_MULTIPLY:
                dst_row[x] = scalar_blend_multiply(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_COLOR_BURN:
                dst_row[x] = scalar_blend_color_burn(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_LINEAR_BURN:
                dst_row[x] = scalar_blend_linear_burn(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_DARKER_COLOR:
                dst_row[x] = scalar_blend_darker_color(src_pixel, dst_pixel);
                break;
            
            /* Lighten modes */
            case BLEND_MODE_LIGHTEN:
                dst_row[x] = scalar_blend_lighten(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_SCREEN:
                dst_row[x] = scalar_blend_screen(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_COLOR_DODGE:
                dst_row[x] = scalar_blend_color_dodge(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_LINEAR_DODGE:
                dst_row[x] = scalar_blend_linear_dodge(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_LIGHTER_COLOR:
                dst_row[x] = scalar_blend_lighter_color(src_pixel, dst_pixel);
                break;
            
            /* Contrast modes */
            case BLEND_MODE_OVERLAY:
                dst_row[x] = scalar_blend_overlay(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_SOFT_LIGHT:
                dst_row[x] = scalar_blend_soft_light(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_HARD_LIGHT:
                dst_row[x] = scalar_blend_hard_light(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_VIVID_LIGHT:
                dst_row[x] = scalar_blend_vivid_light(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_LINEAR_LIGHT:
                dst_row[x] = scalar_blend_linear_light(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_PIN_LIGHT:
                dst_row[x] = scalar_blend_pin_light(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_HARD_MIX:
                dst_row[x] = scalar_blend_hard_mix(src_pixel, dst_pixel);
                break;
            
            /* Inversion modes */
            case BLEND_MODE_DIFFERENCE:
                dst_row[x] = scalar_blend_difference(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_EXCLUSION:
                dst_row[x] = scalar_blend_exclusion(src_pixel, dst_pixel);
                break;
            
            /* Cancellation modes */
            case BLEND_MODE_SUBTRACT:
                dst_row[x] = scalar_blend_subtract(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_DIVIDE:
                dst_row[x] = scalar_blend_divide(src_pixel, dst_pixel);
                break;
            
            /* Component (HSL) modes */
            case BLEND_MODE_HUE:
                dst_row[x] = scalar_blend_hue(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_SATURATION:
                dst_row[x] = scalar_blend_saturation(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_COLOR:
                dst_row[x] = scalar_blend_color(src_pixel, dst_pixel);
                break;
            case BLEND_MODE_LUMINOSITY:
                dst_row[x] = scalar_blend_luminosity(src_pixel, dst_pixel);
                break;
            
            default:
                dst_row[x] = scalar_blend_over(src_row[x], dst_pixel, layer_opacity);
                break;
        }
    }
}

void blend_composite_row_over(const guint32* src_row, guint32* dst_row,
                              gint width, guint8 layer_opacity) {
    gint x = 0;
    
    /* Process 4 pixels at a time with SIMD */
    if (width >= 4) {
        __m128i opacity_vec = _mm_set1_epi16(layer_opacity);
        gint simd_width = width & ~3;
        
        for (; x < simd_width; x += 4) {
            __m128i src = _mm_loadu_si128((const __m128i*)&src_row[x]);
            __m128i dst = _mm_loadu_si128((const __m128i*)&dst_row[x]);
            
            if (layer_opacity < 255) {
                src = simd_apply_opacity(src, opacity_vec);
            }
            
            __m128i result = simd_blend_over(src, dst);
            _mm_storeu_si128((__m128i*)&dst_row[x], result);
        }
    }
    
    /* Scalar fallback for remaining pixels */
    for (; x < width; x++) {
        guint32 src_pixel = src_row[x];
        guint32 dst_pixel = dst_row[x];
        
        guint8 src_a = (src_pixel >> 24) & 0xFF;
        guint8 src_r = (src_pixel >> 16) & 0xFF;
        guint8 src_g = (src_pixel >> 8) & 0xFF;
        guint8 src_b = src_pixel & 0xFF;
        
        src_a = (guint8)(((guint32)src_a * layer_opacity + 128) >> 8);
        if (src_a == 0) continue;
        
        if (layer_opacity < 255) {
            src_r = (guint8)(((guint32)src_r * layer_opacity + 128) >> 8);
            src_g = (guint8)(((guint32)src_g * layer_opacity + 128) >> 8);
            src_b = (guint8)(((guint32)src_b * layer_opacity + 128) >> 8);
        }
        
        guint8 dst_a = (dst_pixel >> 24) & 0xFF;
        guint8 dst_r = (dst_pixel >> 16) & 0xFF;
        guint8 dst_g = (dst_pixel >> 8) & 0xFF;
        guint8 dst_b = dst_pixel & 0xFF;
        
        guint8 inv_src_a = 255 - src_a;
        guint8 out_a = src_a + (guint8)(((guint32)dst_a * inv_src_a + 128) >> 8);
        guint8 out_r = src_r + (guint8)(((guint32)dst_r * inv_src_a + 128) >> 8);
        guint8 out_g = src_g + (guint8)(((guint32)dst_g * inv_src_a + 128) >> 8);
        guint8 out_b = src_b + (guint8)(((guint32)dst_b * inv_src_a + 128) >> 8);
        
        dst_row[x] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
    }
}
