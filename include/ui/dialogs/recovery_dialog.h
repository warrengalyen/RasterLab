/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef RECOVERY_DIALOG_H
#define RECOVERY_DIALOG_H

#include "app/autosave.h"
#include "ui.h"
#include <gtk/gtk.h>

/**
 * Show recovery dialog for autosave files
 * @param ctx Application context
 */
void recovery_dialog_show(AppContext* ctx);

#endif /* RECOVERY_DIALOG_H */
