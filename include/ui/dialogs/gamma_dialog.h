/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GAMMA_DIALOG_H
#define GAMMA_DIALOG_H

#include <gtk/gtk.h>
#include "render/layer.h"

/**
 * Callback function type for gamma dialog preview updates
 * Called when control values change to update the preview
 * @param dialog The gamma dialog (can be NULL, use user_data instead)
 * @param values Array of current control values (gamma values)
 * @param num_values Number of values (should be 3)
 * @param user_data User data (should be GammaDialog*)
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*GammaDialogPreviewCallback)(void *dialog,
                                               const gdouble *values,
                                               gint num_values,
                                               gpointer user_data);

/**
 * Gamma dialog structure
 */
typedef struct _GammaDialog GammaDialog;

/**
 * Create a new gamma correction dialog
 * @param title Dialog title
 * @return New GammaDialog instance, or NULL on error
 */
GammaDialog* gamma_dialog_new(const gchar *title);

/**
 * Free gamma dialog
 */
void gamma_dialog_free(GammaDialog *dialog);

/**
 * Get the dialog window
 */
GtkWindow* gamma_dialog_get_window(GammaDialog *dialog);

/**
 * Set the layers for preview
 */
void gamma_dialog_set_layers(GammaDialog *dialog, ImageLayer *original, ImageLayer *temp);

/**
 * Run the dialog and get gamma values
 * @param dialog The gamma dialog
 * @param parent Parent window
 * @param gamma_values Output array for gamma values (must be size 3)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint gamma_dialog_run(GammaDialog *dialog, GtkWindow *parent, gfloat *gamma_values);

/**
 * Update the after layer in preview
 */
void gamma_dialog_update_after_layer(GammaDialog *dialog, ImageLayer *layer);

/**
 * Set preview callback for live updates
 */
void gamma_dialog_set_preview_callback(GammaDialog *dialog,
                                       GammaDialogPreviewCallback callback,
                                       gpointer user_data);

/**
 * Reset all controls to default values (1.0 for all channels)
 */
void gamma_dialog_reset(GammaDialog *dialog);

#endif /* GAMMA_DIALOG_H */

