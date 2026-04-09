/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Show the application settings dialog (Tools > Settings...)
 * Loads settings from ctx, allows editing, and on OK updates settings and saves to file
 * @param ctx Application context (must have settings and app_dir)
 */
void settings_dialog_show(AppContext* ctx);

#endif /* SETTINGS_DIALOG_H */
