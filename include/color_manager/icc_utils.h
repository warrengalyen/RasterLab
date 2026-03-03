#pragma once
#include <lcms2.h>
#include <stdbool.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

// Load ICC profile from raw memory block.
// Returns NULL if invalid.
cmsHPROFILE icc_profile_from_memory(const void* data, size_t size);

// Load ICC profile from file (ICC or ICM). Returns NULL on error.
// Caller must call icc_destroy() on the result.
cmsHPROFILE icc_profile_from_file(const char* path);

// Returns true if path points to a valid ICC/ICM profile file (any color space).
// Use for validation; icc_profile_from_file() may still reject non-RGB profiles.
bool icc_profile_file_is_valid(const char* path);

// Serialize profile to memory buffer allocated with malloc.
// Caller owns returned buffer.
bool icc_profile_to_memory(cmsHPROFILE profile,
                           void** out_data,
                           size_t* out_size);

// Create standard gamma sRGB profile.
// Caller owns profile and must cmsCloseProfile().
cmsHPROFILE icc_create_srgb_profile(void);

// Returns true if profile is already sRGB (approximate comparison allowed).
bool icc_is_profile_srgb(cmsHPROFILE profile);

// Get profile description for logging. Writes to buf, null-terminates.
// Best-effort conversion from wide string. Returns true if got something.
bool icc_get_profile_description(cmsHPROFILE profile, char* buf, size_t buf_size);

// Destroy profile safely. Safe to call with NULL.
void icc_destroy(cmsHPROFILE profile);

#ifdef __cplusplus
}
#endif
