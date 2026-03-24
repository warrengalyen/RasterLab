#ifndef UI_FILTER_H
#define UI_FILTER_H

#include "ui.h"
#include "ui/widgets/filter_dialog.h"
#include <glib.h>

/**
 * Apply a filter to the currently selected layer
 * This is a reusable function for applying filters that modify layer pixels
 * @param ctx The application context
 * @param filter_func Function pointer to the filter function (returns TRUE on success)
 * @param filter_name Name of the filter for error messages
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean ui_apply_layer_filter(AppContext* ctx,
                               gboolean (*filter_func)(ImageLayer* layer),
                               const gchar* filter_name);

/**
 * Apply a filter with parameter values to the currently selected layer
 * This is a reusable function for applying filters that take one or more parameters
 * @param ctx The application context
 * @param filter_func Function pointer to the filter function (takes layer, values array, and count, returns TRUE on success)
 * @param filter_name Name of the filter for error messages
 * @param values Array of parameter values to pass to the filter function
 * @param num_values Number of values in the array
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean ui_apply_layer_filter_with_value(AppContext* ctx,
                                          gboolean (*filter_func)(ImageLayer* layer, const gfloat* values, gint num_values),
                                          const gchar* filter_name,
                                          const gfloat* values,
                                          gint num_values);

/**
 * Show a filter dialog and get user values
 * @param ctx The application context
 * @param title Dialog title
 * @param controls Array of control parameters
 * @param num_controls Number of controls
 * @param preview_callback Callback for live preview updates (can be NULL)
 * @param values Output array to store user-selected values
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint ui_show_filter_dialog(AppContext* ctx,
                           const gchar* title,
                           FilterControlParam* controls,
                           gint num_controls,
                           FilterDialogPreviewCallback preview_callback,
                           gdouble* values);

/**
 * Show a filter dialog with zoom/pan control
 * @param ctx The application context
 * @param title Dialog title
 * @param controls Array of control parameters
 * @param num_controls Number of controls
 * @param preview_callback Callback for live preview updates (can be NULL)
 * @param values Output array to store user-selected values
 * @param allow_zoom_pan Whether to allow zoom/pan (1:1 mode) in preview. When FALSE, only scaled-down preview is used for better performance.
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint ui_show_filter_dialog_with_zoom_pan(AppContext* ctx,
                                         const gchar* title,
                                         FilterControlParam* controls,
                                         gint num_controls,
                                         FilterDialogPreviewCallback preview_callback,
                                         gdouble* values,
                                         gboolean allow_zoom_pan);

/**
 * Enable Adjustments and Effects top-level menus when the active document has
 * a selected raster (pixel) layer with a surface.
 */
void ui_adjustments_and_effects_menu_update_sensitivity(AppContext* ctx);

#endif /* UI_FILTER_H */
