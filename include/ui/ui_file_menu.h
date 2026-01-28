#ifndef UI_FILE_MENU_H
#define UI_FILE_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * File menu callback functions
 */
void on_file_open(GtkWidget* widget, gpointer data);
void on_file_open_response(GtkNativeDialog* dialog, gint response_id, gpointer user_data);
void on_file_save(GtkWidget* widget, gpointer data);
void on_file_save_as(GtkWidget* widget, gpointer data);
void on_file_save_as_response(GtkDialog* dialog, gint response_id, gpointer user_data);
void on_file_close(GtkWidget* widget, gpointer data);
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

#endif /* UI_FILE_MENU_H */
