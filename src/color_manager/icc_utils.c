/**
 * icc_utils.c — ICC profile extraction, creation, serialization, destruction.
 * Centralizes Little CMS profile handling so file format plugins do not call lcms directly.
 * No pixel transforms are performed in this module.
 */

#include "color_manager/icc_utils.h"
#include <glib.h>
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
    char lang[3], country[3];
    icc_get_system_locale(lang, country);
    wchar_t buf[256];
    if (cmsGetProfileInfo(profile, cmsInfoDescription, lang, country, buf, (cmsUInt32Number)sizeof(buf)))
        return wcsstr(buf, L"sRGB") != NULL;
    /* Many profiles only have en/US; try fallback */
    if ((lang[0] != 'e' || lang[1] != 'n') &&
        cmsGetProfileInfo(profile, cmsInfoDescription, "en", "US", buf, (cmsUInt32Number)sizeof(buf)))
        return wcsstr(buf, L"sRGB") != NULL;
    return false;
}

bool icc_is_profile_srgb(cmsHPROFILE profile)
{
    if (!profile)
        return false;

    if (cmsGetColorSpace(profile) != cmsSigRgbData)
        return false;

    return description_contains_srgb(profile);
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
