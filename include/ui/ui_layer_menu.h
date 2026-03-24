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
void on_layer_visibility_show_current(GtkWidget* widget, gpointer data);
void on_layer_visibility_show_only(GtkWidget* widget, gpointer data);
void on_layer_visibility_hide_only(GtkWidget* widget, gpointer data);
void on_layer_visibility_show_all(GtkWidget* widget, gpointer data);
void on_layer_visibility_hide_all(GtkWidget* widget, gpointer data);
void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data);
void on_layer_rasterize_text(GtkWidget* widget, gpointer data);

/**
 * Update the "Show this layer" check state to match selected layer visibility.
 * Safe to call from ui_update_menu_and_button_states (uses toggled-handler block).
 */
void layer_visibility_update_check_state(AppContext* ctx);

/**
 * Toggle layer visibility (used by layers panel and Layer menu).
 * Creates undo command, executes, updates layers panel and menu states.
 * @param ctx Application context
 * @param doc Document containing the layer
 * @param layer Layer to toggle
 */
void layer_visibility_toggle_execute(AppContext* ctx, ImageDocument* doc, ImageLayer* layer);

/**
 * Create the Layer menu (legacy function for programmatic menu creation)
 */
GtkWidget* create_layer_menu(AppContext* ctx);

/**
 * Setup Layer menu from Glade builder
 */
void ui_layer_menu_setup(GtkBuilder* builder, AppContext* ctx);

void ui_layer_menu_update_sensitivity(AppContext* ctx);

#endif /* UI_LAYER_MENU_H */
