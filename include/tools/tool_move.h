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
