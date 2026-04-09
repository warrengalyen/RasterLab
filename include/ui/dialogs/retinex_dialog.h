/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef RETINEX_DIALOG_H
#define RETINEX_DIALOG_H

#include "ocular.h"
#include "render/layer.h"
#include <gtk/gtk.h>

/**
 * Callback function type for retinex dialog preview updates
 * Called when control values change to update the preview
 * @param dialog The retinex dialog (can be NULL, use user_data instead)
 * @param mode Retinex mode
 * @param scale Scale parameter
 * @param numScales Number of scales
 * @param dynamic Dynamic parameter
 * @param user_data User data (should be RetinexDialog*)
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
typedef gboolean (*RetinexDialogPreviewCallback)(void* dialog,
                                                 OcRetinexMode mode,
                                                 gint scale,
                                                 gint numScales,
                                                 gfloat dynamic,
                                                 gpointer user_data);

/**
 * Retinex dialog structure
 */
typedef struct _RetinexDialog RetinexDialog;

/**
 * Create a new retinex dialog
 * @param title Dialog title
 * @return New RetinexDialog instance, or NULL on error
 */
RetinexDialog* retinex_dialog_new(const gchar* title);

/**
 * Free retinex dialog
 */
void retinex_dialog_free(RetinexDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* retinex_dialog_get_window(RetinexDialog* dialog);

/**
 * Set the layers for preview
 */
void retinex_dialog_set_layers(RetinexDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog and get retinex parameters
 * @param dialog The retinex dialog
 * @param parent Parent window
 * @param mode Output retinex mode
 * @param scale Output scale parameter
 * @param numScales Output number of scales
 * @param dynamic Output dynamic parameter
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint retinex_dialog_run(RetinexDialog* dialog, GtkWindow* parent,
                        OcRetinexMode* mode, gint* scale, gfloat* numScales, gfloat* dynamic);

/**
 * Update the after layer in preview
 */
void retinex_dialog_update_after_layer(RetinexDialog* dialog, ImageLayer* layer);

/**
 * Set preview callback for live updates
 */
void retinex_dialog_set_preview_callback(RetinexDialog* dialog,
                                         RetinexDialogPreviewCallback callback,
                                         gpointer user_data);

/**
 * Reset all controls to default values
 */
void retinex_dialog_reset(RetinexDialog* dialog);

#endif /* RETINEX_DIALOG_H */
