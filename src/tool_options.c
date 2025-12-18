#include "tool_options.h"
#include "tools.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global tool options instance (for backward compatibility) */
static ToolOptions* g_tool_options = NULL;

/* Per-tool options storage */
static ToolOptions* g_tool_options_per_tool[TOOL_COUNT] = {NULL, NULL, NULL, NULL};

/**
 * Create a new tool options instance with defaults
 */
ToolOptions* tool_options_new(void) {
    ToolOptions* opts = (ToolOptions*)g_malloc(sizeof(ToolOptions));

    /* Set default values */
    opts->size = 5.0f;              /* 5px brush size */
    opts->opacity = 1.0f;           /* 100% opacity */
    opts->hardness = 1.0f;          /* Hard edge */
    opts->flow = 1.0f;              /* Full flow */
    opts->spacing = 0.25f;          /* 25% spacing (default) */
    opts->tolerance = 15.0f;        /* 10% tolerance (default) */
    opts->fill_contiguous = TRUE;   /* Contiguous fill by default */
    opts->fill_antialiased = FALSE; /* Hard edges by default */

    return opts;
}

/**
 * Free a tool options instance
 */
void tool_options_free(ToolOptions* opts) {
    if (opts) {
        g_free(opts);
    }
}

/**
 * Get the global tool options
 */
ToolOptions* tool_options_get_global(void) {
    if (!g_tool_options) {
        g_tool_options = tool_options_new();
    }
    return g_tool_options;
}

/**
 * Set size
 */
void tool_options_set_size(ToolOptions* opts, gfloat size) {
    if (!opts) {
        return;
    }

    /* Clamp size to minimum 1.0, but don't cap the maximum as different tools have different limits
       (brush: 0-2000, eraser: 0-100) */
    opts->size = fmaxf(1.0f, size);
}

/**
 * Set opacity
 */
void tool_options_set_opacity(ToolOptions* opts, gfloat opacity) {
    if (!opts) {
        return;
    }

    opts->opacity = fmaxf(0.0f, fminf(1.0f, opacity));
}

/**
 * Set hardness
 */
void tool_options_set_hardness(ToolOptions* opts, gfloat hardness) {
    if (!opts) {
        return;
    }

    /* Clamp hardness to0-1 */
    opts->hardness = fmaxf(0.0f, fminf(1.0f, hardness));
}

/**
 * Set flow
 */
void tool_options_set_flow(ToolOptions* opts, gfloat flow) {
    if (!opts) {
        return;
    }

    opts->flow = fmaxf(0.0f, fminf(1.0f, flow));
}

/**
 * Set spacing
 */
void tool_options_set_spacing(ToolOptions* opts, gfloat spacing) {
    if (!opts) {
        return;
    }

    /* Clamp spacing to 0-1 */
    opts->spacing = fmaxf(0.0f, fminf(1.0f, spacing));
}

/**
 * Set tolerance
 */
void tool_options_set_tolerance(ToolOptions* opts, gfloat tolerance) {
    if (!opts) {
        return;
    }

    opts->tolerance = fmaxf(0.0f, fminf(100.0f, tolerance));
}

/**
 * Set fill area mode
 */
void tool_options_set_fill_contiguous(ToolOptions* opts, gboolean contiguous) {
    if (!opts) {
        return;
    }

    opts->fill_contiguous = contiguous ? TRUE : FALSE;
}

/**
 * Set fill antialiasing
 */
void tool_options_set_fill_antialiased(ToolOptions* opts, gboolean antialiased) {
    if (!opts) {
        return;
    }

    opts->fill_antialiased = antialiased ? TRUE : FALSE;
}

/**
 * Get tool options for a specific tool type
 */
ToolOptions* tool_options_get_for_tool(ToolType tool_type) {
    if (tool_type < 0 || tool_type >= TOOL_COUNT) {
        return tool_options_get_global();
    }

    if (!g_tool_options_per_tool[tool_type]) {
        g_tool_options_per_tool[tool_type] = tool_options_new();
    }

    return g_tool_options_per_tool[tool_type];
}

/**
 * Save current tool options before switching tools
 */
void tool_options_save_for_tool(ToolType tool_type) {
    ToolOptions* global_opts;
    ToolOptions* tool_opts;

    if (tool_type < 0 || tool_type >= TOOL_COUNT) {
        return;
    }

    global_opts = tool_options_get_global();
    tool_opts = tool_options_get_for_tool(tool_type);

    if (global_opts && tool_opts) {
        /* Copy current global options to tool-specific storage */
        memcpy(tool_opts, global_opts, sizeof(ToolOptions));
    }
}

/**
 * Load tool options when switching to a tool
 */
void tool_options_load_for_tool(ToolType tool_type) {
    ToolOptions* global_opts;
    ToolOptions* tool_opts;

    if (tool_type < 0 || tool_type >= TOOL_COUNT) {
        return;
    }

    global_opts = tool_options_get_global();
    tool_opts = tool_options_get_for_tool(tool_type);

    if (global_opts && tool_opts) {
        /* Copy tool-specific options to global (which UI reads from) */
        memcpy(global_opts, tool_opts, sizeof(ToolOptions));
    }
}
