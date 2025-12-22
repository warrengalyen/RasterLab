#include "ui/tool_options_panel.h"
#include "document.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "tools.h"
#include "ui.h"
#include <stdio.h>

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

        /* Update cursor for brush and eraser tools when size changes */
        if (panel && panel->tool_registry) {
            Tool* brush_tool = tool_manager_get(panel->tool_registry, TOOL_BRUSH);
            Tool* eraser_tool = tool_manager_get(panel->tool_registry, TOOL_ERASER);
            Tool* active_tool = tool_manager_get_active(panel->tool_registry);

            /* Update cursors for brush and eraser tools */
            if (brush_tool) {
                tool_update_cursor(brush_tool, size);
            }
            if (eraser_tool) {
                tool_update_cursor(eraser_tool, size);
            }

            /* Update cursor on drawing areas if brush/eraser is active */
            if (active_tool && (active_tool->type == TOOL_BRUSH || active_tool->type == TOOL_ERASER)) {
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

    return panel;
}

/**
 * Signal handler for rectangle select combine mode changes
 */
static void on_rect_select_combine_changed(GtkComboBox* combo_box, gpointer user_data) {
    (void)user_data; /* Unused */

    gint active = gtk_combo_box_get_active(combo_box);
    if (active < 0 || active >= 4) {
        return;
    }

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_combine(opts, (SelectionCombineMode)active);
}

/**
 * Signal handler for rectangle select smoothing mode changes
 */
static void on_rect_select_smooth_changed(GtkComboBox* combo_box, gpointer user_data) {
    (void)user_data; /* Unused */

    gint active = gtk_combo_box_get_active(combo_box);
    if (active < 0 || active >= 3) {
        return;
    }

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_smooth(opts, (SelectionSmoothingMode)active);
}

/**
 * Signal handler for rectangle select feather radius changes
 */
static void on_rect_select_feather_changed(GtkRange* range, gpointer user_data) {
    (void)user_data; /* Unused */

    gdouble value = gtk_range_get_value(range);
    gint feather_radius = (gint)value;

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_feather(opts, (gfloat)feather_radius);
}

/**
 * Signal handler for rectangle select animation checkbox
 */
static void on_rect_select_animate_toggled(GtkToggleButton* button, gpointer user_data) {
    (void)user_data; /* Unused */

    gboolean active = gtk_toggle_button_get_active(button);

    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (!opts) {
        return;
    }

    tool_options_set_rect_select_animate(opts, active);
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
    tool_opts_panel->paintbucket_panel = NULL;
    tool_opts_panel->rect_select_panel = NULL;
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
    tool_opts_panel->rect_combine_combo = NULL;
    tool_opts_panel->rect_smooth_combo = NULL;
    tool_opts_panel->rect_feather_scale = NULL;
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

    /* Load rectangular select panel from Glade */
    GtkBuilder* rect_select_builder = gtk_builder_new();
    GError* rect_select_error = NULL;
    GtkWidget* rect_select_title = NULL;
    GtkWidget* rect_select_animate = NULL;
    GtkWidget* rect_select_combine = NULL;
    GtkWidget* rect_select_smooth = NULL;
    GtkWidget* rect_select_feather = NULL;

    if (gtk_builder_add_from_resource(rect_select_builder, "/ui/rect_select_options.glade", &rect_select_error)) {
        tool_opts_panel->rect_select_panel = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_options_panel"));
        if (tool_opts_panel->rect_select_panel) {
            gtk_container_add(GTK_CONTAINER(container), tool_opts_panel->rect_select_panel);

            /* Get widgets */
            rect_select_title = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_title_label"));
            rect_select_animate = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_animate_checkbox"));
            rect_select_combine = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_combine_combo"));
            rect_select_smooth = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_smooth_combo"));
            rect_select_feather = GTK_WIDGET(gtk_builder_get_object(rect_select_builder, "rect_select_feather_scale"));

            /* Populate combo boxes */
            if (rect_select_combine) {
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_combine), "New");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_combine), "Add");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_combine), "Subtract");
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rect_select_combine), "Intersect");
                gtk_combo_box_set_active(GTK_COMBO_BOX(rect_select_combine), 0);
                g_object_set_data(G_OBJECT(tool_opts_panel->rect_select_panel), "combine_combo", rect_select_combine);
                tool_opts_panel->rect_combine_combo = rect_select_combine;

                /* Connect signal to update tool options when changed */
                g_signal_connect(rect_select_combine, "changed",
                                 G_CALLBACK(on_rect_select_combine_changed), NULL);
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
                                 G_CALLBACK(on_rect_select_smooth_changed), NULL);
            }

            if (rect_select_feather) {
                g_object_set_data(G_OBJECT(tool_opts_panel->rect_select_panel), "feather_scale", rect_select_feather);
                tool_opts_panel->rect_feather_scale = rect_select_feather;

                /* Connect signal to update tool options when changed */
                g_signal_connect(rect_select_feather, "value-changed",
                                 G_CALLBACK(on_rect_select_feather_changed), NULL);
            }

            if (rect_select_animate) {
                g_object_set_data(G_OBJECT(tool_opts_panel->rect_select_panel), "animate_checkbox", rect_select_animate);
                tool_opts_panel->rect_animate_checkbox = rect_select_animate;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rect_select_animate), TRUE);

                /* Connect signal to update tool options when changed */
                g_signal_connect(rect_select_animate, "toggled",
                                 G_CALLBACK(on_rect_select_animate_toggled), NULL);
            }

            /* Hide rect select panel initially */
            gtk_widget_set_visible(tool_opts_panel->rect_select_panel, FALSE);
            gtk_widget_set_no_show_all(tool_opts_panel->rect_select_panel, TRUE);
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
    ToolType new_tool_type = TOOL_MOVE; /* Default to no options panel */
    if (g_strcmp0(tool_name, "Eraser") == 0) {
        new_tool_type = TOOL_ERASER;
    } else if (g_strcmp0(tool_name, "Brush") == 0) {
        new_tool_type = TOOL_BRUSH;
    } else if (g_strcmp0(tool_name, "Paint Bucket") == 0) {
        new_tool_type = TOOL_PAINT_BUCKET;
    } else if (g_strcmp0(tool_name, "Rectangular Select") == 0) {
        new_tool_type = TOOL_RECT_SELECT;
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
    if (panel->paintbucket_panel) {
        gtk_widget_set_visible(panel->paintbucket_panel, FALSE);
    }
    if (panel->rect_select_panel) {
        gtk_widget_set_visible(panel->rect_select_panel, FALSE);
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
    } else {
        /* For tools without options (Move, etc.), hide main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, FALSE);
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
 * Free a tool options panel
 */
void tool_options_panel_free(ToolOptionsPanel* panel) {
    if (panel) {
        g_free(panel);
    }
}
