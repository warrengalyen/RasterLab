/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef BUILTIN_PLUGINS_H
#define BUILTIN_PLUGINS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register built-in plugins (PNG, JPEG, etc.)
 * Should be called during application startup
 */
void builtin_plugins_register(void);

#ifdef __cplusplus
}
#endif

#endif /* BUILTIN_PLUGINS_H */
