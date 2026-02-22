#ifndef UI_LAYER_MENU_H
#define UI_LAYER_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Layer menu callback functions
 */
void on_layer_new(GtkWidget* widget, gpointer data);
void on_layer_delete(GtkWidget* widget, gpointer data);
void on_layer_duplicate(GtkWidget* widget, gpointer data);
void on_layer_move_up(GtkWidget* widget, gpointer data);
void on_layer_move_down(GtkWidget* widget, gpointer data);
void on_layer_order_select_top(GtkWidget* widget, gpointer data);
void on_layer_order_select_above(GtkWidget* widget, gpointer data);
void on_layer_order_select_below(GtkWidget* widget, gpointer data);
void on_layer_order_select_bottom(GtkWidget* widget, gpointer data);
void on_layer_order_move_top(GtkWidget* widget, gpointer data);
void on_layer_order_move_up(GtkWidget* widget, gpointer data);
void on_layer_order_move_down(GtkWidget* widget, gpointer data);
void on_layer_order_move_bottom(GtkWidget* widget, gpointer data);
void on_layer_merge_up(GtkWidget* widget, gpointer data);
void on_layer_merge_down(GtkWidget* widget, gpointer data);
void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data);

/**
 * Create the Layer menu (legacy function for programmatic menu creation)
 */
GtkWidget* create_layer_menu(AppContext* ctx);

/**
 * Setup Layer menu from Glade builder
 */
void ui_layer_menu_setup(GtkBuilder* builder, AppContext* ctx);

#endif /* UI_LAYER_MENU_H */
