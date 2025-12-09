#ifndef TOOL_OPTIONS_PANEL_H
#define TOOL_OPTIONS_PANEL_H

#include <gtk/gtk.h>
#include "tools.h"

/**
 * Tool options panel structure
 */
typedef struct {
    GtkWidget *panel;            /* Main panel container */
    GtkWidget *brush_panel;      /* Brush tool options panel (from Glade) */
    GtkWidget *eraser_panel;     /* Eraser tool options panel (from Glade) */
    GtkWidget *title_label;      /* Title showing tool name (current panel) */
    GtkWidget *size_scale;       /* Size slider (current panel) */
    GtkWidget *opacity_scale;    /* Opacity slider (current panel) */
    GtkWidget *hardness_scale;   /* Hardness slider (current panel) */
    ToolType current_tool_type;  /* Currently displayed tool type */
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
void tool_options_panel_switch_tool(ToolOptionsPanel *panel, const gchar *tool_name);

/**
 * Update tool options panel visibility based on tool capabilities
 * @param panel The tool options panel
 * @param options The tool option flags
 */
void tool_options_panel_update_visibility(ToolOptionsPanel *panel, ToolOptionFlags options);

/**
 * Free a tool options panel
 * @param panel The tool options panel to free
 */
void tool_options_panel_free(ToolOptionsPanel *panel);

#endif /* TOOL_OPTIONS_PANEL_H */

