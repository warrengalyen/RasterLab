/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_VIEW_MENU_H
#define UI_VIEW_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * View menu callback functions
 */
void on_view_zoom_in(GtkWidget* widget, gpointer data);
void on_view_zoom_out(GtkWidget* widget, gpointer data);
void on_view_zoom_reset(GtkWidget* widget, gpointer data);
void on_view_zoom_fit(GtkWidget* widget, gpointer data);
void on_view_show_layer_edges(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_show_smart_guides(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_show_statusbar(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_show_rulers(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_snap(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_snap_to_canvas(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_snap_to_centerlines(GtkCheckMenuItem* check_menu_item, gpointer data);
void on_view_snap_to_layers(GtkCheckMenuItem* check_menu_item, gpointer data);

/* Zoom to specific level callbacks */
void on_view_zoom_1600(GtkWidget* widget, gpointer data);
void on_view_zoom_800(GtkWidget* widget, gpointer data);
void on_view_zoom_400(GtkWidget* widget, gpointer data);
void on_view_zoom_200(GtkWidget* widget, gpointer data);
void on_view_zoom_100(GtkWidget* widget, gpointer data);
void on_view_zoom_50(GtkWidget* widget, gpointer data);
void on_view_zoom_25(GtkWidget* widget, gpointer data);
void on_view_zoom_12_5(GtkWidget* widget, gpointer data);
void on_view_zoom_6_25(GtkWidget* widget, gpointer data);

/**
 * Setup View menu from Glade builder
 */
void ui_view_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);

void ui_view_menu_update_zoom_sensitivity(AppContext* ctx);

#endif /* UI_VIEW_MENU_H */
