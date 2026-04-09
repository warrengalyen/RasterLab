/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef CONVOLUTION_DIALOG_H
#define CONVOLUTION_DIALOG_H

#include "ocular.h"
#include "render/layer.h"
#include <gtk/gtk.h>

/**
 * Callback function type for convolution dialog preview updates
 * Called when control values change to update the preview
 * @param dialog The convolution dialog (can be NULL, use user_data instead)
 * @param kernel The convolution kernel (5x5 = 25 floats)
 * @param divisor The divisor value
 * @param offset The offset value
 * @param auto_normalize Whether to auto-normalize
 * @param user_data User data (should be ConvolutionDialog*)
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*ConvolutionDialogPreviewCallback)(void* dialog,
                                                     float* kernel,
                                                     unsigned char divisor,
                                                     unsigned char offset,
                                                     gboolean auto_normalize,
                                                     gpointer user_data);

/**
 * Convolution dialog structure
 */
typedef struct _ConvolutionDialog ConvolutionDialog;

/**
 * Create a new convolution dialog
 * @param title Dialog title
 * @return New ConvolutionDialog instance, or NULL on error
 */
ConvolutionDialog* convolution_dialog_new(const gchar* title);

/**
 * Free convolution dialog
 */
void convolution_dialog_free(ConvolutionDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* convolution_dialog_get_window(ConvolutionDialog* dialog);

/**
 * Set the layers for preview
 */
void convolution_dialog_set_layers(ConvolutionDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog and get convolution parameters
 * @param dialog The convolution dialog
 * @param parent Parent window
 * @param kernel Output kernel array (must be at least 25 floats)
 * @param divisor Output divisor parameter
 * @param offset Output offset parameter
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint convolution_dialog_run(ConvolutionDialog* dialog, GtkWindow* parent,
                            float* kernel, unsigned char* divisor, unsigned char* offset);

/**
 * Update the after layer in preview
 */
void convolution_dialog_update_after_layer(ConvolutionDialog* dialog, ImageLayer* layer);

/**
 * Set preview callback for live updates
 */
void convolution_dialog_set_preview_callback(ConvolutionDialog* dialog,
                                             ConvolutionDialogPreviewCallback callback,
                                             gpointer user_data);

/**
 * Reset all controls to default values
 */
void convolution_dialog_reset(ConvolutionDialog* dialog);

#endif /* CONVOLUTION_DIALOG_H */
