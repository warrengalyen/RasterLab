#ifndef UI_IMAGE_MENU_H
#define UI_IMAGE_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Image menu callback functions
 */
void on_image_canvas_size(GtkWidget* widget, gpointer data);
void on_image_duplicate(GtkWidget* widget, gpointer data);
void on_image_fit_active_layer(GtkWidget* widget, gpointer data);
void on_image_fit_all_layers(GtkWidget* widget, gpointer data);
void on_image_flip_horizontal(GtkWidget* widget, gpointer data);
void on_image_flip_vertical(GtkWidget* widget, gpointer data);
void on_image_transpose(GtkWidget* widget, gpointer data);
void on_image_merge_visible(GtkWidget* widget, gpointer data);
void on_image_flatten(GtkWidget* widget, gpointer data);
void on_image_rotate_arbitrary(GtkWidget* widget, gpointer data);
void on_image_rotate_90_cw(GtkWidget* widget, gpointer data);
void on_image_rotate_90_ccw(GtkWidget* widget, gpointer data);
void on_image_rotate_180(GtkWidget* widget, gpointer data);

/**
 * Setup Image menu from Glade builder
 */
void ui_image_menu_setup(GtkBuilder* builder, AppContext* ctx);

#endif /* UI_IMAGE_MENU_H */
