/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_UTILS_H
#define UI_UTILS_H

#include <gtk/gtk.h>

/**
 * Custom color button data structure
 */
typedef struct {
    GtkWidget* button;
    GdkRGBA color;
    GtkWindow* parent_window;
    void (*callback)(GtkWidget* button, gpointer user_data);
    gpointer callback_data;
} ColorButtonData;

/**
 * Update button appearance with color using CSS
 * @param button The button to update
 * @param color The color to use
 */
void update_color_button_appearance(GtkWidget* button, GdkRGBA* color);

/**
 * Create a custom color button that opens the custom color chooser dialog
 * @param parent_window Parent window for the color chooser dialog
 * @param initial_color Initial color to display
 * @param callback Callback function called when color changes
 * @param user_data User data passed to callback
 * @return The button widget
 */
GtkWidget* create_custom_color_button(GtkWindow* parent_window,
                                      GdkRGBA* initial_color,
                                      void (*callback)(GtkWidget* button, gpointer user_data),
                                      gpointer user_data);

/**
 * Get the current color from a custom color button
 * @param button The color button
 * @param color Output color
 * @return TRUE if successful, FALSE otherwise
 */
gboolean get_custom_color_button_color(GtkWidget* button, GdkRGBA* color);

/**
 * Set the color of a custom color button
 * @param button The color button
 * @param color The new color
 */
void set_custom_color_button_color(GtkWidget* button, GdkRGBA* color);

/**
 * Replace a GtkSpinButton widget with a VerticalSpinButton in-place.
 * Preserves parent box packing, position, and margins.
 * @param old_spin Existing GtkSpinButton widget
 * @return New VerticalSpinButton widget, or old widget if replacement failed
 */
GtkWidget* ui_utils_replace_spin_with_vertical(GtkWidget* old_spin);

/**
 * Replace a builder spin widget (by object id) with a VerticalSpinButton.
 * @param builder GtkBuilder containing the widget
 * @param spin_id Builder object id of GtkSpinButton
 * @return New VerticalSpinButton widget, or NULL on failure
 */
GtkWidget* ui_utils_replace_builder_spin_with_vertical(GtkBuilder* builder, const gchar* spin_id);

/**
 * Show the hand pointer cursor on hover (e.g. GtkColorButton / color swatches).
 * @param widget Widget to attach enter/leave handlers to; no-op if NULL
 */
void ui_utils_widget_set_hand_cursor(GtkWidget* widget);

/**
 * Replace default titlebar with header bar for a dialog window
 * @param window The window to set header bar for
 * @param title The title to display in the header bar
 */
void ui_utils_set_header_bar(GtkWindow* window, const gchar* title);

/**
 * Create and run a message dialog with custom buttons and spacing.
 * @param parent Parent window for the dialog
 * @param message_type GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING, etc.
 * @param primary_text Primary message text
 * @param secondary_text Secondary text, or NULL
 * @param default_response Response ID of the default button
 * @param ... Button list: "button_text", response_id pairs, NULL-terminated
 * @return The response ID from gtk_dialog_run
 */
gint ui_utils_message_dialog_run(GtkWindow* parent,
                                 GtkMessageType message_type,
                                 const gchar* primary_text,
                                 const gchar* secondary_text,
                                 gint default_response,
                                 ...);

/**
 * Set gettext domain for GtkBuilder so translatable Glade strings use the rasterlab catalog.
 * Call after gtk_builder_new(), before gtk_builder_add_from_resource() / add_from_file().
 */
void ui_utils_builder_set_translation_domain(GtkBuilder* builder);

#endif /* UI_UTILS_H */