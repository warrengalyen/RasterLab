/**
 * color_manager.h — CMS module (Little CMS wrapper)
 *
 * Standalone color management module. All Little CMS types are internal;
 * no cmsHPROFILE or cmsHTRANSFORM is exposed in this header.
 */

#ifndef COLOR_MANAGER_H
#define COLOR_MANAGER_H

#include <stddef.h>

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
 * Destroy a color profile and release resources.
 * Safe to call with NULL (no-op).
 *
 * \param profile  Profile to destroy, or NULL
 */
void cm_profile_destroy(ColorProfile* profile);

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

#ifdef __cplusplus
}
#endif

#endif /* COLOR_MANAGER_H */
