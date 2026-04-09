/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_ERASER_H
#define TOOL_ERASER_H

#include "tools.h"

/**
 * Eraser Tool - Erase pixels from layers
 */

/**
 * Create the Eraser Tool
 * @return Newly created Tool instance configured for erasing
 */
Tool* tool_eraser_create(void);

#endif

