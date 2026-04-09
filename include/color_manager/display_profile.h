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
 * display_profile.h — Resolve display color profile from settings (system or custom).
 */

#ifndef DISPLAY_PROFILE_H
#define DISPLAY_PROFILE_H

#include "app/settings.h"
#include "color_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the color profile to use for display output for the given display.
 * - Mode 0 (system): returns the OS-assigned profile for that display when available.
 * - Mode 1 (custom): returns the profile loaded from the path in settings for that display_id.
 * - If no profile is available (e.g. no system profile, or invalid path), returns NULL
 *   and the pipeline should fall back to internal sRGB (no display transform).
 *
 * \param settings  Application settings (must not be NULL)
 * \param display_id Display identifier (e.g. "monitor-0"). Must match IDs used in settings.
 * \return          New ColorProfile* (caller must cm_profile_destroy), or NULL
 */
ColorProfile* cm_get_display_profile(const Settings* settings, const char* display_id);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_PROFILE_H */
