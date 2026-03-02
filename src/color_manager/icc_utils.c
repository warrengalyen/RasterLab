/**
 * icc_utils.c — ICC profile extraction, creation, serialization, destruction.
 * Centralizes Little CMS profile handling so file format plugins do not call lcms directly.
 * No pixel transforms are performed in this module.
 */

#include "color_manager/icc_utils.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

cmsHPROFILE icc_profile_from_memory(const void *data, size_t size)
{
    if (!data || size == 0 || size > (size_t)(cmsUInt32Number)(-1))
        return NULL;

    cmsHPROFILE h = cmsOpenProfileFromMem(data, (cmsUInt32Number)size);
    if (!h)
        return NULL;

    if (cmsGetColorSpace(h) != cmsSigRgbData) {
        cmsCloseProfile(h);
        return NULL;
    }

    return h;
}

bool icc_profile_to_memory(cmsHPROFILE profile, void **out_data, size_t *out_size)
{
    if (!profile || !out_data || !out_size) {
        if (out_data) *out_data = NULL;
        if (out_size) *out_size = 0;
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    cmsUInt32Number size = 0;
    if (!cmsSaveProfileToMem(profile, NULL, &size) || size == 0)
        return false;

    void *buf = malloc((size_t)size);
    if (!buf)
        return false;

    if (!cmsSaveProfileToMem(profile, buf, &size)) {
        free(buf);
        return false;
    }

    *out_data = buf;
    *out_size = (size_t)size;
    return true;
}

cmsHPROFILE icc_create_srgb_profile(void)
{
    return cmsCreate_sRGBProfile();
}

static bool description_contains_srgb(cmsHPROFILE profile)
{
    wchar_t buf[256];
    if (!cmsGetProfileInfo(profile, cmsInfoDescription, "en", "US", buf, (cmsUInt32Number)sizeof(buf)))
        return false;
    return wcsstr(buf, L"sRGB") != NULL;
}

bool icc_is_profile_srgb(cmsHPROFILE profile)
{
    if (!profile)
        return false;

    if (cmsGetColorSpace(profile) != cmsSigRgbData)
        return false;

    return description_contains_srgb(profile);
}

void icc_destroy(cmsHPROFILE profile)
{
    if (profile)
        cmsCloseProfile(profile);
}
