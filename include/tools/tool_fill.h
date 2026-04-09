/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_FILL_H
#define TOOL_FILL_H

#include "tools.h"

/**
 * Fill Tool - Flood fill regions with color
 */

/**
 * Create the Fill Tool
 * @return Newly created Tool instance configured for filling
 */
Tool* tool_fill_create(void);

#endif

