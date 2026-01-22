#ifndef OVERVIEW_PANEL_H
#define OVERVIEW_PANEL_H

#include "ui/layers_panel.h"
#include <gtk/gtk.h>

/**
 * Create the overview panel widget
 * @param layers_panel The layers panel (needed to access current document)
 * @return GtkWidget* The overview panel widget
 */
GtkWidget* overview_panel_create(LayersPanel* layers_panel);

/**
 * Update the overview panel (queue redraw)
 * @param widget The overview widget
 */
void overview_panel_update(GtkWidget* widget);

#endif /* OVERVIEW_PANEL_H */
