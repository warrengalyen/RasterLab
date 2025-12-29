#ifndef SELECTION_RADIUS_DIALOG_H
#define SELECTION_RADIUS_DIALOG_H

#include <gtk/gtk.h>

/**
 * Selection radius dialog structure
 */
typedef struct _SelectionRadiusDialog SelectionRadiusDialog;

/**
 * Create a new selection radius dialog
 * @param title Dialog title (e.g., "Grow Selection", "Feather Selection")
 * @return New dialog, or NULL on error
 */
SelectionRadiusDialog* selection_radius_dialog_new(const gchar* title);

/**
 * Free selection radius dialog
 * @param dialog The dialog to free
 */
void selection_radius_dialog_free(SelectionRadiusDialog* dialog);

/**
 * Get the dialog window
 * @param dialog The dialog
 * @return GTK window, or NULL
 */
GtkWindow* selection_radius_dialog_get_window(SelectionRadiusDialog* dialog);

/**
 * Run the dialog and get radius value
 * @param dialog The dialog
 * @param parent Parent window
 * @param radius Output radius value (1-500)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint selection_radius_dialog_run(SelectionRadiusDialog* dialog, GtkWindow* parent, gint* radius);

#endif /* SELECTION_RADIUS_DIALOG_H */
