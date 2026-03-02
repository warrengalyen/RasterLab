/**
 * color_manager.h — CMS module (Little CMS wrapper)
 *
 * Standalone color management module. All Little CMS types are internal;
 * no cmsHPROFILE or cmsHTRANSFORM is exposed in this header.
 */

#ifndef COLOR_MANAGER_H
#define COLOR_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque color profile. Internal fields (lcms handle, optional ICC blob)
 * are defined in color_manager.c only.
 */
typedef struct ColorProfile ColorProfile;

/**
 * Pixel format for color transforms. Straight (non-premultiplied) only.
 */
typedef enum {
    CM_PIXELFORMAT_RGBA8,    /**< 8-bit RGBA, 4 bytes per pixel */
    CM_PIXELFORMAT_RGB_FLOAT /**< 32-bit float RGB, 12 bytes per pixel (HDR) */
} CMPixelFormat;

/**
 * Opaque color transform. Internal lcms handle is not exposed.
 */
typedef struct ColorTransform ColorTransform;

/**
 * Create a color profile from an ICC profile blob in memory.
 * A copy of the data is not required; the profile may retain the pointer
 * for as long as the profile exists (caller must not free data before
 * cm_profile_destroy).
 *
 * \param data  Pointer to ICC profile bytes (may be NULL if size is 0)
 * \param size  Size in bytes of the ICC data
 * \return      New ColorProfile*, or NULL on error or if data/size invalid
 */
ColorProfile* cm_profile_from_memory(const void* data, size_t size);

/**
 * Create a standard sRGB profile (sRGB gamma curve).
 *
 * \return  New ColorProfile*, or NULL on error
 */
ColorProfile* cm_profile_create_srgb(void);

/**
 * Create a linear sRGB profile (gamma 1.0, for HDR/linear workflows).
 *
 * \return  New ColorProfile*, or NULL on error
 */
ColorProfile* cm_profile_create_linear_srgb(void);

/**
 * Create a linear RGB profile from CIE (x,y) chromaticities (e.g. from EXR chromaticities attribute).
 * White point and R,G,B primaries in CIE xy; gamma is linear (1.0).
 *
 * \param white_x  White point x
 * \param white_y  White point y
 * \param red_x    Red primary x
 * \param red_y    Red primary y
 * \param green_x  Green primary x
 * \param green_y  Green primary y
 * \param blue_x   Blue primary x
 * \param blue_y   Blue primary y
 * \return         New ColorProfile*, or NULL on error
 */
ColorProfile* cm_profile_create_linear_from_primaries(float white_x, float white_y,
                                                      float red_x, float red_y,
                                                      float green_x, float green_y,
                                                      float blue_x, float blue_y);

/**
 * Destroy a color profile and release resources.
 * Safe to call with NULL (no-op).
 *
 * \param profile  Profile to destroy, or NULL
 */
void cm_profile_destroy(ColorProfile* profile);

/* -------------------------------------------------------------------------
 * sRGB profile embedding (for export)
 * ------------------------------------------------------------------------- */

/**
 * Return a pointer to a standard sRGB ICC profile blob for embedding in saved images.
 * The profile is generated once and cached; safe for repeated use.
 *
 * \param out_size  On success, set to the ICC blob size in bytes. May be NULL.
 * \return          Pointer to the ICC data (read-only), or NULL on error.
 *                  The pointer remains valid for the lifetime of the process.
 */
const void* cm_get_embedded_srgb_profile(size_t* out_size);

/* -------------------------------------------------------------------------
 * Color transforms (SDR + HDR). All data is straight, non-premultiplied.
 * ------------------------------------------------------------------------- */

/**
 * Create a color transform from source to destination profile.
 *
 * \param src   Source color profile (must outlive the transform or be kept alive)
 * \param dst   Destination color profile (must outlive the transform or be kept alive)
 * \param fmt   Pixel format for both input and output
 * \return      New ColorTransform*, or NULL on error or invalid profiles
 */
ColorTransform* cm_transform_create(ColorProfile* src, ColorProfile* dst, CMPixelFormat fmt);

/**
 * Apply the transform in-place to a buffer.
 *
 * \param transform    Transform created with cm_transform_create
 * \param buffer       Pixel buffer (format must match the transform's CMPixelFormat)
 * \param pixel_count  Number of pixels to transform
 */
void cm_transform_apply(ColorTransform* transform, void* buffer, size_t pixel_count);

/**
 * Destroy a color transform. Safe to call with NULL (no-op).
 *
 * \param transform  Transform to destroy, or NULL
 */
void cm_transform_destroy(ColorTransform* transform);

/* -------------------------------------------------------------------------
 * Premultiplied ARGB32 utilities (Cairo-compatible).
 * Memory layout: 4 bytes per pixel, BGRA order (byte 0 = B, 1 = G, 2 = R, 3 = A).
 * ------------------------------------------------------------------------- */

/**
 * Convert a Cairo ARGB32 buffer from premultiplied to straight (unpremultiplied) alpha.
 * When alpha is 0, R/G/B are set to 0 to avoid divide-by-zero.
 *
 * \param buffer       ARGB32 pixel buffer (modified in place)
 * \param pixel_count  Number of pixels
 */
void cm_unpremultiply_argb32(uint8_t* buffer, size_t pixel_count);

/**
 * Convert a Cairo ARGB32 buffer from straight to premultiplied alpha.
 *
 * \param buffer       ARGB32 pixel buffer (modified in place)
 * \param pixel_count  Number of pixels
 */
void cm_premultiply_argb32(uint8_t* buffer, size_t pixel_count);

/* -------------------------------------------------------------------------
 * SDR load-time conversion to internal sRGB
 * ------------------------------------------------------------------------- */

/**
 * Convert SDR image buffer from embedded ICC profile space to sRGB.
 * Buffer must be Cairo ARGB32 (BGRA in memory, premultiplied).
 *
 * If \a icc_data is NULL (or \a icc_size is 0), the buffer is assumed
 * already sRGB and no conversion is performed.
 *
 * If ICC data is provided: unpremultiplies, converts via profile transform,
 * then premultiplies again. All temporary profiles and transform are freed
 * before return.
 *
 * \param buffer       ARGB32 pixel buffer (modified in place)
 * \param pixel_count  Number of pixels
 * \param icc_data     Embedded ICC profile bytes, or NULL to assume sRGB
 * \param icc_size     Size of ICC data in bytes (ignored if icc_data is NULL)
 */
void cm_convert_sdr_to_srgb_argb32(uint8_t* buffer, size_t pixel_count,
                                   const void* icc_data, size_t icc_size);

/* -------------------------------------------------------------------------
 * HDR linear conversion (before tone mapping)
 * ------------------------------------------------------------------------- */

/**
 * Convert linear HDR RGB buffer from a source color profile to linear sRGB.
 * Use this before tone mapping; no gamma encoding or 8-bit quantization is done.
 *
 * Buffer layout: interleaved float RGB, 3 floats per pixel (R, G, B).
 *
 * If \a source is NULL, the buffer is assumed already linear sRGB (e.g. Linear Rec.709)
 * and no conversion is performed.
 *
 * \param buffer       Linear float RGB buffer (modified in place), 3 floats per pixel
 * \param pixel_count  Number of pixels
 * \param source       Source color profile (linear primaries), or NULL to assume linear sRGB
 */
void cm_convert_hdr_linear_to_linear_srgb_from_profile(float* buffer, size_t pixel_count,
                                                       ColorProfile* source);

#ifdef __cplusplus
}
#endif

#endif /* COLOR_MANAGER_H */
