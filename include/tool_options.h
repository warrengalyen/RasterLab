#ifndef TOOL_OPTIONS_H
#define TOOL_OPTIONS_H

#include <glib.h>

/**
 * Tool Options - Configuration for drawing tools
 */

typedef struct {
    gfloat size;        /* Brush/Eraser size in pixels (1-100) */
    gfloat opacity;     /* Tool opacity 0-1 (0=transparent, 1=opaque) */
    gfloat hardness;    /* Brush hardness 0-1 (0=soft, 1=hard) */
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
void tool_options_free(ToolOptions *opts);

/**
 * Get the global tool options
 * @return Pointer to global ToolOptions instance
 */
ToolOptions* tool_options_get_global(void);

/**
 * Set size
 * @param opts The tool options
 * @param size New size (clamped to 1-100)
 */
void tool_options_set_size(ToolOptions *opts, gfloat size);

/**
 * Set opacity
 * @param opts The tool options
 * @param opacity New opacity (clamped to 0-1)
 */
void tool_options_set_opacity(ToolOptions *opts, gfloat opacity);

/**
 * Set hardness
 * @param opts The tool options
 * @param hardness New hardness (clamped to 0-1)
 */
void tool_options_set_hardness(ToolOptions *opts, gfloat hardness);

#endif /* TOOL_OPTIONS_H */

