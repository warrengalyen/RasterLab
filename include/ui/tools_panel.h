#ifndef TOOLS_PANEL_H
#define TOOLS_PANEL_H

#include <gtk/gtk.h>
#include "tools.h"
#include "tool_manager.h"
#include "ui/tool_options_panel.h"

/**
 * Create the tools panel (icon list)
 * @param tool_registry The tool registry for tool selection callbacks
 * @return The tools panel widget
 */
GtkWidget* create_tools_panel(ToolRegistry *tool_registry);

/**
 * Set the tool options panel reference for tool selection callbacks
 * @param panel The tool options panel to update when tools are selected
 */
void tools_panel_set_options_panel(ToolOptionsPanel *panel);

/**
 * Get the current foreground color from the color picker
 * @param rgba Pointer to GdkRGBA to fill with foreground color
 * @return TRUE if color was retrieved, FALSE if color button not available
 */
gboolean tools_panel_get_foreground_color(GdkRGBA *rgba);

#endif /* TOOLS_PANEL_H */

