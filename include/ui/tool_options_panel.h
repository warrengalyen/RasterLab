#ifndef TOOL_OPTIONS_PANEL_H
#define TOOL_OPTIONS_PANEL_H

#include "selection.h"
#include "tools.h"
#include <gtk/gtk.h>

/**
 * Tool options panel structure
 */
typedef struct {
    GtkWidget* panel;                            /* Main panel container */
    GtkWidget* brush_panel;                      /* Brush tool options panel (from Glade) */
    GtkWidget* eraser_panel;                     /* Eraser tool options panel (from Glade) */
    GtkWidget* pencil_panel;                     /* Pencil tool options panel (from Glade) */
    GtkWidget* paintbucket_panel;                /* Paint bucket tool options panel (from Glade) */
    GtkWidget* color_picker_panel;               /* Color picker tool options panel (from Glade) */
    GtkWidget* rect_select_panel;                /* Rectangular select tool options panel (from Glade) */
    GtkWidget* ellipse_select_panel;             /* Elliptical select tool options panel (from Glade) */
    GtkWidget* move_panel;                       /* Move tool options panel (from Glade) */
    GtkWidget* title_label;                      /* Title showing tool name (current panel) */
    GtkWidget* size_scale;                       /* Size slider (current panel) */
    GtkWidget* opacity_scale;                    /* Opacity slider (current panel) */
    GtkWidget* hardness_scale;                   /* Hardness slider (current panel) */
    GtkWidget* flow_scale;                       /* Flow slider (current panel, eraser only) */
    GtkWidget* spacing_scale;                    /* Spacing slider (current panel, eraser only) */
    GtkWidget* tolerance_scale;                  /* Tolerance slider (current panel, paint bucket only) */
    GtkWidget* contiguous_radio;                 /* Contiguous radio button (current panel, paint bucket only) */
    GtkWidget* global_radio;                     /* Global radio button (current panel, paint bucket only) */
    GtkWidget* antialiased_checkbox;             /* Antialiased checkbox (current panel, paint bucket only) */
    GtkWidget* rect_animate_checkbox;            /* Animate checkbox (rect select only) */
    GtkWidget* rect_combine_new_button;          /* Combine mode NEW toggle button (rect select only) */
    GtkWidget* rect_combine_add_button;          /* Combine mode ADD toggle button (rect select only) */
    GtkWidget* rect_combine_subtract_button;     /* Combine mode SUBTRACT toggle button (rect select only) */
    GtkWidget* rect_combine_intersect_button;    /* Combine mode INTERSECT toggle button (rect select only) */
    GtkWidget* rect_smooth_combo;                /* Smoothing mode combo (rect select only) */
    GtkWidget* rect_feather_scale;               /* Feather radius slider (rect select only) */
    GtkWidget* ellipse_animate_checkbox;         /* Animate checkbox (ellipse select only) */
    GtkWidget* ellipse_combine_new_button;       /* Combine mode NEW toggle button (ellipse select only) */
    GtkWidget* ellipse_combine_add_button;       /* Combine mode ADD toggle button (ellipse select only) */
    GtkWidget* ellipse_combine_subtract_button;  /* Combine mode SUBTRACT toggle button (ellipse select only) */
    GtkWidget* ellipse_combine_intersect_button; /* Combine mode INTERSECT toggle button (ellipse select only) */
    GtkWidget* ellipse_smooth_combo;             /* Smoothing mode combo (ellipse select only) */
    GtkWidget* ellipse_feather_scale;            /* Feather radius slider (ellipse select only) */
    GtkWidget* move_auto_select_checkbox;        /* Auto-select layer checkbox (move tool only) */
    ToolType current_tool_type;                  /* Currently displayed tool type */
    ToolRegistry* tool_registry;                 /* Tool registry for cursor updates */
} ToolOptionsPanel;

/**
 * Create the tool options panel
 * @return ToolOptionsPanel structure
 */
ToolOptionsPanel* create_tool_options_panel(void);

/**
 * Switch tool options panel based on tool type
 * @param panel The tool options panel
 * @param tool_name The name of the currently selected tool
 */
void tool_options_panel_switch_tool(ToolOptionsPanel* panel, const gchar* tool_name);

/**
 * Free a tool options panel
 * @param panel The tool options panel to free
 */
void tool_options_panel_free(ToolOptionsPanel* panel);

/**
 * Set the tool registry for the tool options panel
 * @param panel The tool options panel
 * @param registry The tool registry
 */
void tool_options_panel_set_tool_registry(ToolOptionsPanel* panel, ToolRegistry* registry);

/**
 * Update the combine mode buttons to reflect a specific mode
 * Called from tool when hotkeys are pressed to temporarily change mode
 * @param panel The tool options panel
 * @param mode The combine mode to display
 */
void tool_options_panel_set_combine_mode(ToolOptionsPanel* panel, SelectionCombineMode mode);

/**
 * Update color picker preview in tool options (color_draw widget).
 * When has_color is FALSE, show checkerboard only (transparency).
 * When has_color is TRUE, show checkerboard + color overlay (r,g,b,a in 0–1).
 * @param panel The tool options panel (may be NULL)
 * @param has_color Whether a color is under the cursor
 * @param r Red (0–1)
 * @param g Green (0–1)
 * @param b Blue (0–1)
 * @param a Alpha (0–1)
 */
void tool_options_panel_set_color_picker_preview(ToolOptionsPanel* panel,
                                                 gboolean has_color,
                                                 gdouble r, gdouble g, gdouble b, gdouble a);

#endif /* TOOL_OPTIONS_PANEL_H */
