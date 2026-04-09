/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef PALETTIZE_DIALOG_H
#define PALETTIZE_DIALOG_H

#include "ocular.h"
#include "render/layer.h"
#include <gtk/gtk.h>

/**
 * Palettize dialog structure (forward declaration)
 */
typedef struct _PalettizeDialog PalettizeDialog;

/**
 * Palettize parameters structure
 */
typedef struct {
    gboolean use_file;                /* TRUE if using palette file, FALSE if using image quantization */
    gchar* palette_file;              /* Path to palette file (if use_file is TRUE) */
    OcQuantizeMethod quantize_method; /* Quantization method (if use_file is FALSE) */
    gint max_colors;                  /* Maximum colors (if use_file is FALSE) */
    OcDitherMethod dither_method;     /* Dither method */
    gint dither_amount;               /* Dither amount (0-100) */
    PalettizeDialog* dialog;          /* Dialog pointer for accessing document/layer */
} PalettizeParams;

/**
 * Create a new palettize dialog
 * @param title Dialog title
 * @return New PalettizeDialog instance, or NULL on error
 */
PalettizeDialog* palettize_dialog_new(const gchar* title);

/**
 * Free palettize dialog
 */
void palettize_dialog_free(PalettizeDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* palettize_dialog_get_window(PalettizeDialog* dialog);

/**
 * Set the layers for preview
 */
void palettize_dialog_set_layers(PalettizeDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog and get palettize parameters
 * @param dialog The palettize dialog
 * @param parent Parent window
 * @param params Output PalettizeParams structure (caller must free palette_file with g_free)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint palettize_dialog_run(PalettizeDialog* dialog, GtkWindow* parent, PalettizeParams* params);

/**
 * Update the after layer in preview
 */
void palettize_dialog_update_after_layer(PalettizeDialog* dialog, ImageLayer* layer);

/**
 * Reset all controls to default values
 */
void palettize_dialog_reset(PalettizeDialog* dialog);

#endif /* PALETTIZE_DIALOG_H */
