/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef NEW_LAYER_DIALOG_H
#define NEW_LAYER_DIALOG_H

#include "document.h"
#include "render/layer.h"
#include <gtk/gtk.h>

/**
 * New layer dialog structure
 */
typedef struct _NewLayerDialog NewLayerDialog;

/**
 * Result structure for new layer dialog
 */
typedef struct {
    gchar* name;
    LayerBackgroundType background;
    LayerPosition position;
    gdouble custom_color[4]; /* RGBA */
    gboolean set_active;
} NewLayerDialogResult;

/**
 * Create a new layer dialog
 * @return New NewLayerDialog instance, or NULL on error
 */
NewLayerDialog* new_layer_dialog_new(void);

/**
 * Free new layer dialog
 */
void new_layer_dialog_free(NewLayerDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* new_layer_dialog_get_window(NewLayerDialog* dialog);

/**
 * Run the dialog and get layer parameters
 * @param dialog The new layer dialog
 * @param parent Parent window
 * @param result Output structure for dialog results (must be freed with new_layer_dialog_result_free)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint new_layer_dialog_run(NewLayerDialog* dialog, GtkWindow* parent, NewLayerDialogResult** result);

/**
 * Free dialog result structure
 */
void new_layer_dialog_result_free(NewLayerDialogResult* result);

#endif /* NEW_LAYER_DIALOG_H */
