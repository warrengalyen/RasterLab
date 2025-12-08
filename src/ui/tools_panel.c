#include "ui/tools_panel.h"
#include "tool_options.h"
#include <stdio.h>

/**
 * Global reference to tool options panel for callbacks
 * This is set by create_tools_panel and used to update title
 */
static ToolOptionsPanel *g_tool_options_panel = NULL;

/**
 * Global references to color buttons for swap functionality
 */
static GtkWidget *g_fg_color_button = NULL;
static GtkWidget *g_bg_color_button = NULL;

/**
 * Swap foreground and background colors
 */
static void on_swap_colors_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;   /* Unused */
    (void)user_data; /* Unused */
    
    if (!g_fg_color_button || !g_bg_color_button) {
        return;
    }
    
    /* Get current colors */
    GdkRGBA fg_rgba, bg_rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_fg_color_button), &fg_rgba);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_bg_color_button), &bg_rgba);
    
    /* Swap them */
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_fg_color_button), &bg_rgba);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_bg_color_button), &fg_rgba);
}

/**
 * Global references to tool buttons for toggle state management
 */
static GtkWidget *g_tool_buttons[TOOL_COUNT] = {NULL};

/**
 * Flag to prevent recursive signal handling
 */
static gboolean g_updating_tools = FALSE;

static void on_tool_button_clicked(GtkToggleButton *button, gpointer user_data)
{
    ToolType tool_type = GPOINTER_TO_INT(user_data);
    ToolRegistry *registry = (ToolRegistry *)g_object_get_data(G_OBJECT(button), 
                                                               "tool_registry");

    /* Prevent recursion */
    if (g_updating_tools) {
        return;
    }

    if (registry) {
        if (tool_manager_activate(registry, tool_type)) {
            //printf("Tool %d activated\n", tool_type);
            
            /* Set flag to prevent recursive calls */
            g_updating_tools = TRUE;
            
            /* Update toggle button states - only active tool is pressed */
            for (int i = 0; i < TOOL_COUNT; i++) {
                if (g_tool_buttons[i]) {
                    /* Set button active only if it's the selected tool */
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_tool_buttons[i]), 
                                                (i == tool_type));
                }
            }
            
            /* Clear recursion flag */
            g_updating_tools = FALSE;
            
            /* Update tool options panel title and visibility */
            Tool *active_tool = tool_manager_get_active(registry);
            if (active_tool && g_tool_options_panel) {
                tool_options_panel_update_title(g_tool_options_panel, active_tool->name);
                tool_options_panel_update_visibility(g_tool_options_panel, active_tool->options);
            }
        }
    }
}

/**
 * Set the tool options panel reference for callbacks
 */
void tools_panel_set_options_panel(ToolOptionsPanel *panel)
{
    g_tool_options_panel = panel;
}

/**
 * Get the current foreground color from the color picker
 */
gboolean tools_panel_get_foreground_color(GdkRGBA *rgba)
{
    if (!g_fg_color_button || !rgba) {
        return FALSE;
    }
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_fg_color_button), rgba);
    return TRUE;
}

/**
 * Create the tools panel (loads from Glade file)
 */
GtkWidget* create_tools_panel(ToolRegistry *tool_registry)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *panel;
    
    /* Load the Glade file from resources */
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/tool_panel.glade", &error)) {
        g_warning("Failed to load tool_panel.glade: %s", error->message);
        g_error_free(error);
        g_object_unref(builder);
        
        /* Fallback: create empty panel */
        return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }
    
    /* Get the main panel from the builder */
    panel = GTK_WIDGET(gtk_builder_get_object(builder, "tool_panel"));
    if (!panel) {
        g_warning("Failed to get tool_panel object from builder");
        g_object_unref(builder);
        return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }
    
    /* Keep builder alive by storing it on the panel as object data */
    g_object_set_data_full(G_OBJECT(panel), "builder", builder, g_object_unref);
    
    /* Enforce 48px width constraint */
    gtk_widget_set_size_request(panel, 48, -1);
    gtk_widget_set_hexpand(panel, FALSE);
    gtk_widget_set_vexpand(panel, TRUE);
    
    /* Tool button IDs and types */
    const gchar *button_ids[] = {
        "tool_button_move",
        "tool_button_brush",
        "tool_button_eraser",
        "tool_button_fill",
    };
    
    const ToolType tool_types[] = {
        TOOL_MOVE,
        TOOL_BRUSH,
        TOOL_ERASER,
        TOOL_FILL,
    };
    
    /* Set up tool buttons with callbacks */
    for (int i = 0; i < TOOL_COUNT; i++) {
        GtkWidget *tool_button = GTK_WIDGET(gtk_builder_get_object(builder, button_ids[i]));
        if (tool_button) {
            /* Store tool registry and type in button */
            g_object_set_data(G_OBJECT(tool_button), "tool_registry", tool_registry);
            
            /* Store button reference for toggle state management */
            g_tool_buttons[i] = tool_button;
            
            /* Connect button click to tool activation */
            g_signal_connect(tool_button, "toggled",
                            G_CALLBACK(on_tool_button_clicked),
                            GINT_TO_POINTER(tool_types[i]));
        } else {
            g_warning("Failed to get tool button %d from builder", i);
        }
    }
    
    /* Set up color buttons */
    GtkWidget *fg_color = GTK_WIDGET(gtk_builder_get_object(builder, "fg_color_button"));
    GtkWidget *bg_color = GTK_WIDGET(gtk_builder_get_object(builder, "bg_color_button"));
    GtkWidget *swap_button = GTK_WIDGET(gtk_builder_get_object(builder, "swap_button"));
    
    if (fg_color) {
        GdkRGBA fg_rgba = {0.0, 0.0, 0.0, 1.0};  /* Black */
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(fg_color), &fg_rgba);
        g_fg_color_button = fg_color;  /* Store global reference */
    } else {
        g_warning("Failed to get foreground color button from builder");
    }
    
    if (bg_color) {
        GdkRGBA bg_rgba = {1.0, 1.0, 1.0, 1.0};  /* White */
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(bg_color), &bg_rgba);
        g_bg_color_button = bg_color;  /* Store global reference */
    } else {
        g_warning("Failed to get background color button from builder");
    }
    
    /* Connect swap button */
    if (swap_button) {
        g_signal_connect(swap_button, "clicked",
                        G_CALLBACK(on_swap_colors_clicked), NULL);
    } else {
        g_warning("Failed to get swap button from builder");
    }
    
    gtk_widget_show_all(panel);
    
    return panel;
}

