/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef COLOR_BALANCE_DIALOG_H
#define COLOR_BALANCE_DIALOG_H

#include <gtk/gtk.h>
#include "render/layer.h"
#include "ocular.h"

/**
 * Callback function type for color balance dialog preview updates
 * Called when control values change to update the preview
 * @param dialog The color balance dialog (can be NULL, use user_data instead)
 * @param values Array of current control values [red, green, blue]
 * @param num_values Number of values (should be 3)
 * @param user_data User data (should be ColorBalanceDialog*)
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*ColorBalanceDialogPreviewCallback)(void *dialog,
                                                      const gint *values,
                                                      gint num_values,
                                                      OcToneBalanceMode mode,
                                                      gboolean preserve_luminosity,
                                                      gpointer user_data);

/**
 * Color balance dialog structure
 */
typedef struct _ColorBalanceDialog ColorBalanceDialog;

/**
 * Create a new color balance dialog
 * @param title Dialog title
 * @return New ColorBalanceDialog instance, or NULL on error
 */
ColorBalanceDialog* color_balance_dialog_new(const gchar *title);

/**
 * Free color balance dialog
 */
void color_balance_dialog_free(ColorBalanceDialog *dialog);

/**
 * Get the dialog window
 */
GtkWindow* color_balance_dialog_get_window(ColorBalanceDialog *dialog);

/**
 * Set the layers for preview
 */
void color_balance_dialog_set_layers(ColorBalanceDialog *dialog, ImageLayer *original, ImageLayer *temp);

/**
 * Run the dialog and get color balance values
 * @param dialog The color balance dialog
 * @param parent Parent window
 * @param red_balance Output red balance value
 * @param green_balance Output green balance value
 * @param blue_balance Output blue balance value
 * @param mode Output tone balance mode
 * @param preserve_luminosity Output preserve luminosity flag
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint color_balance_dialog_run(ColorBalanceDialog *dialog, GtkWindow *parent, 
                               gint *red_balance, gint *green_balance, gint *blue_balance,
                               OcToneBalanceMode *mode, gboolean *preserve_luminosity);

/**
 * Update the after layer in preview
 */
void color_balance_dialog_update_after_layer(ColorBalanceDialog *dialog, ImageLayer *layer);

/**
 * Set preview callback for live updates
 */
void color_balance_dialog_set_preview_callback(ColorBalanceDialog *dialog,
                                                ColorBalanceDialogPreviewCallback callback,
                                                gpointer user_data);

/**
 * Reset all controls to default values (0 for all channels)
 */
void color_balance_dialog_reset(ColorBalanceDialog *dialog);

#endif /* COLOR_BALANCE_DIALOG_H */

