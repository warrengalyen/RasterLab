#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <lcms2.h>

#ifdef __cplusplus
extern "C" {
#endif

// Load ICC profile from raw memory block.
// Returns NULL if invalid.
cmsHPROFILE icc_profile_from_memory(const void *data, size_t size);

// Serialize profile to memory buffer allocated with malloc.
// Caller owns returned buffer.
bool icc_profile_to_memory(cmsHPROFILE profile,
                           void **out_data,
                           size_t *out_size);

// Create standard gamma sRGB profile.
// Caller owns profile and must cmsCloseProfile().
cmsHPROFILE icc_create_srgb_profile(void);

// Returns true if profile is already sRGB (approximate comparison allowed).
bool icc_is_profile_srgb(cmsHPROFILE profile);

// Destroy profile safely.
void icc_destroy(cmsHPROFILE profile);

#ifdef __cplusplus
}
#endif
