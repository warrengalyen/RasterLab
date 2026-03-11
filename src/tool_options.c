#include "tool_options.h"
#include "app/settings.h"
#include "tools.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global tool options instance (for backward compatibility) */
static ToolOptions* g_tool_options = NULL;

/* Per-tool options storage */
static ToolOptions* g_tool_options_per_tool[TOOL_COUNT] = {NULL};

/**
 * Create a new tool options instance with defaults
 */
ToolOptions* tool_options_new(void) {
    ToolOptions* opts = (ToolOptions*)g_malloc(sizeof(ToolOptions));

    /* Set default values from settings (for consistency) */
    opts->size = settings_get_default_tool_size();
    opts->opacity = settings_get_default_tool_opacity();
    opts->hardness = settings_get_default_tool_hardness();
    opts->flow = settings_get_default_tool_flow();
    opts->spacing = settings_get_default_tool_spacing();
    opts->tolerance = settings_get_default_tool_tolerance();
    opts->fill_contiguous = settings_get_default_tool_fill_contiguous();
    opts->fill_antialiased = settings_get_default_tool_fill_antialiased();
    opts->fill_compare_mode = FILL_COMPARE_COLOR;

    /* Initialize pencil tool options with defaults */
    opts->pencil_antialias = FALSE;       /* Default: no antialiasing */
    opts->pencil_align_pixel_grid = TRUE; /* Default: align to pixel grid */

    /* Initialize rectangle select tool options */
    opts->rect_select_combine = SELECTION_COMBINE_NEW;
    opts->rect_select_smooth = SELECTION_SMOOTH_ANTIALIASED;
    opts->rect_select_feather = 0.0f;
    opts->rect_select_animate = TRUE;

    /* Initialize ellipse select tool options */
    opts->ellipse_select_combine = SELECTION_COMBINE_NEW;
    opts->ellipse_select_smooth = SELECTION_SMOOTH_ANTIALIASED;
    opts->ellipse_select_feather = 0.0f;
    opts->ellipse_select_animate = TRUE;

    /* Initialize polygon select tool options */
    opts->polygon_select_combine = SELECTION_COMBINE_NEW;
    opts->polygon_select_smooth = SELECTION_SMOOTH_ANTIALIASED;
    opts->polygon_select_feather = 0.0f;
    opts->polygon_select_animate = TRUE;
    opts->polygon_select_curvature = 0.0f;
    opts->polygon_select_area = 0;   /* 0=interior */
    opts->polygon_select_border_width = 1;

    /* Initialize lasso select tool options */
    opts->lasso_select_combine = SELECTION_COMBINE_NEW;
    opts->lasso_select_smooth = SELECTION_SMOOTH_ANTIALIASED;
    opts->lasso_select_feather = 0.0f;
    opts->lasso_select_animate = TRUE;
    opts->lasso_select_area = 0;
    opts->lasso_select_border_width = 1;

    /* Initialize move tool options */
    opts->move_auto_select_layer = TRUE; /* Default: auto-select enabled */

    /* Initialize color picker tool options */
    opts->color_picker_sample_radius = 0;        /* Default: single pixel */
    opts->color_picker_sample_from_layer = TRUE; /* Default: sample from layer */

    /* Initialize crop tool options */
    opts->crop_constraint_mode = 0; /* 0=Free */
    opts->crop_ratio_w = 16;
    opts->crop_ratio_h = 9;
    opts->crop_width = 1920;
    opts->crop_height = 1080;
    opts->crop_link = TRUE;
    opts->crop_delete_pixels = TRUE;
    opts->crop_grow_canvas = FALSE;
    opts->crop_darken_outside = FALSE;
    opts->crop_darken_opacity = 60.0f;
    opts->crop_overlay_mode = 1;
    opts->crop_snap = FALSE;

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
 * Set fill pixel comparison mode
 */
void tool_options_set_fill_compare_mode(ToolOptions* opts, FillCompareMode mode) {
    if (!opts) {
        return;
    }
    opts->fill_compare_mode = mode;
}

/**
 * Get fill pixel comparison mode
 */
FillCompareMode tool_options_get_fill_compare_mode(ToolOptions* opts) {
    if (!opts) {
        return FILL_COMPARE_COLOR;
    }
    return opts->fill_compare_mode;
}

/**
 * Set rectangle select combine mode
 */
void tool_options_set_rect_select_combine(ToolOptions* opts, SelectionCombineMode combine) {
    if (!opts) {
        return;
    }
    opts->rect_select_combine = combine;
}

/**
 * Get rectangle select combine mode
 */
SelectionCombineMode tool_options_get_rect_select_combine(ToolOptions* opts) {
    if (!opts) {
        return SELECTION_COMBINE_NEW;
    }
    return opts->rect_select_combine;
}

/**
 * Set rectangle select smoothing mode
 */
void tool_options_set_rect_select_smooth(ToolOptions* opts, SelectionSmoothingMode smooth) {
    if (!opts) {
        return;
    }
    opts->rect_select_smooth = smooth;
}

/**
 * Get rectangle select smoothing mode
 */
SelectionSmoothingMode tool_options_get_rect_select_smooth(ToolOptions* opts) {
    if (!opts) {
        return SELECTION_SMOOTH_NONE;
    }
    return opts->rect_select_smooth;
}

/**
 * Set rectangle select feather radius
 */
void tool_options_set_rect_select_feather(ToolOptions* opts, gfloat feather) {
    if (!opts) {
        return;
    }
    opts->rect_select_feather = (feather < 0.0f) ? 0.0f : feather;
}

/**
 * Get rectangle select feather radius
 */
gfloat tool_options_get_rect_select_feather(ToolOptions* opts) {
    if (!opts) {
        return 0.0f;
    }
    return opts->rect_select_feather;
}

/**
 * Set rectangle select animation
 */
void tool_options_set_rect_select_animate(ToolOptions* opts, gboolean animate) {
    if (!opts) {
        return;
    }
    opts->rect_select_animate = animate ? TRUE : FALSE;
}

/**
 * Get rectangle select animation
 */
gboolean tool_options_get_rect_select_animate(ToolOptions* opts) {
    if (!opts) {
        return TRUE; /* Default to animated */
    }
    return opts->rect_select_animate;
}

/**
 * Set ellipse select combine mode
 */
void tool_options_set_ellipse_select_combine(ToolOptions* opts, SelectionCombineMode combine) {
    if (!opts) {
        return;
    }
    opts->ellipse_select_combine = combine;
}

/**
 * Get ellipse select combine mode
 */
SelectionCombineMode tool_options_get_ellipse_select_combine(ToolOptions* opts) {
    if (!opts) {
        return SELECTION_COMBINE_NEW;
    }
    return opts->ellipse_select_combine;
}

/**
 * Set ellipse select smoothing mode
 */
void tool_options_set_ellipse_select_smooth(ToolOptions* opts, SelectionSmoothingMode smooth) {
    if (!opts) {
        return;
    }
    opts->ellipse_select_smooth = smooth;
}

/**
 * Get ellipse select smoothing mode
 */
SelectionSmoothingMode tool_options_get_ellipse_select_smooth(ToolOptions* opts) {
    if (!opts) {
        return SELECTION_SMOOTH_NONE;
    }
    return opts->ellipse_select_smooth;
}

/**
 * Set ellipse select feather radius
 */
void tool_options_set_ellipse_select_feather(ToolOptions* opts, gfloat feather) {
    if (!opts) {
        return;
    }
    opts->ellipse_select_feather = (feather < 0.0f) ? 0.0f : feather;
}

/**
 * Get ellipse select feather radius
 */
gfloat tool_options_get_ellipse_select_feather(ToolOptions* opts) {
    if (!opts) {
        return 0.0f;
    }
    return opts->ellipse_select_feather;
}

/**
 * Set ellipse select animation
 */
void tool_options_set_ellipse_select_animate(ToolOptions* opts, gboolean animate) {
    if (!opts) {
        return;
    }
    opts->ellipse_select_animate = animate ? TRUE : FALSE;
}

/**
 * Get ellipse select animation
 */
gboolean tool_options_get_ellipse_select_animate(ToolOptions* opts) {
    if (!opts) {
        return TRUE; /* Default to animated */
    }
    return opts->ellipse_select_animate;
}

/* Polygon select tool options */
void tool_options_set_polygon_select_combine(ToolOptions* opts, SelectionCombineMode combine) {
    if (opts) {
        opts->polygon_select_combine = combine;
    }
}
SelectionCombineMode tool_options_get_polygon_select_combine(ToolOptions* opts) {
    return opts ? opts->polygon_select_combine : SELECTION_COMBINE_NEW;
}
void tool_options_set_polygon_select_smooth(ToolOptions* opts, SelectionSmoothingMode smooth) {
    if (opts) {
        opts->polygon_select_smooth = smooth;
    }
}
SelectionSmoothingMode tool_options_get_polygon_select_smooth(ToolOptions* opts) {
    return opts ? opts->polygon_select_smooth : SELECTION_SMOOTH_ANTIALIASED;
}
void tool_options_set_polygon_select_feather(ToolOptions* opts, gfloat feather) {
    if (opts) {
        opts->polygon_select_feather = fmaxf(0.0f, feather);
    }
}
gfloat tool_options_get_polygon_select_feather(ToolOptions* opts) {
    return opts ? opts->polygon_select_feather : 0.0f;
}
void tool_options_set_polygon_select_animate(ToolOptions* opts, gboolean animate) {
    if (opts) {
        opts->polygon_select_animate = animate ? TRUE : FALSE;
    }
}
gboolean tool_options_get_polygon_select_animate(ToolOptions* opts) {
    if (!opts) {
        return TRUE;
    }
    return opts->polygon_select_animate;
}
void tool_options_set_polygon_select_curvature(ToolOptions* opts, gfloat curvature) {
    if (opts) {
        opts->polygon_select_curvature = fmaxf(0.0f, fminf(1.0f, curvature));
    }
}
gfloat tool_options_get_polygon_select_curvature(ToolOptions* opts) {
    return opts ? opts->polygon_select_curvature : 0.0f;
}
void tool_options_set_polygon_select_area(ToolOptions* opts, gint area) {
    if (opts && area >= 0 && area <= 2) {
        opts->polygon_select_area = area;
    }
}
gint tool_options_get_polygon_select_area(ToolOptions* opts) {
    return opts ? opts->polygon_select_area : 0;
}
void tool_options_set_polygon_select_border_width(ToolOptions* opts, gint width) {
    if (opts && width >= 1 && width <= 1000) {
        opts->polygon_select_border_width = width;
    }
}
gint tool_options_get_polygon_select_border_width(ToolOptions* opts) {
    return opts ? opts->polygon_select_border_width : 1;
}

/* Lasso select tool options */
void tool_options_set_lasso_select_combine(ToolOptions* opts, SelectionCombineMode combine) {
    if (opts)
        opts->lasso_select_combine = combine;
}
SelectionCombineMode tool_options_get_lasso_select_combine(ToolOptions* opts) {
    return opts ? opts->lasso_select_combine : SELECTION_COMBINE_NEW;
}
void tool_options_set_lasso_select_smooth(ToolOptions* opts, SelectionSmoothingMode smooth) {
    if (opts)
        opts->lasso_select_smooth = smooth;
}
SelectionSmoothingMode tool_options_get_lasso_select_smooth(ToolOptions* opts) {
    return opts ? opts->lasso_select_smooth : SELECTION_SMOOTH_ANTIALIASED;
}
void tool_options_set_lasso_select_feather(ToolOptions* opts, gfloat feather) {
    if (opts)
        opts->lasso_select_feather = fmaxf(0.0f, feather);
}
gfloat tool_options_get_lasso_select_feather(ToolOptions* opts) {
    return opts ? opts->lasso_select_feather : 0.0f;
}
void tool_options_set_lasso_select_animate(ToolOptions* opts, gboolean animate) {
    if (opts)
        opts->lasso_select_animate = animate ? TRUE : FALSE;
}
gboolean tool_options_get_lasso_select_animate(ToolOptions* opts) {
    return opts ? opts->lasso_select_animate : TRUE;
}
void tool_options_set_lasso_select_area(ToolOptions* opts, gint area) {
    if (opts && area >= 0 && area <= 2)
        opts->lasso_select_area = area;
}
gint tool_options_get_lasso_select_area(ToolOptions* opts) {
    return opts ? opts->lasso_select_area : 0;
}
void tool_options_set_lasso_select_border_width(ToolOptions* opts, gint width) {
    if (opts && width >= 1 && width <= 1000)
        opts->lasso_select_border_width = width;
}
gint tool_options_get_lasso_select_border_width(ToolOptions* opts) {
    return opts ? opts->lasso_select_border_width : 1;
}

/**
 * Set move tool auto select layer
 */
void tool_options_set_move_auto_select(ToolOptions* opts, gboolean auto_select) {
    if (!opts) {
        return;
    }
    opts->move_auto_select_layer = auto_select ? TRUE : FALSE;
}

/**
 * Get move tool auto select layer
 */
gboolean tool_options_get_move_auto_select(ToolOptions* opts) {
    if (!opts) {
        return TRUE; /* Default to auto-select enabled */
    }
    return opts->move_auto_select_layer;
}

/**
 * Set color picker sample radius (0–100)
 */
void tool_options_set_color_picker_sample_radius(ToolOptions* opts, gint radius) {
    if (!opts) {
        return;
    }
    opts->color_picker_sample_radius = (radius < 0) ? 0 : ((radius > 100) ? 100 : radius);
}

/**
 * Get color picker sample radius
 */
gint tool_options_get_color_picker_sample_radius(ToolOptions* opts) {
    if (!opts) {
        return 0;
    }
    return opts->color_picker_sample_radius;
}

/**
 * Set color picker sample from (TRUE = layer, FALSE = image)
 */
void tool_options_set_color_picker_sample_from_layer(ToolOptions* opts, gboolean from_layer) {
    if (!opts) {
        return;
    }
    opts->color_picker_sample_from_layer = from_layer ? TRUE : FALSE;
}

/**
 * Get color picker sample from (TRUE = layer, FALSE = image)
 */
gboolean tool_options_get_color_picker_sample_from_layer(ToolOptions* opts) {
    if (!opts) {
        return TRUE;
    }
    return opts->color_picker_sample_from_layer;
}

/* Crop tool option getters/setters */
void tool_options_set_crop_constraint_mode(ToolOptions* opts, gint mode) {
    if (opts && mode >= 0 && mode <= 2) {
        opts->crop_constraint_mode = mode;
    }
}
gint tool_options_get_crop_constraint_mode(ToolOptions* opts) {
    return opts ? opts->crop_constraint_mode : 0;
}
void tool_options_set_crop_ratio(ToolOptions* opts, gint w, gint h) {
    if (opts && w > 0 && h > 0) {
        opts->crop_ratio_w = w;
        opts->crop_ratio_h = h;
    }
}
void tool_options_get_crop_ratio(ToolOptions* opts, gint* w, gint* h) {
    if (opts && w && h) {
        *w = opts->crop_ratio_w > 0 ? opts->crop_ratio_w : 16;
        *h = opts->crop_ratio_h > 0 ? opts->crop_ratio_h : 9;
    }
}
void tool_options_set_crop_size(ToolOptions* opts, gint w, gint h) {
    if (opts && w > 0 && h > 0) {
        opts->crop_width = w;
        opts->crop_height = h;
    }
}
void tool_options_get_crop_size(ToolOptions* opts, gint* w, gint* h) {
    if (opts && w && h) {
        *w = opts->crop_width > 0 ? opts->crop_width : 1920;
        *h = opts->crop_height > 0 ? opts->crop_height : 1080;
    }
}
void tool_options_set_crop_link(ToolOptions* opts, gboolean link) {
    if (opts)
        opts->crop_link = link ? TRUE : FALSE;
}
gboolean tool_options_get_crop_link(ToolOptions* opts) {
    return opts ? opts->crop_link : TRUE;
}
void tool_options_set_crop_delete_pixels(ToolOptions* opts, gboolean delete_pixels) {
    if (opts)
        opts->crop_delete_pixels = delete_pixels ? TRUE : FALSE;
}
gboolean tool_options_get_crop_delete_pixels(ToolOptions* opts) {
    return opts ? opts->crop_delete_pixels : TRUE;
}
void tool_options_set_crop_grow_canvas(ToolOptions* opts, gboolean grow) {
    if (opts)
        opts->crop_grow_canvas = grow ? TRUE : FALSE;
}
gboolean tool_options_get_crop_grow_canvas(ToolOptions* opts) {
    return opts ? opts->crop_grow_canvas : FALSE;
}
void tool_options_set_crop_darken_outside(ToolOptions* opts, gboolean darken) {
    if (opts)
        opts->crop_darken_outside = darken ? TRUE : FALSE;
}
gboolean tool_options_get_crop_darken_outside(ToolOptions* opts) {
    return opts ? opts->crop_darken_outside : FALSE;
}
void tool_options_set_crop_darken_opacity(ToolOptions* opts, gfloat opacity) {
    if (opts)
        opts->crop_darken_opacity = (opacity < 0.0f) ? 0.0f : (opacity > 100.0f ? 100.0f : opacity);
}
gfloat tool_options_get_crop_darken_opacity(ToolOptions* opts) {
    return opts ? opts->crop_darken_opacity : 60.0f;
}
void tool_options_set_crop_overlay_mode(ToolOptions* opts, gint mode) {
    if (opts && mode >= 0 && mode <= 5)
        opts->crop_overlay_mode = mode;
}
gint tool_options_get_crop_overlay_mode(ToolOptions* opts) {
    return opts ? opts->crop_overlay_mode : 0;
}
void tool_options_set_crop_snap(ToolOptions* opts, gboolean snap) {
    if (opts)
        opts->crop_snap = snap ? TRUE : FALSE;
}
gboolean tool_options_get_crop_snap(ToolOptions* opts) {
    return opts ? opts->crop_snap : FALSE;
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
