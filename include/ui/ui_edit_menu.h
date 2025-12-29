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

#endif /* UI_EDIT_MENU_H */
