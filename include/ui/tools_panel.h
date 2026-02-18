#ifndef TOOLS_PANEL_H
#define TOOLS_PANEL_H

#include "tool_manager.h"
#include "tools.h"
#include "ui/tool_options_panel.h"
#include <gtk/gtk.h>

/**
 * Initialize tools panel from an existing builder (used when panel is in main window)
 * @param builder The GtkBuilder containing the tool panel
 * @param tool_registry The tool registry for tool selection callbacks
 * @return The tools panel widget
 */
GtkWidget* tools_panel_initialize_from_builder(GtkBuilder* builder, ToolRegistry* tool_registry);

/**
 * Set the tool options panel reference for tool selection callbacks
 * @param panel The tool options panel to update when tools are selected
 */
void tools_panel_set_options_panel(ToolOptionsPanel* panel);

/**
 * Set the main window reference for dialog parent
 * @param window The main window to use as parent for dialogs
 */
void tools_panel_set_main_window(GtkWindow* window);

/**
 * Get the current foreground color from the color picker
 * @param rgba Pointer to GdkRGBA to fill with foreground color
 * @return TRUE if color was retrieved, FALSE if color button not available
 */
gboolean tools_panel_get_foreground_color(GdkRGBA* rgba);

/**
 * Set the foreground color programmatically
 * @param color The color to set as foreground color
 * @return TRUE if color was set, FALSE if color button not available
 */
gboolean tools_panel_set_foreground_color(GdkRGBA* color);

/**
 * Get the current background color from the color picker
 * @param rgba Pointer to GdkRGBA to fill with background color
 * @return TRUE if color was retrieved, FALSE if color button not available
 */
gboolean tools_panel_get_background_color(GdkRGBA* rgba);

/**
 * Window key press handler for tool hotkeys
 * @param widget The widget that received the key press
 * @param event The key press event
 * @param user_data AppContext pointer
 * @return TRUE if the event was handled, FALSE otherwise
 */
gboolean tools_panel_on_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data);

#endif /* TOOLS_PANEL_H */
