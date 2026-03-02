/**
 * color_manager.c — CMS module implementation (Little CMS wrapper)
 *
 * All Little CMS types are confined to this file.
 */

#include "color_manager.h"
#include <lcms2.h>
#include <stdlib.h>
#include <string.h>

/* Internal: ColorProfile holds lcms handle and optional ICC blob. */
struct ColorProfile {
    void* handle;   /* cmsHPROFILE, kept opaque in header */
    void* raw_data; /* optional ICC blob (owned or NULL) */
    size_t size;
};

static cmsHPROFILE profile_handle(const ColorProfile* p) {
    return (cmsHPROFILE)p->handle;
}

ColorProfile* cm_profile_from_memory(const void* data, size_t size) {
    if (!data || size == 0 || size > (size_t)(cmsUInt32Number)(-1))
        return NULL;

    cmsHPROFILE h = cmsOpenProfileFromMem(data, (cmsUInt32Number)size);
    if (!h)
        return NULL;

    ColorProfile* p = (ColorProfile*)malloc(sizeof(ColorProfile));
    if (!p) {
        cmsCloseProfile(h);
        return NULL;
    }

    p->handle = (void*)h;
    p->raw_data = NULL; /* we do not take ownership of caller's data */
    p->size = size;
    return p;
}

ColorProfile* cm_profile_create_srgb(void) {
    cmsHPROFILE h = cmsCreate_sRGBProfile();
    if (!h)
        return NULL;

    ColorProfile* p = (ColorProfile*)malloc(sizeof(ColorProfile));
    if (!p) {
        cmsCloseProfile(h);
        return NULL;
    }

    p->handle = (void*)h;
    p->raw_data = NULL;
    p->size = 0;
    return p;
}

/* sRGB primaries (CIE xy, Y=1) and D50 white point for linear sRGB profile */
static void get_srgb_primaries(cmsCIExyYTRIPLE* primaries) {
    /* Red */
    primaries->Red.x = 0.6400;
    primaries->Red.y = 0.3300;
    primaries->Red.Y = 1.0;
    /* Green */
    primaries->Green.x = 0.3000;
    primaries->Green.y = 0.6000;
    primaries->Green.Y = 1.0;
    /* Blue */
    primaries->Blue.x = 0.1500;
    primaries->Blue.y = 0.0600;
    primaries->Blue.Y = 1.0;
}

ColorProfile* cm_profile_create_linear_srgb(void) {
    cmsToneCurve* linear = cmsBuildGamma(0, 1.0);
    if (!linear)
        return NULL;

    cmsCIExyYTRIPLE primaries;
    get_srgb_primaries(&primaries);

    cmsToneCurve* transfer[3] = { linear, linear, linear };
    cmsHPROFILE h = cmsCreateRGBProfile(cmsD50_xyY(), &primaries, transfer);
    cmsFreeToneCurve(linear);

    if (!h)
        return NULL;

    ColorProfile* p = (ColorProfile*)malloc(sizeof(ColorProfile));
    if (!p) {
        cmsCloseProfile(h);
        return NULL;
    }

    p->handle   = (void*)h;
    p->raw_data = NULL;
    p->size     = 0;
    return p;
}

void cm_profile_destroy(ColorProfile* profile) {
    if (!profile)
        return;

    if (profile_handle(profile))
        cmsCloseProfile(profile_handle(profile));

    if (profile->raw_data)
        free(profile->raw_data);

    free(profile);
}
