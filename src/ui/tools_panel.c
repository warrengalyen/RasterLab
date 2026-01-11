#include "ui/tools_panel.h"
#include "tool_options.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"

#include <stdio.h>

/**
 * Global reference to tool options panel for callbacks
 * This is set by tools_panel_set_options_panel and used to update title
 */
static ToolOptionsPanel* g_tool_options_panel = NULL;

/**
 * Global references to color buttons for swap functionality
 */
static GtkWidget* g_fg_color_button = NULL;
static GtkWidget* g_bg_color_button = NULL;

/**
 * Global reference to the main window for dialog parent
 */
static GtkWindow* g_main_window = NULL;

/**
 * Current foreground and background colors (RGB, 0-1 range)
 */
static GdkRGBA g_fg_color = {0.0, 0.0, 0.0, 1.0}; /* Black */
static GdkRGBA g_bg_color = {1.0, 1.0, 1.0, 1.0}; /* White */

/**
 * Swap foreground and background colors
 */
static void on_swap_colors_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Unused */

    if (!g_fg_color_button || !g_bg_color_button) {
        return;
    }

    /* Swap the stored colors */
    GdkRGBA temp = g_fg_color;
    g_fg_color = g_bg_color;
    g_bg_color = temp;

    /* Update button appearances */
    update_color_button_appearance(g_fg_color_button, &g_fg_color);
    update_color_button_appearance(g_bg_color_button, &g_bg_color);
}

/**
 * Color update callback for real-time color updates from dialog
 */
static void on_fg_color_update(double r, double g, double b, gpointer user_data) {
    (void)user_data; /* Unused */

    g_fg_color.red = r;
    g_fg_color.green = g;
    g_fg_color.blue = b;
    g_fg_color.alpha = 1.0;

    update_color_button_appearance(g_fg_color_button, &g_fg_color);
}

static void on_bg_color_update(double r, double g, double b, gpointer user_data) {
    (void)user_data; /* Unused */

    g_bg_color.red = r;
    g_bg_color.green = g;
    g_bg_color.blue = b;
    g_bg_color.alpha = 1.0;

    update_color_button_appearance(g_bg_color_button, &g_bg_color);
}

/**
 * Open custom color chooser dialog for foreground color
 */
static void on_fg_color_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Unused */

    if (!g_main_window) {
        g_warning("Main window not set, cannot show color chooser dialog");
        return;
    }

    // Create and show color chooser dialog with real-time updates
    GtkWidget* dialog = color_chooser_dialog_new(
        g_main_window,
        "Choose Foreground Color",
        &g_fg_color,
        on_fg_color_update,
        NULL,
        TRUE); // Enable real-time updates for tools panel

    // Run dialog
    gtk_dialog_run(GTK_DIALOG(dialog));

    // Get final color
    double r, g, b;
    color_chooser_dialog_get_color(dialog, &r, &g, &b);

    g_fg_color.red = r;
    g_fg_color.green = g;
    g_fg_color.blue = b;
    g_fg_color.alpha = 1.0;

    update_color_button_appearance(g_fg_color_button, &g_fg_color);

    gtk_widget_destroy(dialog);
}

/**
 * Open custom color chooser dialog for background color
 */
static void on_bg_color_clicked(GtkButton* button, gpointer user_data) {
    (void)button;    /* Unused */
    (void)user_data; /* Unused */

    if (!g_main_window) {
        g_warning("Main window not set, cannot show color chooser dialog");
        return;
    }

    // Create and show color chooser dialog with real-time updates
    GtkWidget* dialog = color_chooser_dialog_new(
        g_main_window,
        "Choose Background Color",
        &g_bg_color,
        on_bg_color_update,
        NULL,
        TRUE); // Enable real-time updates for tools panel

    // Run dialog
    gtk_dialog_run(GTK_DIALOG(dialog));

    // Get final color
    double r, g, b;
    color_chooser_dialog_get_color(dialog, &r, &g, &b);

    g_bg_color.red = r;
    g_bg_color.green = g;
    g_bg_color.blue = b;
    g_bg_color.alpha = 1.0;

    update_color_button_appearance(g_bg_color_button, &g_bg_color);

    gtk_widget_destroy(dialog);
}

/**
 * Global references to tool buttons for toggle state management
 */
static GtkWidget* g_tool_buttons[TOOL_COUNT] = {NULL};

/**
 * Flag to prevent recursive signal handling
 */
static gboolean g_updating_tools = FALSE;

static void on_tool_button_clicked(GtkToggleButton* button, gpointer user_data) {
    ToolType tool_type = GPOINTER_TO_INT(user_data);
    ToolRegistry* registry = (ToolRegistry*)g_object_get_data(G_OBJECT(button),
                                                              "tool_registry");

    /* Prevent recursion */
    if (g_updating_tools) {
        return;
    }

    if (registry) {
        if (tool_manager_activate(registry, tool_type)) {
            // printf("Tool %d activated\n", tool_type);

            /* Set flag to prevent recursive calls */
            g_updating_tools = TRUE;

            /* Update toggle button states - only active tool is pressed */
            for (int i = 0; i < TOOL_COUNT; i++) {
                if (g_tool_buttons[i]) {
                    /* Get tool type from button's object data */
                    ToolType button_tool_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(g_tool_buttons[i]), "tool_type"));

                    /* Block signals to prevent recursion */
                    g_signal_handlers_block_by_func(g_tool_buttons[i],
                                                    G_CALLBACK(on_tool_button_clicked),
                                                    GINT_TO_POINTER(button_tool_type));

                    /* Set button active only if it's the selected tool */
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_tool_buttons[i]),
                                                 (button_tool_type == tool_type));

                    /* Unblock signals */
                    g_signal_handlers_unblock_by_func(g_tool_buttons[i],
                                                      G_CALLBACK(on_tool_button_clicked),
                                                      GINT_TO_POINTER(button_tool_type));
                }
            }

            /* Clear recursion flag */
            g_updating_tools = FALSE;

            /* Update tool options panel */
            Tool* active_tool = tool_manager_get_active(registry);
            if (active_tool && g_tool_options_panel) {
                tool_options_panel_switch_tool(g_tool_options_panel, active_tool->name);
            }
        }
    }
}

/**
 * Set the tool options panel reference for callbacks
 */
void tools_panel_set_options_panel(ToolOptionsPanel* panel) {
    g_tool_options_panel = panel;
}

/**
 * Set the main window reference for dialog parent
 */
void tools_panel_set_main_window(GtkWindow* window) {
    g_main_window = window;
}

/**
 * Get the current foreground color from the color picker
 */
gboolean tools_panel_get_foreground_color(GdkRGBA* rgba) {
    if (!rgba) {
        return FALSE;
    }
    *rgba = g_fg_color;
    return TRUE;
}

/**
 * Initialize tools panel from an existing builder (used when panel is in main window)
 */
GtkWidget* tools_panel_initialize_from_builder(GtkBuilder* builder, ToolRegistry* tool_registry) {
    GtkWidget* panel;
    GError* error = NULL;

    if (!builder) {
        g_warning("Invalid builder for tools panel initialization");
        return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }

    /* Get the main panel from the builder */
    panel = GTK_WIDGET(gtk_builder_get_object(builder, "tool_panel"));
    if (!panel) {
        g_warning("Failed to get tool_panel object from builder. Make sure main_window.glade includes the tool_panel with id='tool_panel'");
        return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }

    /* Tool button IDs and types - MUST match glade file order */
    const gchar* button_ids[] = {
        "tool_button_hand",
        "tool_button_zoom",
        "tool_button_move",
        "tool_button_brush",
        "tool_button_eraser",
        "tool_button_fill",
        "tool_button_rect_select",
    };

    const ToolType tool_types[] = {
        TOOL_HAND,
        TOOL_ZOOM,
        TOOL_MOVE,
        TOOL_BRUSH,
        TOOL_ERASER,
        TOOL_PAINT_BUCKET,
        TOOL_RECT_SELECT,
    };

    /* Set up tool buttons with callbacks */
    for (int i = 0; i < TOOL_COUNT; i++) {
        GtkWidget* tool_button = GTK_WIDGET(gtk_builder_get_object(builder, button_ids[i]));
        if (tool_button) {
            /* Remove inner padding and margins using CSS */
            GtkCssProvider* css = gtk_css_provider_new();
            gtk_css_provider_load_from_data(css,
                                            "button { padding: 0px; } ",
                                            -1, NULL);
            GtkStyleContext* context = gtk_widget_get_style_context(tool_button);
            gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(css);

            /* Remove margins */
            gtk_widget_set_margin_start(tool_button, 0);
            gtk_widget_set_margin_end(tool_button, 0);
            gtk_widget_set_margin_top(tool_button, 0);
            gtk_widget_set_margin_bottom(tool_button, 0);

            /* Resize icon to 24x24 by loading resource, scaling, and replacing */
            const gchar* icon_resources[] = {
                "/icons/tool-hand.png",
                "/icons/tool-zoom.png",
                "/icons/tool-move.png",
                "/icons/tool-paintbrush.png",
                "/icons/tool-eraser.png",
                "/icons/tool-paintbucket.png",
                "/icons/tool-rect-select.png",
            };
            if (i < 7) {
                GError* error = NULL;
                GdkPixbuf* pixbuf = gdk_pixbuf_new_from_resource(icon_resources[i], &error);
                if (pixbuf) {
                    /* Scale to 24x24 */
                    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 24, 24, GDK_INTERP_BILINEAR);
                    g_object_unref(pixbuf);

                    if (scaled) {
                        /* Create new image from scaled pixbuf */
                        GtkWidget* new_image = gtk_image_new_from_pixbuf(scaled);
                        g_object_unref(scaled);

                        /* Replace the existing image */
                        GtkWidget* old_image = gtk_bin_get_child(GTK_BIN(tool_button));
                        if (old_image) {
                            gtk_container_remove(GTK_CONTAINER(tool_button), old_image);
                        }
                        gtk_container_add(GTK_CONTAINER(tool_button), new_image);
                    }
                } else if (error) {
                    g_warning("Failed to load icon %s: %s", icon_resources[i], error->message);
                    g_error_free(error);
                }
            }

            /* Store tool registry and type in button */
            g_object_set_data(G_OBJECT(tool_button), "tool_registry", tool_registry);
            g_object_set_data(G_OBJECT(tool_button), "tool_type", GINT_TO_POINTER(tool_types[i]));

            /* Store button reference for toggle state management (indexed by tool type) */
            g_tool_buttons[tool_types[i]] = tool_button;

            /* Connect button click to tool activation */
            g_signal_connect(tool_button, "toggled",
                             G_CALLBACK(on_tool_button_clicked),
                             GINT_TO_POINTER(tool_types[i]));
        } else {
            g_warning("Failed to get tool button %d from builder", i);
        }
    }

    /* Set up color buttons */
    GtkWidget* fg_color = GTK_WIDGET(gtk_builder_get_object(builder, "fg_color_button"));
    GtkWidget* bg_color = GTK_WIDGET(gtk_builder_get_object(builder, "bg_color_button"));
    GtkWidget* swap_button = GTK_WIDGET(gtk_builder_get_object(builder, "swap_button"));

    if (fg_color) {
        g_fg_color_button = fg_color; /* Store global reference */

        /* Set initial color appearance */
        update_color_button_appearance(fg_color, &g_fg_color);

        /* Connect to clicked signal to show our custom dialog */
        g_signal_connect(fg_color, "clicked", G_CALLBACK(on_fg_color_clicked), NULL);
    } else {
        g_warning("Failed to get foreground color button from builder");
    }

    if (bg_color) {
        g_bg_color_button = bg_color; /* Store global reference */

        /* Set initial color appearance */
        update_color_button_appearance(bg_color, &g_bg_color);

        /* Connect to clicked signal to show our custom dialog */
        g_signal_connect(bg_color, "clicked", G_CALLBACK(on_bg_color_clicked), NULL);
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
