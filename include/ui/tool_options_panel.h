/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_OPTIONS_PANEL_H
#define TOOL_OPTIONS_PANEL_H

#include "selection.h"
#include "tools.h"
#include <gtk/gtk.h>

struct ImageLayer;

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
    GtkWidget* crop_panel;                       /* Crop tool options panel (from Glade) */
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
    GtkWidget* compare_combo;                    /* Pixel comparison mode combo (current panel, paint bucket only) */
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
    GtkWidget* polygon_select_panel;             /* Polygon select tool options panel (from Glade) */
    GtkWidget* polygon_combine_new_button;
    GtkWidget* polygon_combine_add_button;
    GtkWidget* polygon_combine_subtract_button;
    GtkWidget* polygon_combine_intersect_button;
    GtkWidget* polygon_smooth_combo;
    GtkWidget* polygon_feather_scale;
    GtkWidget* polygon_animate_checkbox;
    GtkWidget* polygon_curvature_scale;
    GtkWidget* polygon_area_combo;
    GtkWidget* polygon_border_scale;
    GtkWidget* lasso_select_panel; /* Lasso select tool options panel (from Glade) */
    GtkWidget* lasso_combine_new_button;
    GtkWidget* lasso_combine_add_button;
    GtkWidget* lasso_combine_subtract_button;
    GtkWidget* lasso_combine_intersect_button;
    GtkWidget* lasso_smooth_combo;
    GtkWidget* lasso_feather_scale;
    GtkWidget* lasso_animate_checkbox;
    GtkWidget* lasso_area_combo;
    GtkWidget* lasso_border_scale;
    GtkWidget* magic_wand_panel; /* Magic wand select tool options panel (from Glade) */
    GtkWidget* magicwand_combine_new_button;
    GtkWidget* magicwand_combine_add_button;
    GtkWidget* magicwand_combine_subtract_button;
    GtkWidget* magicwand_combine_intersect_button;
    GtkWidget* magicwand_smooth_combo;
    GtkWidget* magicwand_feather_scale;
    GtkWidget* magicwand_animate_checkbox;
    GtkWidget* magicwand_tolerance_scale;
    GtkWidget* magicwand_compare_combo;
    GtkWidget* magicwand_contiguous_radio;
    GtkWidget* magicwand_global_radio;
    GtkWidget* move_auto_select_checkbox;        /* Auto-select layer checkbox (move tool only) */
    GtkWidget* text_panel;                       /* Text tool options panel (from Glade) */
    GtkWidget* text_font_chooser;                /* Font chooser widget (text tool) */
    GtkWidget* text_font_variation_combo;        /* Font face / variation combo (text tool) */
    GtkWidget* text_font_size_spin;              /* Font size spin button (text tool) */
    GtkWidget* text_bold_button;                 /* Bold toggle button (text tool) */
    GtkWidget* text_italic_button;               /* Italic toggle button (text tool) */
    GtkWidget* text_color_button;                /* Text color button (text tool) */
    GtkWidget* text_align_left_button;           /* Align-left toggle (text tool) */
    GtkWidget* text_align_center_button;         /* Align-center toggle (text tool) */
    GtkWidget* text_align_right_button;          /* Align-right toggle (text tool) */
    GtkWidget* text_align_justify_button;        /* Justify toggle (text tool) */
    GtkWidget* text_line_spacing_spin;           /* Line spacing spin (text tool) */
    GtkWidget* text_letter_spacing_spin;         /* Letter spacing spin (text tool) */
    GtkWidget* text_antialias_checkbox;          /* Antialias checkbox (text tool) */
    GtkWidget* text_rotation_spin;               /* Rotation spin button (text tool) */
    GtkWidget* gradient_panel;                   /* Gradient tool options panel (from Glade) */
    GtkWidget* gradient_preview_draw;            /* GtkDrawingArea inside preview button */
    GtkWidget* gradient_blend_combo;             /* Blend mode combo (gradient tool) */
    GtkWidget* gradient_shape_combo;             /* Shape combo (gradient tool) */
    GtkWidget* gradient_repeat_combo;            /* Repeat mode combo (gradient tool) */
    GtkWidget* gradient_opacity_scale;           /* Opacity scale (gradient tool) */
    GtkWidget* gradient_center_offset_scale;     /* Center offset scale (gradient tool) */
    GtkWidget* gradient_center_offset_group;     /* Center offset group (hidden unless spherical) */
    GtkWidget* gradient_center_offset_separator; /* Separator before center offset group */
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
 * Update only the rotation spin button in the text options panel without
 * triggering the signal handler back to the layer.  Call this during live
 * rotation drags so the panel always shows the current angle.
 * @param panel   Tool options panel (may be NULL — silently ignored)
 * @param degrees New rotation value in degrees
 */
void tool_options_panel_set_text_rotation(ToolOptionsPanel* panel, gdouble degrees);

/**
 * Sync the text tool options panel widgets to match the given text layer.
 * Call whenever the user activates an existing text layer so the panel
 * reflects its current font, color, alignment, spacing, etc.
 * @param panel The tool options panel (may be NULL — silently ignored)
 * @param layer An LAYER_TYPE_TEXT ImageLayer whose text_data will be read
 */
void tool_options_panel_sync_text_layer(ToolOptionsPanel* panel,
                                        struct ImageLayer* layer);

/**
 * Sync crop tool options (ratio, fixed size) and panel spinners from manual resize
 * Call when crop preview rect is resized by dragging handles
 * @param drawing_area Document drawing area (to get app context)
 * @param w Crop rect width
 * @param h Crop rect height
 */
void tool_options_panel_sync_crop_from_rect(GtkWidget* drawing_area, gint w, gint h);

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
