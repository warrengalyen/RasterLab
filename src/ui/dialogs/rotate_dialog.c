/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/rotate_dialog.h"
#include "i18n.h"
#include "ui/ui_utils.h"
#include "ui/widgets/vertical_spin_button.h"
#include <glib.h>

struct _RotateDialog {
    GtkWidget* dialog;
    FilterPreview* preview;

    /* Controls */
    GtkWidget* angle_scale;
    GtkWidget* angle_spin;
    GtkWidget* angle_reset_button;

    GtkWidget* size_preserve_radio;
    GtkWidget* size_enlarge_radio;

    GtkWidget* quality_nearest_radio;
    GtkWidget* quality_bilinear_radio;
    GtkWidget* quality_bicubic_radio;

    GtkWidget* border_transparent_radio;
    GtkWidget* border_fill_radio;
    GtkWidget* fill_color_button;

    cairo_surface_t* before_surface; /* owned ref */

    RotateDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

static void trigger_preview(RotateDialog* dialog) {
    if (!dialog || !dialog->preview_callback) {
        return;
    }

    gdouble angle = gtk_range_get_value(GTK_RANGE(dialog->angle_scale));
    gboolean preserve_size = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->size_preserve_radio));

    OcInterpolationMode interpolation = OC_INTERPOLATE_NEAREST;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->quality_bilinear_radio))) {
        interpolation = OC_INTERPOLATE_BILINEAR;
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->quality_bicubic_radio))) {
        interpolation = OC_INTERPOLATE_BICUBIC;
    }

    gboolean use_transparency = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->border_transparent_radio));

    GdkRGBA fill_color = {0};
    if (dialog->fill_color_button) {
        get_custom_color_button_color(dialog->fill_color_button, &fill_color);
    }

    dialog->preview_callback(dialog, angle, preserve_size, interpolation, use_transparency, &fill_color, dialog->preview_user_data);
}

static void update_fill_color_visibility(RotateDialog* dialog) {
    if (!dialog || !dialog->fill_color_button) {
        return;
    }
    gboolean show = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->border_fill_radio));
    gtk_widget_set_visible(dialog->fill_color_button, show);
}

static void on_angle_scale_changed(GtkRange* range, gpointer user_data) {
    RotateDialog* dialog = (RotateDialog*)user_data;
    if (!dialog || !dialog->angle_spin) {
        return;
    }
    gdouble value = gtk_range_get_value(range);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->angle_spin), value);
    trigger_preview(dialog);
}

static void on_angle_spin_changed(GtkWidget* spin, gpointer user_data) {
    RotateDialog* dialog = (RotateDialog*)user_data;
    if (!dialog || !dialog->angle_scale) {
        return;
    }
    gdouble value = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    gtk_range_set_value(GTK_RANGE(dialog->angle_scale), value);
}

static void on_angle_reset_clicked(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    RotateDialog* dialog = (RotateDialog*)user_data;
    if (!dialog || !dialog->angle_scale) {
        return;
    }
    gtk_range_set_value(GTK_RANGE(dialog->angle_scale), 0.0);
}

static void on_radio_changed(GtkToggleButton* toggle, gpointer user_data) {
    (void)toggle;
    RotateDialog* dialog = (RotateDialog*)user_data;
    if (!dialog) {
        return;
    }
    update_fill_color_visibility(dialog);
    trigger_preview(dialog);
}

static void on_fill_color_set(GtkWidget* button, gpointer user_data) {
    RotateDialog* dialog = (RotateDialog*)user_data;
    trigger_preview(dialog);
}

static GtkWidget* create_linked_radio_row(const gchar* a_label,
                                          GtkWidget** out_a,
                                          const gchar* b_label,
                                          GtkWidget** out_b) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkStyleContext* ctx = gtk_widget_get_style_context(box);
    gtk_style_context_add_class(ctx, "linked");

    GtkWidget* a = gtk_radio_button_new_with_label(NULL, a_label);
    GtkWidget* b = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(a), b_label);

    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(a), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);

    gtk_widget_set_hexpand(a, TRUE);
    gtk_widget_set_hexpand(b, TRUE);
    gtk_box_pack_start(GTK_BOX(box), a, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), b, TRUE, TRUE, 0);

    if (out_a)
        *out_a = a;
    if (out_b)
        *out_b = b;
    return box;
}

static GtkWidget* create_linked_radio_row3(const gchar* a_label, GtkWidget** out_a,
                                           const gchar* b_label, GtkWidget** out_b,
                                           const gchar* c_label, GtkWidget** out_c) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkStyleContext* ctx = gtk_widget_get_style_context(box);
    gtk_style_context_add_class(ctx, "linked");

    GtkWidget* a = gtk_radio_button_new_with_label(NULL, a_label);
    GtkWidget* b = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(a), b_label);
    GtkWidget* c = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(a), c_label);

    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(a), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(c), FALSE);

    gtk_widget_set_hexpand(a, TRUE);
    gtk_widget_set_hexpand(b, TRUE);
    gtk_widget_set_hexpand(c, TRUE);
    gtk_box_pack_start(GTK_BOX(box), a, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), b, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), c, TRUE, TRUE, 0);

    if (out_a)
        *out_a = a;
    if (out_b)
        *out_b = b;
    if (out_c)
        *out_c = c;
    return box;
}

RotateDialog* rotate_dialog_new(const gchar* title) {
    if (!title) {
        return NULL;
    }

    RotateDialog* dialog = (RotateDialog*)g_malloc0(sizeof(RotateDialog));
    if (!dialog) {
        return NULL;
    }

    dialog->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog->dialog), title);

    /* Replace default titlebar with header bar - must be done before other window properties */
    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), title);

    gtk_dialog_add_button(GTK_DIALOG(dialog->dialog), "_OK", GTK_RESPONSE_OK);
    gtk_dialog_add_button(GTK_DIALOG(dialog->dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_window_set_modal(GTK_WINDOW(dialog->dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog->dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog->dialog), TRUE);

    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

    GtkWidget* main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    /* Preview (left) */
    dialog->preview = FILTER_PREVIEW(filter_preview_new());
    filter_preview_set_allow_zoom_pan(dialog->preview, FALSE);
    gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), 375, 338);
    gtk_box_pack_start(GTK_BOX(main_hbox), GTK_WIDGET(dialog->preview), FALSE, FALSE, 0);

    /* Controls (right) */
    GtkWidget* right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right_vbox, 320, -1);
    gtk_widget_set_hexpand(right_vbox, TRUE);
    gtk_widget_set_halign(right_vbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, TRUE, TRUE, 0);

    /* Angle */
    {
        GtkWidget* control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_bottom(control_vbox, 10);
        gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

        GtkWidget* label = gtk_label_new(_("angle"));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_bottom(label, 3);
        gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_widget_set_hexpand(row, TRUE);
        gtk_widget_set_halign(row, GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(control_vbox), row, TRUE, TRUE, 0);

        GtkAdjustment* adj = gtk_adjustment_new(0.0, -360.0, 360.0, 1.0, 10.0, 0.0);
        dialog->angle_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
        gtk_scale_set_draw_value(GTK_SCALE(dialog->angle_scale), FALSE);
        gtk_scale_set_digits(GTK_SCALE(dialog->angle_scale), 2);
        gtk_widget_set_hexpand(dialog->angle_scale, TRUE);
        gtk_widget_set_halign(dialog->angle_scale, GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(row), dialog->angle_scale, TRUE, TRUE, 0);
        g_signal_connect(dialog->angle_scale, "value-changed", G_CALLBACK(on_angle_scale_changed), dialog);

        dialog->angle_spin = vertical_spin_button_new(adj, 1.0, 2);
        gtk_widget_set_size_request(dialog->angle_spin, 70, -1);
        gtk_widget_set_hexpand(dialog->angle_spin, FALSE);
        gtk_widget_set_halign(dialog->angle_spin, GTK_ALIGN_END);
        gtk_box_pack_end(GTK_BOX(row), dialog->angle_spin, FALSE, FALSE, 0);
        g_signal_connect(dialog->angle_spin, "value-changed", G_CALLBACK(on_angle_spin_changed), dialog);

        dialog->angle_reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.png");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(dialog->angle_reset_button), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(dialog->angle_reset_button), TRUE);
        } else {
            gtk_button_set_label(GTK_BUTTON(dialog->angle_reset_button), "↺");
        }
        gtk_box_pack_start(GTK_BOX(row), dialog->angle_reset_button, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(row), dialog->angle_scale, 0);
        gtk_box_reorder_child(GTK_BOX(row), dialog->angle_spin, 1);
        gtk_box_reorder_child(GTK_BOX(row), dialog->angle_reset_button, 2);
        g_signal_connect(dialog->angle_reset_button, "clicked", G_CALLBACK(on_angle_reset_clicked), dialog);
    }

    /* Size */
    {
        GtkWidget* control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_bottom(control_vbox, 10);
        gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

        GtkWidget* label = gtk_label_new(_("size"));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_bottom(label, 3);
        gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

        GtkWidget* row = create_linked_radio_row("preserve", &dialog->size_preserve_radio,
                                                 "enlarge to fit", &dialog->size_enlarge_radio);
        gtk_box_pack_start(GTK_BOX(control_vbox), row, FALSE, FALSE, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->size_enlarge_radio), TRUE);
        g_signal_connect(dialog->size_preserve_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);
        g_signal_connect(dialog->size_enlarge_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);
    }

    /* Quality */
    {
        GtkWidget* control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_bottom(control_vbox, 10);
        gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

        GtkWidget* label = gtk_label_new(_("quality"));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_bottom(label, 3);
        gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

        GtkWidget* row = create_linked_radio_row3("nearest-neighbor", &dialog->quality_nearest_radio,
                                                  "bilinear", &dialog->quality_bilinear_radio,
                                                  "bicubic", &dialog->quality_bicubic_radio);
        gtk_box_pack_start(GTK_BOX(control_vbox), row, FALSE, FALSE, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->quality_nearest_radio), TRUE);

        g_signal_connect(dialog->quality_nearest_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);
        g_signal_connect(dialog->quality_bilinear_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);
        g_signal_connect(dialog->quality_bicubic_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);
    }

    /* Border regions */
    {
        GtkWidget* control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_bottom(control_vbox, 10);
        gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

        GtkWidget* label = gtk_label_new(_("border regions"));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_bottom(label, 3);
        gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

        GtkWidget* row = create_linked_radio_row("transparent", &dialog->border_transparent_radio,
                                                 "fill with color", &dialog->border_fill_radio);
        gtk_box_pack_start(GTK_BOX(control_vbox), row, FALSE, FALSE, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->border_transparent_radio), TRUE);
        g_signal_connect(dialog->border_transparent_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);
        g_signal_connect(dialog->border_fill_radio, "toggled", G_CALLBACK(on_radio_changed), dialog);

        GdkRGBA fill_color = {1.0, 1.0, 1.0, 1.0}; // White default
        dialog->fill_color_button = create_custom_color_button(
            GTK_WINDOW(dialog->dialog),
            &fill_color,
            on_fill_color_set,
            dialog);
        gtk_widget_set_hexpand(dialog->fill_color_button, TRUE);
        gtk_widget_set_size_request(dialog->fill_color_button, -1, 35);
        gtk_box_pack_start(GTK_BOX(control_vbox), dialog->fill_color_button, FALSE, FALSE, 0);
        update_fill_color_visibility(dialog);
    }

    gtk_widget_show_all(content_area);
    /* Ensure color button starts hidden if transparent is selected */
    update_fill_color_visibility(dialog);

    return dialog;
}

void rotate_dialog_free(RotateDialog* dialog) {
    if (!dialog) {
        return;
    }
    if (dialog->before_surface) {
        cairo_surface_destroy(dialog->before_surface);
        dialog->before_surface = NULL;
    }
    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }
    g_free(dialog);
}

GtkWindow* rotate_dialog_get_window(RotateDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

FilterPreview* rotate_dialog_get_preview(RotateDialog* dialog) {
    if (!dialog) {
        return NULL;
    }
    return dialog->preview;
}

void rotate_dialog_set_before_surface(RotateDialog* dialog, cairo_surface_t* before_surface) {
    if (!dialog || !dialog->preview) {
        return;
    }
    if (dialog->before_surface) {
        cairo_surface_destroy(dialog->before_surface);
        dialog->before_surface = NULL;
    }
    if (before_surface) {
        dialog->before_surface = cairo_surface_reference(before_surface);
    }
    filter_preview_set_before_surface(dialog->preview, before_surface);
}

cairo_surface_t* rotate_dialog_get_before_surface(RotateDialog* dialog) {
    if (!dialog || !dialog->before_surface) {
        return NULL;
    }
    return dialog->before_surface;
}

void rotate_dialog_set_after_surface(RotateDialog* dialog, cairo_surface_t* after_surface) {
    if (!dialog || !dialog->preview) {
        return;
    }
    filter_preview_set_after_surface(dialog->preview, after_surface);
}

void rotate_dialog_set_preview_callback(RotateDialog* dialog,
                                        RotateDialogPreviewCallback callback,
                                        gpointer user_data) {
    if (!dialog) {
        return;
    }
    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
    trigger_preview(dialog);
}

void rotate_dialog_reset(RotateDialog* dialog) {
    if (!dialog) {
        return;
    }
    gtk_range_set_value(GTK_RANGE(dialog->angle_scale), 0.0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->size_enlarge_radio), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->quality_nearest_radio), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->border_transparent_radio), TRUE);
    if (dialog->fill_color_button) {
        GdkRGBA c = {0.2, 0.2, 0.2, 1.0};
        set_custom_color_button_color(dialog->fill_color_button, &c);
    }
    update_fill_color_visibility(dialog);
    trigger_preview(dialog);
}

gint rotate_dialog_run(RotateDialog* dialog,
                       GtkWindow* parent,
                       gdouble* out_angle_degrees,
                       gboolean* out_preserve_size,
                       OcInterpolationMode* out_interpolation,
                       gboolean* out_use_transparency,
                       GdkRGBA* out_fill_color) {
    if (!dialog || !dialog->dialog) {
        return GTK_RESPONSE_CANCEL;
    }
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    gint response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));
    gtk_widget_hide(dialog->dialog);

    if (response == GTK_RESPONSE_OK) {
        if (out_angle_degrees) {
            *out_angle_degrees = gtk_range_get_value(GTK_RANGE(dialog->angle_scale));
        }
        if (out_preserve_size) {
            *out_preserve_size = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->size_preserve_radio));
        }
        if (out_interpolation) {
            OcInterpolationMode interpolation = OC_INTERPOLATE_NEAREST;
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->quality_bilinear_radio))) {
                interpolation = OC_INTERPOLATE_BILINEAR;
            } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->quality_bicubic_radio))) {
                interpolation = OC_INTERPOLATE_BICUBIC;
            }
            *out_interpolation = interpolation;
        }
        if (out_use_transparency) {
            *out_use_transparency = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->border_transparent_radio));
        }
        if (out_fill_color) {
            if (dialog->fill_color_button) {
                get_custom_color_button_color(dialog->fill_color_button, out_fill_color);
            } else {
                out_fill_color->red = out_fill_color->green = out_fill_color->blue = 0.0;
                out_fill_color->alpha = 1.0;
            }
        }
    }

    return response;
}
