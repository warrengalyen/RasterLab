#ifndef TOOL_OPTIONS_H
#define TOOL_OPTIONS_H

#include "selection.h" /* For SelectionCombineMode and SelectionSmoothingMode */
#include "tools.h"
#include <glib.h>

/**
 * Tool Options - Configuration for drawing tools
 */

typedef struct {
    gfloat size;               /* Brush/Eraser size in pixels (brush: 1-2000, eraser: 1-100) */
    gfloat opacity;            /* Tool opacity 0-1 (0=transparent, 1=opaque) */
    gfloat hardness;           /* Brush hardness 0-1 (0=soft, 1=hard) */
    gfloat flow;               /* Eraser flow 0-1 (0=no effect, 1=full effect) */
    gfloat spacing;            /* Eraser spacing 0-1 (0.01=very close, 1.0=far apart) */
    gfloat tolerance;          /* Paint bucket tolerance 0-100 (0=exact match, 100=all colors) */
    gboolean fill_contiguous;  /* Paint bucket fill area: TRUE=contiguous, FALSE=global */
    gboolean fill_antialiased; /* Paint bucket antialiasing: TRUE=smooth edges, FALSE=hard edges */

    /* Pencil tool options */
    gboolean pencil_antialias;        /* Pencil antialiasing: TRUE=smooth edges, FALSE=hard edges */
    gboolean pencil_align_pixel_grid; /* Pencil pixel grid alignment: TRUE=snap to grid, FALSE=free */

    /* Rectangle select tool options */
    SelectionCombineMode rect_select_combine;  /* How to combine with existing selection */
    SelectionSmoothingMode rect_select_smooth; /* Edge smoothing mode */
    gfloat rect_select_feather;                /* Feather radius in pixels */
    gboolean rect_select_animate;              /* Animate marching ants */
} ToolOptions;

/**
 * Create a new tool options instance with defaults
 * @return Newly created ToolOptions with default values
 */
ToolOptions* tool_options_new(void);

/**
 * Free a tool options instance
 * @param opts The options to free
 */
void tool_options_free(ToolOptions* opts);

/**
 * Get the global tool options
 * @return Pointer to global ToolOptions instance
 */
ToolOptions* tool_options_get_global(void);

/**
 * Get tool options for a specific tool type
 * @param tool_type The tool type (TOOL_BRUSH, TOOL_ERASER, etc.)
 * @return Pointer to ToolOptions for the specified tool type
 */
ToolOptions* tool_options_get_for_tool(ToolType tool_type);

/**
 * Set size
 * @param opts The tool options
 * @param size New size (clamped to minimum 1, maximum depends on tool)
 */
void tool_options_set_size(ToolOptions* opts, gfloat size);

/**
 * Set opacity
 * @param opts The tool options
 * @param opacity New opacity (clamped to 0-1)
 */
void tool_options_set_opacity(ToolOptions* opts, gfloat opacity);

/**
 * Set hardness
 * @param opts The tool options
 * @param hardness New hardness (clamped to 0-1)
 */
void tool_options_set_hardness(ToolOptions* opts, gfloat hardness);

/**
 * Set flow
 * @param opts The tool options
 * @param flow New flow (clamped to 0-1)
 */
void tool_options_set_flow(ToolOptions* opts, gfloat flow);

/**
 * Set spacing
 * @param opts The tool options
 * @param spacing New spacing (clamped to 0-1)
 */
void tool_options_set_spacing(ToolOptions* opts, gfloat spacing);

/**
 * Set tolerance
 * @param opts The tool options
 * @param tolerance New tolerance (clamped to 0-100)
 */
void tool_options_set_tolerance(ToolOptions* opts, gfloat tolerance);

/**
 * Set fill area mode
 * @param opts The tool options
 * @param contiguous TRUE for contiguous fill, FALSE for global fill
 */
void tool_options_set_fill_contiguous(ToolOptions* opts, gboolean contiguous);

/**
 * Set fill antialiasing
 * @param opts The tool options
 * @param antialiased TRUE for antialiased edges, FALSE for hard edges
 */
void tool_options_set_fill_antialiased(ToolOptions* opts, gboolean antialiased);

/**
 * Set rectangle select combine mode
 * @param opts The tool options
 * @param combine The combine mode (NEW, ADD, SUBTRACT, INTERSECT)
 */
void tool_options_set_rect_select_combine(ToolOptions* opts, SelectionCombineMode combine);

/**
 * Get rectangle select combine mode
 * @param opts The tool options
 * @return The current combine mode
 */
SelectionCombineMode tool_options_get_rect_select_combine(ToolOptions* opts);

/**
 * Set rectangle select smoothing mode
 * @param opts The tool options
 * @param smooth The smoothing mode (NONE, ANTIALIASED, FEATHERED)
 */
void tool_options_set_rect_select_smooth(ToolOptions* opts, SelectionSmoothingMode smooth);

/**
 * Get rectangle select smoothing mode
 * @param opts The tool options
 * @return The current smoothing mode
 */
SelectionSmoothingMode tool_options_get_rect_select_smooth(ToolOptions* opts);

/**
 * Set rectangle select feather radius
 * @param opts The tool options
 * @param feather Feather radius in pixels
 */
void tool_options_set_rect_select_feather(ToolOptions* opts, gfloat feather);

/**
 * Get rectangle select feather radius
 * @param opts The tool options
 * @return The current feather radius
 */
gfloat tool_options_get_rect_select_feather(ToolOptions* opts);

/**
 * Set rectangle select animation
 * @param opts The tool options
 * @param animate TRUE to enable marching ants animation, FALSE to disable
 */
void tool_options_set_rect_select_animate(ToolOptions* opts, gboolean animate);

/**
 * Get rectangle select animation
 * @param opts The tool options
 * @return TRUE if animation enabled, FALSE otherwise
 */
gboolean tool_options_get_rect_select_animate(ToolOptions* opts);

#endif /* TOOL_OPTIONS_H */
