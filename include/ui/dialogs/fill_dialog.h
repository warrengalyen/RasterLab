/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILL_DIALOG_H
#define FILL_DIALOG_H

#include "document.h"
#include <gtk/gtk.h>

/**
 * Color source for fill
 */
typedef enum {
    FILL_COLOR_CUSTOM = 0,
    FILL_COLOR_FOREGROUND = 1,
    FILL_COLOR_BACKGROUND = 2
} FillColorSource;

/**
 * Result from fill dialog (when user clicks OK)
 */
typedef struct {
    FillColorSource color_source;
    GdkRGBA color;       /* Used when color_source is FILL_COLOR_CUSTOM */
    gint opacity;        /* 0-100 */
    BlendMode blend_mode;
} FillDialogResult;

/**
 * Run the Fill dialog.
 * @param parent Parent window for the dialog
 * @param result Filled with user choices when dialog is accepted (not modified on Cancel)
 * @return TRUE if user clicked OK, FALSE if Cancel or dialog was destroyed
 */
gboolean fill_dialog_run(GtkWindow* parent, FillDialogResult* result);

#endif /* FILL_DIALOG_H */
