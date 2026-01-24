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
 * Replace default titlebar with header bar for a dialog window
 * @param window The window to set header bar for
 * @param title The title to display in the header bar
 */
void ui_utils_set_header_bar(GtkWindow* window, const gchar* title);

#endif /* UI_UTILS_H */