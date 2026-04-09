/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef RESIZE_DIALOG_H
#define RESIZE_DIALOG_H

#include "document.h"
#include <gtk/gtk.h>

/**
 * Image resize dialog structure
 */
typedef struct _ResizeDialog ResizeDialog;

/**
 * Result structure for image resize dialog
 */
typedef struct {
    guint width;             /* New width in pixels */
    guint height;            /* New height in pixels */
    gdouble resolution;      /* Resolution in PPI */
    gint interpolation_mode; /* OcInterpolationMode (0=Nearest, 1=Bilinear, 2=Bicubic, 3=Lanczos) */
} ResizeDialogResult;

/**
 * Create a new image resize dialog
 * @param doc The document to get initial values from
 * @return New ResizeDialog instance, or NULL on error
 */
ResizeDialog* resize_dialog_new(ImageDocument* doc);

/**
 * Free resize dialog
 */
void resize_dialog_free(ResizeDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* resize_dialog_get_window(ResizeDialog* dialog);

/**
 * Run the dialog and get resize parameters
 * @param dialog The resize dialog
 * @param parent Parent window
 * @param result Output structure for dialog results (must be freed with resize_dialog_result_free)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint resize_dialog_run(ResizeDialog* dialog, GtkWindow* parent, ResizeDialogResult** result);

/**
 * Free dialog result structure
 */
void resize_dialog_result_free(ResizeDialogResult* result);

#endif /* RESIZE_DIALOG_H */
