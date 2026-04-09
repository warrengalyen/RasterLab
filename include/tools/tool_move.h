/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_MOVE_H
#define TOOL_MOVE_H

#include "tools.h"

/* Forward declaration */
typedef struct ImageDocument ImageDocument;

/**
 * Move Tool - Move/translate layers on canvas
 */

/**
 * Create the Move Tool
 * @return Newly created Tool instance configured for moving layers
 */
Tool* tool_move_create(void);

/**
 * Draw move tool preview during dragging
 * @param doc The active image document
 * @param cr Cairo context to draw on
 * @param zoom Current zoom level
 */
void tool_move_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

#endif /* TOOL_MOVE_H */
