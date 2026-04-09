/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/hdr_image_dialog.h"
#include "app/settings.h"
#include "tone_mapping.h"
#include "ui/ui_utils.h"
#include <cairo.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include "i18n.h"
#include "debug_logger.h"

/* Forward declarations */
static void on_operator_toggled(GtkToggleButton* button, gpointer user_data);
static void on_linear_normalize_toggled(GtkToggleButton* button, gpointer user_data);
static void update_controls_visibility(GtkWidget* dialog, ToneMapOperator operator);
static void rgbe_to_rgb_float(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float* out_r, float* out_g, float* out_b);
static cairo_surface_t* create_preview_surface(const uint8_t* rgbe_data, uint32_t width, uint32_t height, const ToneMapParams* params);
static gboolean on_preview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data);
static void update_preview(GtkWidget* dialog, const uint8_t* rgbe_data, uint32_t width, uint32_t height, const ToneMapParams* params);
static void on_parameter_changed(GtkWidget* widget, gpointer user_data);
static gboolean update_preview_timeout(gpointer user_data);
static void schedule_preview_update(GtkWidget* dialog);
static void on_dialog_button_clicked(GtkButton* button, gpointer user_data);
static void on_dialog_map_center_and_clamp(GtkWidget* dialog, gpointer user_data);
static gboolean on_dialog_configure_event(GtkWidget* dialog, GdkEventConfigure* event, gpointer user_data);
static void apply_params_to_widgets(GtkWidget* dialog, const ToneMapParams* params);
static void on_reset_clicked(GtkWidget* widget, gpointer user_data);

/**
 * Callback when operator toggle button is toggled
 * Ensures only one operator button is active at a time
 */
static void on_operator_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkToggleButton** buttons = (GtkToggleButton**)user_data;
    GtkWidget* dialog = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "dialog"));

    if (!buttons || !dialog) {
        return;
    }

    /* If this button was activated, deactivate all others */
    if (gtk_toggle_button_get_active(button)) {
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != GTK_WIDGET(buttons[i])) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_operator_toggled), buttons);
                gtk_toggle_button_set_active(buttons[i], FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_operator_toggled), buttons);
            }
        }

        /* Determine which operator was selected and update visibility */
        ToneMapOperator selected_operator = TONE_MAP_LINEAR;
        if (button == buttons[0]) {
            selected_operator = TONE_MAP_LINEAR;
        } else if (button == buttons[1]) {
            selected_operator = TONE_MAP_FILMIC;
        } else if (button == buttons[2]) {
            selected_operator = TONE_MAP_DRAGO;
        } else if (button == buttons[3]) {
            selected_operator = TONE_MAP_REINHARD;
        }

        update_controls_visibility(dialog, selected_operator);
    } else {
        /* If this button was deactivated, ensure at least one is active */
        gboolean any_active = FALSE;
        for (int i = 0; buttons[i] != NULL; i++) {
            if (gtk_toggle_button_get_active(buttons[i])) {
                any_active = TRUE;
                break;
            }
        }

        /* If no button is active, reactivate this one */
        if (!any_active) {
            g_signal_handlers_block_by_func(button, G_CALLBACK(on_operator_toggled), buttons);
            gtk_toggle_button_set_active(button, TRUE);
            g_signal_handlers_unblock_by_func(button, G_CALLBACK(on_operator_toggled), buttons);
        }
    }
}

/**
 * Callback when linear normalize toggle button is toggled
 */
static void on_linear_normalize_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkToggleButton** buttons = (GtkToggleButton**)user_data;

    if (!buttons) {
        return;
    }

    /* If this button was activated, deactivate all others */
    if (gtk_toggle_button_get_active(button)) {
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != GTK_WIDGET(buttons[i])) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_linear_normalize_toggled), buttons);
                gtk_toggle_button_set_active(buttons[i], FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_linear_normalize_toggled), buttons);
            }
        }
    } else {
        /* If this button was deactivated, ensure at least one is active */
        gboolean any_active = FALSE;
        for (int i = 0; buttons[i] != NULL; i++) {
            if (gtk_toggle_button_get_active(buttons[i])) {
                any_active = TRUE;
                break;
            }
        }

        /* If no button is active, reactivate this one */
        if (!any_active) {
            g_signal_handlers_block_by_func(button, G_CALLBACK(on_linear_normalize_toggled), buttons);
            gtk_toggle_button_set_active(button, TRUE);
            g_signal_handlers_unblock_by_func(button, G_CALLBACK(on_linear_normalize_toggled), buttons);
        }
    }
}

/**
 * Update visibility of control boxes based on selected operator
 */
static void update_controls_visibility(GtkWidget* dialog, ToneMapOperator operator) {
    GtkBuilder* builder = GTK_BUILDER(g_object_get_data(G_OBJECT(dialog), "builder"));
    if (!builder) {
        return;
    }

    GtkWidget* linear_box = GTK_WIDGET(gtk_builder_get_object(builder, "linear_controls_box"));
    GtkWidget* filmic_box = GTK_WIDGET(gtk_builder_get_object(builder, "filmic_controls_box"));
    GtkWidget* drago_box = GTK_WIDGET(gtk_builder_get_object(builder, "drago_controls_box"));
    GtkWidget* reinhard_box = GTK_WIDGET(gtk_builder_get_object(builder, "reinhard_controls_box"));

    /* Hide all boxes first */
    if (linear_box)
        gtk_widget_set_visible(linear_box, FALSE);
    if (filmic_box)
        gtk_widget_set_visible(filmic_box, FALSE);
    if (drago_box)
        gtk_widget_set_visible(drago_box, FALSE);
    if (reinhard_box)
        gtk_widget_set_visible(reinhard_box, FALSE);

    /* Show the appropriate box */
    switch (operator) {
        case TONE_MAP_LINEAR:
            if (linear_box)
                gtk_widget_set_visible(linear_box, TRUE);
            break;
        case TONE_MAP_FILMIC:
            if (filmic_box)
                gtk_widget_set_visible(filmic_box, TRUE);
            break;
        case TONE_MAP_DRAGO:
            if (drago_box)
                gtk_widget_set_visible(drago_box, TRUE);
            break;
        case TONE_MAP_REINHARD:
            if (reinhard_box)
                gtk_widget_set_visible(reinhard_box, TRUE);
            break;
    }
}

/**
 * Convert RGBE pixel to linear RGB float
 */
static void rgbe_to_rgb_float(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float* out_r, float* out_g, float* out_b) {
    if (e == 0) {
        *out_r = *out_g = *out_b = 0.0f;
    } else {
        float f = ldexpf(1.0f, (int)e - 128);
        *out_r = ((float)r + 0.5f) / 256.0f * f;
        *out_g = ((float)g + 0.5f) / 256.0f * f;
        *out_b = ((float)b + 0.5f) / 256.0f * f;
    }
}

/**
 * Create a preview surface from RGBE data with tone mapping applied
 */
static cairo_surface_t* create_preview_surface(const uint8_t* rgbe_data, uint32_t width, uint32_t height, const ToneMapParams* params) {
    if (!rgbe_data || !params || width == 0 || height == 0) {
        return NULL;
    }

    /* Create a scaled-down preview for performance (max 300x300) */
    uint32_t preview_width = width;
    uint32_t preview_height = height;
    float scale = 1.0f;

    if (width > 300 || height > 300) {
        float scale_x = 300.0f / (float)width;
        float scale_y = 300.0f / (float)height;
        scale = (scale_x < scale_y) ? scale_x : scale_y;
        preview_width = (uint32_t)((float)width * scale);
        preview_height = (uint32_t)((float)height * scale);
        /* Ensure minimum size */
        if (preview_width < 1)
            preview_width = 1;
        if (preview_height < 1)
            preview_height = 1;
    }

    /* Create Cairo surface for preview */
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, preview_width, preview_height);
    if (!surface) {
        return NULL;
    }

    cairo_surface_flush(surface);
    guchar* surface_data = cairo_image_surface_get_data(surface);
    int surface_stride = cairo_image_surface_get_stride(surface);

    if (!surface_data) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    /* Sample pixels from original image and apply tone mapping */
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    for (uint32_t y = 0; y < preview_height; y++) {
        guchar* dst_row = surface_data + y * surface_stride;
        uint32_t src_y = (uint32_t)((float)y / scale);
        if (src_y >= height)
            src_y = height - 1;

        for (uint32_t x = 0; x < preview_width; x++) {
            uint32_t src_x = (uint32_t)((float)x / scale);
            if (src_x >= width)
                src_x = width - 1;
            uint8_t* src_pixel = (uint8_t*)(rgbe_data + (src_y * width + src_x) * 4);

            uint8_t r_gbe = src_pixel[0];
            uint8_t g_gbe = src_pixel[1];
            uint8_t b_gbe = src_pixel[2];
            uint8_t e_gbe = src_pixel[3];

            /* Convert RGBE to linear RGB float */
            float r_float, g_float, b_float;
            rgbe_to_rgb_float(r_gbe, g_gbe, b_gbe, e_gbe, &r_float, &g_float, &b_float);

            /* Tone map to 8-bit */
            uint8_t r, g, b;
            tone_map_rgb(r_float, g_float, b_float, params, &r, &g, &b);
            uint8_t a = 255; /* No alpha in HDR */

            /* Cairo ARGB32: BGRA in memory (little-endian) */
            dst_row[x * 4 + 0] = b;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = r;
            dst_row[x * 4 + 3] = a;
        }
    }

    cairo_surface_mark_dirty(surface);
    return surface;
}

/**
 * Draw callback for preview area
 */
static gboolean on_preview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    cairo_surface_t* preview_surface = (cairo_surface_t*)g_object_get_data(G_OBJECT(widget), "preview_surface");

    /* Get canvas background color from settings */
    gdouble bg_r = 0.8, bg_g = 0.8, bg_b = 0.8; /* Default gray */
    GtkWidget* dialog = gtk_widget_get_toplevel(widget);
    if (dialog && GTK_IS_DIALOG(dialog)) {
        Settings* s = (Settings*)g_object_get_data(G_OBJECT(dialog), "settings");
        if (s) {
            settings_get_canvas_background(s, &bg_r, &bg_g, &bg_b);
        }
    }

    if (!preview_surface) {
        /* Draw background with canvas color */
        GtkAllocation alloc;
        gtk_widget_get_allocation(widget, &alloc);
        cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
        cairo_paint(cr);
        return FALSE;
    }

    /* Check if surface is valid before using */
    cairo_status_t status = cairo_surface_status(preview_surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        /* Surface is invalid, draw background */
        GtkAllocation alloc;
        gtk_widget_get_allocation(widget, &alloc);
        cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
        cairo_paint(cr);
        return FALSE;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    int widget_width = alloc.width;
    int widget_height = alloc.height;

    int img_width = cairo_image_surface_get_width(preview_surface);
    int img_height = cairo_image_surface_get_height(preview_surface);

    if (img_width <= 0 || img_height <= 0) {
        return FALSE;
    }

    /* Calculate scale to fit */
    double scale_x = (double)widget_width / img_width;
    double scale_y = (double)widget_height / img_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    double scaled_width = img_width * scale;
    double scaled_height = img_height * scale;
    double draw_x = (widget_width - scaled_width) / 2.0;
    double draw_y = (widget_height - scaled_height) / 2.0;

    /* Draw background with canvas color */
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    /* Draw preview image centered and scaled to fit */
    cairo_save(cr);
    cairo_translate(cr, draw_x, draw_y);
    cairo_scale(cr, scale, scale);

    /* Check surface status again before drawing (it might have been destroyed) */
    status = cairo_surface_status(preview_surface);
    if (status == CAIRO_STATUS_SUCCESS) {
        cairo_set_source_surface(cr, preview_surface, 0, 0);
        cairo_paint(cr);
    }
    cairo_restore(cr);

    return FALSE;
}

/**
 * Update preview with current tone mapping parameters
 */
static void update_preview(GtkWidget* dialog, const uint8_t* rgbe_data, uint32_t width, uint32_t height, const ToneMapParams* params) {
    GtkBuilder* builder = GTK_BUILDER(g_object_get_data(G_OBJECT(dialog), "builder"));
    if (!builder) {
        return;
    }

    GtkWidget* preview_area = GTK_WIDGET(gtk_builder_get_object(builder, "preview_container"));
    if (!preview_area) {
        return;
    }

    /* Validate inputs */
    if (!rgbe_data || width == 0 || height == 0 || !params) {
        return;
    }

    /* Get current preview surface and destroy it */
    /* Use steal_data to remove without triggering destroy notify, then destroy manually */
    cairo_surface_t* old_surface = (cairo_surface_t*)g_object_steal_data(G_OBJECT(preview_area), "preview_surface");
    if (old_surface) {
        cairo_surface_destroy(old_surface);
    }

    /* Create new preview surface */
    cairo_surface_t* new_surface = create_preview_surface(rgbe_data, width, height, params);
    if (new_surface) {
        /* Check surface status before using */
        if (cairo_surface_status(new_surface) == CAIRO_STATUS_SUCCESS) {
            /* Store with reference counting - g_object_set_data_full will call destroy when removed */
            g_object_set_data_full(G_OBJECT(preview_area), "preview_surface", new_surface, (GDestroyNotify)cairo_surface_destroy);
            gtk_widget_queue_draw(preview_area);
        } else {
            debug_log("WRN", "Failed to create preview surface: %s", cairo_status_to_string(cairo_surface_status(new_surface)));
            cairo_surface_destroy(new_surface);
        }
    }
}

/**
 * Timeout callback for delayed preview update
 */
static gboolean update_preview_timeout(gpointer user_data) {
    GtkWidget* dialog = (GtkWidget*)user_data;

    if (!dialog || !GTK_IS_WIDGET(dialog)) {
        return G_SOURCE_REMOVE;
    }

    /* Clear timeout ID */
    g_object_set_data(G_OBJECT(dialog), "preview_update_timeout_id", GUINT_TO_POINTER(0));

    /* Get stored data */
    ToneMapParams* params = (ToneMapParams*)g_object_get_data(G_OBJECT(dialog), "tone_params");
    const uint8_t* rgbe_data = (const uint8_t*)g_object_get_data(G_OBJECT(dialog), "rgbe_data");
    uint32_t width = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "hdr_width"));
    uint32_t height = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "hdr_height"));

    /* Validate data */
    if (!dialog || !GTK_IS_WIDGET(dialog) || !params || !rgbe_data || width == 0 || height == 0) {
        return G_SOURCE_REMOVE;
    }

    /* Update preview */
    update_preview(dialog, rgbe_data, width, height, params);

    return G_SOURCE_REMOVE;
}

/**
 * Button clicked callback to emit dialog response
 * Prevents double-triggering by checking if response was already handled
 */
static void on_dialog_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));

    /* Check if we've already handled this response to prevent double-triggering */
    gpointer handled = g_object_get_data(G_OBJECT(dialog), "response-handled");
    if (handled) {
        /* Already handled, ignore this signal */
        return;
    }

    /* Mark as handled BEFORE emitting response to prevent re-entry */
    g_object_set_data(G_OBJECT(dialog), "response-handled", GINT_TO_POINTER(TRUE));

    /* Emit response - this will cause gtk_dialog_run to return */
    gtk_dialog_response(dialog, response_id);
}

#define HDR_DIALOG_WIDTH 700
#define HDR_DIALOG_HEIGHT 470

static gboolean idle_center_and_clamp(gpointer user_data) {
    GtkWidget* dialog = (GtkWidget*)user_data;
    GtkWindow* parent = (GtkWindow*)g_object_get_data(G_OBJECT(dialog), "hdr_dialog_parent");
    if (!dialog || !gtk_widget_get_visible(dialog)) {
        g_object_set_data(G_OBJECT(dialog), "hdr_resize_pending", NULL);
        return G_SOURCE_REMOVE;
    }
    g_object_set_data(G_OBJECT(dialog), "hdr_dialog_parent", NULL);
    g_object_set_data(G_OBJECT(dialog), "hdr_resize_pending", NULL);

    int w = 0, h = 0;
    gtk_window_get_size(GTK_WINDOW(dialog), &w, &h);
    if (w != HDR_DIALOG_WIDTH || h != HDR_DIALOG_HEIGHT) {
        gtk_window_resize(GTK_WINDOW(dialog), HDR_DIALOG_WIDTH, HDR_DIALOG_HEIGHT);
        w = HDR_DIALOG_WIDTH;
        h = HDR_DIALOG_HEIGHT;
    }

    if (parent && gtk_widget_get_visible(GTK_WIDGET(parent))) {
        int px = 0, py = 0, pw = 0, ph = 0;
        gtk_window_get_position(parent, &px, &py);
        gtk_window_get_size(parent, &pw, &ph);
        gtk_window_move(GTK_WINDOW(dialog), px + (pw - w) / 2, py + (ph - h) / 2);
    }
    return G_SOURCE_REMOVE;
}

/**
 * On configure: if WM/layout resized the window.
 */
static gboolean on_dialog_configure_event(GtkWidget* dialog, GdkEventConfigure* event, gpointer user_data) {
    (void)user_data;
    if ((event->width != HDR_DIALOG_WIDTH || event->height != HDR_DIALOG_HEIGHT) &&
        !g_object_get_data(G_OBJECT(dialog), "hdr_resize_pending")) {
        g_object_set_data(G_OBJECT(dialog), "hdr_resize_pending", GINT_TO_POINTER(1));
        g_idle_add(idle_center_and_clamp, dialog);
    }
    return FALSE;
}

/**
 * On first map: schedule idle to clamp dialog and center on parent.
 * Disconnects after one run.
 */
static void on_dialog_map_center_and_clamp(GtkWidget* dialog, gpointer user_data) {
    GtkWindow* parent = (GtkWindow*)user_data;
    if (!gtk_widget_get_window(dialog) || !GTK_IS_WINDOW(dialog)) {
        return;
    }

    g_signal_handlers_disconnect_by_func(dialog, G_CALLBACK(on_dialog_map_center_and_clamp), user_data);
    g_object_set_data(G_OBJECT(dialog), "hdr_dialog_parent", parent);
    g_idle_add(idle_center_and_clamp, dialog);
}

/**
 * Apply tone mapping params to all dialog widgets (operator, normalize, adjustments).
 * Used when resetting to defaults.
 */
static void apply_params_to_widgets(GtkWidget* dialog, const ToneMapParams* params) {
    GtkBuilder* builder = GTK_BUILDER(g_object_get_data(G_OBJECT(dialog), "builder"));
    if (!builder || !params) {
        return;
    }
    GtkWidget* linear_operator = GTK_WIDGET(gtk_builder_get_object(builder, "linear_operator_button"));
    GtkWidget* filmic_operator = GTK_WIDGET(gtk_builder_get_object(builder, "filmic_operator_button"));
    GtkWidget* drago_operator = GTK_WIDGET(gtk_builder_get_object(builder, "drago_operator_button"));
    GtkWidget* reinhard_operator = GTK_WIDGET(gtk_builder_get_object(builder, "reinhard_operator_button"));
    GtkWidget* norm_none = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_none_button"));
    GtkWidget* norm_visible = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_visible_button"));
    GtkWidget* norm_full = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_full_button"));
    GtkAdjustment* adj;
    switch (params->operator) {
        case TONE_MAP_LINEAR:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(linear_operator), TRUE);
            break;
        case TONE_MAP_FILMIC:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(filmic_operator), TRUE);
            break;
        case TONE_MAP_DRAGO:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(drago_operator), TRUE);
            break;
        case TONE_MAP_REINHARD:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reinhard_operator), TRUE);
            break;
    }
    if (norm_none && norm_visible && norm_full) {
        switch (params->normalize) {
            case TONE_MAP_NORMALIZE_NONE:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(norm_none), TRUE);
                break;
            case TONE_MAP_NORMALIZE_VISIBLE_SPECTRUM:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(norm_visible), TRUE);
                break;
            case TONE_MAP_NORMALIZE_FULL_SPECTRUM:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(norm_full), TRUE);
                break;
        }
    }
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "linear_gamma_adjustment"))))
        gtk_adjustment_set_value(adj, params->gamma);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_gamma_adjustment"))))
        gtk_adjustment_set_value(adj, params->gamma);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_exposure_adjustment"))))
        gtk_adjustment_set_value(adj, params->exposure);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_whitepoint_adjustment"))))
        gtk_adjustment_set_value(adj, params->white_point);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "drago_gamma_adjustment"))))
        gtk_adjustment_set_value(adj, params->gamma);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "drago_exposure_adjustment"))))
        gtk_adjustment_set_value(adj, params->exposure);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_intensity_adjustment"))))
        gtk_adjustment_set_value(adj, params->intensity);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_adaptation_adjustment"))))
        gtk_adjustment_set_value(adj, params->adaptation);
    if ((adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_color_correction_adjustment"))))
        gtk_adjustment_set_value(adj, params->color_correction);
}

/**
 * Reset parameters to defaults
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    GtkWidget* dialog = (GtkWidget*)user_data;
    ToneMapParams* params = (ToneMapParams*)g_object_get_data(G_OBJECT(dialog), "tone_params");
    (void)widget;
    if (!params) {
        return;
    }
    tone_map_params_init(params);
    apply_params_to_widgets(dialog, params);
    update_controls_visibility(dialog, params->operator);
    schedule_preview_update(dialog);
}

/**
 * Schedule a preview update with throttling (debounce)
 */
static void schedule_preview_update(GtkWidget* dialog) {
    if (!dialog) {
        return;
    }

    /* Cancel existing timeout if any */
    guint timeout_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "preview_update_timeout_id"));
    if (timeout_id > 0) {
        g_source_remove(timeout_id);
    }

    /* Schedule new update with 100ms delay (debounce) */
    timeout_id = g_timeout_add(100, update_preview_timeout, dialog);
    g_object_set_data(G_OBJECT(dialog), "preview_update_timeout_id", GUINT_TO_POINTER(timeout_id));
}

/**
 * Callback when any parameter changes - update preview
 */
static void on_parameter_changed(GtkWidget* widget, gpointer user_data) {
    GtkWidget* dialog = (GtkWidget*)user_data;
    ToneMapParams* params = (ToneMapParams*)g_object_get_data(G_OBJECT(dialog), "tone_params");
    const uint8_t* rgbe_data = (const uint8_t*)g_object_get_data(G_OBJECT(dialog), "rgbe_data");
    uint32_t width = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "hdr_width"));
    uint32_t height = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "hdr_height"));

    if (!params || !rgbe_data) {
        return;
    }

    /* Update params from widgets */
    GtkBuilder* builder = GTK_BUILDER(g_object_get_data(G_OBJECT(dialog), "builder"));
    if (!builder) {
        return;
    }

    /* Get current operator */
    GtkWidget* linear_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_operator_button"));
    GtkWidget* filmic_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "filmic_operator_button"));
    GtkWidget* drago_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "drago_operator_button"));
    GtkWidget* reinhard_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "reinhard_operator_button"));

    if (linear_operator_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_operator_button))) {
        params->operator= TONE_MAP_LINEAR;
    } else if (filmic_operator_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(filmic_operator_button))) {
        params->operator= TONE_MAP_FILMIC;
    } else if (drago_operator_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(drago_operator_button))) {
        params->operator= TONE_MAP_DRAGO;
    } else if (reinhard_operator_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(reinhard_operator_button))) {
        params->operator= TONE_MAP_REINHARD;
    }

    /* Get normalize setting */
    GtkWidget* linear_normalize_none_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_none_button"));
    GtkWidget* linear_normalize_visible_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_visible_button"));
    GtkWidget* linear_normalize_full_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_full_button"));

    if (params->operator== TONE_MAP_LINEAR) {
        if (linear_normalize_none_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_normalize_none_button))) {
            params->normalize = TONE_MAP_NORMALIZE_NONE;
        } else if (linear_normalize_visible_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_normalize_visible_button))) {
            params->normalize = TONE_MAP_NORMALIZE_VISIBLE_SPECTRUM;
        } else if (linear_normalize_full_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_normalize_full_button))) {
            params->normalize = TONE_MAP_NORMALIZE_FULL_SPECTRUM;
        }
    }

    /* Get parameter values from adjustments */
    GtkAdjustment* linear_gamma_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "linear_gamma_adjustment"));
    GtkAdjustment* filmic_gamma_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_gamma_adjustment"));
    GtkAdjustment* filmic_exposure_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_exposure_adjustment"));
    GtkAdjustment* filmic_whitepoint_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_whitepoint_adjustment"));
    GtkAdjustment* drago_gamma_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "drago_gamma_adjustment"));
    GtkAdjustment* drago_exposure_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "drago_exposure_adjustment"));
    GtkAdjustment* reinhard_intensity_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_intensity_adjustment"));
    GtkAdjustment* reinhard_adaptation_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_adaptation_adjustment"));
    GtkAdjustment* reinhard_color_correction_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_color_correction_adjustment"));

    if (linear_gamma_adjustment) {
        params->gamma = (float)gtk_adjustment_get_value(linear_gamma_adjustment);
    }
    if (filmic_gamma_adjustment) {
        params->gamma = (float)gtk_adjustment_get_value(filmic_gamma_adjustment);
    }
    if (filmic_exposure_adjustment) {
        params->exposure = (float)gtk_adjustment_get_value(filmic_exposure_adjustment);
    }
    if (filmic_whitepoint_adjustment) {
        params->white_point = (float)gtk_adjustment_get_value(filmic_whitepoint_adjustment);
    }
    if (drago_gamma_adjustment) {
        params->gamma = (float)gtk_adjustment_get_value(drago_gamma_adjustment);
    }
    if (drago_exposure_adjustment) {
        params->exposure = (float)gtk_adjustment_get_value(drago_exposure_adjustment);
    }
    if (reinhard_intensity_adjustment) {
        params->intensity = (float)gtk_adjustment_get_value(reinhard_intensity_adjustment);
    }
    if (reinhard_adaptation_adjustment) {
        params->adaptation = (float)gtk_adjustment_get_value(reinhard_adaptation_adjustment);
    }
    if (reinhard_color_correction_adjustment) {
        params->color_correction = (float)gtk_adjustment_get_value(reinhard_color_correction_adjustment);
    }

    /* Schedule preview update with throttling */
    schedule_preview_update(dialog);
}

/**
 * Show HDR image tone mapping dialog
 */
gint hdr_image_dialog_show(GtkWindow* parent, ToneMapParams* params, gboolean* auto_apply,
                           const uint8_t* rgbe_data, uint32_t width, uint32_t height,
                           void* settings, const char* app_dir) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* linear_operator_button;
    GtkWidget* filmic_operator_button;
    GtkWidget* drago_operator_button;
    GtkWidget* reinhard_operator_button;
    GtkWidget* linear_controls_box;
    GtkWidget* filmic_controls_box;
    GtkWidget* drago_controls_box;
    GtkWidget* reinhard_controls_box;
    GtkWidget* auto_apply_check;
    GtkWidget* linear_normalize_none_button;
    GtkWidget* linear_normalize_visible_button;
    GtkWidget* linear_normalize_full_button;
    GtkAdjustment* linear_gamma_adjustment;
    GtkAdjustment* filmic_gamma_adjustment;
    GtkAdjustment* filmic_exposure_adjustment;
    GtkAdjustment* filmic_whitepoint_adjustment;
    GtkAdjustment* drago_gamma_adjustment;
    GtkAdjustment* drago_exposure_adjustment;
    GtkAdjustment* reinhard_intensity_adjustment;
    GtkAdjustment* reinhard_adaptation_adjustment;
    GtkAdjustment* reinhard_color_correction_adjustment;
    GError* error = NULL;
    gint response;
    GtkToggleButton* operator_buttons[5] = {NULL, NULL, NULL, NULL, NULL};
    GtkToggleButton* normalize_buttons[4] = {NULL, NULL, NULL, NULL};

    if (!params || !auto_apply || !rgbe_data || width == 0 || height == 0) {
        return GTK_RESPONSE_CANCEL;
    }

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/hdr_image_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load hdr_image_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return GTK_RESPONSE_CANCEL;
    }

    /* Get dialog widget */
    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "hdr_image_dialog"));
    if (!dialog) {
        debug_log("WRN", "Failed to get hdr_image_dialog from builder");
        g_object_unref(builder);
        return GTK_RESPONSE_CANCEL;
    }

    /* Store builder reference in dialog for later access */
    g_object_set_data(G_OBJECT(dialog), "builder", builder);

    /* Store RGBE data and dimensions in dialog for preview updates */
    g_object_set_data(G_OBJECT(dialog), "rgbe_data", (gpointer)rgbe_data);
    g_object_set_data(G_OBJECT(dialog), "hdr_width", GUINT_TO_POINTER(width));
    g_object_set_data(G_OBJECT(dialog), "hdr_height", GUINT_TO_POINTER(height));
    g_object_set_data(G_OBJECT(dialog), "tone_params", params);

    /* Store settings for preview background color */
    if (settings) {
        g_object_set_data(G_OBJECT(dialog), "settings", settings);
    }

    /* Replace default titlebar with header bar - must be done before other window properties */
    if (GTK_IS_WINDOW(dialog)) {
        const gchar* title = gtk_window_get_title(GTK_WINDOW(dialog));
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : "HDR Image Tone Mapping");
    }

    /* Get all widgets */
    linear_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_operator_button"));
    filmic_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "filmic_operator_button"));
    drago_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "drago_operator_button"));
    reinhard_operator_button = GTK_WIDGET(gtk_builder_get_object(builder, "reinhard_operator_button"));

    linear_controls_box = GTK_WIDGET(gtk_builder_get_object(builder, "linear_controls_box"));
    filmic_controls_box = GTK_WIDGET(gtk_builder_get_object(builder, "filmic_controls_box"));
    drago_controls_box = GTK_WIDGET(gtk_builder_get_object(builder, "drago_controls_box"));
    reinhard_controls_box = GTK_WIDGET(gtk_builder_get_object(builder, "reinhard_controls_box"));

    auto_apply_check = GTK_WIDGET(gtk_builder_get_object(builder, "auto_apply_settings_check"));

    linear_normalize_none_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_none_button"));
    linear_normalize_visible_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_visible_button"));
    linear_normalize_full_button = GTK_WIDGET(gtk_builder_get_object(builder, "linear_normalize_full_button"));

    linear_gamma_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "linear_gamma_adjustment"));
    filmic_gamma_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_gamma_adjustment"));
    filmic_exposure_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_exposure_adjustment"));
    filmic_whitepoint_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "filmic_whitepoint_adjustment"));
    drago_gamma_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "drago_gamma_adjustment"));
    drago_exposure_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "drago_exposure_adjustment"));
    reinhard_intensity_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_intensity_adjustment"));
    reinhard_adaptation_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_adaptation_adjustment"));
    reinhard_color_correction_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "reinhard_color_correction_adjustment"));

    if (!linear_operator_button || !filmic_operator_button || !drago_operator_button || !reinhard_operator_button) {
        debug_log("WRN", "Failed to get operator buttons from hdr_image_dialog");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return GTK_RESPONSE_CANCEL;
    }

    /* Set up operator toggle button group */
    operator_buttons[0] = GTK_TOGGLE_BUTTON(linear_operator_button);
    operator_buttons[1] = GTK_TOGGLE_BUTTON(filmic_operator_button);
    operator_buttons[2] = GTK_TOGGLE_BUTTON(drago_operator_button);
    operator_buttons[3] = GTK_TOGGLE_BUTTON(reinhard_operator_button);
    operator_buttons[4] = NULL;

    /* Store dialog reference in each button for visibility updates */
    g_object_set_data(G_OBJECT(linear_operator_button), "dialog", dialog);
    g_object_set_data(G_OBJECT(filmic_operator_button), "dialog", dialog);
    g_object_set_data(G_OBJECT(drago_operator_button), "dialog", dialog);
    g_object_set_data(G_OBJECT(reinhard_operator_button), "dialog", dialog);

    /* Connect operator toggle signals */
    g_signal_connect(linear_operator_button, "toggled", G_CALLBACK(on_operator_toggled), operator_buttons);
    g_signal_connect(filmic_operator_button, "toggled", G_CALLBACK(on_operator_toggled), operator_buttons);
    g_signal_connect(drago_operator_button, "toggled", G_CALLBACK(on_operator_toggled), operator_buttons);
    g_signal_connect(reinhard_operator_button, "toggled", G_CALLBACK(on_operator_toggled), operator_buttons);

    /* Set up normalize toggle button group (only for linear operator) */
    if (linear_normalize_none_button && linear_normalize_visible_button && linear_normalize_full_button) {
        normalize_buttons[0] = GTK_TOGGLE_BUTTON(linear_normalize_none_button);
        normalize_buttons[1] = GTK_TOGGLE_BUTTON(linear_normalize_visible_button);
        normalize_buttons[2] = GTK_TOGGLE_BUTTON(linear_normalize_full_button);
        normalize_buttons[3] = NULL;

        g_signal_connect(linear_normalize_none_button, "toggled", G_CALLBACK(on_linear_normalize_toggled), normalize_buttons);
        g_signal_connect(linear_normalize_visible_button, "toggled", G_CALLBACK(on_linear_normalize_toggled), normalize_buttons);
        g_signal_connect(linear_normalize_full_button, "toggled", G_CALLBACK(on_linear_normalize_toggled), normalize_buttons);
    }

    /* Initialize values from params (params may already be loaded from settings by plugin) */
    switch (params->operator) {
        case TONE_MAP_LINEAR:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(linear_operator_button), TRUE);
            break;
        case TONE_MAP_FILMIC:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(filmic_operator_button), TRUE);
            break;
        case TONE_MAP_DRAGO:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(drago_operator_button), TRUE);
            break;
        case TONE_MAP_REINHARD:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reinhard_operator_button), TRUE);
            break;
    }

    /* Set normalize buttons based on params */
    if (linear_normalize_none_button && linear_normalize_visible_button && linear_normalize_full_button) {
        switch (params->normalize) {
            case TONE_MAP_NORMALIZE_NONE:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(linear_normalize_none_button), TRUE);
                break;
            case TONE_MAP_NORMALIZE_VISIBLE_SPECTRUM:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(linear_normalize_visible_button), TRUE);
                break;
            case TONE_MAP_NORMALIZE_FULL_SPECTRUM:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(linear_normalize_full_button), TRUE);
                break;
        }
    }

    /* Set adjustment values */
    if (linear_gamma_adjustment) {
        gtk_adjustment_set_value(linear_gamma_adjustment, params->gamma);
    }
    if (filmic_gamma_adjustment) {
        gtk_adjustment_set_value(filmic_gamma_adjustment, params->gamma);
    }
    if (filmic_exposure_adjustment) {
        gtk_adjustment_set_value(filmic_exposure_adjustment, params->exposure);
    }
    if (filmic_whitepoint_adjustment) {
        gtk_adjustment_set_value(filmic_whitepoint_adjustment, params->white_point);
    }
    if (drago_gamma_adjustment) {
        gtk_adjustment_set_value(drago_gamma_adjustment, params->gamma);
    }
    if (drago_exposure_adjustment) {
        gtk_adjustment_set_value(drago_exposure_adjustment, params->exposure);
    }
    if (reinhard_intensity_adjustment) {
        gtk_adjustment_set_value(reinhard_intensity_adjustment, params->intensity);
    }
    if (reinhard_adaptation_adjustment) {
        gtk_adjustment_set_value(reinhard_adaptation_adjustment, params->adaptation);
    }
    if (reinhard_color_correction_adjustment) {
        gtk_adjustment_set_value(reinhard_color_correction_adjustment, params->color_correction);
    }

    /* Set auto apply checkbox - load from settings if available */
    if (auto_apply_check) {
        gboolean initial_auto_apply = *auto_apply;
        if (settings) {
            Settings* s = (Settings*)settings;
            initial_auto_apply = settings_get_tone_map_auto_apply(s);
        }
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_apply_check), initial_auto_apply);
        *auto_apply = initial_auto_apply;
    }

    /* Get preview container  and connect draw signal */
    GtkWidget* preview_area = GTK_WIDGET(gtk_builder_get_object(builder, "preview_container"));
    if (preview_area) {
        /* Connect draw signal */
        g_signal_connect(preview_area, "draw", G_CALLBACK(on_preview_draw), NULL);
    } else {
        debug_log("WRN", "Failed to get preview_container from hdr_image_dialog");
    }

    /* Connect parameter change signals to update preview */
    if (linear_gamma_adjustment) {
        g_signal_connect(linear_gamma_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (filmic_gamma_adjustment) {
        g_signal_connect(filmic_gamma_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (filmic_exposure_adjustment) {
        g_signal_connect(filmic_exposure_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (filmic_whitepoint_adjustment) {
        g_signal_connect(filmic_whitepoint_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (drago_gamma_adjustment) {
        g_signal_connect(drago_gamma_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (drago_exposure_adjustment) {
        g_signal_connect(drago_exposure_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (reinhard_intensity_adjustment) {
        g_signal_connect(reinhard_intensity_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (reinhard_adaptation_adjustment) {
        g_signal_connect(reinhard_adaptation_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (reinhard_color_correction_adjustment) {
        g_signal_connect(reinhard_color_correction_adjustment, "value-changed", G_CALLBACK(on_parameter_changed), dialog);
    }

    /* Connect operator toggle signals for preview updates (in addition to existing visibility updates) */
    /* Note: on_operator_toggled already handles visibility, so we connect to both */
    g_signal_connect_after(linear_operator_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);
    g_signal_connect_after(filmic_operator_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);
    g_signal_connect_after(drago_operator_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);
    g_signal_connect_after(reinhard_operator_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);

    /* Connect normalize toggle signals for preview updates */
    if (linear_normalize_none_button) {
        g_signal_connect(linear_normalize_none_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (linear_normalize_visible_button) {
        g_signal_connect(linear_normalize_visible_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);
    }
    if (linear_normalize_full_button) {
        g_signal_connect(linear_normalize_full_button, "toggled", G_CALLBACK(on_parameter_changed), dialog);
    }

    /* Connect OK and Cancel buttons - buttons are already in dialog from Glade */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "hdr_image_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "hdr_image_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_dialog_button_clicked), dialog);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_dialog_button_clicked), dialog);
    }
    /* Match filter dialog: action area with 5px vertical margin; 0 horizontal so reset aligns with content */
    GtkWidget* action_area = GTK_WIDGET(gtk_builder_get_object(builder, "hdr_image_dialog_action_area"));
    if (action_area) {
        gtk_widget_set_margin_top(action_area, 5);
        gtk_widget_set_margin_bottom(action_area, 5);
        gtk_widget_set_margin_start(action_area, 0);
        gtk_widget_set_margin_end(action_area, 0);
        gtk_widget_set_hexpand(action_area, TRUE);
    }
    GtkWidget* reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "hdr_reset_button"));
    if (reset_button) {
        g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset_clicked), dialog);
        gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
        gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
        gtk_widget_set_valign(reset_button, GTK_ALIGN_CENTER);
    }

    /* Create initial preview */
    update_preview(dialog, rgbe_data, width, height, params);

    /* Set parent window and modal before showing */
    if (parent && GTK_IS_WINDOW(parent) && GTK_IS_WINDOW(dialog)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), HDR_DIALOG_WIDTH, HDR_DIALOG_HEIGHT);
    gtk_widget_set_size_request(dialog, HDR_DIALOG_WIDTH, HDR_DIALOG_HEIGHT);
    {
        GdkGeometry geometry;
        memset(&geometry, 0, sizeof(geometry));
        geometry.min_width = geometry.max_width = HDR_DIALOG_WIDTH;
        geometry.min_height = geometry.max_height = HDR_DIALOG_HEIGHT;
        gtk_window_set_geometry_hints(GTK_WINDOW(dialog), NULL, &geometry, GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
    }
    g_signal_connect(dialog, "map", G_CALLBACK(on_dialog_map_center_and_clamp), parent);
    g_signal_connect(dialog, "configure-event", G_CALLBACK(on_dialog_configure_event), NULL);

    gtk_widget_show_all(dialog);

    /* Initial visibility update */
    update_controls_visibility(dialog, params->operator);

    /* Clear response-handled flag before showing dialog */
    g_object_set_data(G_OBJECT(dialog), "response-handled", NULL);

    /* Show dialog */
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    /* Clear response-handled flag after dialog returns */
    g_object_set_data(G_OBJECT(dialog), "response-handled", NULL);

    if (response == GTK_RESPONSE_OK) {
        /* Get selected operator */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_operator_button))) {
            params->operator= TONE_MAP_LINEAR;
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(filmic_operator_button))) {
            params->operator= TONE_MAP_FILMIC;
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(drago_operator_button))) {
            params->operator= TONE_MAP_DRAGO;
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(reinhard_operator_button))) {
            params->operator= TONE_MAP_REINHARD;
        }

        /* Get normalize setting (only for linear) */
        if (params->operator== TONE_MAP_LINEAR) {
            if (linear_normalize_none_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_normalize_none_button))) {
                params->normalize = TONE_MAP_NORMALIZE_NONE;
            } else if (linear_normalize_visible_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_normalize_visible_button))) {
                params->normalize = TONE_MAP_NORMALIZE_VISIBLE_SPECTRUM;
            } else if (linear_normalize_full_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(linear_normalize_full_button))) {
                params->normalize = TONE_MAP_NORMALIZE_FULL_SPECTRUM;
            }
        } else {
            params->normalize = TONE_MAP_NORMALIZE_NONE;
        }

        /* Get parameter values */
        if (linear_gamma_adjustment) {
            params->gamma = (float)gtk_adjustment_get_value(linear_gamma_adjustment);
        }
        if (filmic_gamma_adjustment) {
            params->gamma = (float)gtk_adjustment_get_value(filmic_gamma_adjustment);
        }
        if (filmic_exposure_adjustment) {
            params->exposure = (float)gtk_adjustment_get_value(filmic_exposure_adjustment);
        }
        if (filmic_whitepoint_adjustment) {
            params->white_point = (float)gtk_adjustment_get_value(filmic_whitepoint_adjustment);
        }
        if (drago_gamma_adjustment) {
            params->gamma = (float)gtk_adjustment_get_value(drago_gamma_adjustment);
        }
        if (drago_exposure_adjustment) {
            params->exposure = (float)gtk_adjustment_get_value(drago_exposure_adjustment);
        }
        if (reinhard_intensity_adjustment) {
            params->intensity = (float)gtk_adjustment_get_value(reinhard_intensity_adjustment);
        }
        if (reinhard_adaptation_adjustment) {
            params->adaptation = (float)gtk_adjustment_get_value(reinhard_adaptation_adjustment);
        }
        if (reinhard_color_correction_adjustment) {
            params->color_correction = (float)gtk_adjustment_get_value(reinhard_color_correction_adjustment);
        }

        /* Get auto apply checkbox value */
        if (auto_apply_check) {
            *auto_apply = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(auto_apply_check));

            /* If auto apply is checked, save settings */
            if (*auto_apply && settings && app_dir) {
                Settings* s = (Settings*)settings;

                /* Save tone mapping settings */
                settings_set_tone_map_auto_apply(s, TRUE);
                settings_set_tone_map_operator(s, (gint)params->operator);
                settings_set_tone_map_normalize(s, (gint)params->normalize);
                settings_set_tone_map_gamma(s, (gdouble)params->gamma);
                settings_set_tone_map_exposure(s, (gdouble)params->exposure);
                settings_set_tone_map_white_point(s, (gdouble)params->white_point);
                settings_set_tone_map_intensity(s, (gdouble)params->intensity);
                settings_set_tone_map_adaptation(s, (gdouble)params->adaptation);
                settings_set_tone_map_color_correction(s, (gdouble)params->color_correction);

                /* Save to file */
                settings_save(s, app_dir);
            }
        }
    }

    /* Cancel any pending preview update */
    guint timeout_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "preview_update_timeout_id"));
    if (timeout_id > 0) {
        g_source_remove(timeout_id);
    }

    /* Cleanup preview surface */
    preview_area = GTK_WIDGET(gtk_builder_get_object(builder, "preview_container"));
    if (preview_area) {
        /* Remove the data (which will trigger the destroy notify if set_data_full was used) */
        cairo_surface_t* preview_surface = (cairo_surface_t*)g_object_steal_data(G_OBJECT(preview_area), "preview_surface");
        if (preview_surface) {
            /* Surface will be destroyed by g_object_steal_data if it was set with set_data_full,
             * but we need to destroy it manually since we stole it */
            cairo_surface_destroy(preview_surface);
        }
    }

    /* Cleanup */
    gtk_widget_destroy(dialog);
    g_object_unref(builder);

    return response;
}
