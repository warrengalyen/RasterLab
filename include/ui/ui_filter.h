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
gboolean ui_apply_layer_filter(AppContext *ctx, 
                                gboolean (*filter_func)(ImageLayer *layer),
                                const gchar *filter_name);

/**
 * Apply a filter with a parameter value to the currently selected layer
 * This is a reusable function for applying filters that take a single parameter
 * @param ctx The application context
 * @param filter_func Function pointer to the filter function (takes layer and value, returns TRUE on success)
 * @param filter_name Name of the filter for error messages
 * @param value The parameter value to pass to the filter function
 * @return TRUE if filter was applied successfully, FALSE otherwise
 */
gboolean ui_apply_layer_filter_with_value(AppContext *ctx,
                                          gboolean (*filter_func)(ImageLayer *layer, gfloat value),
                                          const gchar *filter_name,
                                          gfloat value);

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
gint ui_show_filter_dialog(AppContext *ctx,
                           const gchar *title,
                           FilterControlParam *controls,
                           gint num_controls,
                           FilterDialogPreviewCallback preview_callback,
                           gdouble *values);

#endif /* UI_FILTER_H */

