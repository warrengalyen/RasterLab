/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef TOOL_GRADIENT_H
#define TOOL_GRADIENT_H

#include "tools.h"
#include <cairo.h>
#include <gdk/gdk.h>

struct ImageDocument;

/**
 * Gradient tool state (stored in tool->user_data)
 */
typedef struct {
    gboolean dragging;          /* TRUE while mouse button is held down */
    gdouble start_x;            /* Gradient start point (image coordinates) */
    gdouble start_y;
    gdouble end_x;              /* Gradient end point (image coordinates) */
    gdouble end_y;
    gpointer draw_cmd;          /* Command* — undo command with "before" state captured at mouse_down */
    guchar* pixel_backup;       /* Copy of original layer pixels for real-time re-application */
    gint backup_width;
    gint backup_height;
    gint backup_stride;
    struct ImageLayer* active_layer; /* Layer being painted during this drag */

    /* --- Performance caches (valid for the duration of a drag) --- */
    cairo_surface_t* grad_surface;  /* Reusable temp surface for compositing */
    gint grad_surf_w;               /* Cached surface width */
    gint grad_surf_h;               /* Cached surface height */
    gint64 last_apply_usec;         /* Monotonic timestamp of last gradient apply */
} GradientToolState;

/**
 * Create the Gradient Tool
 * @return Newly created Tool, or NULL on failure
 */
Tool* tool_gradient_create(void);

/**
 * Create a custom plus-sign cursor for the gradient tool.
 * Caller must g_object_unref() the result when done.
 */
GdkCursor* tool_gradient_create_plus_cursor(void);

/**
 * Draw gradient line preview overlay on the viewport.
 * Called from on_viewport_draw in document.c while dragging.
 * @param tool       Gradient tool (must not be NULL)
 * @param doc        Active document
 * @param cr         Cairo context for the viewport
 * @param zoom       Current zoom factor
 */
void tool_gradient_draw_preview(Tool* tool, struct ImageDocument* doc,
                                cairo_t* cr, gdouble zoom);

#endif /* TOOL_GRADIENT_H */
