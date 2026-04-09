/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/overview_panel.h"
#include "document.h"
#include "render/compositor.h"
#include "render/render_utils.h"
#include "ui/layers_panel.h"
#include <cairo/cairo.h>
#include <gtk/gtk.h>

/**
 * Overview widget state structure
 */
typedef struct {
    gboolean is_dragging;          /* Whether user is dragging the rectangle */
    gboolean is_hovering;          /* Whether mouse is hovering over rectangle */
    gdouble drag_start_x;          /* Starting X position of drag (in widget coordinates) */
    gdouble drag_start_y;          /* Starting Y position of drag (in widget coordinates) */
    gdouble drag_start_viewport_x; /* Starting viewport X position */
    gdouble drag_start_viewport_y; /* Starting viewport Y position */
} OverviewWidgetState;

/* Forward declarations */
static gboolean on_overview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data);
static gboolean on_overview_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_overview_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_overview_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_overview_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
static gboolean get_selection_rectangle(LayersPanel* layers_panel, GtkWidget* widget,
                                        gint* rect_x, gint* rect_y, gint* rect_w, gint* rect_h,
                                        gdouble* scale_out);
static gboolean point_in_selection_rectangle(LayersPanel* layers_panel, GtkWidget* widget,
                                             gdouble x, gdouble y);

/**
 * Overview widget draw callback
 */
static gboolean on_overview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageDocument* doc;
    cairo_surface_t* composite;
    GdkPixbuf* thumbnail_pixbuf = NULL;
    GdkPixbuf* scaled_thumb = NULL;
    gint widget_width, widget_height;
    gint doc_width, doc_height;
    gdouble scale_x, scale_y, scale;
    gint thumb_width, thumb_height;
    gint thumb_x, thumb_y;
    gint viewport_x, viewport_y;
    gint viewport_width, viewport_height;
    GtkAdjustment *hadj = NULL, *vadj = NULL;
    gdouble zoom_factor;

    if (!layers_panel) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc) {
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    doc_width = doc->width;
    doc_height = doc->height;

    if (doc_width <= 0 || doc_height <= 0) {
        return FALSE;
    }

    /* Calculate scale to fit thumbnail */
    scale_x = (gdouble)(widget_width - 8) / doc_width;
    scale_y = (gdouble)(widget_height - 8) / doc_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    thumb_width = (gint)(doc_width * scale);
    thumb_height = (gint)(doc_height * scale);
    thumb_x = (widget_width - thumb_width) / 2;
    thumb_y = (widget_height - thumb_height) / 2;

    /* TILE-BASED: Generate thumbnail directly from tiles at thumbnail size (much faster) */
    composite = document_generate_thumbnail_from_tiles(doc, thumb_width, thumb_height);
    if (!composite) {
        /* Draw empty state if thumbnail generation failed */
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_rectangle(cr, 0, 0, widget_width, widget_height);
        cairo_fill(cr);
        return FALSE;
    }

    /* Convert thumbnail surface to pixbuf */
    scaled_thumb = cairo_surface_to_pixbuf(composite, TRUE);
    cairo_surface_destroy(composite);

    if (!scaled_thumb) {
        return FALSE;
    }

    /* Draw checkered background behind thumbnail (clipped to thumbnail bounds) */
    cairo_save(cr);
    cairo_rectangle(cr, thumb_x, thumb_y, thumb_width, thumb_height);
    cairo_clip(cr);
    cairo_translate(cr, thumb_x, thumb_y);
    draw_checkered_background(cr, thumb_width, thumb_height);
    cairo_restore(cr);

    /* Draw thumbnail */
    gdk_cairo_set_source_pixbuf(cr, scaled_thumb, thumb_x, thumb_y);
    cairo_paint(cr);
    g_object_unref(scaled_thumb);

    /* Get viewport information from scrolled window */
    if (doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        zoom_factor = document_get_zoom(doc);

        if (hadj && vadj) {
            /* Calculate viewport rectangle in document coordinates */
            viewport_x = (gint)(gtk_adjustment_get_value(hadj) / zoom_factor);
            viewport_y = (gint)(gtk_adjustment_get_value(vadj) / zoom_factor);
            viewport_width = (gint)(gtk_adjustment_get_page_size(hadj) / zoom_factor);
            viewport_height = (gint)(gtk_adjustment_get_page_size(vadj) / zoom_factor);

            /* Convert to thumbnail coordinates */
            gint rect_x = thumb_x + (gint)(viewport_x * scale);
            gint rect_y = thumb_y + (gint)(viewport_y * scale);
            gint rect_w = (gint)(viewport_width * scale);
            gint rect_h = (gint)(viewport_height * scale);

            /* Clamp rectangle to thumbnail boundaries */
            if (rect_x < thumb_x) {
                rect_w -= (thumb_x - rect_x);
                rect_x = thumb_x;
            }
            if (rect_y < thumb_y) {
                rect_h -= (thumb_y - rect_y);
                rect_y = thumb_y;
            }
            if (rect_x + rect_w > thumb_x + thumb_width) {
                rect_w = (thumb_x + thumb_width) - rect_x;
            }
            if (rect_y + rect_h > thumb_y + thumb_height) {
                rect_h = (thumb_y + thumb_height) - rect_y;
            }

            /* Only draw rectangle if it's within thumbnail bounds */
            if (rect_w > 0 && rect_h > 0 &&
                rect_x >= thumb_x && rect_y >= thumb_y &&
                rect_x + rect_w <= thumb_x + thumb_width &&
                rect_y + rect_h <= thumb_y + thumb_height) {

                /* Get hover state */
                OverviewWidgetState* state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
                gboolean is_hovering = FALSE;
                if (state) {
                    is_hovering = state->is_hovering || state->is_dragging;
                }

                /* Draw selection rectangle */
                cairo_save(cr);

                /* Draw hover highlight fill if hovering */
                if (is_hovering) {
                    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.2); /* Light blue fill */
                    cairo_rectangle(cr, rect_x, rect_y, rect_w, rect_h);
                    cairo_fill(cr);
                }

                /* Draw border - brighter when hovering */
                cairo_set_line_width(cr, 2.0);
                if (is_hovering) {
                    cairo_set_source_rgba(cr, 0.0, 0.7, 1.0, 1.0); /* Brighter blue when hovering */
                } else {
                    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.8); /* Blue with transparency */
                }
                cairo_rectangle(cr, rect_x, rect_y, rect_w, rect_h);
                cairo_stroke(cr);

                /* Draw inner border for better visibility */
                cairo_set_line_width(cr, 1.0);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9); /* White inner border */
                cairo_rectangle(cr, rect_x + 1, rect_y + 1, rect_w - 2, rect_h - 2);
                cairo_stroke(cr);
                cairo_restore(cr);
            }
        }
    }

    return FALSE;
}

/**
 * Calculate selection rectangle coordinates in widget space
 * Returns TRUE if rectangle is valid, FALSE otherwise
 */
static gboolean get_selection_rectangle(LayersPanel* layers_panel, GtkWidget* widget,
                                        gint* rect_x, gint* rect_y, gint* rect_w, gint* rect_h,
                                        gdouble* scale_out) {
    ImageDocument* doc;
    gint widget_width, widget_height;
    gint doc_width, doc_height;
    gdouble scale_x, scale_y, scale;
    gint thumb_width, thumb_height;
    gint thumb_x, thumb_y;
    gint viewport_x, viewport_y;
    gint viewport_width, viewport_height;
    GtkAdjustment *hadj = NULL, *vadj = NULL;
    gdouble zoom_factor;

    if (!layers_panel || !widget) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc || !doc->scrolled_window || !GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);
    doc_width = doc->width;
    doc_height = doc->height;

    if (doc_width <= 0 || doc_height <= 0) {
        return FALSE;
    }

    /* Calculate scale to fit thumbnail */
    scale_x = (gdouble)(widget_width - 8) / doc_width;
    scale_y = (gdouble)(widget_height - 8) / doc_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    thumb_width = (gint)(doc_width * scale);
    thumb_height = (gint)(doc_height * scale);
    thumb_x = (widget_width - thumb_width) / 2;
    thumb_y = (widget_height - thumb_height) / 2;

    hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    zoom_factor = document_get_zoom(doc);

    if (!hadj || !vadj) {
        return FALSE;
    }

    /* Calculate viewport rectangle in document coordinates */
    viewport_x = (gint)(gtk_adjustment_get_value(hadj) / zoom_factor);
    viewport_y = (gint)(gtk_adjustment_get_value(vadj) / zoom_factor);
    viewport_width = (gint)(gtk_adjustment_get_page_size(hadj) / zoom_factor);
    viewport_height = (gint)(gtk_adjustment_get_page_size(vadj) / zoom_factor);

    /* Convert to thumbnail coordinates */
    *rect_x = thumb_x + (gint)(viewport_x * scale);
    *rect_y = thumb_y + (gint)(viewport_y * scale);
    *rect_w = (gint)(viewport_width * scale);
    *rect_h = (gint)(viewport_height * scale);

    /* Clamp rectangle to thumbnail boundaries */
    if (*rect_x < thumb_x) {
        *rect_w -= (thumb_x - *rect_x);
        *rect_x = thumb_x;
    }
    if (*rect_y < thumb_y) {
        *rect_h -= (thumb_y - *rect_y);
        *rect_y = thumb_y;
    }
    if (*rect_x + *rect_w > thumb_x + thumb_width) {
        *rect_w = (thumb_x + thumb_width) - *rect_x;
    }
    if (*rect_y + *rect_h > thumb_y + thumb_height) {
        *rect_h = (thumb_y + thumb_height) - *rect_y;
    }

    if (scale_out) {
        *scale_out = scale;
    }

    /* Check if rectangle is valid */
    return (*rect_w > 0 && *rect_h > 0 &&
            *rect_x >= thumb_x && *rect_y >= thumb_y &&
            *rect_x + *rect_w <= thumb_x + thumb_width &&
            *rect_y + *rect_h <= thumb_y + thumb_height);
}

/**
 * Check if point is inside selection rectangle
 */
static gboolean point_in_selection_rectangle(LayersPanel* layers_panel, GtkWidget* widget,
                                             gdouble x, gdouble y) {
    gint rect_x, rect_y, rect_w, rect_h;

    if (!get_selection_rectangle(layers_panel, widget, &rect_x, &rect_y, &rect_w, &rect_h, NULL)) {
        return FALSE;
    }

    return (x >= rect_x && x <= rect_x + rect_w &&
            y >= rect_y && y <= rect_y + rect_h);
}

/**
 * Overview widget button press handler
 */
static gboolean on_overview_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageDocument* doc;
    OverviewWidgetState* state;
    gint rect_x, rect_y, rect_w, rect_h;
    gdouble scale;

    if (!layers_panel || event->button != 1) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get or create state */
    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (!state) {
        state = (OverviewWidgetState*)g_malloc0(sizeof(OverviewWidgetState));
        g_object_set_data_full(G_OBJECT(widget), "overview_state", state, g_free);
    }

    /* Check if click is inside selection rectangle */
    if (get_selection_rectangle(layers_panel, widget, &rect_x, &rect_y, &rect_w, &rect_h, &scale)) {
        if (event->x >= rect_x && event->x <= rect_x + rect_w &&
            event->y >= rect_y && event->y <= rect_y + rect_h) {
            /* Start dragging */
            state->is_dragging = TRUE;
            state->drag_start_x = event->x;
            state->drag_start_y = event->y;

            /* Store current viewport position */
            GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
            GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));

            if (hadj && vadj) {
                state->drag_start_viewport_x = gtk_adjustment_get_value(hadj);
                state->drag_start_viewport_y = gtk_adjustment_get_value(vadj);
            }

            gtk_widget_queue_draw(widget);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * Overview widget button release handler
 */
static gboolean on_overview_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    OverviewWidgetState* state;

    (void)user_data; /* Unused */

    if (event->button != 1) {
        return FALSE;
    }

    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (state && state->is_dragging) {
        state->is_dragging = FALSE;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    return FALSE;
}

/**
 * Overview widget motion notify handler
 */
static gboolean on_overview_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    LayersPanel* layers_panel = (LayersPanel*)user_data;
    ImageDocument* doc;
    OverviewWidgetState* state;
    gboolean was_hovering;
    gint rect_x, rect_y, rect_w, rect_h;
    gdouble scale;
    gdouble new_viewport_x, new_viewport_y;

    if (!layers_panel) {
        return FALSE;
    }

    doc = layers_panel->current_doc;
    if (!doc || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get or create state */
    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (!state) {
        state = (OverviewWidgetState*)g_malloc0(sizeof(OverviewWidgetState));
        g_object_set_data_full(G_OBJECT(widget), "overview_state", state, g_free);
    }

    was_hovering = state->is_hovering;

    /* Check if mouse is over selection rectangle */
    state->is_hovering = point_in_selection_rectangle(layers_panel, widget, event->x, event->y);

    /* Update hover state if changed */
    if (was_hovering != state->is_hovering) {
        gtk_widget_queue_draw(widget);
    }

    /* Handle dragging */
    if (state->is_dragging && get_selection_rectangle(layers_panel, widget, &rect_x, &rect_y, &rect_w, &rect_h, &scale)) {
        /* Calculate mouse movement in widget coordinates */
        gdouble delta_x = event->x - state->drag_start_x;
        gdouble delta_y = event->y - state->drag_start_y;

        /* Convert to document coordinates */
        gdouble delta_doc_x = delta_x / scale;
        gdouble delta_doc_y = delta_y / scale;

        /* Calculate new viewport position */
        gdouble zoom_factor = document_get_zoom(doc);
        new_viewport_x = state->drag_start_viewport_x + (delta_doc_x * zoom_factor);
        new_viewport_y = state->drag_start_viewport_y + (delta_doc_y * zoom_factor);

        /* Update scroll adjustments */
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));

        if (hadj && vadj) {
            /* Clamp to valid range */
            gdouble h_min = gtk_adjustment_get_lower(hadj);
            gdouble h_max = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
            gdouble v_min = gtk_adjustment_get_lower(vadj);
            gdouble v_max = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);

            if (new_viewport_x < h_min)
                new_viewport_x = h_min;
            if (new_viewport_x > h_max)
                new_viewport_x = h_max;
            if (new_viewport_y < v_min)
                new_viewport_y = v_min;
            if (new_viewport_y > v_max)
                new_viewport_y = v_max;

            /* Update scroll position */
            gtk_adjustment_set_value(hadj, new_viewport_x);
            gtk_adjustment_set_value(vadj, new_viewport_y);
        }

        return TRUE;
    }

    return FALSE;
}

/**
 * Overview widget leave notify handler
 */
static gboolean on_overview_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    OverviewWidgetState* state;

    (void)event;     /* Unused */
    (void)user_data; /* Unused */

    state = (OverviewWidgetState*)g_object_get_data(G_OBJECT(widget), "overview_state");
    if (state && state->is_hovering) {
        state->is_hovering = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

/**
 * Create overview widget for composite thumbnail
 */
GtkWidget* overview_panel_create(LayersPanel* layers_panel) {
    GtkWidget* drawing_area;
    OverviewWidgetState* state;

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 100); /* Fixed height of 100 */
    gtk_widget_set_vexpand(drawing_area, FALSE);        /* Don't expand vertically */
    gtk_widget_set_hexpand(drawing_area, TRUE);         /* Expand horizontally */

    /* Enable mouse events */
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    /* Connect signals */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_overview_draw), layers_panel);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_overview_button_press), layers_panel);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_overview_button_release), layers_panel);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_overview_motion_notify), layers_panel);
    g_signal_connect(drawing_area, "leave-notify-event", G_CALLBACK(on_overview_leave_notify), layers_panel);

    /* Create and store state */
    state = (OverviewWidgetState*)g_malloc0(sizeof(OverviewWidgetState));
    g_object_set_data_full(G_OBJECT(drawing_area), "overview_state", state, g_free);

    /* Store layers panel reference */
    g_object_set_data(G_OBJECT(drawing_area), "layers_panel", layers_panel);

    return drawing_area;
}

/**
 * Update the overview panel (queue redraw)
 */
void overview_panel_update(GtkWidget* widget) {
    if (widget) {
        gtk_widget_queue_draw(widget);
    }
}
