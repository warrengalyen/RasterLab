#ifndef COLOR_CHOOSER_DIALOG_H
#define COLOR_CHOOSER_DIALOG_H

#include <gtk/gtk.h>

/**
 * Create and show a color chooser dialog
 * 
 * @param parent Parent window for the dialog
 * @param title Dialog title
 * @param initial_color Initial color to display (RGB, 0-1 range)
 * @param callback Callback function called whenever color changes
 * @param callback_data User data passed to callback
 * @return The dialog widget (caller should connect to response signal)
 */
GtkWidget* color_chooser_dialog_new(GtkWindow* parent,
                                    const char* title,
                                    GdkRGBA* initial_color,
                                    void (*callback)(double r, double g, double b, gpointer user_data),
                                    gpointer callback_data);

/**
 * Get the currently selected color from the dialog
 * 
 * @param dialog The color chooser dialog
 * @param r Output red component (0-1)
 * @param g Output green component (0-1)
 * @param b Output blue component (0-1)
 */
void color_chooser_dialog_get_color(GtkWidget* dialog, double* r, double* g, double* b);

#endif /* COLOR_CHOOSER_DIALOG_H */
