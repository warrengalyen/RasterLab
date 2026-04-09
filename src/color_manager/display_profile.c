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

#if defined(__APPLE__) && defined(HAVE_LCMS2)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#if defined(HAVE_DISPLAY_PROFILE_X11) && HAVE_DISPLAY_PROFILE_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
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

#if defined(__APPLE__) && defined(HAVE_LCMS2)
/* Get system display ICC profile for the main display (monitor index 0).
 * Uses Core Graphics: CGDisplayCopyColorSpace + CGColorSpaceCopyICCData (macOS 10.12+).
 * Caller owns returned ColorProfile (cm_profile_destroy). */
static ColorProfile* get_system_icc_profile_macos(void) {
    CGDirectDisplayID displayID = CGMainDisplayID();

    CGColorSpaceRef cs = CGDisplayCopyColorSpace(displayID);
    if (!cs)
        return NULL;

    CFDataRef iccData = CGColorSpaceCopyICCData(cs);
    CGColorSpaceRelease(cs);
    if (!iccData || CFDataGetLength(iccData) == 0) {
        if (iccData)
            CFRelease(iccData);
        return NULL;
    }

    const uint8_t* bytes = (const uint8_t*)CFDataGetBytePtr(iccData);
    size_t len = (size_t)CFDataGetLength(iccData);
    ColorProfile* p = cm_profile_from_memory(bytes, len);
    CFRelease(iccData);
    return p;
}
#endif

#if defined(HAVE_DISPLAY_PROFILE_X11) && HAVE_DISPLAY_PROFILE_X11
/* Get system display ICC profile from X11 root window _ICC_PROFILE (default screen).
 * Freedesktop ICC profile in X spec; used by GNOME, KDE, etc.
 * Caller owns returned ColorProfile (cm_profile_destroy). */
static ColorProfile* get_system_icc_profile_x11(void) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy)
        return NULL;

    Window root = RootWindow(dpy, DefaultScreen(dpy));
    Atom atom = XInternAtom(dpy, "_ICC_PROFILE", False);
    Atom typeReturn;
    int formatReturn;
    unsigned long nitemsReturn, bytesAfterReturn;
    unsigned char* propReturn = NULL;

    if (XGetWindowProperty(dpy, root, atom, 0, (1UL << 24) /* 16MB max */,
                           False, AnyPropertyType, &typeReturn, &formatReturn,
                           &nitemsReturn, &bytesAfterReturn, &propReturn) != Success ||
        !propReturn || nitemsReturn == 0) {
        if (propReturn)
            XFree(propReturn);
        XCloseDisplay(dpy);
        return NULL;
    }

    /* Format 8: one byte per element; nitemsReturn is byte count. Copy so we can XFree before lcms. */
    size_t len = (size_t)nitemsReturn;
    void* copy = g_malloc(len);
    if (!copy) {
        XFree(propReturn);
        XCloseDisplay(dpy);
        return NULL;
    }
    memcpy(copy, propReturn, len);
    XFree(propReturn);
    XCloseDisplay(dpy);

    ColorProfile* p = cm_profile_from_memory(copy, len);
    g_free(copy);
    return p;
}
#endif

ColorProfile* cm_get_display_profile(const Settings* settings, const char* display_id) {
    if (!settings || !display_id)
        return NULL;

    gint mode = settings_get_cm_mode((Settings*)settings);

    if (mode == 2) {
        /* CM_MODE_NONE: no display color management */
        return NULL;
    }
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
#elif defined(__APPLE__) && defined(HAVE_LCMS2)
    return get_system_icc_profile_macos();
#elif defined(HAVE_DISPLAY_PROFILE_X11) && HAVE_DISPLAY_PROFILE_X11
    return get_system_icc_profile_x11();
#else
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
