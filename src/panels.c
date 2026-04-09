/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "panels.h"
#include "i18n.h"
#include <stdio.h>

/**
 * Panel header collapse button callback
 */
static void on_panel_collapse_clicked(GtkButton *button, gpointer user_data)
{
    PanelHeader *header = (PanelHeader *)user_data;
    panel_header_toggle_collapse(header);
}

/**
 * Create a collapsible panel header
 */
PanelHeader* panel_header_new(const gchar *title, GtkWidget *content)
{
    PanelHeader *header = (PanelHeader *)g_malloc(sizeof(PanelHeader));
    GtkWidget *header_box;
    GtkWidget *title_label;
    GtkWidget *collapse_button;
    GtkWidget *content_box;

    /* Main container */
    header->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header box with title and buttons */
    header->header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(header->header_box, 5);
    gtk_widget_set_margin_start(header->header_box, 5);
    gtk_widget_set_margin_end(header->header_box, 5);

    /* Title label */
    title_label = gtk_label_new(title);
    gtk_label_set_markup(GTK_LABEL(title_label), g_strdup_printf("<b>%s</b>", title));
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(header->header_box), title_label, TRUE, TRUE, 0);

    /* Collapse button */
    collapse_button = gtk_button_new_with_label("−");
    gtk_widget_set_size_request(collapse_button, 30, 30);
    g_signal_connect(collapse_button, "clicked", 
                     G_CALLBACK(on_panel_collapse_clicked), header);
    gtk_box_pack_end(GTK_BOX(header->header_box), collapse_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header->container), header->header_box, FALSE, FALSE, 0);

    /* Content area */
    header->content = content;
    gtk_box_pack_start(GTK_BOX(header->container), content, TRUE, TRUE, 0);

    /* Add separator */
    gtk_box_pack_start(GTK_BOX(header->container), 
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 
                       FALSE, FALSE, 0);

    header->is_collapsed = FALSE;

    gtk_widget_show_all(header->container);

    return header;
}

/**
 * Toggle panel collapse state
 */
void panel_header_toggle_collapse(PanelHeader *header)
{
    if (!header) {
        return;
    }

    header->is_collapsed = !header->is_collapsed;

    if (header->is_collapsed) {
        gtk_widget_hide(header->content);
    } else {
        gtk_widget_show(header->content);
    }
}

/**
 * Free a panel header
 */
void panel_header_free(PanelHeader *header)
{
    if (!header) {
        return;
    }

    g_free(header);
}
