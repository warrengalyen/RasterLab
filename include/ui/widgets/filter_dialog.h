#ifndef FILTER_DIALOG_H
#define FILTER_DIALOG_H

#include "render/layer.h"
#include "ui/widgets/filter_preview.h"
#include <gtk/gtk.h>

/**
 * Filter control parameter structure
 * Defines a single control (slider + value entry) in the filter dialog
 */
typedef struct {
    const gchar* label;    /* Label text for the control */
    gdouble min_value;     /* Minimum value (UI range) */
    gdouble max_value;     /* Maximum value (UI range) */
    gdouble default_value; /* Default value (UI range) */
    gdouble step;          /* Step increment (0.0 for default) */
    gint decimals;         /* Number of decimal places to display */
    gdouble filter_min;    /* Minimum value for filter function (defaults to min_value) */
    gdouble filter_max;    /* Maximum value for filter function (defaults to max_value) */
} FilterControlParam;

/**
 * Filter dialog structure
 * Opaque type for the filter dialog
 */
typedef struct _FilterDialog FilterDialog;

/**
 * Callback function type for filter preview updates
 * Called when control values change to update the preview
 * @param dialog The filter dialog
 * @param values Array of current control values
 * @param num_values Number of values
 * @param user_data User data passed to filter_dialog_set_preview_callback
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*FilterDialogPreviewCallback)(FilterDialog* dialog,
                                                const gdouble* values,
                                                gint num_values,
                                                gpointer user_data);

/**
 * Create a new filter dialog
 * @param title Dialog window title
 * @param controls Array of FilterControlParam structures
 * @param num_controls Number of controls in the array
 * @return The new FilterDialog, or NULL on error
 */
FilterDialog* filter_dialog_new(const gchar* title,
                                const FilterControlParam* controls,
                                gint num_controls);

/**
 * Set the image layer for the preview widget
 * @param dialog The filter dialog
 * @param before_layer Layer to show as "before" (can be NULL)
 * @param after_layer Layer to show as "after" (can be NULL)
 */
void filter_dialog_set_layers(FilterDialog* dialog,
                              ImageLayer* before_layer,
                              ImageLayer* after_layer);

/**
 * Update the after layer preview (for live preview updates)
 * @param dialog The filter dialog
 * @param after_layer Updated layer to show as "after"
 */
void filter_dialog_update_after_layer(FilterDialog* dialog, ImageLayer* after_layer);

/**
 * Get the dialog window widget (for setting transient parent, etc.)
 * @param dialog The filter dialog
 * @return The GtkWindow widget, or NULL on error
 */
GtkWindow* filter_dialog_get_window(FilterDialog* dialog);

/**
 * Get the preview widget from the dialog
 * @param dialog The filter dialog
 * @return The FilterPreview widget, or NULL on error
 */
FilterPreview* filter_dialog_get_preview(FilterDialog* dialog);

/**
 * Run the dialog and get the control values
 * @param dialog The filter dialog
 * @param parent Parent window (can be NULL)
 * @param values Output array to store control values (must be allocated by caller)
 * @param num_values Number of values to retrieve (should match num_controls)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint filter_dialog_run(FilterDialog* dialog,
                       GtkWindow* parent,
                       gdouble* values,
                       gint num_values);

/**
 * Get current control values without running the dialog
 * @param dialog The filter dialog
 * @param values Output array to store control values (must be allocated by caller)
 * @param num_values Number of values to retrieve
 */
void filter_dialog_get_values(FilterDialog* dialog,
                              gdouble* values,
                              gint num_values);

/**
 * Set callback function for live preview updates
 * This callback will be called whenever control values change
 * @param dialog The filter dialog
 * @param callback The callback function (can be NULL to disable)
 * @param user_data User data to pass to callback
 */
void filter_dialog_set_preview_callback(FilterDialog* dialog,
                                        FilterDialogPreviewCallback callback,
                                        gpointer user_data);

/**
 * Reset all controls to their default values
 * @param dialog The filter dialog
 */
void filter_dialog_reset(FilterDialog* dialog);

/**
 * Free the filter dialog
 * @param dialog The filter dialog to free
 */
void filter_dialog_free(FilterDialog* dialog);

#endif /* FILTER_DIALOG_H */
