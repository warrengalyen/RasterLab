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
void on_view_show_statusbar(GtkCheckMenuItem* check_menu_item, gpointer data);

/**
 * Setup View menu from Glade builder
 */
void ui_view_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group);

#endif /* UI_VIEW_MENU_H */
