/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "tools/tool_ellipse_select.h"
#include "command.h"
#include "document.h"
#include "selection.h"
#include "selection/selection_mask.h"
#include "selection/selection_undo.h"
#include "selection/selection_undo_helpers.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

/* Timer callback data structure to hold both tool and document */
typedef struct {
    Tool* tool;
    ImageDocument* doc;
} EllipseSelectTimerData;

/* Forward declaration */
static gboolean on_ellipse_select_tool_animation_timer(gpointer user_data);

/**
 * Create a cursor from resource
 */
static GdkCursor* create_ellipse_select_cursor(void) {
    GdkDisplay* display;
    GdkPixbuf* pixbuf;
    GdkCursor* cursor;
    GError* error = NULL;
    GBytes* bytes;
    GInputStream* stream;

    display = gdk_display_get_default();
    if (!display) {
        return NULL;
    }

    /* Load cursor file from resource as bytes */
    bytes = g_resources_lookup_data("/cursors/elliptical_select_cursor.cur",
                                    G_RESOURCE_LOOKUP_FLAGS_NONE,
                                    &error);
    if (!bytes) {
        if (error) {
            debug_log("WRN", "Failed to load ellipse select cursor resource: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    /* Create input stream from bytes */
    stream = g_memory_input_stream_new_from_bytes(bytes);

    /* Load pixbuf from stream */
    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

    g_object_unref(stream);
    g_bytes_unref(bytes);

    if (!pixbuf) {
        if (error) {
            debug_log("WRN", "Failed to parse ellipse select cursor: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    /* Get pixbuf dimensions for hotspot calculation */
    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);

    /* Create cursor from pixbuf with hotspot at center */
    cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    return cursor;
}

/**
 * Check if a point is inside an ellipse (with tolerance for handles)
 */
static gboolean point_in_ellipse(gint px, gint py, gint ex, gint ey, gint ew, gint eh) {
    if (ew <= 0 || eh <= 0)
        return FALSE;

    /* Calculate center */
    gdouble cx = ex + ew / 2.0;
    gdouble cy = ey + eh / 2.0;

    /* Calculate radii */
    gdouble rx = ew / 2.0;
    gdouble ry = eh / 2.0;

    /* Check if point is inside ellipse: (x-cx)^2/rx^2 + (y-cy)^2/ry^2 <= 1 */
    gdouble dx = (px - cx) / rx;
    gdouble dy = (py - cy) / ry;

    return (dx * dx + dy * dy) <= 1.0;
}

/**
 * Animation timer callback for ellipse select tool marching ants
 * This function is independent of which tool is currently active
 */
static gboolean on_ellipse_select_tool_animation_timer(gpointer user_data) {
    EllipseSelectTimerData* timer_data = (EllipseSelectTimerData*)user_data;

    if (!timer_data || !timer_data->tool || !timer_data->doc) {
        g_free(timer_data);
        return FALSE;
    }

    Tool* tool = timer_data->tool;
    ImageDocument* doc = timer_data->doc;

    if (!doc->drawing_area || !tool->user_data) {
        /* Zero the timer ID before returning FALSE so that reset() does not
         * attempt to g_source_remove() an already auto-removed source. */
        if (tool->user_data)
            ((EllipseSelectToolState*)tool->user_data)->animation_timer_id = 0;
        g_free(timer_data);
        return FALSE;
    }

    EllipseSelectToolState* state = (EllipseSelectToolState*)tool->user_data;

    /* Only animate if editing */
    if (!state->is_editing) {
        state->animation_timer_id = 0;
        g_free(timer_data);
        return FALSE; /* Stop timer */
    }

    /* Update animation phase (cycle backwards 0-3 for clockwise rotation of 4-pixel dash) */
    state->animation_phase = (state->animation_phase > 0) ? (state->animation_phase - 1) : 3;

    /* Queue redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE; /* Keep timer running */
}

/**
 * Animation timer callback for ellipse select tool marching ants (public API)
 */
gboolean tool_ellipse_select_animation_timer(gpointer user_data) {
    /* This public function is kept for API compatibility but delegates to the static version */
    return on_ellipse_select_tool_animation_timer(user_data);
}

/**
 * Elliptical Selection Tool: mouse down - start selection drag
 */
static void ellipse_select_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    EllipseSelectToolState* state;

    if (!tool || !doc) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(EllipseSelectToolState));
    }
    state = (EllipseSelectToolState*)tool->user_data;

    /* Check if clicking on existing editable selection */
    if (state->is_editing) {
        /* Check if clicking on a handle or inside selection for move/resize */
        gint handle = selection_detect_handle_at_point(event->x, event->y,
                                                       state->selection_x, state->selection_y,
                                                       state->selection_w, state->selection_h,
                                                       doc->zoom_factor);
        if (handle >= 0) {
            /* Clicking on handle - start resize */
            state->dragging_handle = handle;
            state->is_dragging = TRUE;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            return;
        }

        /* Check if clicking inside ellipse - move it */
        if (point_in_ellipse(event->x, event->y, state->selection_x, state->selection_y,
                             state->selection_w, state->selection_h)) {
            /* Clicking inside - start move */
            state->dragging_handle = -1; /* -1 means move mode */
            state->is_dragging = TRUE;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            return;
        }

        /* Clicking outside - finalize and start new selection */
        /* Finalize the current selection through the undo system */
        tool_ellipse_select_finalize(tool, doc);

        /* Reset state for new selection */
        state->is_editing = FALSE;
        state->has_been_finalized = FALSE; /* Allow new selection to be finalized */

        if (state->animation_timer_id > 0) {
            g_source_remove(state->animation_timer_id);
            state->animation_timer_id = 0;
        }
    }

    /* Start new selection drag */
    state->is_dragging = TRUE;
    state->dragging_handle = -2; /* -2 means new selection */
    state->anchor_x = event->x;
    state->anchor_y = event->y;
    state->current_x = event->x;
    state->current_y = event->y;
    state->hovered_handle = -1;
    state->has_been_finalized = FALSE; /* Reset flag for new selection */

    /* Get current tool options for selection */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
    if (opts) {
        /* Read selection options from tool options panel */
        state->combine_mode = tool_options_get_ellipse_select_combine(opts);
        state->smooth_mode = tool_options_get_ellipse_select_smooth(opts);
        state->feather_radius = (gint)tool_options_get_ellipse_select_feather(opts);
    } else {
        state->combine_mode = SELECTION_COMBINE_NEW;
        state->smooth_mode = SELECTION_SMOOTH_NONE;
        state->feather_radius = 0;
    }

    /* Change cursor to crosshair */
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        /* Default cursor for this tool */
        gdk_window_set_cursor(window, tool->cursor);
    }

    /* Request redraw of selection overlay */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Elliptical Selection Tool: mouse move - update selection preview
 */
static void ellipse_select_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    EllipseSelectToolState* state;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (EllipseSelectToolState*)tool->user_data;
    GdkWindow* window = gtk_widget_get_window(doc->drawing_area);

    /* Handle resize/move during edit drag */
    if (state->dragging_handle >= -1 && state->is_dragging) {
        /* Update cursor while dragging */
        selection_set_cursor_for_handle(window, state->dragging_handle, tool->cursor);

        if (state->dragging_handle == -1) {
            /* Move mode - translate selection */
            gint dx = event->x - state->anchor_x;
            gint dy = event->y - state->anchor_y;
            state->selection_x += dx;
            state->selection_y += dy;
            state->anchor_x = event->x;
            state->anchor_y = event->y;
            /* Update current position for consistency */
            state->current_x = event->x;
            state->current_y = event->y;
        } else if (state->dragging_handle >= 0) {
            /* Resize mode - adjust based on which handle */
            gint dx = event->x - state->anchor_x;
            gint dy = event->y - state->anchor_y;

            switch (state->dragging_handle) {
                case 0: /* top-left */
                    state->selection_x += dx;
                    state->selection_y += dy;
                    state->selection_w -= dx;
                    state->selection_h -= dy;
                    break;
                case 1: /* top-right */
                    state->selection_y += dy;
                    state->selection_w += dx;
                    state->selection_h -= dy;
                    break;
                case 2: /* bottom-left */
                    state->selection_x += dx;
                    state->selection_w -= dx;
                    state->selection_h += dy;
                    break;
                case 3: /* bottom-right */
                    state->selection_w += dx;
                    state->selection_h += dy;
                    break;
            }

            state->anchor_x = event->x;
            state->anchor_y = event->y;
            /* Update current position for consistency */
            state->current_x = event->x;
            state->current_y = event->y;
        }

        /* Clamp selection to valid bounds */
        /* First, handle negative dimensions (can happen during resize) */
        if (state->selection_w < 0) {
            state->selection_x += state->selection_w;
            state->selection_w = -state->selection_w;
        }
        if (state->selection_h < 0) {
            state->selection_y += state->selection_h;
            state->selection_h = -state->selection_h;
        }

        /* For MOVE operations (dragging_handle == -1), only clamp position */
        if (state->dragging_handle == -1) {
            /* Clamp position to keep selection within bounds */
            if (state->selection_x < 0)
                state->selection_x = 0;
            if (state->selection_y < 0)
                state->selection_y = 0;
            if (state->selection_x + state->selection_w > (gint)doc->width)
                state->selection_x = doc->width - state->selection_w;
            if (state->selection_y + state->selection_h > (gint)doc->height)
                state->selection_y = doc->height - state->selection_h;
        } else {
            /* For RESIZE operations, clamp both position and dimensions */
            if (state->selection_x < 0)
                state->selection_x = 0;
            if (state->selection_y < 0)
                state->selection_y = 0;
            if (state->selection_x + state->selection_w > (gint)doc->width) {
                state->selection_w = doc->width - state->selection_x;
            }
            if (state->selection_y + state->selection_h > (gint)doc->height) {
                state->selection_h = doc->height - state->selection_y;
            }
        }

        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        return;
    }

    /* When not dragging but in edit mode, update cursor based on hover */
    if (!state->is_dragging && state->is_editing) {
        /* Check if hovering over a handle */
        gint hovered_handle = selection_detect_handle_at_point(event->x, event->y,
                                                               state->selection_x, state->selection_y,
                                                               state->selection_w, state->selection_h,
                                                               doc->zoom_factor);

        /* Check if hovering inside ellipse for move */
        if (hovered_handle < 0 &&
            point_in_ellipse(event->x, event->y, state->selection_x, state->selection_y,
                             state->selection_w, state->selection_h)) {
            hovered_handle = -1;
        }

        /* Update state->hovered_handle for visual feedback */
        state->hovered_handle = hovered_handle;

        /* Update cursor based on hover */
        if (hovered_handle >= -1) {
            selection_set_cursor_for_handle(window, hovered_handle, tool->cursor);
        } else {
            /* Default cursor outside selection */
            gdk_window_set_cursor(window, tool->cursor);
        }

        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        return;
    }

    /* Handle new selection drag (dragging_handle == -2) */
    if (state->dragging_handle != -2 || !state->is_dragging) {
        return;
    }

    /* Update current position */
    state->current_x = event->x;
    state->current_y = event->y;

    /* Calculate rectangle bounds */
    gint x = state->anchor_x;
    gint y = state->anchor_y;
    gint width = state->current_x - state->anchor_x;
    gint height = state->current_y - state->anchor_y;

    /* Normalize rectangle coordinates */
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }

    /* Check which handle is under mouse (tolerance of 5 pixels) */
    gdouble tolerance = 5.0;
    gint hovered_handle = -1;

    /* Corner positions: top-left, top-right, bottom-left, bottom-right */
    gdouble corners[4][2] = {
        {x, y},                 /* 0: top-left */
        {x + width, y},         /* 1: top-right */
        {x, y + height},        /* 2: bottom-left */
        {x + width, y + height} /* 3: bottom-right */
    };

    for (gint i = 0; i < 4; i++) {
        gdouble dx = event->x - corners[i][0];
        gdouble dy = event->y - corners[i][1];
        gdouble distance = sqrt(dx * dx + dy * dy);

        if (distance <= tolerance) {
            hovered_handle = i;
            break;
        }
    }

    state->hovered_handle = hovered_handle;

    /* Request redraw of selection overlay only */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Elliptical Selection Tool: mouse up - finalize selection drag
 */
static void ellipse_select_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    EllipseSelectToolState* state;

    if (!tool || !doc || !tool->user_data) {
        return;
    }

    state = (EllipseSelectToolState*)tool->user_data;

    (void)event; /* Unused */

    if (!state->is_dragging) {
        return;
    }

    state->is_dragging = FALSE;

    /* Only handle NEW selections (dragging_handle == -2)
     * For moves/resizes, the bounds are already updated by mouse_move */
    if (state->dragging_handle != -2) {
        /* Move or resize just completed - don't recalculate bounds */
        state->is_editing = TRUE; /* Ensure we stay in edit mode */
        return;
    }

    /* Handle NEW selection drag (dragging_handle == -2) */
    /* Calculate rectangle from anchor to current position
     * Handle negative width/height (dragging backwards) */
    gint x = state->anchor_x;
    gint y = state->anchor_y;
    gint width = state->current_x - state->anchor_x;
    gint height = state->current_y - state->anchor_y;

    /* Normalize rectangle coordinates */
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }

    /* Clamp to image bounds */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x + width > (gint)doc->width) {
        width = doc->width - x;
    }
    if (y + height > (gint)doc->height) {
        height = doc->height - y;
    }

    /* Only proceed if ellipse has non-zero area */
    if (width > 0 && height > 0) {
        /* Store selection bounds for editing */
        state->selection_x = x;
        state->selection_y = y;
        state->selection_w = width;
        state->selection_h = height;
        state->is_editing = TRUE;
        state->dragging_handle = -1;
        state->hovered_handle = -1;
        state->animation_phase = 0; /* Start animation */

        /* Start animation timer if not already running and animation is enabled */
        ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
        gboolean should_animate = opts ? tool_options_get_ellipse_select_animate(opts) : TRUE;

        if (state->animation_timer_id == 0 && should_animate) {
            /* Create timer data structure - will be freed when timer stops */
            EllipseSelectTimerData* timer_data = g_malloc(sizeof(EllipseSelectTimerData));
            timer_data->tool = tool;
            timer_data->doc = doc;
            state->animation_timer_id = g_timeout_add(ANT_DASH_SPEED_SLOW,
                                                      on_ellipse_select_tool_animation_timer,
                                                      (gpointer)timer_data);
        }
    }

    /* Request final redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Finalize the current preview selection to the document selection mask
 */
void tool_ellipse_select_finalize(Tool* tool, ImageDocument* doc) {
    if (!tool || !tool->user_data || !doc) {
        return;
    }

    EllipseSelectToolState* state = (EllipseSelectToolState*)tool->user_data;

    /* If there's an active selection in edit mode, finalize it to the mask */
    if (state->is_editing && state->selection_w > 0 && state->selection_h > 0 && doc->selection_mask) {
        /* Check if already finalized to prevent duplicate undo entries */
        if (state->has_been_finalized) {
            return;
        }

        /* Mark as finalized to prevent duplicate entries */
        state->has_been_finalized = TRUE;

        /* Begin undo transaction to capture selection change */
        SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
            doc->selection_mask,
            doc,
            "Ellipse Select");

        if (transaction) {
            /* Register the affected region */
            /* For INTERSECT/SUBTRACT modes, the affected region is larger than just the new ellipse
               We need to register the bounding box of the current selection + new ellipse */
            gint region_x = state->selection_x;
            gint region_y = state->selection_y;
            gint region_w = state->selection_w;
            gint region_h = state->selection_h;

            if (state->combine_mode == SELECTION_COMBINE_INTERSECT ||
                state->combine_mode == SELECTION_COMBINE_SUBTRACT) {
                /* Find bounding box of current selection to capture all changes */
                gint min_x = region_x, max_x = region_x + region_w;
                gint min_y = region_y, max_y = region_y + region_h;

                /* Expand to include all current selected pixels */
                for (int y = 0; y < doc->selection_mask->height; y++) {
                    for (int x = 0; x < doc->selection_mask->width; x++) {
                        if (doc->selection_mask->base_mask[y * doc->selection_mask->stride + x] > 0) {
                            if (x < min_x)
                                min_x = x;
                            if (x + 1 > max_x)
                                max_x = x + 1;
                            if (y < min_y)
                                min_y = y;
                            if (y + 1 > max_y)
                                max_y = y + 1;
                        }
                    }
                }

                region_x = min_x;
                region_y = min_y;
                region_w = max_x - min_x;
                region_h = max_y - min_y;
            }

            selection_undo_transaction_register_region(
                transaction,
                region_x,
                region_y,
                region_w,
                region_h);
        }

        /* Fill ellipse into mask with current smoothing settings */
        selection_mask_fill_ellipse(
            doc->selection_mask,
            state->selection_x,
            state->selection_y,
            state->selection_w,
            state->selection_h,
            state->combine_mode,
            state->smooth_mode,
            state->feather_radius,
            FALSE); /* FALSE = create Selection objects (for tool operations) */

        /* Note: Feathering parameters are already set per-selection in selection_mask_fill_ellipse */
        /* No need to commit global feathering - per-selection system handles it */

        /* Commit undo transaction */
        if (transaction) {
            Command* cmd = selection_undo_transaction_commit(transaction);
            if (cmd) {
                selection_undo_commit_operation(doc, cmd);

                /* Update UI - get app context from tool */
                AppContext* ctx = (AppContext*)tool->app_context;
                if (ctx) {
                    ui_update_menu_and_button_states(ctx);
                    ui_update_window_title(ctx, NULL);
                }

                /* Mark document as modified */
                doc->modified = TRUE;
            }
        }

        /* Request redraw to show the finalized selection and hide the preview */
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
    }
}

/**
 * Reset ellipse select tool state (called when tool is deactivated)
 */
void tool_ellipse_select_reset(Tool* tool) {
    if (!tool || !tool->user_data) {
        return;
    }

    EllipseSelectToolState* state = (EllipseSelectToolState*)tool->user_data;

    /* Reset edit/drag state to prevent preview from rendering */
    state->is_dragging = FALSE;
    state->is_editing = FALSE;
    state->has_been_finalized = FALSE; /* Reset flag when tool is deactivated */
    state->dragging_handle = -2;
    state->hovered_handle = -2;
    state->animation_phase = 0;

    /* Clear the timer */
    if (state->animation_timer_id != 0) {
        g_source_remove(state->animation_timer_id);
        state->animation_timer_id = 0;
    }

    /* Release the feathered-preview cache */
    if (state->preview_cache) {
        selection_mask_free(state->preview_cache);
        state->preview_cache = NULL;
    }
}

/**
 * Create the Elliptical Selection Tool
 */
Tool* tool_ellipse_select_create(void) {
    Tool* tool = tool_new("Elliptical Select",
                          TOOL_ELLIPSE_SELECT,
                          GDK_CROSSHAIR,
                          TOOL_OPT_SELECTION_MODE | TOOL_OPT_SELECTION_SMOOTH);

    if (!tool) {
        return NULL;
    }

    tool->mouse_down = ellipse_select_tool_mouse_down;
    tool->mouse_move = ellipse_select_tool_mouse_move;
    tool->mouse_up = ellipse_select_tool_mouse_up;

    /* Replace cursor with custom ellipse select cursor */
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }
    tool->cursor = create_ellipse_select_cursor();

    return tool;
}

/**
 * Draw elliptical selection preview during drag
 */
void tool_ellipse_select_draw_preview(ImageDocument* doc, cairo_t* cr, gdouble zoom) {
    if (!doc || !doc->drawing_area || !cr) {
        return;
    }

    if (doc->layers && g_list_length(doc->layers) > 0) {
        ToolRegistry* tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
        if (!tool_registry) {
            return;
        }

        Tool* active_tool = tool_manager_get_active(tool_registry);
        if (!active_tool || active_tool->type != TOOL_ELLIPSE_SELECT || !active_tool->user_data) {
            return;
        }

        EllipseSelectToolState* state = (EllipseSelectToolState*)active_tool->user_data;

        /* Draw if dragging OR editing */
        gboolean should_draw = state->is_dragging || state->is_editing;
        if (!should_draw) {
            return;
        }

        /* Determine ellipse bounds to draw */
        gint rect_x, rect_y, rect_w, rect_h;

        /* Check if this is a new selection drag (dragging_handle == -2)
         * Otherwise use stored bounds for move/resize/edit */
        if (state->is_dragging && state->dragging_handle == -2) {
            /* Drawing new selection - calculate from drag state */
            rect_x = state->anchor_x;
            rect_y = state->anchor_y;
            rect_w = state->current_x - state->anchor_x;
            rect_h = state->current_y - state->anchor_y;

            /* Normalize */
            if (rect_w < 0) {
                rect_x += rect_w;
                rect_w = -rect_w;
            }
            if (rect_h < 0) {
                rect_y += rect_h;
                rect_h = -rect_h;
            }
        } else {
            /* Move/resize/edit - use stored bounds */
            rect_x = state->selection_x;
            rect_y = state->selection_y;
            rect_w = state->selection_w;
            rect_h = state->selection_h;
        }

        if (rect_w <= 0 || rect_h <= 0) {
            return;
        }

        /* Save Cairo state to draw preview in zoomed space */
        cairo_save(cr);

        /* Apply zoom transform for preview ellipse */
        if (zoom != 1.0) {
            cairo_scale(cr, zoom, zoom);
        }

        /* Update smoothing mode and feather radius from current tool options (allows real-time updates) */
        ToolOptions* current_opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
        if (current_opts) {
            state->smooth_mode = current_opts->ellipse_select_smooth;
            state->feather_radius = current_opts->ellipse_select_feather;
        }

        /* Determine if we should show feathered outline
           Only show feathering after mouse is released (not dragging) to improve performance */
        gboolean show_feathered = (state->smooth_mode == SELECTION_SMOOTH_FEATHERED &&
                                   state->feather_radius > 0.0f &&
                                   !state->is_dragging);

        if (show_feathered) {
            /* Feathered-preview cache — see tool_rect_select.c for full explanation.
             * Same logic; fill uses an ellipse point-in-test instead of a rectangle. */
            gboolean cache_valid =
                (state->preview_cache          != NULL                    &&
                 state->cache_rect_x           == rect_x                  &&
                 state->cache_rect_y           == rect_y                  &&
                 state->cache_rect_w           == rect_w                  &&
                 state->cache_rect_h           == rect_h                  &&
                 state->cache_feather_radius   == state->feather_radius   &&
                 state->cache_smooth_mode      == state->smooth_mode      &&
                 state->cache_combine_mode     == state->combine_mode);

            if (!cache_valid) {
                if (state->preview_cache) {
                    selection_mask_free(state->preview_cache);
                    state->preview_cache = NULL;
                }

                int pad = (int)ceilf((float)state->feather_radius) + 2;
                int mask_x = rect_x - pad;
                int mask_y = rect_y - pad;
                int mask_x2 = rect_x + rect_w + pad;
                int mask_y2 = rect_y + rect_h + pad;
                if (mask_x < 0)                   mask_x = 0;
                if (mask_y < 0)                   mask_y = 0;
                if (mask_x2 > (int)doc->width)    mask_x2 = (int)doc->width;
                if (mask_y2 > (int)doc->height)   mask_y2 = (int)doc->height;
                int mask_w = mask_x2 - mask_x;
                int mask_h = mask_y2 - mask_y;

                SelectionMask* preview_mask = selection_mask_new_bounded(mask_x, mask_y, mask_w, mask_h);
                Selection* preview_sel = selection_new(rect_x, rect_y, rect_w, rect_h,
                                                       SELECTION_COMBINE_NEW,
                                                       state->smooth_mode,
                                                       (float)state->feather_radius);
                if (preview_sel) {
                    int stride = preview_mask->stride;
                    preview_sel->mask = g_malloc0(stride * mask_h);

                    gdouble cx = rect_x + rect_w / 2.0;
                    gdouble cy = rect_y + rect_h / 2.0;
                    gdouble rx = rect_w / 2.0;
                    gdouble ry = rect_h / 2.0;

                    /* Scanline rasterization: for each row, analytically compute the
                     * horizontal span of pixels whose center lies inside the ellipse,
                     * then fill with a single memset — O(rect_h) instead of O(rect_h*rect_w).
                     *
                     * Pixel col is inside iff (col+0.5 - cx)^2/rx^2 + norm_dy^2 <= 1
                     * => (col+0.5) ∈ [cx - x_half, cx + x_half]
                     * => col ∈ [ceil(cx - x_half - 0.5), floor(cx + x_half - 0.5)]  */
                    if (rx > 0.0 && ry > 0.0) {
                        int row_end = rect_y + rect_h;
                        if (row_end > (int)doc->height) row_end = (int)doc->height;
                        for (int row = rect_y; row < row_end; row++) {
                            int local_row = row - mask_y;
                            if (local_row < 0 || local_row >= mask_h) continue;
                            gdouble norm_dy = (row + 0.5 - cy) / ry;
                            gdouble rem     = 1.0 - norm_dy * norm_dy;
                            if (rem < 0.0) continue;
                            gdouble x_half  = rx * sqrt(rem);
                            int x_left  = (int)ceil (cx - x_half - 0.5);
                            int x_right = (int)floor(cx + x_half - 0.5);
                            if (x_left  < rect_x)           x_left  = rect_x;
                            if (x_right >= rect_x + rect_w) x_right = rect_x + rect_w - 1;
                            if (x_right >= (int)doc->width) x_right = (int)doc->width - 1;
                            int local_x0 = x_left  - mask_x;
                            int local_x1 = x_right - mask_x;
                            if (local_x0 < 0)       local_x0 = 0;
                            if (local_x1 >= mask_w) local_x1 = mask_w - 1;
                            if (local_x0 <= local_x1)
                                memset(&preview_sel->mask[local_row * stride + local_x0],
                                       255, (size_t)(local_x1 - local_x0 + 1));
                        }
                    }
                    selection_mask_add_selection(preview_mask, preview_sel);
                    selection_unref(preview_sel);
                    selection_mask_get_surface(preview_mask);
                }

                state->preview_cache        = preview_mask;
                state->cache_rect_x         = rect_x;
                state->cache_rect_y         = rect_y;
                state->cache_rect_w         = rect_w;
                state->cache_rect_h         = rect_h;
                state->cache_feather_radius = state->feather_radius;
                state->cache_smooth_mode    = state->smooth_mode;
                state->cache_combine_mode   = state->combine_mode;
            }

            if (state->preview_cache) {
                int animation_phase = (current_opts && current_opts->ellipse_select_animate) ? state->animation_phase : 0;
                selection_mask_render_outline(cr, state->preview_cache, animation_phase, zoom, TRUE);
            }
        } else {
            /* Draw hard outline (no feathering) - faster for active dragging */
            int animation_phase = (state->is_editing && current_opts && current_opts->ellipse_select_animate) ? state->animation_phase : 0;
            gdouble animation_offset = (gdouble)animation_phase;
            selection_draw_marching_ants_ellipse(cr, rect_x, rect_y, rect_w, rect_h, 0.0, animation_offset, zoom);
        }

        /* Draw corner resize handles only in edit mode (crop tool style) */
        if (state->is_editing) {
            gdouble handle_size = 12.0 / zoom;
            gdouble half_handle = handle_size / 2.0;
            gdouble handle_line_width = 1.0 / zoom;
            if (handle_line_width < 0.5)
                handle_line_width = 0.5;

#define SEL_SNAP(c) (floor((c)*zoom + 0.5) / zoom)

            cairo_set_dash(cr, NULL, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

            gdouble corners[4][2] = {
                {SEL_SNAP(rect_x), SEL_SNAP(rect_y)},
                {SEL_SNAP(rect_x + rect_w), SEL_SNAP(rect_y)},
                {SEL_SNAP(rect_x), SEL_SNAP(rect_y + rect_h)},
                {SEL_SNAP(rect_x + rect_w), SEL_SNAP(rect_y + rect_h)}};

            for (gint i = 0; i < 4; i++) {
                gdouble cx = corners[i][0];
                gdouble cy = corners[i][1];
                gboolean hovered = (state->hovered_handle == i);
                gdouble hx = SEL_SNAP(cx - half_handle);
                gdouble hy = SEL_SNAP(cy - half_handle);
                gdouble hw = SEL_SNAP(cx + half_handle) - hx;
                gdouble hh = SEL_SNAP(cy + half_handle) - hy;

                cairo_rectangle(cr, hx, hy, hw, hh);
                cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
                cairo_set_line_width(cr, handle_line_width * 3.0);
                cairo_stroke_preserve(cr);
                cairo_set_source_rgba(cr, hovered ? 0.0 : 1.0, hovered ? 0.5 : 1.0, 1.0, 1.0);
                cairo_set_line_width(cr, handle_line_width);
                cairo_stroke(cr);
            }
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

#undef SEL_SNAP
        }

        cairo_restore(cr);
    }
}
