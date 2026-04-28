/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef LUT3D_DIALOG_H
#define LUT3D_DIALOG_H

#include <gtk/gtk.h>
#include "document.h"
#include "render/layer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 3D LUT color lookup filter dialog.
 * Presents a FilterPreview on the left and LUT controls on the right.
 * Supports live preview, LUT file management, intensity, and blend mode.
 */
typedef struct _Lut3dDialog Lut3dDialog;

/**
 * Preview update callback.
 * Called whenever the user changes controls (LUT selection, intensity, blend mode).
 * The handler should copy the original layer surface to the temp layer,
 * apply the filter, then call lut3d_dialog_update_after_layer().
 *
 * @param dialog    The Lut3dDialog instance
 * @param user_data User data passed to lut3d_dialog_set_preview_callback()
 * @return TRUE on success
 */
typedef gboolean (*Lut3dDialogPreviewCallback)(Lut3dDialog* dialog, gpointer user_data);

/**
 * Create a new 3D LUT dialog.
 *
 * @param title   Window / header-bar title (e.g. "Color Lookup")
 * @param app_dir Application executable directory; the "3DLUTs" sub-folder
 *                is used to discover and store LUT files.  May be NULL.
 * @return Newly allocated Lut3dDialog, or NULL on failure
 */
Lut3dDialog* lut3d_dialog_new(const gchar* title, const gchar* app_dir);

/**
 * Destroy the dialog window and free all resources.
 */
void lut3d_dialog_free(Lut3dDialog* dialog);

/**
 * Return the underlying GtkWindow (for set_transient_for, etc.).
 */
GtkWindow* lut3d_dialog_get_window(Lut3dDialog* dialog);

/**
 * Populate the before/after surfaces in the embedded FilterPreview.
 *
 * @param original  Layer shown in the "before" half
 * @param temp      Layer shown in the "after" half (modified by preview callbacks)
 */
void lut3d_dialog_set_layers(Lut3dDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog modally and collect the user's choices.
 *
 * @param parent         Transient-for window (may be NULL)
 * @param out_lut_path   Receives a newly allocated full path to the selected LUT
 *                       file (only valid when GTK_RESPONSE_OK is returned).
 *                       Caller must g_free() this value.
 * @param out_intensity  Receives the chosen intensity value (0–100).
 * @param out_blend_mode Receives the chosen BlendMode.
 * @return GTK_RESPONSE_OK or GTK_RESPONSE_CANCEL
 */
gint lut3d_dialog_run(Lut3dDialog* dialog,
                      GtkWindow* parent,
                      gchar** out_lut_path,
                      gint* out_intensity,
                      BlendMode* out_blend_mode);

/**
 * Update the "after" surface in the FilterPreview from @p layer.
 * Call this from the preview callback after applying the filter to the temp layer.
 */
void lut3d_dialog_update_after_layer(Lut3dDialog* dialog, ImageLayer* layer);

/**
 * Register a live-preview callback invoked whenever control values change.
 */
void lut3d_dialog_set_preview_callback(Lut3dDialog* dialog,
                                       Lut3dDialogPreviewCallback callback,
                                       gpointer user_data);

/**
 * Return the full path of the currently selected LUT file, or NULL.
 * The returned pointer is owned by the dialog; do not free it.
 */
const gchar* lut3d_dialog_get_selected_path(Lut3dDialog* dialog);

/**
 * Return the current intensity value (0–100).
 */
gint lut3d_dialog_get_intensity(Lut3dDialog* dialog);

/**
 * Return the current BlendMode selection.
 */
BlendMode lut3d_dialog_get_blend_mode(Lut3dDialog* dialog);

#ifdef __cplusplus
}
#endif

#endif /* LUT3D_DIALOG_H */
