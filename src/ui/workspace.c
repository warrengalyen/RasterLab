/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "i18n.h"
#include "ui/workspace.h"
#include "ui/layers_panel.h"
#include "ui/overview_panel.h"
#include "ui/swatches_panel.h"
#include "ui/widgets/accordion.h"
#include <glib.h>

/**
 * Create the workspace with accordion and panel sections
 */
Workspace* workspace_create(AppContext* ctx) {
    Workspace* workspace = (Workspace*)g_malloc(sizeof(Workspace));
    if (!workspace) {
        return NULL;
    }

    /* Initialize all fields to NULL */
    workspace->accordion = NULL;
    workspace->overview_widget = NULL;
    workspace->swatches_panel = NULL;
    workspace->layers_panel = NULL;

    /* Create accordion widget - this becomes the main panel container */
    workspace->accordion = accordion_new();
    if (!workspace->accordion) {
        g_free(workspace);
        return NULL;
    }

    /* Create layers panel first (needed for overview) */
    workspace->layers_panel = create_layers_panel(ctx);
    if (!workspace->layers_panel) {
        accordion_free(workspace->accordion);
        g_free(workspace);
        return NULL;
    }

    /* Get the panel widget from layers panel */
    GtkWidget* layers_panel_widget = layers_panel_get_panel(workspace->layers_panel);
    if (!layers_panel_widget) {
        layers_panel_free(workspace->layers_panel);
        accordion_free(workspace->accordion);
        g_free(workspace);
        return NULL;
    }

    /* Create overview widget for composite thumbnail */
    workspace->overview_widget = overview_panel_create(workspace->layers_panel);

    /* Create swatches panel */
    workspace->swatches_panel = swatches_panel_create(ctx);

    /* Ensure layers panel content expands vertically */
    gtk_widget_set_vexpand(layers_panel_widget, TRUE);
    gtk_widget_set_hexpand(layers_panel_widget, TRUE);

    /* Show the layers panel widget */
    gtk_widget_show_all(layers_panel_widget);

    /* Add overview section first, then swatches section, then layers section */
    if (workspace->overview_widget) {
        accordion_add_section(workspace->accordion, _("Overview"), workspace->overview_widget);
    }
    if (workspace->swatches_panel) {
        accordion_add_section(workspace->accordion, _("Swatches"), workspace->swatches_panel);
    }
    accordion_add_section(workspace->accordion, _("Layers"), layers_panel_widget);

    /* Make accordion expand to fill available vertical space */
    GtkWidget* panel = accordion_get_widget(workspace->accordion);
    gtk_widget_set_vexpand(panel, TRUE);
    gtk_widget_set_hexpand(panel, TRUE);

    gtk_widget_show_all(panel);

    return workspace;
}

/**
 * Free the workspace and all its panels
 */
void workspace_free(Workspace* workspace) {
    if (!workspace) {
        return;
    }

    /* Cleanup swatches panel static references before widgets are destroyed */
    swatches_panel_cleanup();

    /* Free layers panel */
    if (workspace->layers_panel) {
        layers_panel_free(workspace->layers_panel);
    }

    /* Free accordion (this will destroy all its sections) */
    if (workspace->accordion) {
        accordion_free(workspace->accordion);
    }

    g_free(workspace);
}

/**
 * Get the layers panel from workspace
 */
LayersPanel* workspace_get_layers_panel(Workspace* workspace) {
    if (!workspace) {
        return NULL;
    }
    return workspace->layers_panel;
}

/**
 * Update the overview panel
 */
void workspace_update_overview(Workspace* workspace) {
    if (!workspace || !workspace->overview_widget) {
        return;
    }
    overview_panel_update(workspace->overview_widget);
}

/**
 * Get the overview widget from workspace
 */
GtkWidget* workspace_get_overview_widget(Workspace* workspace) {
    if (!workspace) {
        return NULL;
    }
    return workspace->overview_widget;
}

/**
 * Get the main panel widget (accordion container) from workspace
 */
GtkWidget* workspace_get_panel(Workspace* workspace) {
    if (!workspace || !workspace->accordion) {
        return NULL;
    }
    return accordion_get_widget(workspace->accordion);
}
