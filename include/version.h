/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef VERSION_H
#define VERSION_H

#define RASTERLAB_VERSION_MAJOR 1
#define RASTERLAB_VERSION_MINOR 0
#define RASTERLAB_VERSION_PATCH 0

/* Pre-release: set at most one to 1 (alpha xor beta; never both). */
#define RASTERLAB_VERSION_IS_ALPHA 1
#define RASTERLAB_VERSION_IS_BETA 0

/* Adjacent string literals: "1" "." "0" "." "0" -> "1.0.0" */
#define RASTERLAB_STRINGIFY_HELPER(x) #x
#define RASTERLAB_STRINGIFY(x) RASTERLAB_STRINGIFY_HELPER(x)
#define RASTERLAB_VERSION_LINE                   \
    RASTERLAB_STRINGIFY(RASTERLAB_VERSION_MAJOR) \
    "." RASTERLAB_STRINGIFY(RASTERLAB_VERSION_MINOR) "." RASTERLAB_STRINGIFY(RASTERLAB_VERSION_PATCH)

#if RASTERLAB_VERSION_IS_ALPHA
#define RASTERLAB_VERSION_SUFFIX " Alpha"
#elif RASTERLAB_VERSION_IS_BETA
#define RASTERLAB_VERSION_SUFFIX " Beta"
#else
#define RASTERLAB_VERSION_SUFFIX ""
#endif

/* Semantic version plus optional channel; use for About, logs, etc. */
#define RASTERLAB_VERSION_FOR_DISPLAY RASTERLAB_VERSION_LINE RASTERLAB_VERSION_SUFFIX

#endif /* VERSION_H */
