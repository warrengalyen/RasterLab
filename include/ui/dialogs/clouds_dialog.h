#ifndef CLOUDS_DIALOG_H
#define CLOUDS_DIALOG_H

#include "ocular.h"
#include "render/layer.h"
#include <gtk/gtk.h>

/**
 * Callback function type for clouds dialog preview updates
 * Called when control values change to update the preview
 * @param dialog The clouds dialog (can be NULL, use user_data instead)
 * @param params CloudParams structure with current values
 * @param user_data User data (should be CloudsDialog*)
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*CloudsDialogPreviewCallback)(void* dialog,
                                                const CloudParams* params,
                                                gpointer user_data);

/**
 * Clouds dialog structure
 */
typedef struct _CloudsDialog CloudsDialog;

/**
 * Create a new clouds dialog
 * @param title Dialog title
 * @return New CloudsDialog instance, or NULL on error
 */
CloudsDialog* clouds_dialog_new(const gchar* title);

/**
 * Free clouds dialog
 */
void clouds_dialog_free(CloudsDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* clouds_dialog_get_window(CloudsDialog* dialog);

/**
 * Set the layers for preview
 */
void clouds_dialog_set_layers(CloudsDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog and get cloud parameters
 * @param dialog The clouds dialog
 * @param parent Parent window
 * @param params Output CloudParams structure
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint clouds_dialog_run(CloudsDialog* dialog, GtkWindow* parent, CloudParams* params);

/**
 * Update the after layer in preview
 */
void clouds_dialog_update_after_layer(CloudsDialog* dialog, ImageLayer* layer);

/**
 * Set preview callback for live updates
 */
void clouds_dialog_set_preview_callback(CloudsDialog* dialog,
                                        CloudsDialogPreviewCallback callback,
                                        gpointer user_data);

/**
 * Reset all controls to default values
 */
void clouds_dialog_reset(CloudsDialog* dialog);

#endif /* CLOUDS_DIALOG_H */
