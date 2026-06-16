/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef LENS_CORRECTION_DIALOG_H
#define LENS_CORRECTION_DIALOG_H

#include "render/layer.h"
#include <gtk/gtk.h>

typedef struct _LensCorrectionDialog LensCorrectionDialog;

/**
 * Lens correction parameters
 */
typedef struct {
    gint camera_index;      /* -1 = (any) */
    gint lens_index;        /* -1 = (none) */
    gfloat focal_length;    /* mm */
    gfloat aperture;        /* f-number */
    gfloat focal_distance;  /* meters */
    gboolean scale_to_fit;
    gboolean distortion;
    gboolean vignetting;
    gboolean tca;
} LensCorrectionParams;

/**
 * Create a new lens correction dialog
 * @param title Dialog title
 * @param app_dir Application executable directory (for locating LensProfiles/)
 * @return New dialog instance, or NULL on error
 */
LensCorrectionDialog* lens_correction_dialog_new(const gchar* title,
                                                  const gchar* app_dir);

/**
 * Free lens correction dialog
 */
void lens_correction_dialog_free(LensCorrectionDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* lens_correction_dialog_get_window(LensCorrectionDialog* dialog);

/**
 * Set the layers for preview
 */
void lens_correction_dialog_set_layers(LensCorrectionDialog* dialog,
                                       ImageLayer* original,
                                       ImageLayer* temp);

/**
 * Update the after layer in preview
 */
void lens_correction_dialog_update_after_layer(LensCorrectionDialog* dialog,
                                               ImageLayer* layer);

/**
 * Run the dialog and get lens correction parameters
 * @param dialog The lens correction dialog
 * @param parent Parent window
 * @param params Output parameters
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint lens_correction_dialog_run(LensCorrectionDialog* dialog,
                                GtkWindow* parent,
                                LensCorrectionParams* params);

/**
 * Callback function type for lens correction dialog preview updates
 */
typedef gboolean (*LensCorrectionPreviewCallback)(void* dialog,
                                                   const LensCorrectionParams* params,
                                                   gpointer user_data);

/**
 * Set preview callback for live updates
 */
void lens_correction_dialog_set_preview_callback(LensCorrectionDialog* dialog,
                                                  LensCorrectionPreviewCallback callback,
                                                  gpointer user_data);

/**
 * Free the global lensfun database cache (call at application shutdown)
 */
void lensfun_db_cache_free(void);

#endif /* LENS_CORRECTION_DIALOG_H */
