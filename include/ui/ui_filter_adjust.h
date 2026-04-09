/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_FILTER_ADJUST_H
#define UI_FILTER_ADJUST_H

#include <gtk/gtk.h>
#include "ui.h"

/**
 * Setup Adjustments menu from Glade builder
 * @param builder The GtkBuilder instance
 * @param ctx The application context
 */
void ui_filter_adjust_setup_menu(GtkBuilder *builder, AppContext *ctx);

#endif /* UI_FILTER_ADJUST_H */

