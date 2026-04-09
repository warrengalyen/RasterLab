/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_SELECT_MENU_H
#define UI_SELECT_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Select menu callback functions
 */
void on_select_all(GtkMenuItem* menu_item, gpointer user_data);
void on_select_none(GtkMenuItem* menu_item, gpointer user_data);
void on_select_invert(GtkMenuItem* menu_item, gpointer user_data);
void on_select_grow(GtkMenuItem* menu_item, gpointer user_data);
void on_select_shrink(GtkMenuItem* menu_item, gpointer user_data);
void on_select_border(GtkMenuItem* menu_item, gpointer user_data);
void on_select_feather(GtkMenuItem* menu_item, gpointer user_data);
void on_select_sharpen(GtkMenuItem* menu_item, gpointer user_data);

/**
 * Setup Select menu from Glade builder
 */
void ui_select_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);

void ui_select_menu_update_sensitivity(AppContext* ctx);

#endif /* UI_SELECT_MENU_H */
