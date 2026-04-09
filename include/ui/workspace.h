/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "ui/layers_panel.h"
#include "ui/widgets/accordion.h"
#include <gtk/gtk.h>

/* Forward declaration - AppContext is defined in ui.h */
struct _AppContext;
typedef struct _AppContext AppContext;

/**
 * Workspace structure - manages the accordion widget and panel sections
 */
typedef struct {
    Accordion* accordion;       /* Accordion widget for sections */
    GtkWidget* overview_widget; /* Overview panel widget */
    GtkWidget* swatches_panel;  /* Swatches panel widget */
    LayersPanel* layers_panel;  /* Layers panel reference */
} Workspace;

/**
 * Create the workspace with accordion and panel sections
 * @param ctx Application context (needed for swatches panel)
 * @return Workspace structure, or NULL on error
 */
Workspace* workspace_create(AppContext* ctx);

/**
 * Free the workspace and all its panels
 * @param workspace The workspace to free
 */
void workspace_free(Workspace* workspace);

/**
 * Get the layers panel from workspace
 * @param workspace The workspace
 * @return The layers panel, or NULL if not available
 */
LayersPanel* workspace_get_layers_panel(Workspace* workspace);

/**
 * Update the overview panel
 * @param workspace The workspace
 */
void workspace_update_overview(Workspace* workspace);

/**
 * Get the overview widget from workspace
 * @param workspace The workspace
 * @return The overview widget, or NULL if not available
 */
GtkWidget* workspace_get_overview_widget(Workspace* workspace);

/**
 * Get the main panel widget (accordion container) from workspace
 * @param workspace The workspace
 * @return The panel widget, or NULL if not available
 */
GtkWidget* workspace_get_panel(Workspace* workspace);

#endif /* WORKSPACE_H */
