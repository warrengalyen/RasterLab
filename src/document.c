#include "document.h"
#include "app/settings.h"
#include "command.h"
#include "io/image_io.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "render/tile_thread_pool.h"
#include "render/tile_worker.h"
#include "selection.h"
#include "selection/selection_mask.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "tools.h"
#include "tools/tool_move.h"
#include "tools/tool_rect_select.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include "undo/undo_disk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static void on_scroll_adjustment_changed(GtkAdjustment* adjustment, gpointer user_data);
static void on_scrolled_window_adjustment_notify(GObject* object, GParamSpec* pspec, gpointer user_data);
static gboolean on_viewport_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_viewport_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_viewport_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_viewport_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);

/**
 * Animation timer callback - updates selection marching ants
 */
static gboolean on_selection_animation_timer(gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc) {
        return FALSE; /* Stop timer if document is gone */
    }

    /* Check if drawing_area is still valid - if NULL, document is being destroyed */
    if (!doc->drawing_area || !GTK_IS_WIDGET(doc->drawing_area)) {
        doc->selection_animation_timer_id = 0; /* Clear timer ID */
        return FALSE;                          /* Stop timer if widget is gone */
    }

    /* Check if animation is enabled for rect select tool */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    gboolean animate_enabled = opts ? tool_options_get_rect_select_animate(opts) : TRUE;

    /* Update animation for mask-based selection if animation is enabled */
    if (animate_enabled && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        /* Advance animation phase (0-3 for 4-pixel dashes) */
        doc->selection_animation_phase = (doc->selection_animation_phase + 1) % 4;
    }

    /* Queue redraw to show animation */
    if (doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Keep timer running - it needs to stay active for when selections are created */
    return TRUE;
}

/**
 * Callback for scroll adjustment changes - triggers redraw
 */
static void on_scroll_adjustment_changed(GtkAdjustment* adjustment, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    LayersPanel* layers_panel;

    (void)adjustment; /* Unused */

    if (doc && doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
        /* Invalidate the entire drawing area to trigger redraw */
        gtk_widget_queue_draw(doc->drawing_area);

        /* Update overview widget selection rectangle when scrolling */
        layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(doc->drawing_area), "layers_panel");
        if (layers_panel && layers_panel->overview_widget && layers_panel->current_doc == doc) {
            gtk_widget_queue_draw(layers_panel->overview_widget);
        }
    }
}

/**
 * Callback for when scroll adjustments are created/updated
 */
static void on_scrolled_window_adjustment_notify(GObject* object, GParamSpec* pspec, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    GtkScrolledWindow* scrolled_window = GTK_SCROLLED_WINDOW(object);
    GtkAdjustment* adj;

    (void)pspec; /* Unused */

    if (!doc || !scrolled_window) {
        return;
    }

    /* Get the adjustment that was just created/updated */
    if (g_strcmp0(pspec->name, "hadjustment") == 0) {
        adj = gtk_scrolled_window_get_hadjustment(scrolled_window);
    } else if (g_strcmp0(pspec->name, "vadjustment") == 0) {
        adj = gtk_scrolled_window_get_vadjustment(scrolled_window);
    } else {
        return;
    }

    /* Connect to value-changed signal if adjustment exists and not already connected */
    if (adj && !g_signal_handler_find(adj, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                      G_CALLBACK(on_scroll_adjustment_changed), doc)) {
        g_signal_connect(adj, "value-changed",
                         G_CALLBACK(on_scroll_adjustment_changed), doc);
    }
}

/**
 * Forward declarations for mouse event handlers
 */
static gboolean on_drawing_area_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_drawing_area_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_drawing_area_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_drawing_area_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
static gboolean on_drawing_area_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);

/**
 * Drawing area draw callback
 */
/**
 * Drawing area draw callback - TILE-BASED RENDERING
 *
 * OLD BEHAVIOR: Drew entire composite surface at once
 * NEW BEHAVIOR: Loops over tiles and draws only visible tiles
 *
 * This dramatically improves performance for large images by:
 * - Only compositing dirty tiles instead of entire surface
 * - Only drawing tiles that are visible in the viewport
 * - Caching composited tiles to avoid recompositing
 */
static gboolean on_drawing_area_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    double x1, y1, x2, y2;
    gint clip_width, clip_height;
    gint viewport_x, viewport_y, viewport_w, viewport_h;
    gint start_tile_x, start_tile_y, end_tile_x, end_tile_y;
    gint tx, ty;
    Tile* tile;
    gdouble zoom;

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so just draw empty background */
    if (!doc || !doc->drawing_area) {
        cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
        clip_width = (gint)(x2 - x1);
        clip_height = (gint)(y2 - y1);
        draw_checkered_background(cr, clip_width, clip_height);
        return FALSE;
    }

    /* Process completed tiles from Cairo-safe worker pool
       Workers have finished compositing into pixel_buffer, now upload to Cairo */
    if (doc->tile_worker_pool) {
        guint uploaded = tile_worker_pool_process_uploads(doc->tile_worker_pool);
        if (uploaded > 0) {
            g_debug("Uploaded %u tiles to Cairo surfaces", uploaded);
        }
    }

    /* Poll completed tiles from legacy thread pool (if enabled) */
    if (doc->tile_thread_pool) {
        CompletedTile completed;
        gint updated_count = 0;

        while (tile_thread_pool_pop_completed(doc->tile_thread_pool, &completed)) {
            /* Safety: check tile still exists and surface is valid */
            if (!completed.tile || !doc->tile_grid || !completed.surface) {
                if (completed.surface) {
                    cairo_surface_destroy(completed.surface);
                }
                continue;
            }

            /* Safety: verify Cairo surface is valid */
            if (cairo_surface_status(completed.surface) != CAIRO_STATUS_SUCCESS) {
                g_warning("Discarding invalid Cairo surface from worker thread");
                cairo_surface_destroy(completed.surface);
                continue;
            }

            /* Apply result if generation ID matches (not stale) */
            if (tile_apply_completed_result(completed.tile,
                                            completed.surface,
                                            completed.generation_id)) {
                /* Successfully applied, queue redraw of this tile */
                gint draw_x = completed.tile->px;
                gint draw_y = completed.tile->py;
                gint draw_w = completed.tile->w;
                gint draw_h = completed.tile->h;

                gtk_widget_queue_draw_area(widget,
                                           (gint)(draw_x * doc->zoom_factor),
                                           (gint)(draw_y * doc->zoom_factor),
                                           (gint)(draw_w * doc->zoom_factor),
                                           (gint)(draw_h * doc->zoom_factor));
                updated_count++;
            } else {
                /* Result was stale, discard it */
                cairo_surface_destroy(completed.surface);
            }
        }

        if (updated_count > 0) {
            g_debug("Applied %d completed tiles to main cache", updated_count);
        }
    }

    zoom = doc->zoom_factor;

    /* Get the clip region to determine what needs to be drawn */
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    clip_width = (gint)(x2 - x1);
    clip_height = (gint)(y2 - y1);

    /* Draw the document if image is loaded */
    if (doc->layers && g_list_length(doc->layers) > 0) {
        /* Calculate viewport in document coordinates (unscaled) with proper rounding */
        /* Use proper rounding to avoid pixel misalignment that causes visible lines when scrolling */
        viewport_x = (gint)(round(x1 / zoom));
        viewport_y = (gint)(round(y1 / zoom));
        viewport_w = (gint)(round((x2 - x1) / zoom));
        viewport_h = (gint)(round((y2 - y1) / zoom));

        /* Save Cairo state before applying zoom transform */
        cairo_save(cr);

        /* Apply zoom transform */
        if (zoom != 1.0) {
            cairo_scale(cr, zoom, zoom);
        }

        /* Draw checkered background for visible area, starting from viewport position */
        /* This ensures the pattern is continuous across the entire canvas when scrolling */
        draw_checkered_background_offset(cr, viewport_x, viewport_y, viewport_w, viewport_h);

        /* When zoomed, render layers directly to avoid tile boundary artifacts */
        if (zoom != 1.0) {
            document_render_layers_at_zoom(doc, cr, viewport_x, viewport_y, viewport_w, viewport_h);
        } else {
            /* At 100% zoom, use tiles for performance */
            if (doc->tile_grid) {
                /* Composite dirty tiles before drawing */
                tile_grid_composite(doc, doc->tile_grid);

                /* Calculate which tiles are visible in viewport */
                tile_grid_pixel_to_tile(doc->tile_grid, viewport_x, viewport_y, &start_tile_x, &start_tile_y);
                tile_grid_pixel_to_tile(doc->tile_grid, viewport_x + viewport_w, viewport_y + viewport_h, &end_tile_x, &end_tile_y);

                /* Clamp to grid bounds */
                if (start_tile_x < 0)
                    start_tile_x = 0;
                if (start_tile_y < 0)
                    start_tile_y = 0;
                if (end_tile_x >= doc->tile_grid->tiles_x)
                    end_tile_x = doc->tile_grid->tiles_x - 1;
                if (end_tile_y >= doc->tile_grid->tiles_y)
                    end_tile_y = doc->tile_grid->tiles_y - 1;

                /* Draw each visible tile */
                for (ty = start_tile_y; ty <= end_tile_y; ty++) {
                    for (tx = start_tile_x; tx <= end_tile_x; tx++) {
                        tile = tile_grid_get_tile(doc->tile_grid, tx, ty);

                        if (tile && tile->surface) {
                            /* Draw tile at its document pixel position */
                            cairo_set_source_surface(cr, tile->surface, tile->px, tile->py);
                            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                            cairo_paint(cr);
                        }
                    }
                }
            }
        }

        /* Restore Cairo state after layer rendering (undoes zoom transform) */
        cairo_restore(cr);
    } else {
        /* Draw checkered background for empty canvas */
        draw_checkered_background(cr, clip_width, clip_height);
    }

    /* Draw rect select tool preview during drag */
    tool_rect_select_draw_preview(doc, cr, zoom);

    /* Render selection overlays after all content is drawn */
    if (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        selection_mask_render_outline(cr, doc->selection_mask,
                                      doc->selection_animation_phase, zoom);

        /* TEMPORARY: Visualize selection mask for debugging feathered selections */
        cairo_save(cr);
        if (zoom != 1.0) {
            cairo_scale(cr, zoom, zoom);
        }
        // render_utils_visualize_selection_mask(cr, doc->selection_mask);
        cairo_restore(cr);
    }

    return FALSE;
}

/**
 * Viewport draw callback - draws overlays on top of everything (including outside canvas)
 */
static gboolean on_viewport_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    GtkAllocation drawing_area_alloc;

    (void)widget; /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get drawing area allocation to find its position in viewport */
    gtk_widget_get_allocation(doc->drawing_area, &drawing_area_alloc);

    /* Save cairo state */
    cairo_save(cr);

    /* Translate to drawing area position within viewport */
    cairo_translate(cr, drawing_area_alloc.x, drawing_area_alloc.y);

    /* Draw move tool outline overlay (in drawing area coordinates) */
    tool_move_draw_preview(doc, cr, doc->zoom_factor);

    cairo_restore(cr);

    return FALSE; /* Let other handlers run */
}

/**
 * Convert widget coordinates to image coordinates
 */
static void widget_to_image_coords(ImageDocument* doc, gdouble widget_x, gdouble widget_y,
                                   gint* image_x, gint* image_y) {
    gdouble scaled_x, scaled_y;

    if (!doc || !image_x || !image_y) {
        return;
    }

    /* Unscale by zoom factor */
    scaled_x = widget_x / doc->zoom_factor;
    scaled_y = widget_y / doc->zoom_factor;

    /* Round to nearest integer to prevent pixel shifting */
    *image_x = (gint)(scaled_x + 0.5);
    *image_y = (gint)(scaled_y + 0.5);
}

/**
 * Drawing area button press callback
 */
static gboolean on_drawing_area_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    gpointer ctx_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get app context from drawing area data and extract tool registry */
    ctx_data = g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (!ctx_data) {
        return FALSE;
    }

    /* Access tool registry through doc if available */
    /* This is a minimal integration - in real code, pass registry more directly */

    /* For now, use a safer approach: store tool_registry directly */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_down) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_down(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area button release callback
 */
static gboolean on_drawing_area_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_up) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_up(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area motion notify callback
 */
static gboolean on_drawing_area_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;
    AppContext* ctx = NULL;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = 0; /* No button pressed during motion */
    tool_event.state = event->state;

    /* Update cursor position in statusbar */
    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx) {
        ui_update_cursor_position(ctx, doc, tool_event.x, tool_event.y);
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_move) {
        return FALSE;
    }

    /* Call tool handler */
    active_tool->mouse_move(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area enter notify callback - set cursor for active tool
 */
static gboolean on_drawing_area_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    GdkWindow* window;

    (void)event; /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Grab focus for keyboard events */
    gtk_widget_grab_focus(doc->drawing_area);

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->cursor) {
        return FALSE;
    }

    /* Set cursor on drawing area window */
    window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        gdk_window_set_cursor(window, active_tool->cursor);
    }

    return FALSE;
}

/**
 * Drawing area leave notify callback - no longer used for hiding position
 * (position is now hidden when leaving viewport)
 */
static gboolean on_drawing_area_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    (void)widget;    /* Unused */
    (void)event;     /* Unused */
    (void)user_data; /* Unused */

    /* No-op: cursor position tracking is handled at viewport level */
    return FALSE;
}

/**
 * Viewport leave notify callback - hide cursor position
 */
static gboolean on_viewport_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    AppContext* ctx = NULL;

    (void)widget; /* Unused */
    (void)event;  /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Hide cursor position in statusbar */
    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx) {
        ui_hide_cursor_position(ctx);
    }

    return FALSE;
}

/**
 * Viewport button press callback - for hand tool panning anywhere in viewport
 */
static gboolean on_viewport_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check */
    if (!doc || !doc->drawing_area || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_HAND || !active_tool->mouse_down) {
        return FALSE; /* Only handle hand tool */
    }

    /* For hand tool, pass viewport coordinates directly */
    /* Viewport coordinates are relative to the viewport widget and are stable */
    tool_event.x = (gint)event->x;
    tool_event.y = (gint)event->y;

    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_down(active_tool, doc, &tool_event);

    return TRUE;
}

/**
 * Viewport button release callback - for hand tool panning
 */
static gboolean on_viewport_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check */
    if (!doc || !doc->drawing_area || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_HAND || !active_tool->mouse_up) {
        return FALSE; /* Only handle hand tool */
    }

    /* For hand tool, pass viewport coordinates directly */
    /* Viewport coordinates are relative to the viewport widget and are stable */
    tool_event.x = (gint)event->x;
    tool_event.y = (gint)event->y;

    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_up(active_tool, doc, &tool_event);

    return TRUE;
}

/**
 * Viewport motion notify callback - for hand tool panning and cursor position tracking
 */
static gboolean on_viewport_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;
    AppContext* ctx = NULL;
    GtkAllocation drawing_area_alloc;
    gint image_x, image_y;
    gdouble widget_x, widget_y;

    /* Safety check */
    if (!doc || !doc->drawing_area || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get drawing area allocation to find its position in viewport */
    gtk_widget_get_allocation(doc->drawing_area, &drawing_area_alloc);

    /* Convert viewport coordinates to drawing area coordinates
     * The drawing area is centered in the viewport, so we need to account for its offset */
    widget_x = event->x - drawing_area_alloc.x;
    widget_y = event->y - drawing_area_alloc.y;

    /* Convert to image coordinates (can be negative if outside canvas) */
    widget_to_image_coords(doc, widget_x, widget_y, &image_x, &image_y);

    /* Update cursor position in statusbar */
    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx) {
        ui_update_cursor_position(ctx, doc, image_x, image_y);
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_HAND || !active_tool->mouse_move) {
        return FALSE; /* Only handle hand tool */
    }

    /* For hand tool, pass viewport coordinates directly */
    /* Viewport coordinates are relative to the viewport widget and are stable */
    tool_event.x = (gint)event->x;
    tool_event.y = (gint)event->y;

    tool_event.button = 0; /* No button pressed during motion */
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_move(active_tool, doc, &tool_event);

    return TRUE;
}

/**
 * Create a new image document
 */
ImageDocument* document_new(const gchar* filename, gboolean create_worker_pool) {
    ImageDocument* doc = (ImageDocument*)g_malloc(sizeof(ImageDocument));

    doc->filename = g_strdup(filename);
    doc->file_path = NULL;
    doc->modified = FALSE;
    doc->drawing_area = NULL;
    doc->scrolled_window = NULL;
    doc->viewport = NULL;

    /* Initialize image metadata */
    doc->width = 0;
    doc->height = 0;
    doc->channels = 0;
    doc->bit_depth = 0;
    doc->has_alpha = FALSE;

    /* Initialize rendering pipeline */
    doc->layers = NULL;
    doc->selected_layer = NULL;
    doc->composite_surface = NULL;
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    doc->tile_grid = NULL;        /* Will be created when image is loaded */
    doc->tile_thread_pool = NULL; /* Will be created when image is loaded */
    doc->zoom_factor = 1.0;
    doc->zoom_mode = 0; /* 0=manual zoom */

    /* Initialize mask-based selection (empty) */
    /* Will be allocated when document dimensions are known */
    doc->selection_mask = NULL;
    doc->selection_animation_phase = 0;
    doc->selection_animation_timer_id = 0; /* Timer ID (0 means not active) */

    /* Create tile worker pool if requested (for on-screen rendering) */
    if (create_worker_pool) {
        doc->tile_worker_pool = tile_worker_pool_create(0);
        if (!doc->tile_worker_pool) {
            g_warning("Failed to create tile worker pool, will use single-threaded compositing");
        }
    } else {
        doc->tile_worker_pool = NULL;
    }

    /* Initialize undo/redo stacks (max 50 undo steps) */
    doc->undo_stack = command_stack_new(50);
    doc->redo_stack = command_stack_new(50);
    doc->undo_journal = NULL; /* Will be created when settings are available */

    return doc;
}

/**
 * Free an image document
 */
void document_free(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->filename) {
        g_free(doc->filename);
    }

    if (doc->file_path) {
        g_free(doc->file_path);
    }

    /* Mark document as being freed by setting layers to NULL first
     * This allows command destroy callbacks to detect that the document is being freed */
    GList* layers_to_free = doc->layers;
    doc->layers = NULL;

    /* Free undo journal BEFORE freeing undo stacks */
    if (doc->undo_journal) {
        undo_journal_free(doc->undo_journal);
        doc->undo_journal = NULL;
    }

    /* Free undo/redo stacks BEFORE freeing layers
     * This ensures command destroy callbacks can safely check layer ownership
     * and free any layers they own (e.g., layers in undo state)
     * Note: doc->layers is now NULL, so destroy callbacks know the document is being freed */
    if (doc->undo_stack) {
        command_stack_free(doc->undo_stack);
        doc->undo_stack = NULL;
    }
    if (doc->redo_stack) {
        command_stack_free(doc->redo_stack);
        doc->redo_stack = NULL;
    }

    /* Free all layers */
    for (GList* iter = layers_to_free; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(layers_to_free);

    /* Free composite surface */
    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }

    /* Free tile grid */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }

    /* Shutdown Cairo-safe worker pool before freeing document */
    if (doc->tile_worker_pool) {
        g_message("Shutting down tile worker pool...");
        tile_worker_pool_destroy(doc->tile_worker_pool);
        doc->tile_worker_pool = NULL;
    }

    /* Shutdown legacy thread pool (if enabled) */
    if (doc->tile_thread_pool) {
        g_message("Shutting down legacy tile thread pool...");
        tile_thread_pool_destroy(doc->tile_thread_pool);
        doc->tile_thread_pool = NULL;
    }

    /* Remove selection animation timer if it's still running */
    if (doc->selection_animation_timer_id > 0) {
        g_source_remove(doc->selection_animation_timer_id);
        doc->selection_animation_timer_id = 0;
    }

    /* Free selection mask if it exists */
    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
        doc->selection_mask = NULL;
    }

    g_free(doc);
}

/**
 * Key press event handler for modifiers used by rectangular select tool
 */
static gboolean on_drawing_area_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    ToolRegistry* tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    Tool* active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_RECT_SELECT) {
        return FALSE;
    }

    /* Check if this is a modifier key press (Shift or Alt) */
    gboolean is_shift = (event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R);
    gboolean is_alt = (event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R);

    if (!is_shift && !is_alt) {
        return FALSE;
    }

    /* Now check the new state AFTER this key is pressed */
    /* We need to check what modifiers will be active */
    gboolean shift_will_be_pressed = (event->state & GDK_SHIFT_MASK) != 0 || is_shift;
    gboolean alt_will_be_pressed = (event->state & GDK_MOD1_MASK) != 0 || is_alt;

    SelectionCombineMode temp_mode = SELECTION_COMBINE_NEW;

    /* Determine temporary mode based on modifier state */
    if (shift_will_be_pressed && alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_INTERSECT;
    } else if (shift_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_ADD;
    } else if (alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_SUBTRACT;
    } else {
        temp_mode = SELECTION_COMBINE_NEW;
    }

    /* Temporarily change the tool options mode */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (opts) {
        opts->rect_select_combine = temp_mode;
    }

    /* Update UI buttons to show the temporary mode */
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx && ctx->tool_options_panel) {
        tool_options_panel_set_combine_mode(ctx->tool_options_panel, temp_mode);
    }

    return FALSE; /* Let GTK handle the event normally */
}

/**
 * Key release event handler for modifiers used by rectangular select tool
 */
static gboolean on_drawing_area_key_release(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    ToolRegistry* tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    Tool* active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_RECT_SELECT) {
        return FALSE;
    }

    /* Check if this is a modifier key release (Shift or Alt) */
    gboolean is_shift = (event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R);
    gboolean is_alt = (event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R);

    if (!is_shift && !is_alt) {
        return FALSE;
    }

    /* Now check the state AFTER this key is released */
    /* event->state still contains the OLD state (before release), so we need to subtract the released key */
    gboolean shift_will_be_pressed = (event->state & GDK_SHIFT_MASK) != 0 && !is_shift;
    gboolean alt_will_be_pressed = (event->state & GDK_MOD1_MASK) != 0 && !is_alt;

    SelectionCombineMode temp_mode = SELECTION_COMBINE_NEW;

    /* Determine temporary mode based on remaining modifier state */
    if (shift_will_be_pressed && alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_INTERSECT;
    } else if (shift_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_ADD;
    } else if (alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_SUBTRACT;
    } else {
        temp_mode = SELECTION_COMBINE_NEW;
    }

    /* Restore the original or new combine mode */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    if (opts) {
        opts->rect_select_combine = temp_mode;
    }

    /* Update UI buttons to show the new mode */
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx && ctx->tool_options_panel) {
        tool_options_panel_set_combine_mode(ctx->tool_options_panel, temp_mode);
    }

    return FALSE; /* Let GTK handle the event normally */
}

/**
 * Create a drawing area widget for the document
 */
GtkWidget* document_create_drawing_area(ImageDocument* doc) {
    GtkWidget* scrolled_window;
    GtkWidget* viewport;
    GtkWidget* drawing_area;

    /* Create scrolled window */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    /* Allow viewport to shrink and center content */
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),
                                        GTK_SHADOW_NONE);

    /* Create a viewport to hold the drawing area (allows centering) */
    viewport = gtk_viewport_new(NULL, NULL);

    /* Set a name on the viewport for CSS targeting */
    gtk_widget_set_name(viewport, "canvas-viewport");

    /* Set default canvas background color on viewport using CSS (will be updated when AppContext is available) */
    gchar* css = g_strdup("#canvas-viewport { background-color: rgb(160, 160, 160); }");
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    g_free(css);

    /* Store provider in widget data so it can be updated later */
    g_object_set_data_full(G_OBJECT(viewport), "canvas_bg_provider", provider, g_object_unref);

    GtkStyleContext* style_context = gtk_widget_get_style_context(viewport);
    gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Store viewport reference in document for later updates */
    doc->viewport = viewport;

    gtk_container_add(GTK_CONTAINER(scrolled_window), viewport);
    gtk_widget_show(viewport);

    /* Create drawing area */
    drawing_area = gtk_drawing_area_new();
    /* Start with default size - will be updated when image loads */
    gtk_widget_set_size_request(drawing_area, 800, 600);

    /* Prevent drawing area from expanding to fill viewport */
    gtk_widget_set_hexpand(drawing_area, FALSE);
    gtk_widget_set_vexpand(drawing_area, FALSE);

    /* Align to top-left corner */
    gtk_widget_set_halign(drawing_area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(drawing_area, GTK_ALIGN_CENTER);

    gtk_container_add(GTK_CONTAINER(viewport), drawing_area);
    gtk_widget_show(drawing_area);

    /* Enable mouse events on drawing area */
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_ENTER_NOTIFY_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    /* Connect draw signal */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), doc);

    /* Connect mouse event signals */
    g_signal_connect(drawing_area, "button-press-event",
                     G_CALLBACK(on_drawing_area_button_press), doc);
    g_signal_connect(drawing_area, "button-release-event",
                     G_CALLBACK(on_drawing_area_button_release), doc);
    g_signal_connect(drawing_area, "motion-notify-event",
                     G_CALLBACK(on_drawing_area_motion_notify), doc);
    g_signal_connect(drawing_area, "enter-notify-event",
                     G_CALLBACK(on_drawing_area_enter_notify), doc);
    g_signal_connect(drawing_area, "leave-notify-event",
                     G_CALLBACK(on_drawing_area_leave_notify), doc);

    /* Enable mouse events on viewport for hand tool panning and cursor position tracking */
    gtk_widget_set_events(viewport,
                          gtk_widget_get_events(viewport) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    /* Connect viewport mouse events for hand tool and cursor tracking */
    g_signal_connect(viewport, "button-press-event",
                     G_CALLBACK(on_viewport_button_press), doc);
    g_signal_connect(viewport, "button-release-event",
                     G_CALLBACK(on_viewport_button_release), doc);
    g_signal_connect(viewport, "motion-notify-event",
                     G_CALLBACK(on_viewport_motion_notify), doc);
    g_signal_connect(viewport, "leave-notify-event",
                     G_CALLBACK(on_viewport_leave_notify), doc);

    /* Connect viewport draw signal for overlays (move tool outline, etc.) that extend beyond canvas */
    g_signal_connect_after(viewport, "draw",
                           G_CALLBACK(on_viewport_draw), doc);

    /* Store references in document */
    doc->drawing_area = drawing_area;
    doc->scrolled_window = scrolled_window;

    /* Start animation timer for selection marching ants (using standard speed) */
    doc->selection_animation_timer_id = g_timeout_add(ANT_DASH_SPEED_SLOW, on_selection_animation_timer, doc);

    /* Connect to scroll adjustment signals to trigger redraws when scrolling */
    /* Note: Adjustments might be NULL initially, so we'll connect when they're created */
    if (GTK_IS_SCROLLED_WINDOW(scrolled_window)) {
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window));

        if (hadj) {
            g_signal_connect(hadj, "value-changed",
                             G_CALLBACK(on_scroll_adjustment_changed), doc);
        }
        if (vadj) {
            g_signal_connect(vadj, "value-changed",
                             G_CALLBACK(on_scroll_adjustment_changed), doc);
        }

        /* Also connect to notify signal to catch when adjustments are created */
        g_signal_connect(scrolled_window, "notify::hadjustment",
                         G_CALLBACK(on_scrolled_window_adjustment_notify), doc);
        g_signal_connect(scrolled_window, "notify::vadjustment",
                         G_CALLBACK(on_scrolled_window_adjustment_notify), doc);
    }

    /* Enable key events for modifier key handling (especially for rect select tool hotkeys) */
    /* Note: We need to enable focus on the drawing area to receive key events */
    gtk_widget_set_can_focus(drawing_area, TRUE);
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_KEY_PRESS_MASK |
                              GDK_KEY_RELEASE_MASK);

    /* Connect key event signals */
    g_signal_connect(drawing_area, "key-press-event",
                     G_CALLBACK(on_drawing_area_key_press), doc);
    g_signal_connect(drawing_area, "key-release-event",
                     G_CALLBACK(on_drawing_area_key_release), doc);

    gtk_widget_show(scrolled_window);

    return scrolled_window;
}

/**
 * Set the document as modified
 */
void document_set_modified(ImageDocument* doc, gboolean modified) {
    if (!doc) {
        return;
    }

    doc->modified = modified;
}

/**
 * Get the document filename
 */
const gchar* document_get_filename(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    return doc->filename;
}

/**
 * Initialize document rendering structures after image dimensions are set
 * This should be called after loading an image
 */
static gboolean document_init_rendering_structures(ImageDocument* doc) {
    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    /* Free old tile grid if exists */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }

    /* Create tile grid for tile-based rendering
       Tile size of 128 is a good balance between memory and performance */
    doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid");
        return FALSE;
    }

    /* Create mask-based selection (initially empty) */
    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
    }
    doc->selection_mask = selection_mask_new(doc->width, doc->height);
    if (!doc->selection_mask) {
        g_warning("Failed to create selection mask");
        return FALSE;
    }

    /* Create Cairo-safe tile worker pool for asynchronous rendering if not already created */
    /* Workers composite into pixel buffers only, main thread handles Cairo surfaces */
    if (!doc->tile_worker_pool) {
        doc->tile_worker_pool = tile_worker_pool_create(0);
        if (!doc->tile_worker_pool) {
            g_warning("Failed to create tile worker pool, will use single-threaded compositing");
        } else {
            g_message("Tile compositing: Using worker threads (Cairo-safe pixel buffer approach)");
        }
    }

    /* Legacy thread pool (disabled - kept for reference) */
    doc->tile_thread_pool = NULL;

    return TRUE;
}

/**
 * Load an image from file into the document using the plugin system
 */
gboolean document_load_image_from_file(ImageDocument* doc, const gchar* file_path) {
    gboolean result;

    if (!doc || !file_path) {
        return FALSE;
    }

    /* Free old layers if exists */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Load image using plugin system */
    result = image_io_load(doc, file_path);

    if (!result) {
        return FALSE;
    }

    /* Initialize rendering structures after dimensions are set */
    if (!document_init_rendering_structures(doc)) {
        return FALSE;
    }

    /* Ensure we have at least one layer selected */
    ImageLayer* layer_0 = document_get_layer(doc, 0);
    if (layer_0) {
        document_set_selected_layer(doc, layer_0);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    /* Update drawing area size to match image dimensions */
    if (doc->drawing_area) {
        /* Set exact size for the drawing area based on image dimensions */
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);

        /* Queue redraw to display the image */
        gtk_widget_queue_draw(doc->drawing_area);
    }

    return TRUE;
}

/**
 * Get image width
 */
guint document_get_width(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return doc->width;
}

/**
 * Get image height
 */
guint document_get_height(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return doc->height;
}

/**
 * Get image metadata string
 */
gchar* document_get_image_info(ImageDocument* doc) {
    if (!doc || doc->width == 0) {
        return g_strdup("No image loaded");
    }

    return g_strdup_printf("%ux%u, %d-bit %s%s (zoom: %.0f%%)",
                           doc->width, doc->height,
                           doc->bit_depth,
                           doc->channels == 3 ? "RGB" : "RGBA",
                           doc->has_alpha ? " (with alpha)" : "",
                           doc->zoom_factor * 100.0);
}

/**
 * Set zoom factor for the document
 */
void document_set_zoom(ImageDocument* doc, gdouble zoom_factor) {
    if (!doc) {
        return;
    }

    /* Clamp zoom to reasonable range (1% - 3200%) */
    if (zoom_factor < 0.01) {
        zoom_factor = 0.01;
    } else if (zoom_factor > 32.0) {
        zoom_factor = 32.0;
    }

    doc->zoom_factor = zoom_factor;

    /* Update drawing area and trigger redraw */
    if (doc->drawing_area) {
        gint scaled_width = (gint)(doc->width * zoom_factor);
        gint scaled_height = (gint)(doc->height * zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, scaled_width, scaled_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Get current zoom factor
 */
gdouble document_get_zoom(ImageDocument* doc) {
    if (!doc) {
        return 1.0;
    }

    return doc->zoom_factor;
}

/**
 * Execute an undo command
 */
gboolean document_undo(ImageDocument* doc) {
    Command* cmd;

    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->undo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the undo */
    command_undo(cmd, (struct ImageDocument*)doc);

    /* Push to redo stack */
    command_stack_push(doc->redo_stack, cmd);

    /* Clear journal's redo stack if it exists (since we're creating a new branch) */
    if (doc->undo_journal) {
        undo_journal_clear_redo(doc->undo_journal);
    }

    /* Mark composite for redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Mark document as modified after undo */
    doc->modified = TRUE;

    return TRUE;
}

/**
 * Execute a redo command
 */
gboolean document_redo(ImageDocument* doc) {
    Command* cmd;

    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->redo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the redo (apply again) */
    command_execute(cmd, (struct ImageDocument*)doc);

    /* Push back to undo stack */
    command_stack_push(doc->undo_stack, cmd);

    /* Mark composite for redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Mark document as modified after redo */
    doc->modified = TRUE;

    return TRUE;
}

/**
 * Check if undo is available
 */
gboolean document_can_undo(ImageDocument* doc) {
    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->undo_stack);
}

/**
 * Check if redo is available
 */
gboolean document_can_redo(ImageDocument* doc) {
    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->redo_stack);
}

/**
 * Save document as PNG with alpha channel
 */
gboolean document_save_as_png(ImageDocument* doc, const gchar* filename) {
    cairo_surface_t* composite;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result = FALSE;

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_png");
        return FALSE;
    }

    /* Get a fresh composite surface for export (includes all layers) */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Convert to pixbuf with alpha channel */
    pixbuf = cairo_surface_to_pixbuf(composite, TRUE);
    if (!pixbuf) {
        g_warning("Failed to convert surface to pixbuf");
        return FALSE;
    }

    /* Verify pixbuf has alpha channel (should always be true when keep_alpha=TRUE) */
    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        g_warning("Pixbuf does not have alpha channel - this should not happen");
    }

    /* Save as PNG with alpha channel preserved
       gdk_pixbuf_save automatically saves alpha if pixbuf has it */
    result = gdk_pixbuf_save(pixbuf, filename, "png", &error, NULL);

    if (!result) {
        g_warning("Failed to save PNG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        // printf("Saved PNG: %s\n", filename);
        /* Update document path */
        if (doc->file_path) {
            g_free(doc->file_path);
        }
        doc->file_path = g_strdup(filename);
        doc->modified = FALSE;
    }

    g_object_unref(pixbuf);

    /* Clean up the export surface */
    cairo_surface_destroy(composite);

    return result;
}

/**
 * Save document as JPEG (flattened with white background)
 */
gboolean document_save_as_jpeg(ImageDocument* doc, const gchar* filename, gint quality) {
    cairo_surface_t* composite;
    cairo_surface_t* flattened;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result = FALSE;
    gchar quality_str[4];

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_jpeg");
        return FALSE;
    }

    /* Clamp quality to valid range */
    if (quality < 0)
        quality = 0;
    if (quality > 100)
        quality = 100;

    /* Get a fresh composite surface for export (includes all layers) */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Flatten to white background */
    flattened = compositor_flatten_to_white_background(composite, doc->width, doc->height);

    /* Clean up the export surface */
    cairo_surface_destroy(composite);
    if (!flattened) {
        g_warning("Failed to flatten image");
        return FALSE;
    }

    /* Convert to pixbuf (no alpha) */
    pixbuf = cairo_surface_to_pixbuf(flattened, FALSE);
    cairo_surface_destroy(flattened);

    if (!pixbuf) {
        g_warning("Failed to convert surface to pixbuf");
        return FALSE;
    }

    /* Save as JPEG with quality parameter */
    g_snprintf(quality_str, sizeof(quality_str), "%d", quality);
    result = gdk_pixbuf_save(pixbuf, filename, "jpeg", &error, "quality", quality_str, NULL);

    if (!result) {
        g_warning("Failed to save JPEG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        // printf("Saved JPEG: %s (quality=%d)\n", filename, quality);
        /* Update document path */
        if (doc->file_path) {
            g_free(doc->file_path);
        }
        doc->file_path = g_strdup(filename);
        doc->modified = FALSE;
    }

    g_object_unref(pixbuf);

    return result;
}

/**
 * Save document with auto-detection by file extension using plugin system
 * @param doc Document to save
 * @param filename Filename to save to
 * @param opts Save options (can be NULL to use defaults)
 */
gboolean document_save_as(ImageDocument* doc, const gchar* filename, const SaveOptions* opts) {
    SaveOptions default_opts;

    if (!doc || !filename) {
        return FALSE;
    }

    /* Use provided options or defaults */
    if (opts) {
        /* Use plugin system to save with provided options */
        /* Note: plugin_data should already be allocated and initialized by caller */
        return image_io_save(doc, filename, opts);
    }

    /* Set default save options */
    memset(&default_opts, 0, sizeof(SaveOptions));
    default_opts.quality = -1;           /* Use default */
    default_opts.compression_level = -1; /* Use default */
    default_opts.preserve_alpha = doc->has_alpha ? true : false;
    default_opts.flatten_layers = FALSE; /* Keep layers for now */
    default_opts.plugin_data = NULL;

    /* Use plugin system to save */
    return image_io_save(doc, filename, &default_opts);
}

/**
 * Mark document as saved
 */
void document_mark_saved(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->modified = FALSE;
    // printf("Document marked as saved\n");
}

/**
 * Check if document is dirty
 */
gboolean document_is_dirty(ImageDocument* doc) {
    if (!doc) {
        return FALSE;
    }

    return doc->modified;
}

/* Discrete zoom levels (as integer percentages) */
static const int zoom_levels[] = {
    1, 2, 3, 4, 8, 12, 16, 20, 25, 33, 50, 67, 75, 100,
    150, 200, 300, 400, 500, 600, 700, 800, 1200, 1600, 2400, 3200};
static const int num_zoom_levels = sizeof(zoom_levels) / sizeof(zoom_levels[0]);

/**
 * Find the closest zoom level index for the given zoom factor
 */
static int find_closest_zoom_level(double zoom_factor) {
    int zoom_percent = (int)(zoom_factor * 100.0 + 0.5); /* Round to nearest integer */
    int closest_index = 0;
    int min_diff = abs(zoom_percent - zoom_levels[0]);

    for (int i = 1; i < num_zoom_levels; i++) {
        int diff = abs(zoom_percent - zoom_levels[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest_index = i;
        }
    }

    return closest_index;
}

/**
 * Zoom in
 */
void document_zoom_in(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    int current_index = find_closest_zoom_level(doc->zoom_factor);

    /* Move to next zoom level if not at maximum */
    if (current_index < num_zoom_levels - 1) {
        current_index++;
        doc->zoom_factor = zoom_levels[current_index] / 100.0;
        doc->zoom_mode = 0; /* Manual zoom */
    }

    document_set_zoom(doc, doc->zoom_factor);
}

/**
 * Zoom out
 */
void document_zoom_out(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    int current_index = find_closest_zoom_level(doc->zoom_factor);

    /* Move to previous zoom level if not at minimum */
    if (current_index > 0) {
        current_index--;
        doc->zoom_factor = zoom_levels[current_index] / 100.0;
        doc->zoom_mode = 0; /* Manual zoom */
    }

    document_set_zoom(doc, doc->zoom_factor);
}

/**
 * Zoom fit (fit to viewport - canvas extends to edges while maintaining aspect ratio)
 */
void document_zoom_fit(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->width <= 0 || doc->height <= 0) {
        return;
    }

    /* Get the viewport size (visible area) */
    gint viewport_width = 0;
    gint viewport_height = 0;

    if (doc->viewport && gtk_widget_get_visible(doc->viewport)) {
        /* Get allocated size of viewport (visible area) */
        viewport_width = gtk_widget_get_allocated_width(doc->viewport);
        viewport_height = gtk_widget_get_allocated_height(doc->viewport);
    }

    /* If viewport is not available or not yet allocated, use a default size */
    if (viewport_width <= 0 || viewport_height <= 0) {
        viewport_width = 800;
        viewport_height = 600;
    }

    /* Calculate zoom factors for width and height */
    gdouble zoom_w = (gdouble)viewport_width / (gdouble)doc->width;
    gdouble zoom_h = (gdouble)viewport_height / (gdouble)doc->height;

    /* Use the smaller zoom factor to ensure canvas fits inside viewport
     * while maintaining aspect ratio. This makes the canvas extend to
     * the edges of the viewport in at least one dimension. */
    gdouble zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;

    /* Clamp zoom to reasonable range (10% - 400%) */
    if (zoom < 0.1) {
        zoom = 0.1;
    } else if (zoom > 4.0) {
        zoom = 4.0;
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 1; /* Fit image mode */
    document_set_zoom(doc, zoom);
}

/**
 * Fit image to viewport width
 */
void document_zoom_fit_width(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->width <= 0) {
        return;
    }

    /* Get the viewport size (visible area) */
    gint viewport_width = 0;

    if (doc->viewport && gtk_widget_get_visible(doc->viewport)) {
        /* Get allocated width of viewport (visible area) */
        viewport_width = gtk_widget_get_allocated_width(doc->viewport);
    }

    /* If viewport is not available or not yet allocated, use a default size */
    if (viewport_width <= 0) {
        viewport_width = 800;
    }

    /* Calculate zoom factor for width */
    gdouble zoom = (gdouble)viewport_width / (gdouble)doc->width;

    /* Clamp zoom to reasonable range (1% - 3200%) */
    if (zoom < 0.01) {
        zoom = 0.01;
    } else if (zoom > 32.0) {
        zoom = 32.0;
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 2; /* Fit width mode */
    document_set_zoom(doc, zoom);
}

/**
 * Fit image to viewport height
 */
void document_zoom_fit_height(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->height <= 0) {
        return;
    }

    /* Get the viewport size (visible area) */
    gint viewport_height = 0;

    if (doc->viewport && gtk_widget_get_visible(doc->viewport)) {
        /* Get allocated height of viewport (visible area) */
        viewport_height = gtk_widget_get_allocated_height(doc->viewport);
    }

    /* If viewport is not available or not yet allocated, use a default size */
    if (viewport_height <= 0) {
        viewport_height = 600;
    }

    /* Calculate zoom factor for height */
    gdouble zoom = (gdouble)viewport_height / (gdouble)doc->height;

    /* Clamp zoom to reasonable range (1% - 3200%) */
    if (zoom < 0.01) {
        zoom = 0.01;
    } else if (zoom > 32.0) {
        zoom = 32.0;
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 3; /* Fit height mode */
    document_set_zoom(doc, zoom);
}

/**
 * Reset zoom to 100%
 */
void document_zoom_reset(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->zoom_factor = 1.0;
    doc->zoom_mode = 0; /* Manual zoom */
    document_set_zoom(doc, 1.0);
}

/**
 * Resize the canvas
 */
gboolean document_resize_canvas(ImageDocument* doc, guint new_width, guint new_height,
                                gdouble resolution, CanvasAnchorPosition anchor) {
    guint old_width, old_height;
    gint offset_x, offset_y;
    gint delta_width, delta_height;
    GList* iter;
    ImageLayer* layer;
    cairo_surface_t* new_surface;
    cairo_t* cr;

    (void)resolution; /* Reserved for future use */

    if (!doc || new_width == 0 || new_height == 0) {
        return FALSE;
    }

    old_width = doc->width;
    old_height = doc->height;

    /* If dimensions haven't changed, nothing to do */
    if (old_width == new_width && old_height == new_height) {
        return TRUE;
    }

    delta_width = (gint)new_width - (gint)old_width;
    delta_height = (gint)new_height - (gint)old_height;

    /* Calculate offsets based on anchor position */
    switch (anchor) {
        case CANVAS_ANCHOR_TOP_LEFT:
            offset_x = 0;
            offset_y = 0;
            break;
        case CANVAS_ANCHOR_TOP_CENTER:
            offset_x = delta_width / 2;
            offset_y = 0;
            break;
        case CANVAS_ANCHOR_TOP_RIGHT:
            offset_x = delta_width;
            offset_y = 0;
            break;
        case CANVAS_ANCHOR_MIDDLE_LEFT:
            offset_x = 0;
            offset_y = delta_height / 2;
            break;
        case CANVAS_ANCHOR_CENTER:
            offset_x = delta_width / 2;
            offset_y = delta_height / 2;
            break;
        case CANVAS_ANCHOR_MIDDLE_RIGHT:
            offset_x = delta_width;
            offset_y = delta_height / 2;
            break;
        case CANVAS_ANCHOR_BOTTOM_LEFT:
            offset_x = 0;
            offset_y = delta_height;
            break;
        case CANVAS_ANCHOR_BOTTOM_CENTER:
            offset_x = delta_width / 2;
            offset_y = delta_height;
            break;
        case CANVAS_ANCHOR_BOTTOM_RIGHT:
            offset_x = delta_width;
            offset_y = delta_height;
            break;
        case CANVAS_ANCHOR_NONE:
        default:
            offset_x = 0;
            offset_y = 0;
            break;
    }

    /* Adjust layer offsets based on anchor position */
    /* Layers keep their original size, only their position on the canvas changes */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (!layer) {
            continue;
        }

        /* Update layer offset to position it correctly on the new canvas */
        layer->offset_x += offset_x;
        layer->offset_y += offset_y;

        /* Invalidate layer cache */
        layer_invalidate_cache(layer);
    }

    /* Update document dimensions */
    doc->width = new_width;
    doc->height = new_height;

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(new_width, new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after canvas resize");
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);

    return TRUE;
}
