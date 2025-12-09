#include "ui/tool_options_panel.h"
#include "tool_options.h"
#include "tools.h"
#include <stdio.h>

/**
 * Tool options panel callback for size slider
 */
static void on_tool_size_changed(GtkScale *scale, gpointer user_data)
{
    (void)user_data;  /* Unused */
    gfloat size = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptions *opts = tool_options_get_global();
    if (opts) {
        tool_options_set_size(opts, size);
    }
}

/**
 * Tool options panel callback for opacity slider
 */
static void on_tool_opacity_changed(GtkScale *scale, gpointer user_data)
{
    (void)user_data;  /* Unused */
    gfloat opacity = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptions *opts = tool_options_get_global();
    if (opts) {
        tool_options_set_opacity(opts, opacity / 100.0f);
    }
}

/**
 * Tool options panel callback for hardness slider
 */
static void on_tool_hardness_changed(GtkScale *scale, gpointer user_data)
{
    (void)user_data;  /* Unused */
    gfloat hardness = gtk_range_get_value(GTK_RANGE(scale));
    ToolOptions *opts = tool_options_get_global();
    if (opts) {
        tool_options_set_hardness(opts, hardness / 100.0f);
    }
}

/**
 * Initialize a scale widget with adjustment and value
 */
static void initialize_scale(GtkWidget *scale, gdouble min, gdouble max, gdouble step, gdouble value)
{
    if (!scale) {
        return;
    }
    
    GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(scale));
    if (!adj) {
        /* Create adjustment if it doesn't exist */
        adj = gtk_adjustment_new(value, min, max, step, step * 10, 0);
        gtk_range_set_adjustment(GTK_RANGE(scale), adj);
    } else {
        /* Update existing adjustment */
        gtk_adjustment_configure(adj, value, min, max, step, step * 10, 0);
    }
    
    /* Set the value */
    gtk_range_set_value(GTK_RANGE(scale), value);
    
    /* Ensure widget is enabled */
    gtk_widget_set_sensitive(scale, TRUE);
}

/**
 * Load a tool options panel from Glade file
 */
static GtkWidget* load_panel_from_glade(const gchar *resource_path, const gchar *panel_id,
                                         GtkWidget **title_label, GtkWidget **size_scale,
                                         GtkWidget **opacity_scale, GtkWidget **hardness_scale)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *panel;
    ToolOptions *opts = tool_options_get_global();

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
    const gchar *title_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_title_label" : "eraser_title_label";
    const gchar *size_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_size_scale" : "eraser_size_scale";
    const gchar *opacity_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_opacity_scale" : "eraser_opacity_scale";
    const gchar *hardness_id = (g_strcmp0(panel_id, "brush_options_panel") == 0) ? "brush_hardness_scale" : "eraser_hardness_scale";

    if (title_label) {
        GtkWidget *widget = GTK_WIDGET(gtk_builder_get_object(builder, title_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", title_id);
        }
        *title_label = widget;
    }

    if (size_scale) {
        GtkWidget *widget = GTK_WIDGET(gtk_builder_get_object(builder, size_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", size_id);
            *size_scale = NULL;
        } else {
            *size_scale = widget;
            if (opts) {
                initialize_scale(widget, 1.0, 100.0, 1.0, opts->size);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
            }
        }
    }

    if (opacity_scale) {
        GtkWidget *widget = GTK_WIDGET(gtk_builder_get_object(builder, opacity_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", opacity_id);
            *opacity_scale = NULL;
        } else {
            *opacity_scale = widget;
            if (opts) {
                initialize_scale(widget, 0.0, 100.0, 1.0, opts->opacity * 100.0);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
            }
        }
    }

    if (hardness_scale) {
        GtkWidget *widget = GTK_WIDGET(gtk_builder_get_object(builder, hardness_id));
        if (!widget) {
            g_warning("Failed to get %s from builder", hardness_id);
            *hardness_scale = NULL;
        } else {
            *hardness_scale = widget;
            if (opts) {
                initialize_scale(widget, 0.0, 100.0, 1.0, opts->hardness * 100.0);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
            }
        }
    }

    /* Don't show the panel here - let the container handle it */
    /* gtk_widget_show_all(panel); */

    return panel;
}

ToolOptionsPanel* create_tool_options_panel(void)
{
    ToolOptionsPanel *tool_opts_panel = (ToolOptionsPanel *)g_malloc(sizeof(ToolOptionsPanel));
    
    if (!tool_opts_panel) {
        g_warning("Failed to allocate ToolOptionsPanel");
        return NULL;
    }
    
    /* Initialize all fields to NULL */
    tool_opts_panel->panel = NULL;
    tool_opts_panel->brush_panel = NULL;
    tool_opts_panel->eraser_panel = NULL;
    tool_opts_panel->title_label = NULL;
    tool_opts_panel->size_scale = NULL;
    tool_opts_panel->opacity_scale = NULL;
    tool_opts_panel->hardness_scale = NULL;
    tool_opts_panel->current_tool_type = TOOL_MOVE;  /* Start with no tool selected */

    /* Create container to hold the current panel */
    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    if (!container) {
        g_warning("Failed to create tool options container");
        g_free(tool_opts_panel);
        return NULL;
    }
    tool_opts_panel->panel = container;

    /* Load brush panel from Glade */
    GtkWidget *brush_title = NULL, *brush_size = NULL, *brush_opacity = NULL, *brush_hardness = NULL;
    tool_opts_panel->brush_panel = load_panel_from_glade(
        "/ui/brush_options.glade", "brush_options_panel",
        &brush_title,
        &brush_size,
        &brush_opacity,
        &brush_hardness);

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
    
    /* Hide brush panel initially - will be shown when tool is selected */
    gtk_widget_set_visible(tool_opts_panel->brush_panel, FALSE);
    gtk_widget_set_no_show_all(tool_opts_panel->brush_panel, TRUE);

    /* Load eraser panel from Glade (but don't show it yet) */
    GtkWidget *eraser_title = NULL, *eraser_size = NULL, *eraser_opacity = NULL, *eraser_hardness = NULL;
    tool_opts_panel->eraser_panel = load_panel_from_glade(
        "/ui/eraser_options.glade", "eraser_options_panel",
        &eraser_title,
        &eraser_size,
        &eraser_opacity,
        &eraser_hardness);

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
        /* Hide eraser panel initially - will be shown when tool is selected */
        gtk_widget_set_visible(tool_opts_panel->eraser_panel, FALSE);
        gtk_widget_set_no_show_all(tool_opts_panel->eraser_panel, TRUE);
    }

    /* Hide the container initially - will be shown when a tool with options is selected */
    gtk_widget_set_visible(container, FALSE);

    return tool_opts_panel;
}

/**
 * Switch panels based on tool type
 */
void tool_options_panel_switch_tool(ToolOptionsPanel *panel, const gchar *tool_name)
{
    if (!panel || !tool_name) {
        return;
    }

    /* Determine tool type from name */
    ToolType new_tool_type = TOOL_MOVE;  /* Default to no options panel */
    if (g_strcmp0(tool_name, "Eraser") == 0) {
        new_tool_type = TOOL_ERASER;
    } else if (g_strcmp0(tool_name, "Brush") == 0) {
        new_tool_type = TOOL_BRUSH;
    }

    /* Hide all panels first */
    if (panel->brush_panel) {
        gtk_widget_set_visible(panel->brush_panel, FALSE);
    }
    if (panel->eraser_panel) {
        gtk_widget_set_visible(panel->eraser_panel, FALSE);
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
        GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(panel->brush_panel), "builder"));
        ToolOptions *opts = tool_options_get_global();
        if (builder) {
            GtkWidget *widget;
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_title_label"));
            if (widget) panel->title_label = widget;
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_size_scale"));
            if (widget) {
                panel->size_scale = widget;
                if (opts) {
                    initialize_scale(widget, 1.0, 100.0, 1.0, opts->size);
                    /* Reconnect signal in case it was disconnected */
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_size_changed), NULL);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
                }
            }
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_opacity_scale"));
            if (widget) {
                panel->opacity_scale = widget;
                if (opts) {
                    initialize_scale(widget, 0.0, 100.0, 1.0, opts->opacity * 100.0);
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_opacity_changed), NULL);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
                }
            }
            widget = GTK_WIDGET(gtk_builder_get_object(builder, "brush_hardness_scale"));
            if (widget) {
                panel->hardness_scale = widget;
                if (opts) {
                    initialize_scale(widget, 0.0, 100.0, 1.0, opts->hardness * 100.0);
                    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_hardness_changed), NULL);
                    g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
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
        ToolOptions *opts = tool_options_get_global();
        GtkWidget *widget;
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "title_label"));
        if (widget) panel->title_label = widget;
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "size_scale"));
        if (widget) {
            panel->size_scale = widget;
            if (opts) {
                initialize_scale(widget, 1.0, 100.0, 1.0, opts->size);
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_size_changed), NULL);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "opacity_scale"));
        if (widget) {
            panel->opacity_scale = widget;
            if (opts) {
                initialize_scale(widget, 0.0, 100.0, 1.0, opts->opacity * 100.0);
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_opacity_changed), NULL);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
            }
        }
        widget = GTK_WIDGET(g_object_get_data(G_OBJECT(panel->eraser_panel), "hardness_scale"));
        if (widget) {
            panel->hardness_scale = widget;
            if (opts) {
                initialize_scale(widget, 0.0, 100.0, 1.0, opts->hardness * 100.0);
                g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_tool_hardness_changed), NULL);
                g_signal_connect(widget, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
            }
        }
    } else {
        /* For tools without options (Move, Fill, etc.), hide main panel container */
        if (panel->panel) {
            gtk_widget_set_visible(panel->panel, FALSE);
        }
        /* Clear widget references */
        panel->title_label = NULL;
        panel->size_scale = NULL;
        panel->opacity_scale = NULL;
        panel->hardness_scale = NULL;
    }

    panel->current_tool_type = new_tool_type;
}

/**
 * Update tool options panel visibility based on tool capabilities
 */
void tool_options_panel_update_visibility(ToolOptionsPanel *panel, ToolOptionFlags options)
{
    if (!panel) {
        return;
    }

    /* Show/hide size slider based on TOOL_OPT_SIZE flag */
    if (panel->size_scale) {
        GtkWidget *size_box = gtk_widget_get_parent(panel->size_scale);
        if (size_box) {
            if (options & TOOL_OPT_SIZE) {
                gtk_widget_show(size_box);
            } else {
                gtk_widget_hide(size_box);
            }
        }
    }

    /* Show/hide opacity slider based on TOOL_OPT_OPACITY flag */
    if (panel->opacity_scale) {
        GtkWidget *opacity_box = gtk_widget_get_parent(panel->opacity_scale);
        if (opacity_box) {
            if (options & TOOL_OPT_OPACITY) {
                gtk_widget_show(opacity_box);
            } else {
                gtk_widget_hide(opacity_box);
            }
        }
    }

    /* Show/hide hardness slider based on TOOL_OPT_HARDNESS flag */
    if (panel->hardness_scale) {
        GtkWidget *hardness_box = gtk_widget_get_parent(panel->hardness_scale);
        if (hardness_box) {
            if (options & TOOL_OPT_HARDNESS) {
                gtk_widget_show(hardness_box);
            } else {
                gtk_widget_hide(hardness_box);
            }
        }
    }
}

/**
 * Free a tool options panel
 */
void tool_options_panel_free(ToolOptionsPanel *panel)
{
    if (panel) {
        g_free(panel);
    }
}

