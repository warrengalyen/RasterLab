/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef GRADIENT_EDITOR_DIALOG_H
#define GRADIENT_EDITOR_DIALOG_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Show the Gradient Editor dialog (Tools > Developer > Gradient Editor).
 *
 * Scans ctx->app_dir/gradients/ for .ggr and .grd files, loads them, caches
 * UI preview textures, and displays a two-tab dialog:
 *   - Collection: scrollable list with swatch preview + gradient name
 *   - Editor:     large preview bar and detail info for the selected gradient
 *
 * @param ctx  Application context (must not be NULL; needs app_dir and window)
 */
void gradient_editor_dialog_show(AppContext* ctx);

#endif /* GRADIENT_EDITOR_DIALOG_H */
