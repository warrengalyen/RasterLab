#include "tool_options.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Global tool options instance */
static ToolOptions *g_tool_options = NULL;

/**
 * Create a new tool options instance with defaults
 */
ToolOptions* tool_options_new(void)
{
    ToolOptions *opts = (ToolOptions *)g_malloc(sizeof(ToolOptions));
    
    /* Set default values */
    opts->size = 5.0f;          /* 5px brush size */
    opts->opacity = 1.0f;       /* 100% opacity */
    opts->hardness = 1.0f;      /* Hard edge */
    opts->flow = 1.0f;          /* Full flow */
    
    return opts;
}

/**
 * Free a tool options instance
 */
void tool_options_free(ToolOptions *opts)
{
    if (opts) {
        g_free(opts);
    }
}

/**
 * Get the global tool options
 */
ToolOptions* tool_options_get_global(void)
{
    if (!g_tool_options) {
        g_tool_options = tool_options_new();
    }
    return g_tool_options;
}

/**
 * Set size
 */
void tool_options_set_size(ToolOptions *opts, gfloat size)
{
    if (!opts) {
        return;
    }
    
    /* Clamp size to 1-100 */
    opts->size = fmaxf(1.0f, fminf(100.0f, size));
    //printf("Tool size set to: %.1f\n", opts->size);
}

/**
 * Set opacity
 */
void tool_options_set_opacity(ToolOptions *opts, gfloat opacity)
{
    if (!opts) {
        return;
    }
    
    /* Clamp opacity to 0-1 */
    opts->opacity = fmaxf(0.0f, fminf(1.0f, opacity));
    //printf("Tool opacity set to: %.2f\n", opts->opacity);
}

/**
 * Set hardness
 */
void tool_options_set_hardness(ToolOptions *opts, gfloat hardness)
{
    if (!opts) {
        return;
    }
    
    /* Clamp hardness to 0-1 */
    opts->hardness = fmaxf(0.0f, fminf(1.0f, hardness));
    //printf("Tool hardness set to: %.2f\n", opts->hardness);
}

/**
 * Set flow
 */
void tool_options_set_flow(ToolOptions *opts, gfloat flow)
{
    if (!opts) {
        return;
    }
    
    /* Clamp flow to 0-1 */
    opts->flow = fmaxf(0.0f, fminf(1.0f, flow));
    //printf("Tool flow set to: %.2f\n", opts->flow);
}

