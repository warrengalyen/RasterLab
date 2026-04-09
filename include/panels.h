/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PANELS_H
#define PANELS_H

#include <gtk/gtk.h>

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

#endif /* PANELS_H */

