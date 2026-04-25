/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/color_balance_dialog.h"
#include "i18n.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_utils.h"
#include "ui/ui_utils.h"
#include "ui/widgets/filter_preview.h"
#include "ui/widgets/vertical_spin_button.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * Color balance dialog structure
 */
struct _ColorBalanceDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* red_scale;
    GtkWidget* green_scale;
    GtkWidget* blue_scale;
    GtkWidget* red_spin;
    GtkWidget* green_spin;
    GtkWidget* blue_spin;
    GtkWidget* shadows_radio;
    GtkWidget* midtones_radio;
    GtkWidget* highlights_radio;
    GtkWidget* preserve_luminosity_checkbox;
    OcToneBalanceMode tone_mode;
    gboolean preserve_luminosity;
    ColorBalanceDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/**
 * Red scale value changed callback
 */
static void on_red_scale_changed(GtkRange* range, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gint value;

    if (!dialog || !dialog->red_spin) {
        return;
    }

    value = (gint)gtk_range_get_value(range);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->red_spin), (gdouble)value);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gint values[3];
        values[0] = (gint)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        values[1] = (gint)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        values[2] = (gint)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        dialog->preview_callback(dialog, values, 3, dialog->tone_mode, dialog->preserve_luminosity, dialog->preview_user_data);
    }
}

/**
 * Green scale value changed callback
 */
static void on_green_scale_changed(GtkRange* range, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gint value;

    if (!dialog || !dialog->green_spin) {
        return;
    }

    value = (gint)gtk_range_get_value(range);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->green_spin), (gdouble)value);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gint values[3];
        values[0] = (gint)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        values[1] = (gint)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        values[2] = (gint)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        dialog->preview_callback(dialog, values, 3, dialog->tone_mode, dialog->preserve_luminosity, dialog->preview_user_data);
    }
}

/**
 * Blue scale value changed callback
 */
static void on_blue_scale_changed(GtkRange* range, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gint value;

    if (!dialog || !dialog->blue_spin) {
        return;
    }

    value = (gint)gtk_range_get_value(range);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->blue_spin), (gdouble)value);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gint values[3];
        values[0] = (gint)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        values[1] = (gint)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        values[2] = (gint)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        dialog->preview_callback(dialog, values, 3, dialog->tone_mode, dialog->preserve_luminosity, dialog->preview_user_data);
    }
}

/**
 * Red spin value changed callback
 */
static void on_red_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gint value;

    if (!dialog || !dialog->red_scale) {
        return;
    }

    value = (gint)vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    gtk_range_set_value(GTK_RANGE(dialog->red_scale), (gdouble)value);
}

/**
 * Green spin value changed callback
 */
static void on_green_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gint value;

    if (!dialog || !dialog->green_scale) {
        return;
    }

    value = (gint)vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    gtk_range_set_value(GTK_RANGE(dialog->green_scale), (gdouble)value);
}

/**
 * Blue spin value changed callback
 */
static void on_blue_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gint value;

    if (!dialog || !dialog->blue_scale) {
        return;
    }

    value = (gint)vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    gtk_range_set_value(GTK_RANGE(dialog->blue_scale), (gdouble)value);
}

/**
 * Tone balance mode changed callback
 */
static void on_tone_mode_changed(GtkToggleButton* button, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    gpointer button_widget = GTK_WIDGET(button);

    if (!dialog || !gtk_toggle_button_get_active(button)) {
        return;
    }

    if (button_widget == dialog->shadows_radio) {
        dialog->tone_mode = SHADOWS;
    } else if (button_widget == dialog->midtones_radio) {
        dialog->tone_mode = MIDTONES;
    } else if (button_widget == dialog->highlights_radio) {
        dialog->tone_mode = HIGHLIGHTS;
    }

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gint values[3];
        values[0] = (gint)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        values[1] = (gint)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        values[2] = (gint)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        dialog->preview_callback(dialog, values, 3, dialog->tone_mode, dialog->preserve_luminosity, dialog->preview_user_data);
    }
}

/**
 * Preserve luminosity checkbox toggled callback
 */
static void on_preserve_luminosity_toggled(GtkToggleButton* button, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;

    if (!dialog) {
        return;
    }

    dialog->preserve_luminosity = gtk_toggle_button_get_active(button);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gint values[3];
        values[0] = (gint)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        values[1] = (gint)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        values[2] = (gint)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        dialog->preview_callback(dialog, values, 3, dialog->tone_mode, dialog->preserve_luminosity, dialog->preview_user_data);
    }
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)user_data;
    (void)widget;
    color_balance_dialog_reset(dialog);
}

/**
 * Create a new color balance dialog
 */
ColorBalanceDialog* color_balance_dialog_new(const gchar* title) {
    ColorBalanceDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adjustment;
    GtkWidget* tone_vbox;
    GtkWidget* tone_label;
    GtkWidget* preserve_hbox;
    GtkWidget* preserve_label;
    GtkWidget* reset_button;
    GtkWidget* button_box;

    if (!title) {
        return NULL;
    }

    dialog = (ColorBalanceDialog*)g_malloc(sizeof(ColorBalanceDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->red_scale = NULL;
    dialog->green_scale = NULL;
    dialog->blue_scale = NULL;
    dialog->red_spin = NULL;
    dialog->green_spin = NULL;
    dialog->blue_spin = NULL;
    dialog->shadows_radio = NULL;
    dialog->midtones_radio = NULL;
    dialog->highlights_radio = NULL;
    dialog->preserve_luminosity_checkbox = NULL;
    dialog->tone_mode = MIDTONES;
    dialog->preserve_luminosity = TRUE;
    dialog->preview_callback = NULL;
    dialog->preview_user_data = NULL;

    /* Create dialog window */
    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK",
                                                 GTK_RESPONSE_OK,
                                                 "_Cancel",
                                                 GTK_RESPONSE_CANCEL,
                                                 NULL);

    /* Replace default titlebar with header bar - must be done before other window properties */
    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), title);

    /* Don't set a fixed default size - let dialog size to content */
    gtk_window_set_resizable(GTK_WINDOW(dialog->dialog), TRUE);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

    /* Create main horizontal box */
    main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    /* Create filter preview widget (left side) */
    dialog->preview = FILTER_PREVIEW(filter_preview_new());
    gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), 375, 338);
    gtk_widget_set_hexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_box_pack_start(GTK_BOX(main_hbox), GTK_WIDGET(dialog->preview), FALSE, FALSE, 0);

    /* Create right side vertical box */
    right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right_vbox, 320, -1);
    gtk_widget_set_margin_top(right_vbox, 10);
    gtk_widget_set_margin_bottom(right_vbox, 10);
    gtk_widget_set_margin_start(right_vbox, 10);
    gtk_widget_set_margin_end(right_vbox, 10);
    gtk_widget_set_hexpand(right_vbox, TRUE);
    gtk_widget_set_halign(right_vbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, TRUE, TRUE, 0);

    /* Create red control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("red"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(scale_hbox, TRUE);
    gtk_widget_set_halign(scale_hbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(0.0, -100.0, 100.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_halign(scale, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->red_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_red_scale_changed), dialog);

    spin = vertical_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_widget_set_hexpand(spin, FALSE);
    gtk_widget_set_halign(spin, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->red_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_red_spin_changed), dialog);

    /* Create green control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("green"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(scale_hbox, TRUE);
    gtk_widget_set_halign(scale_hbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(0.0, -100.0, 100.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_halign(scale, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->green_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_green_scale_changed), dialog);

    spin = vertical_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_widget_set_hexpand(spin, FALSE);
    gtk_widget_set_halign(spin, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->green_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_green_spin_changed), dialog);

    /* Create blue control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("blue"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(scale_hbox, TRUE);
    gtk_widget_set_halign(scale_hbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(0.0, -100.0, 100.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_halign(scale, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->blue_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_blue_scale_changed), dialog);

    spin = vertical_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_widget_set_hexpand(spin, FALSE);
    gtk_widget_set_halign(spin, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->blue_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_blue_spin_changed), dialog);

    /* Create tone balance mode selector */
    tone_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(tone_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), tone_vbox, FALSE, FALSE, 0);

    tone_label = gtk_label_new(_("tone balance:"));
    gtk_widget_set_halign(tone_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(tone_label, 3);
    gtk_box_pack_start(GTK_BOX(tone_vbox), tone_label, FALSE, FALSE, 0);

    dialog->shadows_radio = gtk_radio_button_new_with_label(NULL, "Shadows");
    gtk_box_pack_start(GTK_BOX(tone_vbox), dialog->shadows_radio, FALSE, FALSE, 0);
    g_signal_connect(dialog->shadows_radio, "toggled", G_CALLBACK(on_tone_mode_changed), dialog);

    dialog->midtones_radio = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(dialog->shadows_radio), "Midtones");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->midtones_radio), TRUE);
    gtk_box_pack_start(GTK_BOX(tone_vbox), dialog->midtones_radio, FALSE, FALSE, 0);
    g_signal_connect(dialog->midtones_radio, "toggled", G_CALLBACK(on_tone_mode_changed), dialog);

    dialog->highlights_radio = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(dialog->shadows_radio), "Highlights");
    gtk_box_pack_start(GTK_BOX(tone_vbox), dialog->highlights_radio, FALSE, FALSE, 0);
    g_signal_connect(dialog->highlights_radio, "toggled", G_CALLBACK(on_tone_mode_changed), dialog);

    /* Create preserve luminosity checkbox */
    preserve_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(right_vbox), preserve_hbox, FALSE, FALSE, 0);

    dialog->preserve_luminosity_checkbox = gtk_check_button_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->preserve_luminosity_checkbox), TRUE);
    gtk_box_pack_start(GTK_BOX(preserve_hbox), dialog->preserve_luminosity_checkbox, FALSE, FALSE, 0);
    g_signal_connect(dialog->preserve_luminosity_checkbox, "toggled", G_CALLBACK(on_preserve_luminosity_toggled), dialog);

    preserve_label = gtk_label_new(_("preserve luminosity"));
    gtk_box_pack_start(GTK_BOX(preserve_hbox), preserve_label, FALSE, FALSE, 0);

/* Get button box from dialog (for OK/Cancel) */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    button_box = gtk_dialog_get_action_area(GTK_DIALOG(dialog->dialog));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    if (button_box) {
        gtk_widget_set_margin_top(button_box, 5);
        gtk_widget_set_margin_bottom(button_box, 5);
        gtk_widget_set_margin_start(button_box, 5);
        gtk_widget_set_margin_end(button_box, 5);

        /* Make action area expand horizontally to fill width */
        gtk_widget_set_hexpand(button_box, TRUE);

        /* Create reset button with icon and add it to the left side of action area */
        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.png");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(reset_button), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
        }
        gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(button_box), reset_button, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(button_box), reset_button, 0); /* Move to start */
        g_signal_connect(reset_button, "clicked",
                         G_CALLBACK(on_reset_clicked), dialog);
    }

    /* Show all widgets */
    gtk_widget_show_all(content_area);

    return dialog;
}

/**
 * Free color balance dialog
 */
void color_balance_dialog_free(ColorBalanceDialog* dialog) {
    if (!dialog) {
        return;
    }

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    g_free(dialog);
}

/**
 * Get the dialog window
 */
GtkWindow* color_balance_dialog_get_window(ColorBalanceDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }

    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the image layers for the preview widget
 */
void color_balance_dialog_set_layers(ColorBalanceDialog* dialog,
                                     ImageLayer* before_layer,
                                     ImageLayer* after_layer) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get composite surfaces from layers - pass full unmasked surfaces
       The preview widget will handle masking display based on selection */
    if (before_layer && before_layer->surface) {
        before_surface = cairo_surface_reference(before_layer->surface);
    }

    if (after_layer && after_layer->surface) {
        after_surface = cairo_surface_reference(after_layer->surface);
    }

    filter_preview_set_before_surface(dialog->preview, before_surface);
    filter_preview_set_after_surface(dialog->preview, after_surface);

    /* Clean up references */
    if (before_surface) {
        cairo_surface_destroy(before_surface);
    }
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Update the after layer in preview
 */
void color_balance_dialog_update_after_layer(ColorBalanceDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;
    struct ImageDocument* doc = NULL;
    struct ImageLayer* original_layer = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get document and layer from dialog if available */
    GtkWindow* window = color_balance_dialog_get_window(dialog);
    if (window) {
        doc = (struct ImageDocument*)g_object_get_data(G_OBJECT(window), "filter_doc");
        original_layer = (struct ImageLayer*)g_object_get_data(G_OBJECT(window), "original_layer");
    }

    if (layer && layer->surface) {
        /* Pass the full unmasked surface - preview widget will handle masking display */
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);

    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Run the dialog and get color balance values
 */
gint color_balance_dialog_run(ColorBalanceDialog* dialog, GtkWindow* parent,
                              gint* red_balance, gint* green_balance, gint* blue_balance,
                              OcToneBalanceMode* mode, gboolean* preserve_luminosity) {
    gint response;

    if (!dialog || !dialog->dialog) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        if (red_balance) {
            *red_balance = (gint)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        }
        if (green_balance) {
            *green_balance = (gint)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        }
        if (blue_balance) {
            *blue_balance = (gint)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        }
        if (mode) {
            *mode = dialog->tone_mode;
        }
        if (preserve_luminosity) {
            *preserve_luminosity = dialog->preserve_luminosity;
        }
    }

    return response;
}

/**
 * Set preview callback
 */
void color_balance_dialog_set_preview_callback(ColorBalanceDialog* dialog,
                                               ColorBalanceDialogPreviewCallback callback,
                                               gpointer user_data) {
    if (!dialog) {
        return;
    }

    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

/**
 * Reset all controls to default values (0 for all channels)
 */
void color_balance_dialog_reset(ColorBalanceDialog* dialog) {
    const gint default_balance = 0;

    if (!dialog) {
        return;
    }

    /* Reset all scales to default value (0) */
    if (dialog->red_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->red_scale), (gdouble)default_balance);
    }
    if (dialog->green_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->green_scale), (gdouble)default_balance);
    }
    if (dialog->blue_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->blue_scale), (gdouble)default_balance);
    }

    /* Reset tone mode to midtones */
    if (dialog->midtones_radio) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->midtones_radio), TRUE);
    }

    /* Reset preserve luminosity to TRUE */
    if (dialog->preserve_luminosity_checkbox) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->preserve_luminosity_checkbox), TRUE);
    }

    /* Trigger preview update if callback is set */
    if (dialog->preview_callback) {
        gint values[3] = {default_balance, default_balance, default_balance};
        dialog->preview_callback(dialog, values, 3, MIDTONES, TRUE, dialog->preview_user_data);
    }
}
