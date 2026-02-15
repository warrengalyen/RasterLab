#include "ui/tool_options_panel.h"
#include "command.h"
#include "commands/command_image.h"
#include "document.h"
#include "render/render_utils.h"
#include "selection.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "tools.h"
#include "tools/tool_crop.h"
#include "tools/tool_ellipse_select.h"
#include "tools/tool_rect_select.h"
#include "ui.h"
#include "ui/widgets/vertical_spin_button.h"
#include <stdio.h>

/* Flag to prevent recursive spin updates when crop link toggle is active */
static gboolean g_crop_spin_updating = FALSE;

static gint gcd(gint a, gint b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b) {
        gint t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/* Color picker preview state for color_draw */
static gboolean g_color_picker_preview_has_color = FALSE;
static gdouble g_color_picker_preview_r = 0.0;
static gdouble g_color_picker_preview_g = 0.0;
static gdouble g_color_picker_preview_b = 0.0;
static gdouble g_color_picker_preview_a = 1.0;

/**
 * Add a vertical spin button next to a scale widget
 * @param builder The GtkBuilder containing the widgets
 * @param scale_id The ID of the scale widget
 * @param control_box_id The ID of the control box containing the scale
 */
static void add_spin_button_to_scale(GtkBuilder* builder, const gchar* scale_id, const gchar* control_box_id) {
    if (!builder || !scale_id || !control_box_id) {
        return;
    }

    GtkWidget* scale = GTK_WIDGET(gtk_builder_get_object(builder, scale_id));
    GtkWidget* control_box = GTK_WIDGET(gtk_builder_get_object(builder, control_box_id));

    if (!scale || !control_box) {
        return;
    }

    /* Get the adjustment from the scale */
    GtkAdjustment* adjustment = gtk_range_get_adjustment(GTK_RANGE(scale));
    if (!adjustment) {
        return;
    }

    /* Get the number of digits from the scale */
    guint digits = gtk_scale_get_digits(GTK_SCALE(scale));

    /* Create vertical spin button with the same adjustment */
    GtkWidget* spin_button = vertical_spin_button_new(adjustment, 1.0, digits);
    if (!spin_button) {
        return;
    }

    /* Add the spin button to the control box */
    gtk_box_pack_start(GTK_BOX(control_box), spin_button, FALSE, FALSE, 0);
    gtk_widget_show_all(spin_button);
}

/**
 * Helper function to save tool options to settings
 */
static void save_tool_options_to_settings(ToolOptionsPanel* panel, ToolType tool_type) {
    if (!panel || !panel->panel) {
        return;
    }

    /* Get window to find AppContext */
    GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
    if (window) {
        AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
        if (ctx) {
            ui_save_tool_options_to_settings(ctx, tool_type);
        }
    }
}

/**
 * Tool options panel callback for size slider
 */
static void on_tool_size_changed(GtkScale* scale, gpointer user_data) {
    (void)user_data; /* Unused */
    gfloat size = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(scale), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        /* Update only the per-tool options */
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            tool_options_set_size(opts, size);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }

        /* Update cursor for brush, eraser, and pencil tools when size changes */
        if (panel && panel->tool_registry) {
            Tool* brush_tool = tool_manager_get(panel->tool_registry, TOOL_BRUSH);
            Tool* eraser_tool = tool_manager_get(panel->tool_registry, TOOL_ERASER);
            Tool* pencil_tool = tool_manager_get(panel->tool_registry, TOOL_PENCIL);
            Tool* active_tool = tool_manager_get_active(panel->tool_registry);

            /* Update cursors for brush, eraser, and pencil tools */
            if (brush_tool) {
                tool_update_cursor(brush_tool, size);
            }
            if (eraser_tool) {
                tool_update_cursor(eraser_tool, size);
            }
            if (pencil_tool) {
                tool_update_cursor(pencil_tool, size);
            }

            /* Update cursor on drawing areas if brush/eraser/pencil is active */
            if (active_tool && (active_tool->type == TOOL_BRUSH || active_tool->type == TOOL_ERASER || active_tool->type == TOOL_PENCIL)) {
                /* Get window to find all documents */
                GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
                if (window) {
                    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
                    if (ctx && ctx->documents) {
                        GList* iter;
                        for (iter = ctx->documents; iter; iter = iter->next) {
                            ImageDocument* doc = (ImageDocument*)iter->data;
                            if (doc && doc->drawing_area) {
                                GdkWindow* gdk_window = gtk_widget_get_window(doc->drawing_area);
                                if (gdk_window && active_tool->cursor) {
                                    gdk_window_set_cursor(gdk_window, active_tool->cursor);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * Tool options panel callback for opacity slider
 */
static void on_tool_opacity_changed(GtkScale* scale, gpointer user_data) {
    (void)user_data; /* Unused */
    gfloat opacity = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(scale), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            tool_options_set_opacity(opts, opacity / 100.0f);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }
    }
}

/**
 * Tool options panel callback for hardness slider
 */
static void on_tool_hardness_changed(GtkScale* scale, gpointer user_data) {
    (void)user_data; /* Unused */
    gfloat hardness = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(scale), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            tool_options_set_hardness(opts, hardness / 100.0f);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }
    }
}

/**
 * Tool options panel callback for flow slider
 */
static void on_tool_flow_changed(GtkScale* scale, gpointer user_data) {
    (void)user_data; /* Unused */
    gfloat flow = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(scale), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            tool_options_set_flow(opts, flow / 100.0f);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }
    }
}

/**
 * Tool options panel callback for spacing slider
 */
static void on_tool_spacing_changed(GtkScale* scale, gpointer user_data) {
    (void)user_data; /* Unused */
    gfloat spacing = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(scale), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            tool_options_set_spacing(opts, spacing / 100.0f);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }
    }
}

/**
 * Tool options panel callback for tolerance slider
 */
static void on_tool_tolerance_changed(GtkScale* scale, gpointer user_data) {
    (void)user_data; /* Unused */
    gfloat tolerance = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(scale), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            tool_options_set_tolerance(opts, tolerance);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }
    }
}

/**
 * Tool options panel callback for fill area radio buttons
 */
static void on_fill_area_changed(GtkToggleButton* button, gpointer user_data) {
    (void)user_data; /* Unused */
    if (!gtk_toggle_button_get_active(button)) {
        return; /* Only handle activation, not deactivation */
    }

    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(button), "tool_options_panel");
    if (!panel || panel->current_tool_type == TOOL_MOVE) {
        return;
    }

    ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
    if (!opts) {
        return;
    }

    /* Determine which button was activated by checking the button's label or name */
    const gchar* label = gtk_button_get_label(GTK_BUTTON(button));
    if (label && g_strcmp0(label, "Contiguous") == 0) {
        tool_options_set_fill_contiguous(opts, TRUE);
        /* Save to settings */
        save_tool_options_to_settings(panel, panel->current_tool_type);
    } else if (label && g_strcmp0(label, "Global") == 0) {
        tool_options_set_fill_contiguous(opts, FALSE);
        /* Save to settings */
        save_tool_options_to_settings(panel, panel->current_tool_type);
    }
}

/**
 * Tool options panel callback for antialiased checkbox
 */
static void on_fill_antialiased_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data; /* Unused */
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(button), "tool_options_panel");
    if (panel && panel->current_tool_type != TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(panel->current_tool_type);
        if (opts) {
            gboolean active = gtk_toggle_button_get_active(button);
            tool_options_set_fill_antialiased(opts, active);
            /* Save to settings */
            save_tool_options_to_settings(panel, panel->current_tool_type);
        }
    }
}

/**
 * Tool options panel callback for pencil antialias checkbox
 */
static void on_pencil_antialias_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data; /* Unused */
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(button), "tool_options_panel");
    if (panel && panel->current_tool_type == TOOL_PENCIL) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_PENCIL);
        if (opts) {
            gboolean active = gtk_toggle_button_get_active(button);
            opts->pencil_antialias = active;
            /* Save to settings */
            save_tool_options_to_settings(panel, TOOL_PENCIL);
        }
    }
}

/**
 * Tool options panel callback for pencil align to pixel grid checkbox
 */
static void on_pencil_align_pixel_grid_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data; /* Unused */
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(button), "tool_options_panel");
    if (panel && panel->current_tool_type == TOOL_PENCIL) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_PENCIL);
        if (opts) {
            gboolean active = gtk_toggle_button_get_active(button);
            opts->pencil_align_pixel_grid = active;
            /* Save to settings */
            save_tool_options_to_settings(panel, TOOL_PENCIL);
        }
    }
}

/**
 * Tool options panel callback for move tool auto select layer checkbox
 */
static void on_move_auto_select_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data; /* Unused */
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(button), "tool_options_panel");
    if (panel && panel->current_tool_type == TOOL_MOVE) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_MOVE);
        if (opts) {
            gboolean active = gtk_toggle_button_get_active(button);
            tool_options_set_move_auto_select(opts, active);
            /* Save to settings */
            save_tool_options_to_settings(panel, TOOL_MOVE);
        }
    }
}

/**
 * Color picker: draw callback for color_draw (checkerboard + optional color overlay)
 */
static gboolean on_color_draw_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    (void)user_data;
    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);
    if (w <= 0 || h <= 0) {
        gint rw = -1, rh = -1;
        gtk_widget_get_size_request(widget, &rw, &rh);
        w = (rw > 0) ? rw : 80;
        h = (rh > 0) ? rh : 54;
    }
    if (w <= 0 || h <= 0) {
        return TRUE;
    }
    draw_checkered_background(cr, w, h);
    if (g_color_picker_preview_has_color) {
        cairo_set_source_rgba(cr,
                              g_color_picker_preview_r,
                              g_color_picker_preview_g,
                              g_color_picker_preview_b,
                              g_color_picker_preview_a);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);
    }

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, (gdouble)w - 1.0, (gdouble)h - 1.0);
    cairo_stroke(cr);
    return TRUE; /* We drew everything; do not chain to default (would overwrite) */
}

/**
 * Color picker: sample radius changed
 */
static void on_color_picker_sample_radius_changed(GtkRange* range, gpointer user_data) {
    (void)user_data;
    ToolOptionsPanel* panel = (ToolOptionsPanel*)g_object_get_data(G_OBJECT(range), "tool_options_panel");
    if (!panel || panel->current_tool_type != TOOL_COLOR_PICKER) {
        return;
    }
    ToolOptions* opts = tool_options_get_for_tool(TOOL_COLOR_PICKER);
    if (opts) {
        gint v = (gint)gtk_range_get_value(range);
        tool_options_set_color_picker_sample_radius(opts, v);
        save_tool_options_to_settings(panel, TOOL_COLOR_PICKER);
    }
}

/**
 * Color picker: sample from (layer/image) toggled
 */
static void on_color_picker_sample_from_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    if (!gtk_toggle_button_get_active(button)) {
        return;
    }
    ToolOptions* opts = tool_options_get_for_tool(TOOL_COLOR_PICKER);
    if (!opts) {
        return;
    }
    GtkWidget* layer_btn = (GtkWidget*)g_object_get_data(G_OBJECT(panel->color_picker_panel), "sample_layer_button");
    GtkWidget* image_btn = (GtkWidget*)g_object_get_data(G_OBJECT(panel->color_picker_panel), "sample_image_button");
    if (button == (GtkToggleButton*)layer_btn) {
        tool_options_set_color_picker_sample_from_layer(opts, TRUE);
        if (image_btn) {
            g_signal_handlers_block_by_func(image_btn, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(image_btn), FALSE);
            g_signal_handlers_unblock_by_func(image_btn, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
        }
    } else if (button == (GtkToggleButton*)image_btn) {
        tool_options_set_color_picker_sample_from_layer(opts, FALSE);
        if (layer_btn) {
            g_signal_handlers_block_by_func(layer_btn, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(layer_btn), FALSE);
            g_signal_handlers_unblock_by_func(layer_btn, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
        }
    }
    save_tool_options_to_settings(panel, TOOL_COLOR_PICKER);
}

/**
 * Set scale widget value
 */
static void set_scale_value(GtkWidget* scale, gdouble value) {
    if (!scale) {
        return;
    }

    /* Set the value */
    gtk_range_set_value(GTK_RANGE(scale), value);

    /* Ensure widget is enabled */
    gtk_widget_set_sensitive(scale, TRUE);
}

/**
 * Load a tool options panel from Glade file
 */
static GtkWidget* load_panel_from_glade(const gchar* resource_path, const gchar* panel_id,
                                        GtkWidget** title_label, GtkWidget** size_scale,
                                        GtkWidget** opacity_scale, GtkWidget** hardness_scale,
                                        GtkWidget** flow_scale, GtkWidget** spacing_scale) {
    GtkBuilder* builder;
    GError* error = NULL;
    GtkWidget* panel;
    /* Note: Don't set initial values here - they will be set when switching tools */
    ToolOptions* opts = NULL;

    builder = gtk_builder_new();
    if (!builder) {
        g_warning("Failed to create GtkBuilder");
        return NULL;
    }

    /* Try to load the resource */
    gboolean success = gtk_builder_add_from_resource(builder, resource_path, &error);
    if (!success) {
        g_warning("Failed to load %s", resource_path);
        if (error) {
            g_warning("Error details: domain=%d, code=%d, message=%s",
                      error->domain, error->code, error->message);
            g_error_free(error);
            error = NULL;
        }
        g_object_unref(builder);
        return NULL;
    }

    panel = GTK_WIDGET(gtk_builder_get_object(builder, panel_id));
    if (!panel) {
        g_warning("Failed to get %s object from builder", panel_id);
        g_object_unref(builder);
        return NULL;
    }

    /* Keep builder alive by storing it on the panel as object data */
    /* Increment builder ref count since we're storing it */
    g_object_ref(builder);
    g_object_set_data_full(G_OBJECT(panel), "builder", builder, g_object_unref);

    /* Get widget references */
    const gchar* title_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_title_label" : "eraser_title_label";
    const gchar* size_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_size_scale" : "eraser_size_scale";
    const gchar* opacity_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_opacity_scale" : "eraser_opacity_scale";
    const gchar* hardness_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_hardness_scale" : "eraser_hardness_scale";
    const gchar* flow_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_flow_scale" : "eraser_flow_scale";
    const gchar* spacing_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_spacing_scale" : "eraser_spacing_scale";

    if (title_label) {
        GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(builder, title_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", title_id);
        }
        *title_label = widget;
    }

    if (size_scale) {
        GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(builder, size_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", size_id);
            *size_scale = NULL;
        } else {
            *size_scale = widget;
            /* Store panel reference for callbacks */
            g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
            /* Connect signal - values will be set when switching tools */
            g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
        }
    }

    if (opacity_scale) {
        GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(builder, opacity_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", opacity_id);
            *opacity_scale = NULL;
        } else {
            *opacity_scale = widget;
            if (opts) {
                set_scale_value(widget, opts->opacity * 100.0);
            }
            /* Store panel reference for callbacks */
            g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
            g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
        }
    }

    if (hardness_scale) {
        GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(builder, hardness_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", hardness_id);
            *hardness_scale = NULL;
        } else {
            *hardness_scale = widget;
            /* Store panel reference for callbacks */
            g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
            /* Connect signal - values will be set when switching tools */
            g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
        }
    }

    if (flow_scale) {
        GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(builder, flow_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", flow_id);
            *flow_scale = NULL;
        } else {
            *flow_scale = widget;
            /* Store panel reference for callbacks */
            g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
            /* Connect signal - values will be set when switching tools */
            g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_flow_changed), NULL);
        }
    }

    if (spacing_scale) {
        GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(builder, spacing_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", spacing_id);
            *spacing_scale = NULL;
        } else {
            *spacing_scale = widget;
            /* Store panel reference for callbacks */
            g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
            /* Connect signal - values will be set when switching tools */
            g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_spacing_changed), NULL);
        }
    }

    /* Don't show the panel here - let the container handle it */
    /* gtk_widget_show_all(panel); */

    /* Add vertical spin buttons to scale widgets */
    if (g_strcmp0(panel_id, "brush_options_panel") == 0) {
        add_spin_button_to_scale(builder, "brush_size_scale", "brush_size_control_box");
        add_spin_button_to_scale(builder, "brush_opacity_scale", "brush_opacity_control_box");
        add_spin_button_to_scale(builder, "brush_hardness_scale", "brush_hardness_control_box");
        add_spin_button_to_scale(builder, "brush_flow_scale", "brush_flow_control_box");
        add_spin_button_to_scale(builder, "brush_spacing_scale", "brush_spacing_control_box");
    } else if (g_strcmp0(panel_id, "eraser_options_panel") == 0) {
        add_spin_button_to_scale(builder, "eraser_size_scale", "eraser_size_control_box");
        add_spin_button_to_scale(builder, "eraser_opacity_scale", "eraser_opacity_control_box");
        add_spin_button_to_scale(builder, "eraser_hardness_scale", "eraser_hardness_control_box");
        add_spin_button_to_scale(builder, "eraser_flow_scale", "eraser_flow_control_box");
        add_spin_button_to_scale(builder, "eraser_spacing_scale", "eraser_spacing_control_box");
    }

    return panel;
}

/**
 * Signal handler for rectangle select combine mode button group changes
 */
static void on_rect_select_combine_button_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    /* Only act on button activation, not deactivation */
    if (!gtk_toggle_button_get_active(button)) {
        return;
    }

    SelectionCombineMode mode = SELECTION_COMBINE_NEW;

    /* Determine which button was clicked */
    if (button == GTK_TOGGLE_BUTTON(panel->rect_combine_new_button)) {
        mode = SELECTION_COMBINE_NEW;
    } else if (button == GTK_TOGGLE_BUTTON(panel->rect_combine_add_button)) {
        mode = SELECTION_COMBINE_ADD;
    } else if (button == GTK_TOGGLE_BUTTON(panel->rect_combine_subtract_button)) {
        mode = SELECTION_COMBINE_SUBTRACT;
    } else if (button == GTK_TOGGLE_BUTTON(panel->rect_combine_intersect_button)) {
        mode = SELECTION_COMBINE_INTERSECT;
    }

    /* Block signals to avoid recursive calls while updating button states */
    g_signal_handlers_block_by_func(panel->rect_combine_new_button,
                                    G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    if (panel->rect_combine_add_button) {
        g_signal_handlers_block_by_func(panel->rect_combine_add_button,
                                        G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_subtract_button) {
        g_signal_handlers_block_by_func(panel->rect_combine_subtract_button,
                                        G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_intersect_button) {
        g_signal_handlers_block_by_func(panel->rect_combine_intersect_button,
                                        G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }

    /* Deactivate all other buttons and activate the clicked one */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_new_button),
                                 (mode == SELECTION_COMBINE_NEW));
    if (panel->rect_combine_add_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_add_button),
                                     (mode == SELECTION_COMBINE_ADD));
    }
    if (panel->rect_combine_subtract_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_subtract_button),
                                     (mode == SELECTION_COMBINE_SUBTRACT));
    }
    if (panel->rect_combine_intersect_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_intersect_button),
                                     (mode == SELECTION_COMBINE_INTERSECT));
    }

    /* Unblock signals */
    g_signal_handlers_unblock_by_func(panel->rect_combine_new_button,
                                      G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    if (panel->rect_combine_add_button) {
        g_signal_handlers_unblock_by_func(panel->rect_combine_add_button,
                                          G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_subtract_button) {
        g_signal_handlers_unblock_by_func(panel->rect_combine_subtract_button,
                                          G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_intersect_button) {
        g_signal_handlers_unblock_by_func(panel->rect_combine_intersect_button,
                                          G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }

    /* Update tool options with the new mode */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_combine(opts, mode);
}

/**
 * Update the combine mode button group UI to reflect the current mode
 */
static void update_combine_mode_buttons(ToolOptionsPanel* panel, SelectionCombineMode mode) {
    if (!panel || !panel->rect_combine_new_button) {
        return;
    }

    /* Disconnect signals to avoid triggering callbacks during state update */
    g_signal_handlers_block_by_func(panel->rect_combine_new_button,
                                    G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    if (panel->rect_combine_add_button) {
        g_signal_handlers_block_by_func(panel->rect_combine_add_button,
                                        G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_subtract_button) {
        g_signal_handlers_block_by_func(panel->rect_combine_subtract_button,
                                        G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_intersect_button) {
        g_signal_handlers_block_by_func(panel->rect_combine_intersect_button,
                                        G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }

    /* Update button states */
    gboolean new_active = (mode == SELECTION_COMBINE_NEW);
    gboolean add_active = (mode == SELECTION_COMBINE_ADD);
    gboolean subtract_active = (mode == SELECTION_COMBINE_SUBTRACT);
    gboolean intersect_active = (mode == SELECTION_COMBINE_INTERSECT);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_new_button), new_active);
    if (panel->rect_combine_add_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_add_button), add_active);
    }
    if (panel->rect_combine_subtract_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_subtract_button), subtract_active);
    }
    if (panel->rect_combine_intersect_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_combine_intersect_button), intersect_active);
    }

    /* Force redraw of all buttons so visual state updates */
    gtk_widget_queue_draw(panel->rect_combine_new_button);
    if (panel->rect_combine_add_button) {
        gtk_widget_queue_draw(panel->rect_combine_add_button);
    }
    if (panel->rect_combine_subtract_button) {
        gtk_widget_queue_draw(panel->rect_combine_subtract_button);
    }
    if (panel->rect_combine_intersect_button) {
        gtk_widget_queue_draw(panel->rect_combine_intersect_button);
    }

    /* Unblock signals now that state is updated */
    g_signal_handlers_unblock_by_func(panel->rect_combine_new_button,
                                      G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    if (panel->rect_combine_add_button) {
        g_signal_handlers_unblock_by_func(panel->rect_combine_add_button,
                                          G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_subtract_button) {
        g_signal_handlers_unblock_by_func(panel->rect_combine_subtract_button,
                                          G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
    if (panel->rect_combine_intersect_button) {
        g_signal_handlers_unblock_by_func(panel->rect_combine_intersect_button,
                                          G_CALLBACK(on_rect_select_combine_button_toggled), panel);
    }
}

/**
 * Signal handler for rectangle select smoothing mode changes
 */
static void on_rect_select_smooth_changed(GtkComboBox* combo_box, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    gint active = gtk_combo_box_get_active(combo_box);
    if (active < 0 || active >= 3) {
        return;
    }

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_smooth(opts, (SelectionSmoothingMode)active);

    /* Trigger canvas redraw to update preview with new smoothing mode */
    if (panel && panel->panel) {
        GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
        if (window) {
            AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
            if (ctx) {
                ImageDocument* doc = ui_get_active_document(ctx);
                if (doc && doc->drawing_area) {
                    gtk_widget_queue_draw(doc->drawing_area);
                }
            }
        }
    }
}

/**
 * Signal handler for rectangle select feather radius changes
 */
static void on_rect_select_feather_changed(GtkRange* range, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    gdouble value = gtk_range_get_value(range);
    gint feather_radius = (gint)value;

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_feather(opts, (gfloat)feather_radius);

    /* Trigger canvas redraw to update preview with new feather radius */
    if (panel && panel->panel) {
        GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
        if (window) {
            AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
            if (ctx) {
                ImageDocument* doc = ui_get_active_document(ctx);
                if (doc && doc->drawing_area) {
                    gtk_widget_queue_draw(doc->drawing_area);
                }
            }
        }
    }
}

/**
 * Signal handler for rectangle select animation checkbox
 */
static void on_rect_select_animate_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    gboolean active = gtk_toggle_button_get_active(button);

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_animate(opts, active);

    /* Trigger redraw to update preview - the animation timer is managed by document.c
       which will start/stop based on the tool options animate flag */
    if (panel && panel->panel) {
        GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
        if (window) {
            AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
            if (ctx) {
                ImageDocument* doc = ui_get_active_document(ctx);
                if (doc && doc->drawing_area) {
                    gtk_widget_queue_draw(doc->drawing_area);
                }
            }
        }
    }
}

/**
 * Signal handler for ellipse select combine mode button group changes
 */
static void on_ellipse_select_combine_button_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    /* Only act on button activation, not deactivation */
    if (!gtk_toggle_button_get_active(button)) {
        return;
    }

    SelectionCombineMode mode = SELECTION_COMBINE_NEW;

    /* Determine which button was clicked */
    if (button == GTK_TOGGLE_BUTTON(panel->ellipse_combine_new_button)) {
        mode = SELECTION_COMBINE_NEW;
    } else if (button == GTK_TOGGLE_BUTTON(panel->ellipse_combine_add_button)) {
        mode = SELECTION_COMBINE_ADD;
    } else if (button == GTK_TOGGLE_BUTTON(panel->ellipse_combine_subtract_button)) {
        mode = SELECTION_COMBINE_SUBTRACT;
    } else if (button == GTK_TOGGLE_BUTTON(panel->ellipse_combine_intersect_button)) {
        mode = SELECTION_COMBINE_INTERSECT;
    }

    /* Block signals to avoid recursive calls while updating button states */
    g_signal_handlers_block_by_func(panel->ellipse_combine_new_button,
                                    G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    if (panel->ellipse_combine_add_button) {
        g_signal_handlers_block_by_func(panel->ellipse_combine_add_button,
                                        G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_subtract_button) {
        g_signal_handlers_block_by_func(panel->ellipse_combine_subtract_button,
                                        G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_intersect_button) {
        g_signal_handlers_block_by_func(panel->ellipse_combine_intersect_button,
                                        G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }

    /* Deactivate all other buttons and activate the clicked one */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_new_button),
                                 (mode == SELECTION_COMBINE_NEW));
    if (panel->ellipse_combine_add_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_add_button),
                                     (mode == SELECTION_COMBINE_ADD));
    }
    if (panel->ellipse_combine_subtract_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_subtract_button),
                                     (mode == SELECTION_COMBINE_SUBTRACT));
    }
    if (panel->ellipse_combine_intersect_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_intersect_button),
                                     (mode == SELECTION_COMBINE_INTERSECT));
    }

    /* Unblock signals */
    g_signal_handlers_unblock_by_func(panel->ellipse_combine_new_button,
                                      G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    if (panel->ellipse_combine_add_button) {
        g_signal_handlers_unblock_by_func(panel->ellipse_combine_add_button,
                                          G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_subtract_button) {
        g_signal_handlers_unblock_by_func(panel->ellipse_combine_subtract_button,
                                          G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_intersect_button) {
        g_signal_handlers_unblock_by_func(panel->ellipse_combine_intersect_button,
                                          G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }

    /* Update tool options with the new mode */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_ellipse_select_combine(opts, mode);
}

/**
 * Update the ellipse combine mode button group UI to reflect the current mode
 */
static void update_ellipse_combine_mode_buttons(ToolOptionsPanel* panel, SelectionCombineMode mode) {
    if (!panel || !panel->ellipse_combine_new_button) {
        return;
    }

    /* Disconnect signals to avoid triggering callbacks during state update */
    g_signal_handlers_block_by_func(panel->ellipse_combine_new_button,
                                    G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    if (panel->ellipse_combine_add_button) {
        g_signal_handlers_block_by_func(panel->ellipse_combine_add_button,
                                        G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_subtract_button) {
        g_signal_handlers_block_by_func(panel->ellipse_combine_subtract_button,
                                        G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_intersect_button) {
        g_signal_handlers_block_by_func(panel->ellipse_combine_intersect_button,
                                        G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }

    /* Update button states */
    gboolean new_active = (mode == SELECTION_COMBINE_NEW);
    gboolean add_active = (mode == SELECTION_COMBINE_ADD);
    gboolean subtract_active = (mode == SELECTION_COMBINE_SUBTRACT);
    gboolean intersect_active = (mode == SELECTION_COMBINE_INTERSECT);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_new_button), new_active);
    if (panel->ellipse_combine_add_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_add_button), add_active);
    }
    if (panel->ellipse_combine_subtract_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_subtract_button), subtract_active);
    }
    if (panel->ellipse_combine_intersect_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_combine_intersect_button), intersect_active);
    }

    /* Force redraw of all buttons so visual state updates */
    gtk_widget_queue_draw(panel->ellipse_combine_new_button);
    if (panel->ellipse_combine_add_button) {
        gtk_widget_queue_draw(panel->ellipse_combine_add_button);
    }
    if (panel->ellipse_combine_subtract_button) {
        gtk_widget_queue_draw(panel->ellipse_combine_subtract_button);
    }
    if (panel->ellipse_combine_intersect_button) {
        gtk_widget_queue_draw(panel->ellipse_combine_intersect_button);
    }

    /* Unblock signals now that state is updated */
    g_signal_handlers_unblock_by_func(panel->ellipse_combine_new_button,
                                      G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    if (panel->ellipse_combine_add_button) {
        g_signal_handlers_unblock_by_func(panel->ellipse_combine_add_button,
                                          G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_subtract_button) {
        g_signal_handlers_unblock_by_func(panel->ellipse_combine_subtract_button,
                                          G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
    if (panel->ellipse_combine_intersect_button) {
        g_signal_handlers_unblock_by_func(panel->ellipse_combine_intersect_button,
                                          G_CALLBACK(on_ellipse_select_combine_button_toggled), panel);
    }
}

/**
 * Signal handler for ellipse select smoothing mode changes
 */
static void on_ellipse_select_smooth_changed(GtkComboBox* combo_box, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    gint active = gtk_combo_box_get_active(combo_box);
    if (active < 0 || active >= 3) {
        return;
    }

    ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_ellipse_select_smooth(opts, (SelectionSmoothingMode)active);

    /* Trigger canvas redraw to update preview with new smoothing mode */
    if (panel && panel->panel) {
        GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
        if (window) {
            AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
            if (ctx) {
                ImageDocument* doc = ui_get_active_document(ctx);
                if (doc && doc->drawing_area) {
                    gtk_widget_queue_draw(doc->drawing_area);
                }
            }
        }
    }
}

/**
 * Signal handler for ellipse select feather radius changes
 */
static void on_ellipse_select_feather_changed(GtkRange* range, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    gdouble value = gtk_range_get_value(range);
    gint feather_radius = (gint)value;

    ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_ellipse_select_feather(opts, (gfloat)feather_radius);

    /* Trigger canvas redraw to update preview with new feather radius */
    if (panel && panel->panel) {
        GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
        if (window) {
            AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
            if (ctx) {
                ImageDocument* doc = ui_get_active_document(ctx);
                if (doc && doc->drawing_area) {
                    gtk_widget_queue_draw(doc->drawing_area);
                }
            }
        }
    }
}

/**
 * Signal handler for ellipse select animation checkbox
 */
static void on_ellipse_select_animate_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;

    gboolean active = gtk_toggle_button_get_active(button);

    ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_ellipse_select_animate(opts, active);

    /* Trigger redraw to update preview */
    if (panel && panel->panel) {
        GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
        if (window) {
            AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
            if (ctx) {
                ImageDocument* doc = ui_get_active_document(ctx);
                if (doc && doc->drawing_area) {
                    gtk_widget_queue_draw(doc->drawing_area);
                }
            }
        }
    }
}

static gboolean crop_panel_do_redraw_idle(gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    if (!panel || !panel->panel) {
        return G_SOURCE_REMOVE;
    }
    GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
    if (window) {
        AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
        if (ctx) {
            ImageDocument* doc = ui_get_active_document(ctx);
            if (doc && doc->drawing_area) {
                /* Update crop rect to match new ratio/size options before redraw */
                if (ctx->tool_registry) {
                    tool_crop_update_rect_from_options(doc, ctx->tool_registry);
                }
                gtk_widget_queue_draw(doc->drawing_area);
                if (doc->viewport) {
                    gtk_widget_queue_draw(doc->viewport);
                }
            }
        }
    }
    return G_SOURCE_REMOVE;
}

void tool_options_panel_sync_crop_from_rect(GtkWidget* drawing_area, gint w, gint h) {
    AppContext* ctx;
    ToolOptionsPanel* panel;
    ToolOptions* opts;
    GtkWidget *ratio_w_spin, *ratio_h_spin, *width_spin, *height_spin;
    gint rw, rh;

    if (!drawing_area || w < 1 || h < 1) {
        return;
    }

    ctx = (AppContext*)g_object_get_data(G_OBJECT(drawing_area), "app_context");
    if (!ctx || !ctx->tool_options_panel) {
        return;
    }

    panel = ctx->tool_options_panel;
    if (!panel->crop_panel) {
        return;
    }

    opts = tool_options_get_for_tool(TOOL_CROP);
    if (!opts) {
        return;
    }

    /* Update fixed size */
    tool_options_set_crop_size(opts, w, h);

    /* Simplify ratio (gcd) for display */
    rw = w;
    rh = h;
    {
        gint g = gcd(w, h);
        if (g > 0) {
            rw = w / g;
            rh = h / g;
        }
        if (rw < 1)
            rw = 1;
        if (rh < 1)
            rh = 1;
    }
    tool_options_set_crop_ratio(opts, rw, rh);

    /* Update spinners without triggering callbacks */
    g_crop_spin_updating = TRUE;
    ratio_w_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_ratio_width_spin");
    ratio_h_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_ratio_height_spin");
    width_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_width_spin");
    height_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_height_spin");
    if (ratio_w_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ratio_w_spin), (gdouble)rw);
    if (ratio_h_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ratio_h_spin), (gdouble)rh);
    if (width_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), (gdouble)w);
    if (height_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_spin), (gdouble)h);
    g_crop_spin_updating = FALSE;
}

/* Helper: update crop rect from options and redraw (called directly for spin changes) */
static void crop_panel_update_and_redraw(ToolOptionsPanel* panel) {
    if (!panel || !panel->panel) {
        return;
    }
    GtkWidget* window = gtk_widget_get_toplevel(panel->panel);
    if (window) {
        AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
        if (ctx) {
            ImageDocument* doc = ui_get_active_document(ctx);
            if (doc && doc->drawing_area) {
                if (ctx->tool_registry) {
                    tool_crop_update_rect_from_options(doc, ctx->tool_registry);
                }
                gtk_widget_queue_draw(doc->drawing_area);
                if (doc->viewport) {
                    gtk_widget_queue_draw(doc->viewport);
                }
            }
        }
    }
}

/* Helper: trigger crop overlay redraw (deferred for stack switch, immediate for spin changes) */
static void crop_panel_trigger_redraw(ToolOptionsPanel* panel) {
    if (!panel || !panel->panel) {
        return;
    }
    g_idle_add(crop_panel_do_redraw_idle, panel);
}

static void on_crop_stack_changed(GtkStack* stack, GParamSpec* pspec, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    const gchar* name = gtk_stack_get_visible_child_name(stack);
    gint mode = 0;
    if (name) {
        if (g_str_equal(name, "ratio"))
            mode = 1;
        else if (g_str_equal(name, "size"))
            mode = 2;
    }
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_constraint_mode(opts, mode);
    }
    /* Immediate update so new constraint mode takes effect right away */
    crop_panel_update_and_redraw(panel);
}

static void on_crop_ratio_width_changed(GtkSpinButton* spin, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gint w = (gint)gtk_spin_button_get_value(spin);
    gint h;
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (!opts)
        return;
    tool_options_get_crop_ratio(opts, NULL, &h);
    tool_options_set_crop_ratio(opts, w > 0 ? w : 1, h);
    crop_panel_update_and_redraw(panel);
}

static void on_crop_ratio_height_changed(GtkSpinButton* spin, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gint h = (gint)gtk_spin_button_get_value(spin);
    gint w;
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (!opts)
        return;
    tool_options_get_crop_ratio(opts, &w, NULL);
    tool_options_set_crop_ratio(opts, w, h > 0 ? h : 1);
    crop_panel_update_and_redraw(panel);
}

static void on_crop_width_changed(GtkSpinButton* spin, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    if (g_crop_spin_updating)
        return;
    gint w = (gint)gtk_spin_button_get_value(spin);
    gint h;
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (!opts)
        return;
    tool_options_get_crop_size(opts, NULL, &h);
    tool_options_set_crop_size(opts, w > 0 ? w : 1, h);
    if (tool_options_get_crop_link(opts) && opts->crop_ratio_w > 0 && opts->crop_ratio_h > 0 && panel->crop_panel) {
        gint new_h = (gint)((gdouble)w * opts->crop_ratio_h / opts->crop_ratio_w + 0.5);
        if (new_h < 1)
            new_h = 1;
        GtkWidget* height_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_height_spin");
        if (height_spin) {
            g_crop_spin_updating = TRUE;
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_spin), (gdouble)new_h);
            tool_options_set_crop_size(opts, w, new_h);
            g_crop_spin_updating = FALSE;
        }
    }
    crop_panel_update_and_redraw(panel);
}

static void on_crop_height_changed(GtkSpinButton* spin, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    if (g_crop_spin_updating)
        return;
    gint h = (gint)gtk_spin_button_get_value(spin);
    gint w;
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (!opts)
        return;
    tool_options_get_crop_size(opts, &w, NULL);
    tool_options_set_crop_size(opts, w, h > 0 ? h : 1);
    if (tool_options_get_crop_link(opts) && opts->crop_ratio_w > 0 && opts->crop_ratio_h > 0 && panel->crop_panel) {
        gint new_w = (gint)((gdouble)h * opts->crop_ratio_w / opts->crop_ratio_h + 0.5);
        if (new_w < 1)
            new_w = 1;
        GtkWidget* width_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_width_spin");
        if (width_spin) {
            g_crop_spin_updating = TRUE;
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), (gdouble)new_w);
            tool_options_set_crop_size(opts, new_w, h);
            g_crop_spin_updating = FALSE;
        }
    }
    crop_panel_update_and_redraw(panel);
}

static void on_crop_link_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_link(opts, active);
    }
    /* Update icon: active (linked) = lock, inactive (unlinked) = unlock */
    GtkWidget* img = gtk_bin_get_child(GTK_BIN(button));
    if (img && GTK_IS_IMAGE(img)) {
        gtk_image_set_from_resource(GTK_IMAGE(img),
            active ? "/icons/lock.png" : "/icons/unlock.png");
    }
}

static void on_crop_delete_pixels_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_delete_pixels(opts, active);
    }
}

static void on_crop_grow_canvas_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_grow_canvas(opts, active);
    }
}

static void on_crop_darken_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_darken_outside(opts, active);
    }
    crop_panel_trigger_redraw(panel);
}

static void on_crop_darken_opacity_changed(GtkRange* range, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gfloat val = (gfloat)gtk_range_get_value(range);
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_darken_opacity(opts, val);
    }
    crop_panel_trigger_redraw(panel);
}

static void on_crop_overlay_changed(GtkComboBox* combo, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gint active = gtk_combo_box_get_active(combo);
    if (active < 0 || active > 5)
        return;
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_overlay_mode(opts, active);
    }
    crop_panel_trigger_redraw(panel);
}

static void on_crop_snap_toggled(GtkToggleButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
    if (opts) {
        tool_options_set_crop_snap(opts, active);
    }
}

static void on_crop_reset_clicked(GtkButton* button, gpointer user_data) {
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    if (!panel || !panel->tool_registry)
        return;
    Tool* crop_tool = tool_manager_get(panel->tool_registry, TOOL_CROP);
    if (crop_tool) {
        tool_crop_reset(crop_tool);
    }
    crop_panel_trigger_redraw(panel);
}

gboolean crop_apply_if_active(AppContext* ctx) {
    ImageDocument* doc;
    LayersPanel* layers_panel;
    Tool* crop_tool;
    Command* cmd;
    gint x, y, w, h;

    if (!ctx || !ctx->tool_registry) {
        return FALSE;
    }

    doc = ui_get_active_document(ctx);
    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return FALSE;
    }

    crop_tool = tool_manager_get(ctx->tool_registry, TOOL_CROP);
    if (!crop_tool || !tool_crop_get_rect(crop_tool, &x, &y, &w, &h)) {
        return FALSE;
    }

    {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
        gboolean grow_canvas = opts ? tool_options_get_crop_grow_canvas(opts) : FALSE;
        gboolean delete_pixels = opts ? tool_options_get_crop_delete_pixels(opts) : TRUE;
        cmd = command_create_crop_to_rect(doc, x, y, (guint)w, (guint)h, grow_canvas, delete_pixels);
    }
    if (!cmd) {
        return FALSE;
    }

    command_execute(cmd, doc);

    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    tool_crop_reset(crop_tool);

    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    ui_update_status_bar(ctx, NULL);
    doc->modified = TRUE;

    if (ctx->tool_options_panel && ctx->tool_options_panel->crop_panel) {
        crop_panel_trigger_redraw(ctx->tool_options_panel);
    }

    return TRUE;
}

static void on_crop_apply_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    ToolOptionsPanel* panel = (ToolOptionsPanel*)user_data;
    AppContext* ctx;
    GtkWidget* window;

    if (!panel || !panel->panel || !panel->tool_registry) {
        return;
    }

    window = gtk_widget_get_toplevel(panel->panel);
    ctx = window ? (AppContext*)g_object_get_data(G_OBJECT(window), "app_context") : NULL;
    if (!ctx) {
        return;
    }

    if (!crop_apply_if_active(ctx)) {
        g_warning("No active crop rectangle to apply");
    }
}

ToolOptionsPanel* create_tool_options_panel(void) {
    ToolOptionsPanel* tool_opts_panel = (ToolOptionsPanel*)g_malloc(sizeof(ToolOptionsPanel));

    if (!tool_opts_panel) {
        g_warning("Failed to allocate ToolOptionsPanel");
        return NULL;
    }

    /* Initialize all fields to NULL */
    tool_opts_panel->panel = NULL;
    tool_opts_panel->brush_panel = NULL;
    tool_opts_panel->eraser_panel = NULL;
    tool_opts_panel->pencil_panel = NULL;
    tool_opts_panel->paintbucket_panel = NULL;
    tool_opts_panel->color_picker_panel = NULL;
    tool_opts_panel->rect_select_panel = NULL;
    tool_opts_panel->ellipse_select_panel = NULL;
    tool_opts_panel->crop_panel = NULL;
    tool_opts_panel->move_panel = NULL;
    tool_opts_panel->title_label = NULL;
    tool_opts_panel->size_scale = NULL;
    tool_opts_panel->opacity_scale = NULL;
    tool_opts_panel->hardness_scale = NULL;
    tool_opts_panel->flow_scale = NULL;
    tool_opts_panel->spacing_scale = NULL;
    tool_opts_panel->tolerance_scale = NULL;
    tool_opts_panel->contiguous_radio = NULL;
    tool_opts_panel->global_radio = NULL;
    tool_opts_panel->rect_animate_checkbox = NULL;
    tool_opts_panel->rect_combine_new_button = NULL;
    tool_opts_panel->rect_combine_add_button = NULL;
    tool_opts_panel->rect_combine_subtract_button = NULL;
    tool_opts_panel->rect_combine_intersect_button = NULL;
    tool_opts_panel->rect_smooth_combo = NULL;
    tool_opts_panel->rect_feather_scale = NULL;
    tool_opts_panel->ellipse_animate_checkbox = NULL;
    tool_opts_panel->ellipse_combine_new_button = NULL;
    tool_opts_panel->ellipse_combine_add_button = NULL;
    tool_opts_panel->ellipse_combine_subtract_button = NULL;
    tool_opts_panel->ellipse_combine_intersect_button = NULL;
    tool_opts_panel->ellipse_smooth_combo = NULL;
    tool_opts_panel->ellipse_feather_scale = NULL;
    tool_opts_panel->move_auto_select_checkbox = NULL;
    tool_opts_panel->current_tool_type = TOOL_MOVE; /* Start with no tool selected */

    /* Create container to hold the current panel */
    GtkWidget* container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    if (!container) {
        g_warning("Failed to create tool options container");
        g_free(tool_opts_panel);
        return NULL;
    }
    tool_opts_panel->panel = container;

    /* Load brush panel from Glade */
    GtkWidget *brush_title = NULL, *brush_size = NULL, *brush_opacity = NULL, *brush_hardness = NULL, *brush_flow = NULL, *brush_spacing = NULL;
    tool_opts_panel->brush_panel = load_panel_from_glade(
        "/ui/brush_options.glade", "brush_options_panel",
        &brush_title,
        &brush_size,
        &brush_opacity,
        &brush_hardness,
        &brush_flow,
        &brush_spacing);

    if (!tool_opts_panel->brush_panel) {
        g_warning("Failed to load brush options panel from Glade");
        g_object_unref(container);
        g_free(tool_opts_panel);
        return NULL;
    }

    /* Add brush panel to container */
    gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->brush_panel);

    /* Set initial references to brush panel widgets (may be NULL if widgets not found) */
    tool_opts_panel->title_label = brush_title;
    tool_opts_panel->size_scale = brush_size;
    tool_opts_panel->opacity_scale = brush_opacity;
    tool_opts_panel->hardness_scale = brush_hardness;
    tool_opts_panel->flow_scale = brush_flow;
    tool_opts_panel->spacing_scale = brush_spacing;

    /* Hide brush panel initially - will be shown when tool is selected */
    gtk_widget_set_visible(tool_opts_panel->brush_panel, FALSE);
    gtk_widget_set_no_show_all(tool_opts_panel->brush_panel, TRUE);

    /* Load eraser panel from Glade (but don't show it yet) */
    GtkWidget *eraser_title = NULL, *eraser_size = NULL, *eraser_opacity = NULL, *eraser_hardness = NULL, *eraser_flow = NULL, *eraser_spacing = NULL;
    tool_opts_panel->eraser_panel = load_panel_from_glade(
        "/ui/eraser_options.glade", "eraser_options_panel",
        &eraser_title,
        &eraser_size,
        &eraser_opacity,
        &eraser_hardness,
        &eraser_flow,
        &eraser_spacing);

    if (!tool_opts_panel->eraser_panel) {
        g_warning("Failed to load eraser options panel from Glade");
        /* Continue anyway - brush panel is loaded */
    } else {
        /* Add eraser panel to container */
        gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->eraser_panel);
        /* Store eraser widget references for later use (may be NULL if widgets not found) */
        if (eraser_title) {
            g_object_set_data(G_OBJECT(tool_opts_panel->eraser_panel), "title_label", eraser_title);
        }
        if (eraser_size) {
            g_object_set_data(G_OBJECT(tool_opts_panel->eraser_panel), "size_scale", eraser_size);
        }
        if (eraser_opacity) {
            g_object_set_data(G_OBJECT(tool_opts_panel->eraser_panel), "opacity_scale", eraser_opacity);
        }
        if (eraser_hardness) {
            g_object_set_data(G_OBJECT(tool_opts_panel->eraser_panel), "hardness_scale", eraser_hardness);
        }
        if (eraser_flow) {
            g_object_set_data(G_OBJECT(tool_opts_panel->eraser_panel), "flow_scale", eraser_flow);
        }
        if (eraser_spacing) {
            g_object_set_data(G_OBJECT(tool_opts_panel->eraser_panel), "spacing_scale", eraser_spacing);
        }
        /* Hide eraser panel initially - will be shown when tool is selected */
        gtk_widget_set_visible(tool_opts_panel->eraser_panel, FALSE);
        gtk_widget_set_no_show_all(tool_opts_panel->eraser_panel, TRUE);
    }

    /* Load pencil panel from Glade */
    GtkBuilder* pencil_builder = gtk_builder_new();
    GError* pencil_error = NULL;
    GtkWidget* pencil_title = NULL;
    GtkWidget* pencil_size = NULL;
    GtkWidget* pencil_opacity = NULL;
    GtkWidget* pencil_antialias = NULL;
    GtkWidget* pencil_align_pixel_grid = NULL;
    ToolOptions* pencil_opts = tool_options_get_for_tool(TOOL_PENCIL);

    if (gtk_builder_add_from_resource(pencil_builder, "/ui/pencil_options.glade", &pencil_error)) {
        tool_opts_panel->pencil_panel = GTK_WIDGET(gtk_builder_get_object(pencil_builder, "pencil_options_panel"));
        if (tool_opts_panel->pencil_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->pencil_panel);

            /* Get widgets */
            pencil_title = GTK_WIDGET(gtk_builder_get_object(pencil_builder, "pencil_title_label"));
            pencil_size = GTK_WIDGET(gtk_builder_get_object(pencil_builder, "pencil_size_scale"));
            pencil_opacity = GTK_WIDGET(gtk_builder_get_object(pencil_builder, "pencil_opacity_scale"));
            pencil_antialias = GTK_WIDGET(gtk_builder_get_object(pencil_builder, "pencil_antialias_checkbox"));
            pencil_align_pixel_grid = GTK_WIDGET(gtk_builder_get_object(pencil_builder, "pencil_align_pixel_grid_checkbox"));

            /* Store references */
            if (pencil_title) {
                g_object_set_data(G_OBJECT(tool_opts_panel->pencil_panel), "title_label", pencil_title);
            }
            if (pencil_size) {
                g_object_set_data(G_OBJECT(tool_opts_panel->pencil_panel), "size_scale", pencil_size);
                if (pencil_opts) {
                    set_scale_value(pencil_size, pencil_opts->size);
                    g_object_set_data(G_OBJECT(pencil_size), "tool_options_panel", tool_opts_panel);
                    g_signal_connect(pencil_size, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
                }
            }
            if (pencil_opacity) {
                g_object_set_data(G_OBJECT(tool_opts_panel->pencil_panel), "opacity_scale", pencil_opacity);
                if (pencil_opts) {
                    set_scale_value(pencil_opacity, pencil_opts->opacity * 100.0);
                    g_object_set_data(G_OBJECT(pencil_opacity), "tool_options_panel", tool_opts_panel);
                    g_signal_connect(pencil_opacity, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
                }
            }
            if (pencil_antialias) {
                g_object_set_data(G_OBJECT(tool_opts_panel->pencil_panel), "antialias_checkbox", pencil_antialias);
                if (pencil_opts) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pencil_antialias), pencil_opts->pencil_antialias);
                    g_object_set_data(G_OBJECT(pencil_antialias), "tool_options_panel", tool_opts_panel);
                    g_signal_connect(pencil_antialias, "toggled", G_CALLBACK(on_pencil_antialias_toggled), NULL);
                }
            }
            if (pencil_align_pixel_grid) {
                g_object_set_data(G_OBJECT(tool_opts_panel->pencil_panel), "align_pixel_grid_checkbox", pencil_align_pixel_grid);
                if (pencil_opts) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pencil_align_pixel_grid), pencil_opts->pencil_align_pixel_grid);
                    g_object_set_data(G_OBJECT(pencil_align_pixel_grid), "tool_options_panel", tool_opts_panel);
                    g_signal_connect(pencil_align_pixel_grid, "toggled", G_CALLBACK(on_pencil_align_pixel_grid_toggled), NULL);
                }
            }

            /* Hide pencil panel initially */
            gtk_widget_set_visible(tool_opts_panel->pencil_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->pencil_panel, TRUE);

            /* Add vertical spin buttons to scale widgets */
            add_spin_button_to_scale(pencil_builder, "pencil_size_scale", "pencil_size_control_box");
            add_spin_button_to_scale(pencil_builder, "pencil_opacity_scale", "pencil_opacity_control_box");
        }
        g_object_unref(pencil_builder);
    } else {
        g_warning("Failed to load pencil options panel: %s", pencil_error ? pencil_error->message : "Unknown error");
        if (pencil_error) {
            g_error_free(pencil_error);
        }
        if (pencil_builder) {
            g_object_unref(pencil_builder);
        }
    }

    /* Load paint bucket panel from Glade */
    GtkBuilder* paintbucket_builder = gtk_builder_new();
    GError* paintbucket_error = NULL;
    GtkWidget *paintbucket_title = NULL, *paintbucket_tolerance = NULL;
    GtkWidget *paintbucket_contiguous = NULL, *paintbucket_global = NULL;
    GtkWidget* paintbucket_antialiased = NULL;
    ToolOptions* paintbucket_opts = tool_options_get_for_tool(TOOL_PAINT_BUCKET);

    if (gtk_builder_add_from_resource(paintbucket_builder, "/ui/paintbucket_options.glade", &paintbucket_error)) {
        tool_opts_panel->paintbucket_panel = GTK_WIDGET(gtk_builder_get_object(paintbucket_builder, "paintbucket_options_panel"));
        if (tool_opts_panel->paintbucket_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->paintbucket_panel);

            /* Get widgets */
            paintbucket_title = GTK_WIDGET(gtk_builder_get_object(paintbucket_builder, "paintbucket_title_label"));
            paintbucket_tolerance = GTK_WIDGET(gtk_builder_get_object(paintbucket_builder, "paintbucket_tolerance_scale"));
            paintbucket_contiguous = GTK_WIDGET(gtk_builder_get_object(paintbucket_builder, "paintbucket_contiguous_radio"));
            paintbucket_global = GTK_WIDGET(gtk_builder_get_object(paintbucket_builder, "paintbucket_global_radio"));
            paintbucket_antialiased = GTK_WIDGET(gtk_builder_get_object(paintbucket_builder, "paintbucket_antialiased_checkbox"));

            /* Store references */
            if (paintbucket_title) {
                g_object_set_data(G_OBJECT(tool_opts_panel->paintbucket_panel), "title_label", paintbucket_title);
            }
            if (paintbucket_tolerance) {
                g_object_set_data(G_OBJECT(tool_opts_panel->paintbucket_panel), "tolerance_scale", paintbucket_tolerance);
                if (paintbucket_opts) {
                    set_scale_value(paintbucket_tolerance, paintbucket_opts->tolerance);
                    g_signal_connect(paintbucket_tolerance, "value-changed", G_CALLBACK(on_tool_tolerance_changed), NULL);
                }
            }
            if (paintbucket_contiguous && paintbucket_global) {
                /* Group radio buttons together */
                gtk_radio_button_join_group(GTK_RADIO_BUTTON(paintbucket_global),
                                            GTK_RADIO_BUTTON(paintbucket_contiguous));
            }
            if (paintbucket_contiguous) {
                g_object_set_data(G_OBJECT(tool_opts_panel->paintbucket_panel), "contiguous_radio", paintbucket_contiguous);
                if (paintbucket_opts) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(paintbucket_contiguous), paintbucket_opts->fill_contiguous);
                    g_signal_connect(paintbucket_contiguous, "toggled", G_CALLBACK(on_fill_area_changed), NULL);
                }
            }
            if (paintbucket_global) {
                g_object_set_data(G_OBJECT(tool_opts_panel->paintbucket_panel), "global_radio", paintbucket_global);
                if (paintbucket_opts) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(paintbucket_global), !paintbucket_opts->fill_contiguous);
                    g_signal_connect(paintbucket_global, "toggled", G_CALLBACK(on_fill_area_changed), NULL);
                }
            }
            if (paintbucket_antialiased) {
                g_object_set_data(G_OBJECT(tool_opts_panel->paintbucket_panel), "antialiased_checkbox", paintbucket_antialiased);
                if (paintbucket_opts) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(paintbucket_antialiased), paintbucket_opts->fill_antialiased);
                    g_signal_connect(paintbucket_antialiased, "toggled", G_CALLBACK(on_fill_antialiased_toggled), NULL);
                }
            }

            /* Hide paint bucket panel initially */
            gtk_widget_set_visible(tool_opts_panel->paintbucket_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->paintbucket_panel, TRUE);

            /* Add vertical spin button to tolerance scale widget */
            add_spin_button_to_scale(paintbucket_builder, "paintbucket_tolerance_scale", "paintbucket_tolerance_control_box");
        }
        g_object_unref(paintbucket_builder);
    } else {
        g_warning("Failed to load paint bucket options panel: %s", paintbucket_error ? paintbucket_error->message : "Unknown error");
        if (paintbucket_error) {
            g_error_free(paintbucket_error);
        }
        if (paintbucket_builder) {
            g_object_unref(paintbucket_builder);
        }
    }

    /* Load color picker panel from Glade */
    GtkBuilder* color_picker_builder = gtk_builder_new();
    GError* color_picker_error = NULL;
    GtkWidget* color_picker_title = NULL;
    GtkWidget* color_picker_sample_radius = NULL;
    GtkWidget* color_picker_sample_layer = NULL;
    GtkWidget* color_picker_sample_image = NULL;
    GtkWidget* color_draw = NULL;
    ToolOptions* color_picker_opts = tool_options_get_for_tool(TOOL_COLOR_PICKER);

    if (gtk_builder_add_from_resource(color_picker_builder, "/ui/color_picker_options.glade", &color_picker_error)) {
        tool_opts_panel->color_picker_panel = GTK_WIDGET(gtk_builder_get_object(color_picker_builder, "color_picker_options_panel"));
        if (tool_opts_panel->color_picker_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->color_picker_panel);

            color_picker_title = GTK_WIDGET(gtk_builder_get_object(color_picker_builder, "color_picker_title_label"));
            color_picker_sample_radius = GTK_WIDGET(gtk_builder_get_object(color_picker_builder, "sample_radius_scale"));
            color_picker_sample_layer = GTK_WIDGET(gtk_builder_get_object(color_picker_builder, "sample_layer_button"));
            color_picker_sample_image = GTK_WIDGET(gtk_builder_get_object(color_picker_builder, "sample_image_button"));
            color_draw = GTK_WIDGET(gtk_builder_get_object(color_picker_builder, "color_draw"));

            if (color_picker_title) {
                g_object_set_data(G_OBJECT(tool_opts_panel->color_picker_panel), "title_label", color_picker_title);
            }
            if (color_draw) {
                g_object_set_data(G_OBJECT(tool_opts_panel->color_picker_panel), "color_draw", color_draw);
                g_signal_connect(color_draw, "draw", G_CALLBACK(on_color_draw_draw), NULL);
            }
            if (color_picker_sample_radius) {
                g_object_set_data(G_OBJECT(tool_opts_panel->color_picker_panel), "sample_radius_scale", color_picker_sample_radius);
                if (color_picker_opts) {
                    set_scale_value(color_picker_sample_radius, (gdouble)color_picker_opts->color_picker_sample_radius);
                    g_object_set_data(G_OBJECT(color_picker_sample_radius), "tool_options_panel", tool_opts_panel);
                    g_signal_connect(color_picker_sample_radius, "value-changed", G_CALLBACK(on_color_picker_sample_radius_changed), NULL);
                }
            }
            if (color_picker_sample_layer) {
                g_object_set_data(G_OBJECT(tool_opts_panel->color_picker_panel), "sample_layer_button", color_picker_sample_layer);
                g_signal_connect(color_picker_sample_layer, "toggled", G_CALLBACK(on_color_picker_sample_from_toggled), tool_opts_panel);
            }
            if (color_picker_sample_image) {
                g_object_set_data(G_OBJECT(tool_opts_panel->color_picker_panel), "sample_image_button", color_picker_sample_image);
                g_signal_connect(color_picker_sample_image, "toggled", G_CALLBACK(on_color_picker_sample_from_toggled), tool_opts_panel);
            }
            if (color_picker_opts) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_picker_sample_layer), color_picker_opts->color_picker_sample_from_layer);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_picker_sample_image), !color_picker_opts->color_picker_sample_from_layer);
            }

            gtk_widget_set_visible(tool_opts_panel->color_picker_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->color_picker_panel, TRUE);

            add_spin_button_to_scale(color_picker_builder, "sample_radius_scale", "sample_radius_control_box");
        }
        g_object_unref(color_picker_builder);
    } else {
        g_warning("Failed to load color picker options panel: %s", color_picker_error ? color_picker_error->message : "Unknown error");
        if (color_picker_error) {
            g_error_free(color_picker_error);
        }
        if (color_picker_builder) {
            g_object_unref(color_picker_builder);
        }
    }

    /* Load rectangular select panel from Glade */
    GtkBuilder* rect_select_builder = gtk_builder_new();
    GError* rect_select_error = NULL;
    GtkWidget* rect_select_title = NULL;
    GtkWidget* rect_select_animate = NULL;
    GtkWidget* rect_select_smooth = NULL;
    GtkWidget* rect_select_feather = NULL;

    if (gtk_builder_add_from_resource(rect_select_builder, "/ui/rect_select_options.glade", &rect_select_error)) {
        tool_opts_panel->rect_select_panel = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_options_panel"));
        if (tool_opts_panel->rect_select_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->rect_select_panel);

            /* Get widgets */
            rect_select_title = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_title_label"));
            rect_select_animate = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_animate_checkbox"));
            tool_opts_panel->rect_combine_new_button = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_combine_new_button"));
            tool_opts_panel->rect_combine_add_button = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_combine_add_button"));
            tool_opts_panel->rect_combine_subtract_button = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_combine_subtract_button"));
            tool_opts_panel->rect_combine_intersect_button = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_combine_intersect_button"));
            rect_select_smooth = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_smooth_combo"));
            rect_select_feather = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_feather_scale"));

            /* Set up combine mode toggle buttons */
            if (tool_opts_panel->rect_combine_new_button) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_opts_panel->rect_combine_new_button), TRUE);
                g_signal_connect(tool_opts_panel->rect_combine_new_button, "toggled",
                                 G_CALLBACK(on_rect_select_combine_button_toggled), tool_opts_panel);
            }
            if (tool_opts_panel->rect_combine_add_button) {
                g_signal_connect(tool_opts_panel->rect_combine_add_button, "toggled",
                                 G_CALLBACK(on_rect_select_combine_button_toggled), tool_opts_panel);
            }
            if (tool_opts_panel->rect_combine_subtract_button) {
                g_signal_connect(tool_opts_panel->rect_combine_subtract_button, "toggled",
                                 G_CALLBACK(on_rect_select_combine_button_toggled), tool_opts_panel);
            }
            if (tool_opts_panel->rect_combine_intersect_button) {
                g_signal_connect(tool_opts_panel->rect_combine_intersect_button, "toggled",
                                 G_CALLBACK(on_rect_select_combine_button_toggled), tool_opts_panel);
            }

            if (rect_select_smooth) {
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_smooth), "None");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_smooth), "Antialiased");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_smooth), "Feathered");
                gtk_combo_box_set_active(GTK_COMBO_BOX(rect_select_smooth), 1);
                g_object_set_data(G_OBJECT(tool_opts_panel->rect_select_panel), "smooth_combo", rect_select_smooth);
                tool_opts_panel->rect_smooth_combo = rect_select_smooth;

                /* Connect signal to update tool options when changed */
                g_signal_connect(rect_select_smooth, "changed",
                                 G_CALLBACK(on_rect_select_smooth_changed), tool_opts_panel);
            }

            if (rect_select_feather) {
                g_object_set_data(G_OBJECT(tool_opts_panel->rect_select_panel), "feather_scale", rect_select_feather);
                tool_opts_panel->rect_feather_scale = rect_select_feather;

                /* Connect signal to update tool options when changed */
                g_signal_connect(rect_select_feather, "value-changed",
                                 G_CALLBACK(on_rect_select_feather_changed), tool_opts_panel);
            }

            if (rect_select_animate) {
                g_object_set_data(G_OBJECT(tool_opts_panel->rect_select_panel), "animate_checkbox", rect_select_animate);
                tool_opts_panel->rect_animate_checkbox = rect_select_animate;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rect_select_animate), TRUE);

                /* Connect signal to update tool options when changed */
                g_signal_connect(rect_select_animate, "toggled",
                                 G_CALLBACK(on_rect_select_animate_toggled), tool_opts_panel);
            }

            /* Hide rect select panel initially */
            gtk_widget_set_visible(tool_opts_panel->rect_select_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->rect_select_panel, TRUE);

            /* Add vertical spin button to feather scale widget */
            add_spin_button_to_scale(rect_select_builder, "rect_select_feather_scale", "rect_select_feather_control_box");
        }
        g_object_unref(rect_select_builder);
    } else {
        g_warning("Failed to load rectangular select options panel: %s", rect_select_error ? rect_select_error->message : "Unknown error");
        if (rect_select_error) {
            g_error_free(rect_select_error);
        }
        if (rect_select_builder) {
            g_object_unref(rect_select_builder);
        }
    }

    /* Load elliptical select panel from Glade */
    GtkBuilder* ellipse_select_builder = gtk_builder_new();
    GError* ellipse_select_error = NULL;
    GtkWidget* ellipse_select_title = NULL;
    GtkWidget* ellipse_select_animate = NULL;
    GtkWidget* ellipse_select_smooth = NULL;
    GtkWidget* ellipse_select_feather = NULL;

    if (gtk_builder_add_from_resource(ellipse_select_builder, "/ui/elliptical_select_options.glade", &ellipse_select_error)) {
        tool_opts_panel->ellipse_select_panel = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_options_panel"));
        if (tool_opts_panel->ellipse_select_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->ellipse_select_panel);

            /* Get widgets */
            ellipse_select_title = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_title_label"));
            ellipse_select_animate = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_animate_checkbox"));
            tool_opts_panel->ellipse_combine_new_button = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_combine_new_button"));
            tool_opts_panel->ellipse_combine_add_button = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_combine_add_button"));
            tool_opts_panel->ellipse_combine_subtract_button = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_combine_subtract_button"));
            tool_opts_panel->ellipse_combine_intersect_button = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_combine_intersect_button"));
            ellipse_select_smooth = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_smooth_combo"));
            ellipse_select_feather = GTK_WIDGET(gtk_builder_get_object(ellipse_select_builder, "elliptical_select_feather_scale"));

            /* Set up combine mode toggle buttons */
            if (tool_opts_panel->ellipse_combine_new_button) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_opts_panel->ellipse_combine_new_button), TRUE);
                g_signal_connect(tool_opts_panel->ellipse_combine_new_button, "toggled",
                                 G_CALLBACK(on_ellipse_select_combine_button_toggled), tool_opts_panel);
            }
            if (tool_opts_panel->ellipse_combine_add_button) {
                g_signal_connect(tool_opts_panel->ellipse_combine_add_button, "toggled",
                                 G_CALLBACK(on_ellipse_select_combine_button_toggled), tool_opts_panel);
            }
            if (tool_opts_panel->ellipse_combine_subtract_button) {
                g_signal_connect(tool_opts_panel->ellipse_combine_subtract_button, "toggled",
                                 G_CALLBACK(on_ellipse_select_combine_button_toggled), tool_opts_panel);
            }
            if (tool_opts_panel->ellipse_combine_intersect_button) {
                g_signal_connect(tool_opts_panel->ellipse_combine_intersect_button, "toggled",
                                 G_CALLBACK(on_ellipse_select_combine_button_toggled), tool_opts_panel);
            }

            if (ellipse_select_smooth) {
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ellipse_select_smooth), "None");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ellipse_select_smooth), "Antialiased");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ellipse_select_smooth), "Feathered");
                gtk_combo_box_set_active(GTK_COMBO_BOX(ellipse_select_smooth), 1);
                g_object_set_data(G_OBJECT(tool_opts_panel->ellipse_select_panel), "smooth_combo", ellipse_select_smooth);
                tool_opts_panel->ellipse_smooth_combo = ellipse_select_smooth;

                g_signal_connect(ellipse_select_smooth, "changed",
                                 G_CALLBACK(on_ellipse_select_smooth_changed), tool_opts_panel);
            }

            if (ellipse_select_feather) {
                g_object_set_data(G_OBJECT(tool_opts_panel->ellipse_select_panel), "feather_scale", ellipse_select_feather);
                tool_opts_panel->ellipse_feather_scale = ellipse_select_feather;

                g_signal_connect(ellipse_select_feather, "value-changed",
                                 G_CALLBACK(on_ellipse_select_feather_changed), tool_opts_panel);
            }

            if (ellipse_select_animate) {
                g_object_set_data(G_OBJECT(tool_opts_panel->ellipse_select_panel), "animate_checkbox", ellipse_select_animate);
                tool_opts_panel->ellipse_animate_checkbox = ellipse_select_animate;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ellipse_select_animate), TRUE);

                g_signal_connect(ellipse_select_animate, "toggled",
                                 G_CALLBACK(on_ellipse_select_animate_toggled), tool_opts_panel);
            }

            /* Hide ellipse select panel initially */
            gtk_widget_set_visible(tool_opts_panel->ellipse_select_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->ellipse_select_panel, TRUE);

            /* Add vertical spin button to feather scale widget */
            add_spin_button_to_scale(ellipse_select_builder, "elliptical_select_feather_scale", "elliptical_select_feather_control_box");
        }
        g_object_unref(ellipse_select_builder);
    } else {
        g_warning("Failed to load elliptical select options panel: %s", ellipse_select_error ? ellipse_select_error->message : "Unknown error");
        if (ellipse_select_error) {
            g_error_free(ellipse_select_error);
        }
        if (ellipse_select_builder) {
            g_object_unref(ellipse_select_builder);
        }
    }

    /* Load crop panel from Glade */
    GtkBuilder* crop_builder = gtk_builder_new();
    GError* crop_error = NULL;
    GtkWidget* crop_constraint_stack = NULL;
    GtkWidget* crop_ratio_w_spin = NULL;
    GtkWidget* crop_ratio_h_spin = NULL;
    GtkWidget* crop_width_spin = NULL;
    GtkWidget* crop_height_spin = NULL;
    GtkWidget* crop_link_toggle = NULL;
    GtkWidget* crop_delete_pixels_check = NULL;
    GtkWidget* crop_grow_canvas_check = NULL;
    GtkWidget* crop_darken_check = NULL;
    GtkWidget* crop_darken_opacity_scale = NULL;
    GtkWidget* crop_overlay_combo = NULL;
    GtkWidget* crop_snap_check = NULL;
    GtkWidget* crop_reset_btn = NULL;
    GtkWidget* crop_apply_btn = NULL;
    ToolOptions* crop_opts = tool_options_get_for_tool(TOOL_CROP);

    if (gtk_builder_add_from_resource(crop_builder, "/ui/crop_options.glade", &crop_error)) {
        tool_opts_panel->crop_panel = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_options_panel"));
        if (tool_opts_panel->crop_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->crop_panel);

            crop_constraint_stack = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_constraint_stack"));
            crop_ratio_w_spin = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_ratio_width_spin"));
            crop_ratio_h_spin = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_ratio_height_spin"));
            crop_width_spin = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_width_spin"));
            crop_height_spin = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_height_spin"));
            crop_link_toggle = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_link_toggle"));
            crop_delete_pixels_check = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_delete_pixels_checkbox"));
            crop_grow_canvas_check = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_grow_canvas_checkbox"));
            crop_darken_check = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_darken_checkbox"));
            crop_darken_opacity_scale = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_darken_opacity_scale"));
            crop_overlay_combo = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_overlay_combo"));
            crop_snap_check = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_snap_checkbox"));
            crop_reset_btn = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_reset_button"));
            crop_apply_btn = GTK_WIDGET(gtk_builder_get_object(crop_builder, "crop_apply_button"));

            g_object_set_data(G_OBJECT(tool_opts_panel->crop_panel), "crop_ratio_width_spin", crop_ratio_w_spin);
            g_object_set_data(G_OBJECT(tool_opts_panel->crop_panel), "crop_ratio_height_spin", crop_ratio_h_spin);
            g_object_set_data(G_OBJECT(tool_opts_panel->crop_panel), "crop_width_spin", crop_width_spin);
            g_object_set_data(G_OBJECT(tool_opts_panel->crop_panel), "crop_height_spin", crop_height_spin);
            g_object_set_data(G_OBJECT(tool_opts_panel->crop_panel), "crop_constraint_stack", crop_constraint_stack);

            if (crop_constraint_stack) {
                g_signal_connect(crop_constraint_stack, "notify::visible-child-name",
                                 G_CALLBACK(on_crop_stack_changed), tool_opts_panel);
            }
            if (crop_ratio_w_spin) {
                g_signal_connect(crop_ratio_w_spin, "value-changed", G_CALLBACK(on_crop_ratio_width_changed), tool_opts_panel);
                if (crop_opts)
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(crop_ratio_w_spin), (gdouble)crop_opts->crop_ratio_w);
            }
            if (crop_ratio_h_spin) {
                g_signal_connect(crop_ratio_h_spin, "value-changed", G_CALLBACK(on_crop_ratio_height_changed), tool_opts_panel);
                if (crop_opts)
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(crop_ratio_h_spin), (gdouble)crop_opts->crop_ratio_h);
            }
            if (crop_width_spin) {
                g_signal_connect(crop_width_spin, "value-changed", G_CALLBACK(on_crop_width_changed), tool_opts_panel);
                if (crop_opts)
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(crop_width_spin), (gdouble)crop_opts->crop_width);
            }
            if (crop_height_spin) {
                g_signal_connect(crop_height_spin, "value-changed", G_CALLBACK(on_crop_height_changed), tool_opts_panel);
                if (crop_opts)
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(crop_height_spin), (gdouble)crop_opts->crop_height);
            }
            if (crop_link_toggle) {
                g_signal_connect(crop_link_toggle, "toggled", G_CALLBACK(on_crop_link_toggled), tool_opts_panel);
                if (crop_opts)
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(crop_link_toggle), crop_opts->crop_link);
                /* Ensure icon matches initial toggle state (set_active may not emit toggled) */
                on_crop_link_toggled(GTK_TOGGLE_BUTTON(crop_link_toggle), tool_opts_panel);
            }
            if (crop_delete_pixels_check) {
                g_signal_connect(crop_delete_pixels_check, "toggled", G_CALLBACK(on_crop_delete_pixels_toggled), tool_opts_panel);
                if (crop_opts)
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(crop_delete_pixels_check), crop_opts->crop_delete_pixels);
            }
            if (crop_grow_canvas_check) {
                g_signal_connect(crop_grow_canvas_check, "toggled", G_CALLBACK(on_crop_grow_canvas_toggled), tool_opts_panel);
                if (crop_opts)
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(crop_grow_canvas_check), crop_opts->crop_grow_canvas);
            }
            if (crop_darken_check) {
                g_signal_connect(crop_darken_check, "toggled", G_CALLBACK(on_crop_darken_toggled), tool_opts_panel);
                if (crop_opts)
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(crop_darken_check), crop_opts->crop_darken_outside);
            }
            if (crop_darken_opacity_scale) {
                g_signal_connect(crop_darken_opacity_scale, "value-changed", G_CALLBACK(on_crop_darken_opacity_changed), tool_opts_panel);
                if (crop_opts)
                    gtk_range_set_value(GTK_RANGE(crop_darken_opacity_scale), (gdouble)crop_opts->crop_darken_opacity);
            }
            if (crop_overlay_combo) {
                g_signal_connect(crop_overlay_combo, "changed", G_CALLBACK(on_crop_overlay_changed), tool_opts_panel);
                if (crop_opts)
                    gtk_combo_box_set_active(GTK_COMBO_BOX(crop_overlay_combo), crop_opts->crop_overlay_mode);
            }
            if (crop_snap_check) {
                g_signal_connect(crop_snap_check, "toggled", G_CALLBACK(on_crop_snap_toggled), tool_opts_panel);
                if (crop_opts)
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(crop_snap_check), crop_opts->crop_snap);
            }
            if (crop_reset_btn) {
                g_signal_connect(crop_reset_btn, "clicked", G_CALLBACK(on_crop_reset_clicked), tool_opts_panel);
            }
            if (crop_apply_btn) {
                g_signal_connect(crop_apply_btn, "clicked", G_CALLBACK(on_crop_apply_clicked), tool_opts_panel);
            }

            gtk_widget_set_visible(tool_opts_panel->crop_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->crop_panel, TRUE);
        }
        g_object_unref(crop_builder);
    } else {
        g_warning("Failed to load crop options panel: %s", crop_error ? crop_error->message : "Unknown error");
        if (crop_error)
            g_error_free(crop_error);
        if (crop_builder)
            g_object_unref(crop_builder);
    }

    /* Load move panel from Glade */
    GtkBuilder* move_builder = gtk_builder_new();
    GError* move_error = NULL;
    GtkWidget* move_title = NULL;
    GtkWidget* move_auto_select = NULL;
    ToolOptions* move_opts = tool_options_get_for_tool(TOOL_MOVE);

    if (gtk_builder_add_from_resource(move_builder, "/ui/move_options.glade", &move_error)) {
        tool_opts_panel->move_panel = GTK_WIDGET(gtk_builder_get_object(move_builder, "move_options_panel"));
        if (tool_opts_panel->move_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->move_panel);

            /* Get widgets */
            move_title = GTK_WIDGET(gtk_builder_get_object(move_builder, "move_title_label"));
            move_auto_select = GTK_WIDGET(gtk_builder_get_object(move_builder, "move_auto_select_checkbox"));

            /* Store references */
            if (move_title) {
                g_object_set_data(G_OBJECT(tool_opts_panel->move_panel), "title_label", move_title);
            }
            if (move_auto_select) {
                g_object_set_data(G_OBJECT(tool_opts_panel->move_panel), "auto_select_checkbox", move_auto_select);
                tool_opts_panel->move_auto_select_checkbox = move_auto_select;
                if (move_opts) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(move_auto_select),
                                                 tool_options_get_move_auto_select(move_opts));
                    g_object_set_data(G_OBJECT(move_auto_select), "tool_options_panel", tool_opts_panel);
                    g_signal_connect(move_auto_select, "toggled",
                                     G_CALLBACK(on_move_auto_select_toggled), NULL);
                }
            }

            /* Hide move panel initially */
            gtk_widget_set_visible(tool_opts_panel->move_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->move_panel, TRUE);
        }
        g_object_unref(move_builder);
    } else {
        g_warning("Failed to load move options panel: %s", move_error ? move_error->message : "Unknown error");
        if (move_error) {
            g_error_free(move_error);
        }
        if (move_builder) {
            g_object_unref(move_builder);
        }
    }

    /* Hide the container initially - will be shown when a tool with options is selected */
    gtk_widget_set_visible(container, FALSE);

    return tool_opts_panel;
}

/**
 * Switch panels based on tool type
 */
void tool_options_panel_switch_tool(ToolOptionsPanel* panel, const gchar* tool_name) {
    if (!panel || !tool_name) {
        return;
    }

    /* Determine tool type from name */
    ToolType new_tool_type = TOOL_COUNT; /* Default to invalid (no options) */
    if (g_strcmp0(tool_name, "Eraser") == 0) {
        new_tool_type = TOOL_ERASER;
    } else if (g_strcmp0(tool_name, "Brush") == 0) {
        new_tool_type = TOOL_BRUSH;
    } else if (g_strcmp0(tool_name, "Pencil") == 0) {
        new_tool_type = TOOL_PENCIL;
    } else if (g_strcmp0(tool_name, "Paint Bucket") == 0) {
        new_tool_type = TOOL_PAINT_BUCKET;
    } else if (g_strcmp0(tool_name, "Rectangular Select") == 0) {
        new_tool_type = TOOL_RECT_SELECT;
    } else if (g_strcmp0(tool_name, "Elliptical Select") == 0) {
        new_tool_type = TOOL_ELLIPSE_SELECT;
    } else if (g_strcmp0(tool_name, "Move") == 0) {
        new_tool_type = TOOL_MOVE;
    } else if (g_strcmp0(tool_name, "Crop") == 0) {
        new_tool_type = TOOL_CROP;
    } else if (g_strcmp0(tool_name, "Color Picker") == 0) {
        new_tool_type = TOOL_COLOR_PICKER;
    } else if (g_strcmp0(tool_name, "Hand") == 0) {
        new_tool_type = TOOL_HAND;
    } else if (g_strcmp0(tool_name, "Zoom") == 0) {
        new_tool_type = TOOL_ZOOM;
    }

    /* Update current tool type */
    panel->current_tool_type = new_tool_type;

    /* Hide all panels first */
    if (panel->brush_panel) {
        gtk_widget_set_visible(panel->brush_panel, FALSE);
    }
    if (panel->eraser_panel) {
        gtk_widget_set_visible(panel->eraser_panel, FALSE);
    }
    if (panel->pencil_panel) {
        gtk_widget_set_visible(panel->pencil_panel, FALSE);
    }
    if (panel->paintbucket_panel) {
        gtk_widget_set_visible(panel->paintbucket_panel, FALSE);
    }
    if (panel->color_picker_panel) {
        gtk_widget_set_visible(panel->color_picker_panel, FALSE);
    }
    if (panel->rect_select_panel) {
        gtk_widget_set_visible(panel->rect_select_panel, FALSE);
    }
    if (panel->ellipse_select_panel) {
        gtk_widget_set_visible(panel->ellipse_select_panel, FALSE);
    }
    if (panel->crop_panel) {
        gtk_widget_set_visible(panel->crop_panel, FALSE);
    }
    if (panel->move_panel) {
        gtk_widget_set_visible(panel->move_panel, FALSE);
    }

    /* For crop tool, show the options panel */
    if (new_tool_type == TOOL_CROP && panel->crop_panel) {
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        gtk_widget_set_no_show_all(panel->crop_panel, FALSE);
        gtk_widget_set_visible(panel->crop_panel, TRUE);
        gtk_widget_show_all(panel->crop_panel);

        ToolOptions* opts = tool_options_get_for_tool(TOOL_CROP);
        if (opts) {
            /* Initialize fixed size to canvas size when switching to crop tool */
            GtkWidget* window = panel->panel ? gtk_widget_get_toplevel(panel->panel) : NULL;
            if (window) {
                AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(window), "app_context");
                if (ctx) {
                    ImageDocument* doc = ui_get_active_document(ctx);
                    if (doc && doc->width > 0 && doc->height > 0) {
                        tool_options_set_crop_size(opts, (gint)doc->width, (gint)doc->height);
                        GtkWidget* width_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_width_spin");
                        GtkWidget* height_spin = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_height_spin");
                        if (width_spin && GTK_IS_SPIN_BUTTON(width_spin)) {
                            g_signal_handlers_block_by_func(width_spin, G_CALLBACK(on_crop_width_changed), panel);
                            gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), (gdouble)doc->width);
                            g_signal_handlers_unblock_by_func(width_spin, G_CALLBACK(on_crop_width_changed), panel);
                        }
                        if (height_spin && GTK_IS_SPIN_BUTTON(height_spin)) {
                            g_signal_handlers_block_by_func(height_spin, G_CALLBACK(on_crop_height_changed), panel);
                            gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_spin), (gdouble)doc->height);
                            g_signal_handlers_unblock_by_func(height_spin, G_CALLBACK(on_crop_height_changed), panel);
                        }
                    }
                }
            }

            GtkWidget* stack = (GtkWidget*)g_object_get_data(G_OBJECT(panel->crop_panel), "crop_constraint_stack");
            if (stack && GTK_IS_STACK(stack)) {
                const gchar* name = (opts->crop_constraint_mode == 1) ? "ratio" : (opts->crop_constraint_mode == 2) ? "size"
                                                                                                                    : "free";
                g_signal_handlers_block_by_func(stack, G_CALLBACK(on_crop_stack_changed), panel);
                gtk_stack_set_visible_child_name(GTK_STACK(stack), name);
                g_signal_handlers_unblock_by_func(stack, G_CALLBACK(on_crop_stack_changed), panel);
            }
        }
        return;
    }

    /* For ellipse select tool, show the options panel */
    if (new_tool_type == TOOL_ELLIPSE_SELECT && panel->ellipse_select_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show ellipse select panel */
        gtk_widget_set_no_show_all(panel->ellipse_select_panel, FALSE);
        gtk_widget_set_visible(panel->ellipse_select_panel, TRUE);
        gtk_widget_show_all(panel->ellipse_select_panel);

        /* Update all ellipse select tool options to match current settings */
        ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
        if (opts) {
            /* Update combine mode buttons */
            update_ellipse_combine_mode_buttons(panel, (SelectionCombineMode)opts->ellipse_select_combine);

            /* Update smoothing mode combo */
            if (panel->ellipse_smooth_combo) {
                g_signal_handlers_block_by_func(panel->ellipse_smooth_combo,
                                                G_CALLBACK(on_ellipse_select_smooth_changed), panel);
                gtk_combo_box_set_active(GTK_COMBO_BOX(panel->ellipse_smooth_combo),
                                         (gint)opts->ellipse_select_smooth);
                g_signal_handlers_unblock_by_func(panel->ellipse_smooth_combo,
                                                  G_CALLBACK(on_ellipse_select_smooth_changed), panel);
            }

            /* Update feather radius slider */
            if (panel->ellipse_feather_scale) {
                g_signal_handlers_block_by_func(panel->ellipse_feather_scale,
                                                G_CALLBACK(on_ellipse_select_feather_changed), panel);
                gtk_range_set_value(GTK_RANGE(panel->ellipse_feather_scale), opts->ellipse_select_feather);
                g_signal_handlers_unblock_by_func(panel->ellipse_feather_scale,
                                                  G_CALLBACK(on_ellipse_select_feather_changed), panel);
            }

            /* Update animation checkbox */
            if (panel->ellipse_animate_checkbox) {
                g_signal_handlers_block_by_func(panel->ellipse_animate_checkbox,
                                                G_CALLBACK(on_ellipse_select_animate_toggled), panel);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->ellipse_animate_checkbox),
                                             opts->ellipse_select_animate);
                g_signal_handlers_unblock_by_func(panel->ellipse_animate_checkbox,
                                                  G_CALLBACK(on_ellipse_select_animate_toggled), panel);
            }
        }

        return;
    }

    /* For rect select tool, show the options panel */
    if (new_tool_type == TOOL_RECT_SELECT && panel->rect_select_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show rect select panel */
        gtk_widget_set_no_show_all(panel->rect_select_panel, FALSE);
        gtk_widget_set_visible(panel->rect_select_panel, TRUE);
        gtk_widget_show_all(panel->rect_select_panel);

        /* Update all rect select tool options to match current settings */
        ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
        if (opts) {
            /* Update combine mode buttons */
            update_combine_mode_buttons(panel, (SelectionCombineMode)opts->rect_select_combine);

            /* Update smoothing mode combo */
            if (panel->rect_smooth_combo) {
                g_signal_handlers_block_by_func(panel->rect_smooth_combo,
                                                G_CALLBACK(on_rect_select_smooth_changed), panel);
                gtk_combo_box_set_active(GTK_COMBO_BOX(panel->rect_smooth_combo),
                                         (gint)opts->rect_select_smooth);
                g_signal_handlers_unblock_by_func(panel->rect_smooth_combo,
                                                  G_CALLBACK(on_rect_select_smooth_changed), panel);
            }

            /* Update feather radius slider */
            if (panel->rect_feather_scale) {
                g_signal_handlers_block_by_func(panel->rect_feather_scale,
                                                G_CALLBACK(on_rect_select_feather_changed), panel);
                gtk_range_set_value(GTK_RANGE(panel->rect_feather_scale), opts->rect_select_feather);
                g_signal_handlers_unblock_by_func(panel->rect_feather_scale,
                                                  G_CALLBACK(on_rect_select_feather_changed), panel);
            }

            /* Update animation checkbox */
            if (panel->rect_animate_checkbox) {
                g_signal_handlers_block_by_func(panel->rect_animate_checkbox,
                                                G_CALLBACK(on_rect_select_animate_toggled), panel);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->rect_animate_checkbox),
                                             opts->rect_select_animate);
                g_signal_handlers_unblock_by_func(panel->rect_animate_checkbox,
                                                  G_CALLBACK(on_rect_select_animate_toggled), panel);
            }
        }

        return;
    }

    /* Show appropriate panel if tool has options */
    if (new_tool_type == TOOL_BRUSH && panel->brush_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show brush panel */
        gtk_widget_set_no_show_all(panel->brush_panel, FALSE);
        gtk_widget_set_visible(panel->brush_panel, TRUE);
        gtk_widget_show_all(panel->brush_panel);
        /* Get brush panel widgets from builder and initialize them */
        GtkBuilder* builder = GTK_BUILDER(g_object_get_data(G_OBJECT(panel->brush_panel), "builder"));
        ToolOptions* opts = tool_options_get_for_tool(TOOL_BRUSH);
        if (builder) {
            GtkWidget* widget;
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_title_label"));
            if (widget)
                panel->title_label = widget;
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_size_scale"));
            if (widget) {
                panel->size_scale = widget;
                if (opts) {
                    /* Disconnect signal before setting value to prevent triggering callback */
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_size_changed), NULL);
                    set_scale_value(widget, opts->size);
                    /* Store panel reference in scale widget for cursor updates */
                    g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                    /* Reconnect signal after setting value */
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
                }
            }
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_opacity_scale"));
            if (widget) {
                panel->opacity_scale = widget;
                if (opts) {
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_opacity_changed), NULL);
                    set_scale_value(widget, opts->opacity * 100.0);
                    /* Store panel reference in scale widget for callback */
                    g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
                }
            }
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_hardness_scale"));
            if (widget) {
                panel->hardness_scale = widget;
                if (opts) {
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_hardness_changed), NULL);
                    set_scale_value(widget, opts->hardness * 100.0);
                    /* Store panel reference in scale widget for callback */
                    g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
                }
            }
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_flow_scale"));
            if (widget) {
                panel->flow_scale = widget;
                if (opts) {
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_flow_changed), NULL);
                    set_scale_value(widget, opts->flow * 100.0);
                    /* Store panel reference in scale widget for callback */
                    g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_flow_changed), NULL);
                }
            }
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_spacing_scale"));
            if (widget) {
                panel->spacing_scale = widget;
                if (opts) {
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_spacing_changed), NULL);
                    set_scale_value(widget, opts->spacing * 100.0);
                    /* Store panel reference in scale widget for callback */
                    g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_spacing_changed), NULL);
                }
            }
        }
    } else if (new_tool_type == TOOL_ERASER && panel->eraser_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show eraser panel */
        gtk_widget_set_no_show_all(panel->eraser_panel, FALSE);
        gtk_widget_set_visible(panel->eraser_panel, TRUE);
        gtk_widget_show_all(panel->eraser_panel);
        /* Get eraser panel widgets from stored references and initialize them */
        ToolOptions* opts = tool_options_get_for_tool(TOOL_ERASER);
        GtkWidget* widget;
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "title_label"));
        if (widget)
            panel->title_label = widget;
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "size_scale"));
        if (widget) {
            panel->size_scale = widget;
            if (opts) {
                /* Disconnect signal before setting value to prevent triggering callback */
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_size_changed), NULL);
                set_scale_value(widget, opts->size);
                /* Store panel reference in scale widget for cursor updates */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                /* Reconnect signal after setting value */
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "opacity_scale"));
        if (widget) {
            panel->opacity_scale = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_opacity_changed), NULL);
                set_scale_value(widget, opts->opacity * 100.0);
                /* Store panel reference in scale widget for callback */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "hardness_scale"));
        if (widget) {
            panel->hardness_scale = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_hardness_changed), NULL);
                set_scale_value(widget, opts->hardness * 100.0);
                /* Store panel reference in scale widget for callback */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "flow_scale"));
        if (widget) {
            panel->flow_scale = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_flow_changed), NULL);
                set_scale_value(widget, opts->flow * 100.0);
                /* Store panel reference in scale widget for callback */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_flow_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "spacing_scale"));
        if (widget) {
            panel->spacing_scale = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_spacing_changed), NULL);
                set_scale_value(widget, opts->spacing * 100.0);
                /* Store panel reference in scale widget for callback */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_spacing_changed), NULL);
            }
        }
    } else if (new_tool_type == TOOL_PENCIL && panel->pencil_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show pencil panel */
        gtk_widget_set_no_show_all(panel->pencil_panel, FALSE);
        gtk_widget_set_visible(panel->pencil_panel, TRUE);
        gtk_widget_show_all(panel->pencil_panel);
        /* Get pencil panel widgets from stored references and initialize them */
        ToolOptions* opts = tool_options_get_for_tool(TOOL_PENCIL);
        GtkWidget* widget;
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->pencil_panel), "title_label"));
        if (widget)
            panel->title_label = widget;
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->pencil_panel), "size_scale"));
        if (widget) {
            panel->size_scale = widget;
            if (opts) {
                /* Disconnect signal before setting value to prevent triggering callback */
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_size_changed), NULL);
                set_scale_value(widget, opts->size);
                /* Store panel reference in scale widget for cursor updates */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                /* Reconnect signal after setting value */
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->pencil_panel), "opacity_scale"));
        if (widget) {
            panel->opacity_scale = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_opacity_changed), NULL);
                set_scale_value(widget, opts->opacity * 100.0);
                /* Store panel reference in scale widget for callback */
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->pencil_panel), "antialias_checkbox"));
        if (widget) {
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_pencil_antialias_toggled), NULL);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), opts->pencil_antialias);
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "toggled", G_CALLBACK(on_pencil_antialias_toggled), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->pencil_panel), "align_pixel_grid_checkbox"));
        if (widget) {
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_pencil_align_pixel_grid_toggled), NULL);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), opts->pencil_align_pixel_grid);
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "toggled", G_CALLBACK(on_pencil_align_pixel_grid_toggled), NULL);
            }
        }
    } else if (new_tool_type == TOOL_PAINT_BUCKET && panel->paintbucket_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show paint bucket panel */
        gtk_widget_set_no_show_all(panel->paintbucket_panel, FALSE);
        gtk_widget_set_visible(panel->paintbucket_panel, TRUE);
        gtk_widget_show_all(panel->paintbucket_panel);

        /* Get paint bucket panel widgets and initialize them */
        GtkWidget* widget;
        ToolOptions* opts = tool_options_get_for_tool(TOOL_PAINT_BUCKET);

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->paintbucket_panel), "title_label"));
        if (widget)
            panel->title_label = widget;

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->paintbucket_panel), "tolerance_scale"));
        if (widget) {
            panel->tolerance_scale = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_tolerance_changed), NULL);
                set_scale_value(widget, opts->tolerance);
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_tolerance_changed), NULL);
            }
        }

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->paintbucket_panel), "contiguous_radio"));
        if (widget) {
            panel->contiguous_radio = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_fill_area_changed), NULL);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), opts->fill_contiguous);
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "toggled", G_CALLBACK(on_fill_area_changed), NULL);
            }
        }

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->paintbucket_panel), "global_radio"));
        if (widget) {
            panel->global_radio = widget;
            if (opts && panel->contiguous_radio) {
                /* Ensure radio buttons are grouped */
                gtk_radio_button_join_group(GTK_RADIO_BUTTON(widget),
                                            GTK_RADIO_BUTTON(panel->contiguous_radio));
            }
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_fill_area_changed), NULL);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !opts->fill_contiguous);
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "toggled", G_CALLBACK(on_fill_area_changed), NULL);
            }
        }

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->paintbucket_panel), "antialiased_checkbox"));
        if (widget) {
            panel->antialiased_checkbox = widget;
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_fill_antialiased_toggled), NULL);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), opts->fill_antialiased);
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "toggled", G_CALLBACK(on_fill_antialiased_toggled), NULL);
            }
        }
    } else if (new_tool_type == TOOL_COLOR_PICKER && panel->color_picker_panel) {
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        gtk_widget_set_no_show_all(panel->color_picker_panel, FALSE);
        gtk_widget_set_visible(panel->color_picker_panel, TRUE);
        gtk_widget_show_all(panel->color_picker_panel);

        GtkWidget* widget;
        ToolOptions* opts = tool_options_get_for_tool(TOOL_COLOR_PICKER);

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->color_picker_panel), "title_label"));
        if (widget) {
            panel->title_label = widget;
        }

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->color_picker_panel), "sample_radius_scale"));
        if (widget) {
            if (opts) {
                g_signal_handlers_block_by_func(widget, G_CALLBACK(on_color_picker_sample_radius_changed), NULL);
                set_scale_value(widget, (gdouble)opts->color_picker_sample_radius);
                g_signal_handlers_unblock_by_func(widget, G_CALLBACK(on_color_picker_sample_radius_changed), NULL);
            }
        }

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->color_picker_panel), "sample_layer_button"));
        if (widget && opts) {
            g_signal_handlers_block_by_func(widget, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), opts->color_picker_sample_from_layer);
            g_signal_handlers_unblock_by_func(widget, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->color_picker_panel), "sample_image_button"));
        if (widget && opts) {
            g_signal_handlers_block_by_func(widget, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !opts->color_picker_sample_from_layer);
            g_signal_handlers_unblock_by_func(widget, G_CALLBACK(on_color_picker_sample_from_toggled), panel);
        }
    } else if (new_tool_type == TOOL_MOVE && panel->move_panel) {
        /* Show main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, TRUE);
        }
        /* Show move panel */
        gtk_widget_set_no_show_all(panel->move_panel, FALSE);
        gtk_widget_set_visible(panel->move_panel, TRUE);
        gtk_widget_show_all(panel->move_panel);

        /* Get move panel widgets and initialize them */
        GtkWidget* widget;
        ToolOptions* opts = tool_options_get_for_tool(TOOL_MOVE);

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->move_panel), "title_label"));
        if (widget)
            panel->title_label = widget;

        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->move_panel), "auto_select_checkbox"));
        if (widget) {
            if (opts) {
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_move_auto_select_toggled), NULL);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
                                             tool_options_get_move_auto_select(opts));
                g_object_set_data(G_OBJECT(widget), "tool_options_panel", panel);
                g_signal_connect(widget, "toggled", G_CALLBACK(on_move_auto_select_toggled), NULL);
            }
        }
    } else {
        /* For tools without options (Hand, Zoom, etc.), hide main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, FALSE);
        }

        /* Explicitly hide all option panels */
        if (panel->brush_panel) {
            gtk_widget_set_no_show_all(panel->brush_panel, TRUE);
            gtk_widget_hide(panel->brush_panel);
        }
        if (panel->eraser_panel) {
            gtk_widget_set_no_show_all(panel->eraser_panel, TRUE);
            gtk_widget_hide(panel->eraser_panel);
        }
        if (panel->pencil_panel) {
            gtk_widget_set_no_show_all(panel->pencil_panel, TRUE);
            gtk_widget_hide(panel->pencil_panel);
        }
        if (panel->paintbucket_panel) {
            gtk_widget_set_no_show_all(panel->paintbucket_panel, TRUE);
            gtk_widget_hide(panel->paintbucket_panel);
        }
        if (panel->color_picker_panel) {
            gtk_widget_set_no_show_all(panel->color_picker_panel, TRUE);
            gtk_widget_hide(panel->color_picker_panel);
        }
        if (panel->rect_select_panel) {
            gtk_widget_set_no_show_all(panel->rect_select_panel, TRUE);
            gtk_widget_hide(panel->rect_select_panel);
        }
        if (panel->ellipse_select_panel) {
            gtk_widget_set_no_show_all(panel->ellipse_select_panel, TRUE);
            gtk_widget_hide(panel->ellipse_select_panel);
        }
        if (panel->crop_panel) {
            gtk_widget_set_no_show_all(panel->crop_panel, TRUE);
            gtk_widget_hide(panel->crop_panel);
        }
        if (panel->move_panel) {
            gtk_widget_set_no_show_all(panel->move_panel, TRUE);
            gtk_widget_hide(panel->move_panel);
        }

        /* Clear widget references */
        panel->title_label = NULL;
        panel->size_scale = NULL;
        panel->opacity_scale = NULL;
        panel->hardness_scale = NULL;
        panel->flow_scale = NULL;
        panel->spacing_scale = NULL;
        panel->tolerance_scale = NULL;
        panel->contiguous_radio = NULL;
        panel->global_radio = NULL;
        panel->antialiased_checkbox = NULL;
    }

    panel->current_tool_type = new_tool_type;
}

/**
 * Set the tool registry for the tool options panel
 */
void tool_options_panel_set_tool_registry(ToolOptionsPanel* panel, ToolRegistry* registry) {
    if (panel) {
        panel->tool_registry = registry;
    }
}

/**
 * Update the combine mode buttons to reflect a specific mode
 * Called from tool when hotkeys are pressed
 */
void tool_options_panel_set_combine_mode(ToolOptionsPanel* panel, SelectionCombineMode mode) {
    if (!panel) {
        return;
    }

    update_combine_mode_buttons(panel, mode);
}

void tool_options_panel_set_color_picker_preview(ToolOptionsPanel* panel,
                                                 gboolean has_color,
                                                 gdouble r, gdouble g, gdouble b, gdouble a) {
    g_color_picker_preview_has_color = has_color ? TRUE : FALSE;
    g_color_picker_preview_r = r;
    g_color_picker_preview_g = g;
    g_color_picker_preview_b = b;
    g_color_picker_preview_a = a;

    if (!panel || !panel->color_picker_panel) {
        return;
    }
    GtkWidget* color_draw = (GtkWidget*)g_object_get_data(G_OBJECT(panel->color_picker_panel), "color_draw");
    if (color_draw) {
        gtk_widget_queue_draw(color_draw);
    }
    /* Also queue the panel so the preview section redraws reliably */
    gtk_widget_queue_draw(panel->color_picker_panel);
}

/**
 * Free a tool options panel
 */
void tool_options_panel_free(ToolOptionsPanel* panel) {
    if (panel) {
        g_free(panel);
    }
}
