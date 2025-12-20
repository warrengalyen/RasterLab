#ifndef CURVES_DIALOG_H
#define CURVES_DIALOG_H

#include "render/layer.h"
#include "ui/widgets/curves_widget.h"
#include <gtk/gtk.h>

/**
 * Callback function type for curves dialog preview updates
 * Called when control values change to update the preview
 * @param dialog The curves dialog (can be NULL, use user_data instead)
 * @param user_data User data (should be CurvesDialog*)
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*CurvesDialogPreviewCallback)(void* dialog,
                                                gpointer user_data);

/**
 * Curves dialog structure
 */
typedef struct _CurvesDialog CurvesDialog;

/**
 * Create a new curves adjustment dialog
 * @param title Dialog title
 * @return New CurvesDialog instance, or NULL on error
 */
CurvesDialog* curves_dialog_new(const gchar* title);

/**
 * Free curves dialog
 */
void curves_dialog_free(CurvesDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* curves_dialog_get_window(CurvesDialog* dialog);

/**
 * Set the layers for preview
 */
void curves_dialog_set_layers(CurvesDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog
 * @param dialog The curves dialog
 * @param parent Parent window
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint curves_dialog_run(CurvesDialog* dialog, GtkWindow* parent);

/**
 * Update the after layer in preview
 */
void curves_dialog_update_after_layer(CurvesDialog* dialog, ImageLayer* layer);

/**
 * Set preview callback for live updates
 */
void curves_dialog_set_preview_callback(CurvesDialog* dialog,
                                        CurvesDialogPreviewCallback callback,
                                        gpointer user_data);

/**
 * Reset all controls to default values
 */
void curves_dialog_reset(CurvesDialog* dialog);

/**
 * Get the curves widget from the dialog
 */
CurvesWidget* curves_dialog_get_curves_widget(CurvesDialog* dialog);

#endif /* CURVES_DIALOG_H */
