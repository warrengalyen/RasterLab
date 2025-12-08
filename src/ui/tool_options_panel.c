#include "ui/tool_options_panel.h"
#include "tool_options.h"
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

ToolOptionsPanel* create_tool_options_panel(void)
{
    ToolOptionsPanel *tool_opts_panel = (ToolOptionsPanel *)g_malloc(sizeof(ToolOptionsPanel));
    ToolOptions *opts = tool_options_get_global();

    /* Main horizontal container */
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_top(panel, 8);
    gtk_widget_set_margin_start(panel, 10);
    gtk_widget_set_margin_end(panel, 10);
    gtk_widget_set_margin_bottom(panel, 8);

    /* Title label showing tool name */
    GtkWidget *title_label = gtk_label_new("Tool Options");
    gtk_label_set_markup(GTK_LABEL(title_label), "<b>Brush</b>");
    gtk_widget_set_size_request(title_label, 100, -1);
    gtk_box_pack_start(GTK_BOX(panel), title_label, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(panel), sep1, FALSE, FALSE, 5);

    /* Size section */
    GtkWidget *size_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *size_label = gtk_label_new("size");
    gtk_label_set_xalign(GTK_LABEL(size_label), 0.0);
    gtk_widget_set_tooltip_text(size_label, "Brush size in pixels");
    gtk_box_pack_start(GTK_BOX(size_box), size_label, FALSE, FALSE, 0);

    GtkWidget *size_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(size_scale), opts->size);
    gtk_scale_set_draw_value(GTK_SCALE(size_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(size_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(size_scale, 80, -1);
    gtk_scale_set_digits(GTK_SCALE(size_scale), 1);
    g_signal_connect(size_scale, "value-changed", G_CALLBACK(on_tool_size_changed), NULL);
    gtk_box_pack_start(GTK_BOX(size_box), size_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), size_box, FALSE, FALSE, 0);

    /* Opacity section */
    GtkWidget *opacity_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *opacity_label = gtk_label_new("opacity");
    gtk_label_set_xalign(GTK_LABEL(opacity_label), 0.0);
    gtk_widget_set_tooltip_text(opacity_label, "Tool opacity (0-100%)");
    gtk_box_pack_start(GTK_BOX(opacity_box), opacity_label, FALSE, FALSE, 0);

    GtkWidget *opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(opacity_scale), opts->opacity * 100.0f);
    gtk_scale_set_draw_value(GTK_SCALE(opacity_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(opacity_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(opacity_scale, 80, -1);
    gtk_scale_set_digits(GTK_SCALE(opacity_scale), 0);
    g_signal_connect(opacity_scale, "value-changed", G_CALLBACK(on_tool_opacity_changed), NULL);
    gtk_box_pack_start(GTK_BOX(opacity_box), opacity_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), opacity_box, FALSE, FALSE, 0);

    /* Hardness section */
    GtkWidget *hardness_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *hardness_label = gtk_label_new("hardness");
    gtk_label_set_xalign(GTK_LABEL(hardness_label), 0.0);
    gtk_widget_set_tooltip_text(hardness_label, "Brush hardness (0=soft, 100=hard)");
    gtk_box_pack_start(GTK_BOX(hardness_box), hardness_label, FALSE, FALSE, 0);

    GtkWidget *hardness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(hardness_scale), opts->hardness * 100.0f);
    gtk_scale_set_draw_value(GTK_SCALE(hardness_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(hardness_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(hardness_scale, 80, -1);
    gtk_scale_set_digits(GTK_SCALE(hardness_scale), 0);
    g_signal_connect(hardness_scale, "value-changed", G_CALLBACK(on_tool_hardness_changed), NULL);
    gtk_box_pack_start(GTK_BOX(hardness_box), hardness_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), hardness_box, FALSE, FALSE, 0);

    /* Store references */
    tool_opts_panel->panel = panel;
    tool_opts_panel->title_label = title_label;
    tool_opts_panel->size_scale = size_scale;
    tool_opts_panel->opacity_scale = opacity_scale;
    tool_opts_panel->hardness_scale = hardness_scale;

    gtk_widget_show_all(panel);

    return tool_opts_panel;
}

/**
 * Update tool options panel title
 */
void tool_options_panel_update_title(ToolOptionsPanel *panel, const gchar *tool_name)
{
    if (!panel || !tool_name) {
        return;
    }

    gchar *markup = g_strdup_printf("<b>%s</b>", tool_name);
    gtk_label_set_markup(GTK_LABEL(panel->title_label), markup);
    g_free(markup);
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
    if (options & TOOL_OPT_SIZE) {
        gtk_widget_show(gtk_widget_get_parent(panel->size_scale));
    } else {
        gtk_widget_hide(gtk_widget_get_parent(panel->size_scale));
    }

    /* Show/hide opacity slider based on TOOL_OPT_OPACITY flag */
    if (options & TOOL_OPT_OPACITY) {
        gtk_widget_show(gtk_widget_get_parent(panel->opacity_scale));
    } else {
        gtk_widget_hide(gtk_widget_get_parent(panel->opacity_scale));
    }

    /* Show/hide hardness slider based on TOOL_OPT_HARDNESS flag */
    if (options & TOOL_OPT_HARDNESS) {
        gtk_widget_show(gtk_widget_get_parent(panel->hardness_scale));
    } else {
        gtk_widget_hide(gtk_widget_get_parent(panel->hardness_scale));
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

