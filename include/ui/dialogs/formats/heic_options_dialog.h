/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef HEIC_OPTIONS_DIALOG_H
#define HEIC_OPTIONS_DIALOG_H

#include "document.h"
#include "image_format_plugin.h"
#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show HEIC save options dialog
 * @param parent Parent window (can be NULL)
 * @param opts SaveOptions structure with plugin_data pointing to HEICSaveOptions
 * @param doc Document being saved (can be NULL, used for layer count)
 * @return TRUE if user clicked OK, FALSE if cancelled
 */
gboolean heic_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc);

#ifdef __cplusplus
}
#endif

#endif /* HEIC_OPTIONS_DIALOG_H */
