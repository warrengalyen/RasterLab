/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/**
 * icc_utils.c — ICC profile extraction, creation, serialization, destruction.
 * Centralizes Little CMS profile handling so file format plugins do not call lcms directly.
 * No pixel transforms are performed in this module.
 */

#include "color_manager/icc_utils.h"
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Get ICC locale (language + country) from user's system. Fills lang[3] and country[3].
 * Uses g_get_language_names() — respects LC_ALL, LC_MESSAGES, LANG, and OS locale.
 * Fallback "en"/"US" if system locale is C or unknown. */
static void icc_get_system_locale(char lang[3], char country[3])
{
    lang[0] = lang[1] = lang[2] = '\0';
    country[0] = country[1] = country[2] = '\0';

    const gchar* const* names = g_get_language_names();
    if (!names)
        goto fallback;

    for (; *names; names++) {
        const char* s = *names;
        if (!s || !s[0] || (s[0] == 'C' && !s[1]))
            continue;

        /* Parse "en_US" or "en" */
        size_t i = 0;
        while (s[i] && s[i] != '_' && s[i] != '-' && s[i] != '.' && i < 2) {
            lang[i] = (char)s[i];
            i++;
        }
        lang[i < 2 ? i : 2] = '\0';
        if (i == 0)
            continue;

        if (s[i] == '_' || s[i] == '-') {
            s += i + 1;
            i = 0;
            while (s[i] && s[i] != '.' && i < 2) {
                country[i] = (char)s[i];
                i++;
            }
            country[i < 2 ? i : 2] = '\0';
        }
        return;
    }

fallback:
    lang[0] = 'e';
    lang[1] = 'n';
    country[0] = 'U';
    country[1] = 'S';
}

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

cmsHPROFILE icc_profile_from_memory_any(const void *data, size_t size)
{
    if (!data || size == 0 || size > (size_t)(cmsUInt32Number)(-1))
        return NULL;

    return cmsOpenProfileFromMem(data, (cmsUInt32Number)size);
}

bool icc_profile_is_cmyk(cmsHPROFILE profile)
{
    return profile && cmsGetColorSpace(profile) == cmsSigCmykData;
}

cmsHPROFILE icc_profile_from_file(const char* path)
{
    if (!path || !path[0])
        return NULL;

    gchar* data = NULL;
    gsize size = 0;
    if (!g_file_get_contents(path, &data, &size, NULL))
        return NULL;

    cmsHPROFILE h = icc_profile_from_memory(data, size);
    g_free(data);
    return h;
}

bool icc_profile_file_is_valid(const char* path)
{
    if (!path || !path[0])
        return false;

    gchar* data = NULL;
    gsize size = 0;
    if (!g_file_get_contents(path, &data, &size, NULL))
        return false;

    if (size == 0 || size > (size_t)(cmsUInt32Number)(-1)) {
        g_free(data);
        return false;
    }

    cmsHPROFILE h = cmsOpenProfileFromMem(data, (cmsUInt32Number)size);
    g_free(data);
    if (!h)
        return false;

    cmsCloseProfile(h);
    return true;
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

static bool xy_near(double a, double b, double tol) { return fabs(a - b) <= tol; }

static bool primaries_and_trc_match_srgb(cmsHPROFILE profile)
{
    const double tol = 0.003;

    cmsCIEXYZ* wp = (cmsCIEXYZ*)cmsReadTag(profile, cmsSigMediaWhitePointTag);
    if (!wp) return false;
    cmsCIExyY wp_xy;
    cmsXYZ2xyY(&wp_xy, wp);
    /* sRGB uses D50 adapted white in the ICC PCS (D50: x=0.3457, y=0.3585) */
    if (!xy_near(wp_xy.x, 0.3457, tol) || !xy_near(wp_xy.y, 0.3585, tol))
        return false;

    cmsCIEXYZ* r = (cmsCIEXYZ*)cmsReadTag(profile, cmsSigRedColorantTag);
    cmsCIEXYZ* g = (cmsCIEXYZ*)cmsReadTag(profile, cmsSigGreenColorantTag);
    cmsCIEXYZ* b = (cmsCIEXYZ*)cmsReadTag(profile, cmsSigBlueColorantTag);
    if (!r || !g || !b) return false;

    cmsCIExyY r_xy, g_xy, b_xy;
    cmsXYZ2xyY(&r_xy, r);
    cmsXYZ2xyY(&g_xy, g);
    cmsXYZ2xyY(&b_xy, b);

    /* sRGB primaries (Rec. 709) adapted to D50 PCS via Bradford.
     * Typical ICC values after adaptation: R(0.4361,0.2225), G(0.3851,0.7169), B(0.1431,0.0606).
     * Use a wider tolerance here because different profile generators
     * produce slightly different adapted values. */
    const double ptol = 0.006;
    if (!xy_near(r_xy.x, 0.4361, ptol) || !xy_near(r_xy.y, 0.2225, ptol))
        return false;
    if (!xy_near(g_xy.x, 0.3851, ptol) || !xy_near(g_xy.y, 0.7169, ptol))
        return false;
    if (!xy_near(b_xy.x, 0.1431, ptol) || !xy_near(b_xy.y, 0.0606, ptol))
        return false;

    /* TRC: sRGB effective gamma is ~2.2. Reject linear (1.0), ProPhoto-like (1.8), etc. */
    cmsToneCurve* r_trc = (cmsToneCurve*)cmsReadTag(profile, cmsSigRedTRCTag);
    cmsToneCurve* g_trc = (cmsToneCurve*)cmsReadTag(profile, cmsSigGreenTRCTag);
    cmsToneCurve* b_trc = (cmsToneCurve*)cmsReadTag(profile, cmsSigBlueTRCTag);
    if (!r_trc || !g_trc || !b_trc) return false;

    double gr = cmsEstimateGamma(r_trc, 0.1);
    double gg = cmsEstimateGamma(g_trc, 0.1);
    double gb = cmsEstimateGamma(b_trc, 0.1);
    if (gr < 2.0 || gr > 2.5 || gg < 2.0 || gg > 2.5 || gb < 2.0 || gb > 2.5)
        return false;

    return true;
}

bool icc_is_profile_srgb(cmsHPROFILE profile)
{
    if (!profile)
        return false;

    if (cmsGetColorSpace(profile) != cmsSigRgbData)
        return false;

    return primaries_and_trc_match_srgb(profile);
}

bool icc_get_profile_description(cmsHPROFILE profile, char* buf, size_t buf_size)
{
    if (!profile || !buf || buf_size == 0) {
        if (buf && buf_size > 0)
            buf[0] = '\0';
        return false;
    }

    buf[0] = '\0';
    char lang[3], country[3];
    icc_get_system_locale(lang, country);
    wchar_t wbuf[256];
    if (!cmsGetProfileInfo(profile, cmsInfoDescription, lang, country, wbuf, (cmsUInt32Number)(sizeof(wbuf) / sizeof(wbuf[0])))) {
        /* Many profiles only have en/US; try fallback */
        if (!cmsGetProfileInfo(profile, cmsInfoDescription, "en", "US", wbuf, (cmsUInt32Number)(sizeof(wbuf) / sizeof(wbuf[0]))))
            return false;
    }

    size_t i = 0;
    for (; wbuf[i] && i < buf_size - 1; i++) {
        buf[i] = (wbuf[i] > 0 && wbuf[i] < 128) ? (char)wbuf[i] : '?';
    }
    buf[i] = '\0';
    return i > 0;
}

void icc_destroy(cmsHPROFILE profile)
{
    if (profile)
        cmsCloseProfile(profile);
}
