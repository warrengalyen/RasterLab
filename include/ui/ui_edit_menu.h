/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_EDIT_MENU_H
#define UI_EDIT_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Edit menu callback functions
 */
void on_edit_undo(GtkWidget* widget, gpointer data);
void on_edit_redo(GtkWidget* widget, gpointer data);
void on_edit_copy(GtkWidget* widget, gpointer data);
void on_edit_cut(GtkWidget* widget, gpointer data);
void on_edit_paste(GtkWidget* widget, gpointer data);
void on_edit_paste_new_image(GtkWidget* widget, gpointer data);
void on_edit_copy_merged(GtkWidget* widget, gpointer data);
void on_edit_cut_merged(GtkWidget* widget, gpointer data);
void on_edit_clear(GtkWidget* widget, gpointer data);

/**
 * Setup Edit menu from Glade builder
 */
void ui_edit_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);

/**
 * Update Edit menu item sensitivity from document and clipboard state
 */
void ui_edit_menu_update_sensitivity(AppContext* ctx);

#endif /* UI_EDIT_MENU_H */
