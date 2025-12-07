#ifndef PANELS_H
#define PANELS_H

#include <gtk/gtk.h>
#include "document.h"

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
 * Layers panel structure
 */
typedef struct {
    GtkWidget *panel;            /* Main panel widget */
    GtkWidget *tree_view;        /* Layer list tree view */
    GtkListStore *store;         /* List store for layers */
    ImageDocument *current_doc;  /* Current document reference */
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
 * @return The tools panel widget
 */
GtkWidget* create_tools_panel(void);

/**
 * Create the tool options panel (placeholder)
 * @return The tool options panel widget
 */
GtkWidget* create_tool_options_panel(void);

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
 * Free a layers panel
 * @param layers_panel The layers panel to free
 */
void layers_panel_free(LayersPanel *layers_panel);

#endif /* PANELS_H */

