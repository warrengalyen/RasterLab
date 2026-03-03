/**
 * color_manager.c — CMS module implementation (Little CMS wrapper)
 *
 * All Little CMS types are confined to this file.
 */

#include "color_manager.h"
#include <lcms2.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Internal: ColorProfile holds lcms handle and optional ICC blob. */
struct ColorProfile {
    void* handle;   /* cmsHPROFILE, kept opaque in header */
    void* raw_data; /* optional ICC blob (owned or NULL) */
    size_t size;
};

static cmsHPROFILE profile_handle(const ColorProfile* p) {
    return (cmsHPROFILE)p->handle;
}

/* Map public pixel format to lcms type (straight, non-premultiplied). Add new formats here when needed. */
static cmsUInt32Number pixel_format_to_lcms(CMPixelFormat fmt) {
    switch (fmt) {
        case CM_PIXELFORMAT_RGBA8:     return TYPE_RGBA_8;
        case CM_PIXELFORMAT_RGB_FLOAT: return TYPE_RGB_FLT;
        case CM_PIXELFORMAT_RGBA16:    return TYPE_RGBA_16;
        case CM_PIXELFORMAT_RGBAF:     return TYPE_RGBA_FLT;
        default: return (cmsUInt32Number)-1;
    }
}

/* Internal: ColorTransform wraps cmsHTRANSFORM. */
struct ColorTransform {
    void* xform; /* cmsHTRANSFORM */
};

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

ColorProfile* cm_profile_create_linear_from_primaries(float white_x, float white_y,
                                                     float red_x, float red_y,
                                                     float green_x, float green_y,
                                                     float blue_x, float blue_y) {
    cmsToneCurve* linear = cmsBuildGamma(0, 1.0);
    if (!linear)
        return NULL;

    cmsCIExyY white_pt;
    white_pt.x = (cmsFloat64Number)white_x;
    white_pt.y = (cmsFloat64Number)white_y;
    white_pt.Y = 1.0;

    cmsCIExyYTRIPLE primaries;
    primaries.Red.x  = (cmsFloat64Number)red_x;
    primaries.Red.y  = (cmsFloat64Number)red_y;
    primaries.Red.Y  = 1.0;
    primaries.Green.x = (cmsFloat64Number)green_x;
    primaries.Green.y = (cmsFloat64Number)green_y;
    primaries.Green.Y = 1.0;
    primaries.Blue.x  = (cmsFloat64Number)blue_x;
    primaries.Blue.y  = (cmsFloat64Number)blue_y;
    primaries.Blue.Y  = 1.0;

    cmsToneCurve* transfer[3] = { linear, linear, linear };
    cmsHPROFILE h = cmsCreateRGBProfile(&white_pt, &primaries, transfer);
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

/* -------------------------------------------------------------------------
 * sRGB profile embedding (cached ICC blob)
 * ------------------------------------------------------------------------- */

static uint8_t* s_srgb_icc_blob = NULL;
static size_t s_srgb_icc_size = 0;

const void* cm_get_embedded_srgb_profile(size_t* out_size) {
    if (out_size)
        *out_size = 0;

    if (s_srgb_icc_blob == NULL) {
        cmsHPROFILE h = cmsCreate_sRGBProfile();
        if (!h)
            return NULL;

        cmsUInt32Number size = 0;
        if (!cmsSaveProfileToMem(h, NULL, &size) || size == 0) {
            cmsCloseProfile(h);
            return NULL;
        }

        uint8_t* blob = (uint8_t*)malloc((size_t)size);
        if (!blob) {
            cmsCloseProfile(h);
            return NULL;
        }

        if (!cmsSaveProfileToMem(h, blob, &size)) {
            free(blob);
            cmsCloseProfile(h);
            return NULL;
        }
        cmsCloseProfile(h);

        s_srgb_icc_blob = blob;
        s_srgb_icc_size = (size_t)size;
    }

    if (out_size)
        *out_size = s_srgb_icc_size;
    return s_srgb_icc_blob;
}

/* -------------------------------------------------------------------------
 * Color transforms
 * ------------------------------------------------------------------------- */

ColorTransform* cm_transform_create(ColorProfile* src, ColorProfile* dst, CMPixelFormat fmt) {
    if (!src || !dst || !profile_handle(src) || !profile_handle(dst))
        return NULL;

    cmsUInt32Number lcms_fmt = pixel_format_to_lcms(fmt);
    if (lcms_fmt == (cmsUInt32Number)-1)
        return NULL;

    cmsUInt32Number flags = 0;
    if (fmt == CM_PIXELFORMAT_RGBA8 || fmt == CM_PIXELFORMAT_RGBA16 || fmt == CM_PIXELFORMAT_RGBAF)
        flags = cmsFLAGS_COPY_ALPHA; /* pass alpha through unchanged */

    cmsHTRANSFORM h = cmsCreateTransform(
        profile_handle(src),  lcms_fmt,
        profile_handle(dst), lcms_fmt,
        INTENT_PERCEPTUAL,
        flags
    );
    if (!h)
        return NULL;

    ColorTransform* t = (ColorTransform*)malloc(sizeof(ColorTransform));
    if (!t) {
        cmsDeleteTransform(h);
        return NULL;
    }
    t->xform = (void*)h;
    return t;
}

void cm_transform_apply(ColorTransform* transform, void* buffer, size_t pixel_count) {
    if (!transform || !transform->xform || !buffer || pixel_count == 0)
        return;

    cmsDoTransform((cmsHTRANSFORM)transform->xform, buffer, buffer, (cmsUInt32Number)pixel_count);
}

void cm_transform_destroy(ColorTransform* transform) {
    if (!transform)
        return;

    if (transform->xform)
        cmsDeleteTransform((cmsHTRANSFORM)transform->xform);

    free(transform);
}

/* Swap R and B in each pixel (BGRA <-> RGBA) for lcms RGBA8 transform. */
static void argb32_swap_rb(uint8_t* buffer, size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t* p = buffer + (i * 4);
        uint8_t t = p[0];
        p[0] = p[2];
        p[2] = t;
    }
}

/* -------------------------------------------------------------------------
 * Premultiplied ARGB32 utilities (Cairo layout: BGRA in memory)
 * ------------------------------------------------------------------------- */

void cm_unpremultiply_argb32(uint8_t* buffer, size_t pixel_count) {
    if (!buffer)
        return;

    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t* p = buffer + (i * 4);
        uint8_t b = p[0], g = p[1], r = p[2], a = p[3];

        if (a == 0) {
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            /* p[3] stays 0 */
            continue;
        }

        /* Straight = (premul * 255 + a/2) / a; use uint32_t to avoid overflow */
        p[0] = (uint8_t)(((uint32_t)b * 255 + (a / 2)) / a);
        p[1] = (uint8_t)(((uint32_t)g * 255 + (a / 2)) / a);
        p[2] = (uint8_t)(((uint32_t)r * 255 + (a / 2)) / a);
        /* p[3] = a unchanged */
    }
}

void cm_premultiply_argb32(uint8_t* buffer, size_t pixel_count) {
    if (!buffer)
        return;

    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t* p = buffer + (i * 4);
        uint8_t b = p[0], g = p[1], r = p[2], a = p[3];

        /* Premul = (c * a + 127) / 255 */
        p[0] = (uint8_t)((b * (uint32_t)a + 127) / 255);
        p[1] = (uint8_t)((g * (uint32_t)a + 127) / 255);
        p[2] = (uint8_t)((r * (uint32_t)a + 127) / 255);
        /* p[3] = a unchanged */
    }
}

/* -------------------------------------------------------------------------
 * SDR load-time conversion to sRGB
 * ------------------------------------------------------------------------- */

void cm_convert_sdr_to_srgb_argb32(uint8_t* buffer, size_t pixel_count,
                                   const void* icc_data, size_t icc_size) {
    if (!buffer)
        return;

    if (!icc_data || icc_size == 0)
        return; /* assume sRGB, no conversion */

    ColorProfile* src = cm_profile_from_memory(icc_data, icc_size);
    if (!src)
        return;

    ColorProfile* dst = cm_profile_create_srgb();
    if (!dst) {
        cm_profile_destroy(src);
        return;
    }

    ColorTransform* transform = cm_transform_create(src, dst, CM_PIXELFORMAT_RGBA8);
    if (!transform) {
        cm_profile_destroy(dst);
        cm_profile_destroy(src);
        return;
    }

    cm_unpremultiply_argb32(buffer, pixel_count);
    argb32_swap_rb(buffer, pixel_count);           /* BGRA -> RGBA for lcms */
    cm_transform_apply(transform, buffer, pixel_count);
    argb32_swap_rb(buffer, pixel_count);           /* RGBA -> BGRA (Cairo) */
    cm_premultiply_argb32(buffer, pixel_count);

    cm_transform_destroy(transform);
    cm_profile_destroy(dst);
    cm_profile_destroy(src);
}

bool cm_convert_sdr_to_srgb_argb32_from_profile(uint8_t* buffer, size_t pixel_count,
                                                void* source_profile_handle,
                                                int rendering_intent,
                                                bool use_black_point_comp) {
    if (!buffer || !source_profile_handle)
        return false;

    cmsHPROFILE src = (cmsHPROFILE)source_profile_handle;
    cmsHPROFILE dst = cmsCreate_sRGBProfile();
    if (!dst)
        return false;

    /* Clamp intent to 0-3 (perceptual, relative colorimetric, saturation, absolute) */
    if (rendering_intent < 0) rendering_intent = 0;
    if (rendering_intent > 3) rendering_intent = 3;
    cmsUInt32Number flags = cmsFLAGS_COPY_ALPHA;
    if (use_black_point_comp)
        flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
    cmsHTRANSFORM transform = cmsCreateTransform(
        src,  TYPE_RGBA_8,
        dst, TYPE_RGBA_8,
        (cmsUInt32Number)rendering_intent,
        flags
    );
    cmsCloseProfile(dst);
    if (!transform)
        return false;

    /* Do NOT allow premultiplied data through CMS: convert to straight alpha, transform, then premultiply for Cairo */
    cm_unpremultiply_argb32(buffer, pixel_count);
    argb32_swap_rb(buffer, pixel_count);
    cmsDoTransform(transform, buffer, buffer, (cmsUInt32Number)pixel_count);
    argb32_swap_rb(buffer, pixel_count);
    cm_premultiply_argb32(buffer, pixel_count);

    cmsDeleteTransform(transform);
    return true;
}

/* -------------------------------------------------------------------------
 * HDR linear conversion (linear space only; no tone mapping / gamma / 8-bit)
 * ------------------------------------------------------------------------- */

void cm_convert_hdr_linear_to_linear_srgb_from_profile(float* buffer, size_t pixel_count,
                                                       ColorProfile* source) {
    if (!buffer)
        return;

    if (!source)
        return; /* assume already linear sRGB / Linear Rec.709 */

    ColorProfile* dst = cm_profile_create_linear_srgb();
    if (!dst)
        return;

    ColorTransform* transform = cm_transform_create(source, dst, CM_PIXELFORMAT_RGB_FLOAT);
    if (!transform) {
        cm_profile_destroy(dst);
        return;
    }

    cm_transform_apply(transform, buffer, pixel_count);

    cm_transform_destroy(transform);
    cm_profile_destroy(dst);
}
