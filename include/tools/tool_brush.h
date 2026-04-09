/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_BRUSH_H
#define TOOL_BRUSH_H

#include "tools.h"

/**
 * Brush Tool - Draw freehand strokes on layers
 */

/**
 * Create the Brush Tool
 * @return Newly created Tool instance configured for drawing
 */
Tool* tool_brush_create(void);

#endif

