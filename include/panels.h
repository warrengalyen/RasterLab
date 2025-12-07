#ifndef PANELS_H
#define PANELS_H

#include <gtk/gtk.h>
#include "document.h"
#include "tools.h"
#include "tool_manager.h"

/**
 * Panel header structure for collapsible panels
 */
typedef struct {
    GtkWidget *container;        /* Main container */
    GtkWidget *header_box;       /* Header with title and buttons */
    GtkWidget *content;          /* Content area */
    gboolean is_collapsed;       /* Collapsed state */
} PanelHeader;

/**
 * Tool options panel structure
 */
typedef struct {
    GtkWidget *panel;            /* Main panel container */
    GtkWidget *title_label;      /* Title showing tool name */
    GtkWidget *size_scale;       /* Size slider */
    GtkWidget *opacity_scale;    /* Opacity slider */
    GtkWidget *hardness_scale;   /* Hardness slider */
} ToolOptionsPanel;

/**
 * Layers panel structure
 */
typedef struct {
    GtkWidget *panel;            /* Main panel widget */
    GtkWidget *tree_view;        /* Layer list tree view */
    GtkListStore *store;         /* List store for layers */
    ImageDocument *current_doc;  /* Current document reference */
    GtkWidget *btn_new;          /* New layer button */
    GtkWidget *btn_delete;       /* Delete layer button */
    GtkWidget *btn_duplicate;    /* Duplicate layer button */
    gpointer app_context;        /* Reference to AppContext for callbacks */
} LayersPanel;

/**
 * Create a collapsible panel header
 * @param title Panel title
 * @param content The content widget
 * @return PanelHeader structure
 */
PanelHeader* panel_header_new(const gchar *title, GtkWidget *content);

/**
 * Toggle panel collapse state
 * @param header The panel header
 */
void panel_header_toggle_collapse(PanelHeader *header);

/**
 * Free a panel header
 * @param header The panel header to free
 */
void panel_header_free(PanelHeader *header);

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

/**
 * Create the tool options panel
 * @return ToolOptionsPanel structure
 */
ToolOptionsPanel* create_tool_options_panel(void);

/**
 * Update tool options panel title and visibility
 * @param panel The tool options panel
 * @param tool_name The name of the currently selected tool
 */
void tool_options_panel_update_title(ToolOptionsPanel *panel, const gchar *tool_name);

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

/**
 * Create the layers panel with tree view
 * @return LayersPanel structure
 */
LayersPanel* create_layers_panel(void);

/**
 * Update layers panel with document layers
 * @param layers_panel The layers panel
 * @param doc The document to display
 */
void layers_panel_update(LayersPanel *layers_panel, ImageDocument *doc);

/**
 * Get the currently selected layer from the panel
 * @param layers_panel The layers panel
 * @return The selected layer, or NULL if none selected
 */
ImageLayer* layers_panel_get_selected_layer(LayersPanel *layers_panel);

/**
 * Update button sensitivity based on state
 * @param layers_panel The layers panel
 * @param has_document Whether a document is open
 * @param has_selection Whether a layer is selected
 */
void layers_panel_update_button_sensitivity(LayersPanel *layers_panel,
                                            gboolean has_document,
                                            gboolean has_selection);

/**
 * Connect layers panel buttons to UI callbacks
 * @param layers_panel The layers panel
 * @param new_callback Callback for New button
 * @param delete_callback Callback for Delete button
 * @param duplicate_callback Callback for Duplicate button
 * @param user_data User data to pass to callbacks
 */
void layers_panel_connect_buttons(LayersPanel *layers_panel,
                                  GCallback new_callback,
                                  GCallback delete_callback,
                                  GCallback duplicate_callback,
                                  gpointer user_data);

/**
 * Free a layers panel
 * @param layers_panel The layers panel to free
 */
void layers_panel_free(LayersPanel *layers_panel);

#endif /* PANELS_H */

