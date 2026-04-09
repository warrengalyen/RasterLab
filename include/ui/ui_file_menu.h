/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_FILE_MENU_H
#define UI_FILE_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * File menu callback functions
 */
void on_file_new(GtkWidget* widget, gpointer data);
void on_file_open(GtkWidget* widget, gpointer data);
void on_file_open_response(GtkNativeDialog* dialog, gint response_id, gpointer user_data);
void on_file_save(GtkWidget* widget, gpointer data);
void on_file_save_as(GtkWidget* widget, gpointer data);
void on_file_save_as_response(GtkDialog* dialog, gint response_id, gpointer user_data);
void on_file_close(GtkWidget* widget, gpointer data);
void on_file_close_all(GtkWidget* widget, gpointer data);
void on_file_exit(GtkWidget* widget, gpointer data);
void on_recent_file_activate(GtkMenuItem* menu_item, gpointer user_data);
void on_clear_recent_files(GtkMenuItem* menu_item, gpointer user_data);
gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data);

/**
 * Update the "Open Recent" submenu with current recent files
 */
void ui_update_recent_files_menu(AppContext* ctx);

/**
 * Setup File menu from Glade builder
 */
void ui_file_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);

void ui_file_menu_update_sensitivity(AppContext* ctx);

/**
 * Load an image from disk into a new tab (same pipeline as File > Open).
 * Skips unsupported formats silently. Returns TRUE on success.
 */
gboolean ui_file_menu_open_path_as_new_document(AppContext* ctx, const gchar* file_path);

/**
 * Enable drag-and-drop of image files onto the canvas viewport / drawing area.
 */
void ui_file_menu_setup_viewport_drag_drop(ImageDocument* doc, AppContext* ctx);

/**
 * Enable drag-and-drop onto the central notebook when no tabs exist (open files as new documents).
 */
void ui_file_menu_setup_notebook_drag_drop(GtkWidget* notebook, AppContext* ctx);

#endif /* UI_FILE_MENU_H */
