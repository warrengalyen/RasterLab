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

    /* Ellipse select tool options */
    SelectionCombineMode ellipse_select_combine;  /* How to combine with existing selection */
    SelectionSmoothingMode ellipse_select_smooth; /* Edge smoothing mode */
    gfloat ellipse_select_feather;                /* Feather radius in pixels */
    gboolean ellipse_select_animate;              /* Animate marching ants */

    /* Move tool options */
    gboolean move_auto_select_layer; /* Auto-select layer under cursor based on visibility */

    /* Color picker tool options */
    gint color_picker_sample_radius;      /* Sample radius 0–100, default 0 (single pixel) */
    gboolean color_picker_sample_from_layer; /* TRUE = layer, FALSE = image; default TRUE */

    /* Crop tool options */
    gint crop_constraint_mode;       /* 0=Free, 1=Fixed Ratio, 2=Fixed Size */
    gint crop_ratio_w;               /* Ratio width (e.g. 16) */
    gint crop_ratio_h;               /* Ratio height (e.g. 9) */
    gint crop_width;                 /* Fixed width in pixels */
    gint crop_height;                /* Fixed height in pixels */
    gboolean crop_link;              /* Link width/height to maintain aspect ratio */
    gboolean crop_delete_pixels;     /* Delete pixels vs non-destructive */
    gboolean crop_grow_canvas;       /* Grow canvas if crop exceeds image bounds */
    gboolean crop_darken_outside;    /* Darken area outside crop rect */
    gfloat crop_darken_opacity;      /* Darken overlay opacity 0–100 (default 60) */
    gint crop_overlay_mode;          /* 0=None, 1=Rule of Thirds, 2=Phi Grid, 3=Golden Spiral, 4=Diagonal, 5=Center Lines */
    gboolean crop_snap;              /* Snap to guides */
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

/**
 * Set ellipse select combine mode
 * @param opts The tool options
 * @param combine The combine mode (NEW, ADD, SUBTRACT, INTERSECT)
 */
void tool_options_set_ellipse_select_combine(ToolOptions* opts, SelectionCombineMode combine);

/**
 * Get ellipse select combine mode
 * @param opts The tool options
 * @return The current combine mode
 */
SelectionCombineMode tool_options_get_ellipse_select_combine(ToolOptions* opts);

/**
 * Set ellipse select smoothing mode
 * @param opts The tool options
 * @param smooth The smoothing mode (NONE, ANTIALIASED, FEATHERED)
 */
void tool_options_set_ellipse_select_smooth(ToolOptions* opts, SelectionSmoothingMode smooth);

/**
 * Get ellipse select smoothing mode
 * @param opts The tool options
 * @return The current smoothing mode
 */
SelectionSmoothingMode tool_options_get_ellipse_select_smooth(ToolOptions* opts);

/**
 * Set ellipse select feather radius
 * @param opts The tool options
 * @param feather Feather radius in pixels
 */
void tool_options_set_ellipse_select_feather(ToolOptions* opts, gfloat feather);

/**
 * Get ellipse select feather radius
 * @param opts The tool options
 * @return The current feather radius
 */
gfloat tool_options_get_ellipse_select_feather(ToolOptions* opts);

/**
 * Set ellipse select animation
 * @param opts The tool options
 * @param animate TRUE to enable marching ants animation, FALSE to disable
 */
void tool_options_set_ellipse_select_animate(ToolOptions* opts, gboolean animate);

/**
 * Get ellipse select animation
 * @param opts The tool options
 * @return TRUE if animation enabled, FALSE otherwise
 */
gboolean tool_options_get_ellipse_select_animate(ToolOptions* opts);

/**
 * Set move tool auto select layer
 * @param opts The tool options
 * @param auto_select TRUE to enable auto-select, FALSE to use selected layer
 */
void tool_options_set_move_auto_select(ToolOptions* opts, gboolean auto_select);

/**
 * Get move tool auto select layer
 * @param opts The tool options
 * @return TRUE if auto-select enabled, FALSE otherwise
 */
gboolean tool_options_get_move_auto_select(ToolOptions* opts);

/**
 * Set color picker sample radius (0–100)
 */
void tool_options_set_color_picker_sample_radius(ToolOptions* opts, gint radius);

/**
 * Get color picker sample radius
 */
gint tool_options_get_color_picker_sample_radius(ToolOptions* opts);

/**
 * Set color picker sample from (TRUE = layer, FALSE = image)
 */
void tool_options_set_color_picker_sample_from_layer(ToolOptions* opts, gboolean from_layer);

/**
 * Get color picker sample from (TRUE = layer, FALSE = image)
 */
gboolean tool_options_get_color_picker_sample_from_layer(ToolOptions* opts);

/**
 * Crop tool: set/get constraint mode (0=Free, 1=Ratio, 2=Size)
 */
void tool_options_set_crop_constraint_mode(ToolOptions* opts, gint mode);
gint tool_options_get_crop_constraint_mode(ToolOptions* opts);

/**
 * Crop tool: set/get ratio (width:height)
 */
void tool_options_set_crop_ratio(ToolOptions* opts, gint w, gint h);
void tool_options_get_crop_ratio(ToolOptions* opts, gint* w, gint* h);

/**
 * Crop tool: set/get fixed size
 */
void tool_options_set_crop_size(ToolOptions* opts, gint w, gint h);
void tool_options_get_crop_size(ToolOptions* opts, gint* w, gint* h);

/**
 * Crop tool: set/get link toggle (maintain aspect ratio when editing size)
 */
void tool_options_set_crop_link(ToolOptions* opts, gboolean link);
gboolean tool_options_get_crop_link(ToolOptions* opts);

/**
 * Crop tool: set/get delete pixels, grow canvas
 */
void tool_options_set_crop_delete_pixels(ToolOptions* opts, gboolean delete_pixels);
gboolean tool_options_get_crop_delete_pixels(ToolOptions* opts);
void tool_options_set_crop_grow_canvas(ToolOptions* opts, gboolean grow);
gboolean tool_options_get_crop_grow_canvas(ToolOptions* opts);

/**
 * Crop tool: set/get darken outside and opacity
 */
void tool_options_set_crop_darken_outside(ToolOptions* opts, gboolean darken);
gboolean tool_options_get_crop_darken_outside(ToolOptions* opts);
void tool_options_set_crop_darken_opacity(ToolOptions* opts, gfloat opacity);
gfloat tool_options_get_crop_darken_opacity(ToolOptions* opts);

/**
 * Crop tool: set/get overlay mode (0=None, 1=Rule of Thirds, 2=Phi Grid, 3=Golden Spiral, 4=Diagonal, 5=Center Lines)
 */
void tool_options_set_crop_overlay_mode(ToolOptions* opts, gint mode);
gint tool_options_get_crop_overlay_mode(ToolOptions* opts);

/**
 * Crop tool: set/get snap to guides
 */
void tool_options_set_crop_snap(ToolOptions* opts, gboolean snap);
gboolean tool_options_get_crop_snap(ToolOptions* opts);

#endif /* TOOL_OPTIONS_H */
