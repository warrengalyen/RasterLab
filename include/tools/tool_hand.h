/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_HAND_H
#define TOOL_HAND_H

#include "tools.h"

/**
 * Create the Hand Tool
 * @return Newly created Tool, or NULL on failure
 */
Tool* tool_hand_create(void);

#endif /* TOOL_HAND_H */
