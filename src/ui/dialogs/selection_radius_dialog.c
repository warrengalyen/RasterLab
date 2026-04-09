/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/selection_radius_dialog.h"
#include "ui/ui_utils.h"
#include "ui/widgets/vertical_spin_button.h"
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

/**
 * Selection radius dialog structure
 */
struct _SelectionRadiusDialog {
    GtkWidget* dialog;
    GtkWidget* radius_scale;
    GtkWidget* radius_spin;
};

/**
 * Radius scale value changed callback
 */
static void on_radius_scale_changed(GtkRange* range, gpointer user_data) {
    SelectionRadiusDialog* dialog = (SelectionRadiusDialog*)user_data;
    gint value;

    if (!dialog || !dialog->radius_spin) {
        return;
    }

    value = (gint)gtk_range_get_value(range);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->radius_spin), (gdouble)value);
}

/**
 * Radius spin value changed callback
 */
static void on_radius_spin_changed(GtkWidget* spin, gpointer user_data) {
    SelectionRadiusDialog* dialog = (SelectionRadiusDialog*)user_data;
    gint value;

    if (!dialog || !dialog->radius_scale) {
        return;
    }

    value = (gint)vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    gtk_range_set_value(GTK_RANGE(dialog->radius_scale), (gdouble)value);
}

/**
 * Create a new selection radius dialog
 */
SelectionRadiusDialog* selection_radius_dialog_new(const gchar* title) {
    SelectionRadiusDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_vbox;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adjustment;

    if (!title) {
        return NULL;
    }

    dialog = (SelectionRadiusDialog*)g_malloc(sizeof(SelectionRadiusDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->radius_scale = NULL;
    dialog->radius_spin = NULL;

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
    gtk_window_set_default_size(GTK_WINDOW(dialog->dialog), 400, 200);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Create main vertical box */
    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), main_vbox);

    /* Create control vertical box */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("Radius:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(scale_hbox, TRUE);
    gtk_widget_set_halign(scale_hbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(1.0, 1.0, 500.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_halign(scale, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->radius_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_radius_scale_changed), dialog);

    spin = vertical_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_widget_set_hexpand(spin, FALSE);
    gtk_widget_set_halign(spin, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->radius_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_radius_spin_changed), dialog);

    /* Show all widgets */
    gtk_widget_show_all(content_area);

    return dialog;
}

/**
 * Free selection radius dialog
 */
void selection_radius_dialog_free(SelectionRadiusDialog* dialog) {
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
GtkWindow* selection_radius_dialog_get_window(SelectionRadiusDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }

    return GTK_WINDOW(dialog->dialog);
}

/**
 * Run the dialog and get radius value
 */
gint selection_radius_dialog_run(SelectionRadiusDialog* dialog, GtkWindow* parent, gint* radius) {
    gint response;

    if (!dialog || !dialog->dialog || !radius) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(selection_radius_dialog_get_window(dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        *radius = (gint)gtk_range_get_value(GTK_RANGE(dialog->radius_scale));
    }

    return response;
}
