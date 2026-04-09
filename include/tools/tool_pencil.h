/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_PENCIL_H
#define TOOL_PENCIL_H

#include "tools.h"

/**
 * Pencil Tool - Draw precise, hard-edged strokes on layers
 */

/**
 * Create the Pencil Tool
 * @return Newly created Tool instance configured for precise drawing
 */
Tool* tool_pencil_create(void);

#endif
