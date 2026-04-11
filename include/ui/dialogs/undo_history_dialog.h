/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UNDO_HISTORY_DIALOG_H
#define UNDO_HISTORY_DIALOG_H

#include "document.h"
#include <gtk/gtk.h>

/**
 * Show the undo history dialog and, on OK, navigate the document to the
 * selected history state (applying undos or redos as needed).
 *
 * The list is built from doc->undo_stack (oldest→newest) followed by
 * doc->redo_stack (next-redo→oldest-redo). A synthetic "Original image"
 * entry at position 0 represents the pre-command baseline.
 *
 * @param parent Parent window (for modality)
 * @param doc    Document whose undo/redo history to display
 */
void undo_history_dialog_show(GtkWindow* parent, ImageDocument* doc);

#endif /* UNDO_HISTORY_DIALOG_H */
