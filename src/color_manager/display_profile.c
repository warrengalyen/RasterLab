/**
 * display_profile.c — Resolve display color profile (system or custom from settings).
 */

#include "color_manager/display_profile.h"
#include "app/settings.h"
#include "color_manager.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && defined(HAVE_LCMS2)
#include <windows.h>
#endif

#if HAVE_LCMS2

static int parse_monitor_index(const char* display_id) {
    if (!display_id || strncmp(display_id, "monitor-", 8) != 0)
        return -1;
    const char* p = display_id + 8;
    if (!*p || *p == '-')
        return -1;
    char* end = NULL;
    long n = strtol(p, &end, 10);
    if (end && *end == '\0' && n >= 0 && n <= 1024)
        return (int)n;
    return -1;
}

#ifdef _WIN32
/* Get system ICM profile path for the primary display (monitor index 0).
 * Returns a newly allocated string (g_free), or NULL. */
static char* get_system_icm_path_primary(void) {
    HDC hdc = GetDC(NULL);
    if (!hdc)
        return NULL;

    DWORD size = 0;
    GetICMProfileA(hdc, &size, NULL);
    if (size == 0) {
        ReleaseDC(NULL, hdc);
        return NULL;
    }

    char* path = (char*)g_malloc(size);
    if (!path) {
        ReleaseDC(NULL, hdc);
        return NULL;
    }

    if (!GetICMProfileA(hdc, &size, path)) {
        g_free(path);
        ReleaseDC(NULL, hdc);
        return NULL;
    }
    ReleaseDC(NULL, hdc);
    return path;
}
#endif

ColorProfile* cm_get_display_profile(const Settings* settings, const char* display_id) {
    if (!settings || !display_id)
        return NULL;

    gint mode = settings_get_cm_mode((Settings*)settings);

    if (mode == 1) {
        /* Custom: load from path in settings */
        const gchar* path = settings_get_cm_display_profile((Settings*)settings, display_id);
        if (!path || path[0] == '\0')
            return NULL;
        return cm_profile_from_file(path);
    }

    /* Mode 0: system profile. Only primary (monitor-0) supported for now. */
    int idx = parse_monitor_index(display_id);
    if (idx != 0)
        return NULL;

#ifdef _WIN32
    {
        gchar* path = get_system_icm_path_primary();
        if (!path) return NULL;
        ColorProfile* p = cm_profile_from_file(path);
        g_free(path);
        return p;
    }
#else
    /* TODO: Linux (colord/X11 _ICC_PROFILE), macOS (ColorSync) */
    (void)display_id;
    return NULL;
#endif
}

#else /* !HAVE_LCMS2 */

ColorProfile* cm_get_display_profile(const Settings* settings, const char* display_id) {
    (void)settings;
    (void)display_id;
    return NULL;
}

#endif
