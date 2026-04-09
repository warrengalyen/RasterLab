/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_COLORPICKER_H
#define TOOL_COLORPICKER_H

#include "tools.h"

/**
 * Color Picker Tool - Sample colors from canvas
 */

/**
 * Create the Color Picker Tool
 * @return Newly created Tool instance configured for color picking
 */
Tool* tool_colorpicker_create(void);

/**
 * Reset preview throttle (e.g. when leaving canvas). Next move will update immediately.
 */
void tool_colorpicker_reset_preview_throttle(void);

#endif
