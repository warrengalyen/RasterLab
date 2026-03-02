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

#ifdef __cplusplus
}
#endif

#endif /* COLOR_MANAGER_H */
