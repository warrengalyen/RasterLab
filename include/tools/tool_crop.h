#ifndef TOOL_CROP_H
#define TOOL_CROP_H

#include "tools.h"

/* Forward declaration */
typedef struct ImageDocument ImageDocument;

/**
 * Crop tool drag mode values:
 * -2 = new crop (creating rectangle), -1 = move, 0-7 = resize handle
 * Handle indices: 0-3 corners (TL, TR, BL, BR), 4-7 edges (T, R, B, L)
 */

/**
 * Crop Tool state and options
 */
typedef struct {
    gboolean is_active;        /* Tool has an active crop rectangle */
    gboolean is_dragging;      /* Currently dragging (new, move, or resize) */
    gint start_x;              /* Mouse down position X in image space */
    gint start_y;              /* Mouse down position Y in image space */
    gint current_x;            /* Current mouse position X in image space */
    gint current_y;            /* Current mouse position Y in image space */
    gint rect_x;               /* Crop rectangle X */
    gint rect_y;               /* Crop rectangle Y */
    gint rect_w;               /* Crop rectangle width */
    gint rect_h;               /* Crop rectangle height */
    gint drag_mode;            /* CROP_DRAG_* or handle index 0-7 */
    gint hovered_handle;       /* Handle under mouse (-1 = none, 0-7 = handle) */
} CropToolState;

/**
 * Create the Crop Tool
 * @return Newly created Tool, or NULL on failure
 */
Tool* tool_crop_create(void);

/**
 * Update crop rectangle to match current ratio/size options (when not dragging)
 * @param doc The active image document
 * @param registry The tool registry (to get crop tool state)
 */
void tool_crop_update_rect_from_options(ImageDocument* doc, void* registry);

/**
 * Draw crop overlay during drag/edit
 * @param doc The active image document
 * @param cr Cairo context to draw on
 * @param zoom Current zoom level
 */
void tool_crop_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom);

/**
 * Reset crop tool state when switching away
 * @param tool The crop tool
 */
void tool_crop_reset(Tool* tool);

/**
 * Initialize crop preview at full canvas size (for tool activation or Reset)
 * @param tool The crop tool
 * @param doc The active image document
 */
void tool_crop_init_at_canvas(Tool* tool, struct ImageDocument* doc);

/**
 * Reset crop preview to full canvas size
 * @param tool The crop tool
 * @param doc The active image document
 */
void tool_crop_reset_to_canvas(Tool* tool, struct ImageDocument* doc);

/**
 * Get the crop rectangle from the crop tool (when active)
 * @param tool The crop tool (from tool_manager_get(registry, TOOL_CROP))
 * @param out_x Output: left edge
 * @param out_y Output: top edge
 * @param out_w Output: width
 * @param out_h Output: height
 * @return TRUE if crop is active and has valid rect, FALSE otherwise
 */
gboolean tool_crop_get_rect(Tool* tool, gint* out_x, gint* out_y, gint* out_w, gint* out_h);

#endif /* TOOL_CROP_H */
