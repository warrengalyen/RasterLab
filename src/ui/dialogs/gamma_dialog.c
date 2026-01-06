#include "ui/dialogs/gamma_dialog.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_utils.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Gamma dialog structure
 */
struct _GammaDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* gamma_curve_area; /* Drawing area for gamma curve */
    GtkWidget* red_scale;
    GtkWidget* green_scale;
    GtkWidget* blue_scale;
    GtkWidget* red_spin;
    GtkWidget* green_spin;
    GtkWidget* blue_spin;
    GtkWidget* sync_checkbox;
    gfloat red_gamma;
    gfloat green_gamma;
    gfloat blue_gamma;
    gboolean synced;
    GammaDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/**
 * Draw gamma curve preview
 */
static gboolean on_gamma_curve_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    GammaDialog* dialog = (GammaDialog*)user_data;
    gint width, height;
    gint i;
    gdouble x, y;
    gdouble curve_x, curve_y;
    gfloat gamma_r, gamma_g, gamma_b;

    if (!dialog) {
        return FALSE;
    }

    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);

    /* Clear background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Draw border */
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, width - 1, height - 1);
    cairo_stroke(cr);

    /* Draw diagonal gray line (base gamma = 1.0) */
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0.5, height - 0.5);
    cairo_line_to(cr, width - 0.5, 0.5);
    cairo_stroke(cr);

    /* Get current gamma values */
    if (dialog->red_scale) {
        gamma_r = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
    } else {
        gamma_r = 1.0f;
    }
    if (dialog->green_scale) {
        gamma_g = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
    } else {
        gamma_g = 1.0f;
    }
    if (dialog->blue_scale) {
        gamma_b = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
    } else {
        gamma_b = 1.0f;
    }

    /* Draw curves */
    if (dialog->synced) {
        /* Draw single blue curve for all channels */
        cairo_set_source_rgb(cr, 0.0, 0.0, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, 0.5, height - 0.5);

        for (i = 0; i <= 100; i++) {
            x = (gdouble)i / 100.0;
            /* Gamma curve: output = input^(1/gamma) */
            y = pow(x, 1.0 / gamma_r);
            curve_x = 0.5 + x * (width - 1);
            curve_y = height - 0.5 - y * (height - 1);
            cairo_line_to(cr, curve_x, curve_y);
        }
        cairo_stroke(cr);
    } else {
        /* Draw separate curves for each channel */
        /* Red curve */
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, 0.5, height - 0.5);
        for (i = 0; i <= 100; i++) {
            x = (gdouble)i / 100.0;
            y = pow(x, 1.0 / gamma_r);
            curve_x = 0.5 + x * (width - 1);
            curve_y = height - 0.5 - y * (height - 1);
            cairo_line_to(cr, curve_x, curve_y);
        }
        cairo_stroke(cr);

        /* Green curve */
        cairo_set_source_rgb(cr, 0.0, 1.0, 0.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, 0.5, height - 0.5);
        for (i = 0; i <= 100; i++) {
            x = (gdouble)i / 100.0;
            y = pow(x, 1.0 / gamma_g);
            curve_x = 0.5 + x * (width - 1);
            curve_y = height - 0.5 - y * (height - 1);
            cairo_line_to(cr, curve_x, curve_y);
        }
        cairo_stroke(cr);

        /* Blue curve */
        cairo_set_source_rgb(cr, 0.0, 0.0, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, 0.5, height - 0.5);
        for (i = 0; i <= 100; i++) {
            x = (gdouble)i / 100.0;
            y = pow(x, 1.0 / gamma_b);
            curve_x = 0.5 + x * (width - 1);
            curve_y = height - 0.5 - y * (height - 1);
            cairo_line_to(cr, curve_x, curve_y);
        }
        cairo_stroke(cr);
    }

    return FALSE;
}

/**
 * Red scale value changed callback
 */
static void on_red_scale_changed(GtkRange* range, gpointer user_data) {
    GammaDialog* dialog = (GammaDialog*)user_data;
    gdouble value;

    if (!dialog) {
        return;
    }

    value = gtk_range_get_value(range);
    if (dialog->red_spin) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->red_spin), value);
    }

    /* If synced, update other channels */
    if (dialog->synced && dialog->green_scale && dialog->blue_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->green_scale), value);
        gtk_range_set_value(GTK_RANGE(dialog->blue_scale), value);
    }

    /* Redraw gamma curve */
    gtk_widget_queue_draw(dialog->gamma_curve_area);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gdouble gamma_values[3];
        gamma_values[0] = value;
        if (dialog->synced) {
            gamma_values[1] = value;
            gamma_values[2] = value;
        } else {
            gamma_values[1] = gtk_range_get_value(GTK_RANGE(dialog->green_scale));
            gamma_values[2] = gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        }
        dialog->preview_callback(dialog, gamma_values, 3, dialog->preview_user_data);
    }
}

/**
 * Green scale value changed callback
 */
static void on_green_scale_changed(GtkRange* range, gpointer user_data) {
    GammaDialog* dialog = (GammaDialog*)user_data;
    gdouble value;

    if (!dialog) {
        return;
    }

    value = gtk_range_get_value(range);
    if (dialog->green_spin) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->green_spin), value);
    }

    /* If synced, update other channels */
    if (dialog->synced && dialog->red_scale && dialog->blue_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->red_scale), value);
        gtk_range_set_value(GTK_RANGE(dialog->blue_scale), value);
    }

    /* Redraw gamma curve */
    gtk_widget_queue_draw(dialog->gamma_curve_area);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gdouble gamma_values[3];
        if (dialog->synced) {
            gamma_values[0] = value;
            gamma_values[1] = value;
            gamma_values[2] = value;
        } else {
            gamma_values[0] = gtk_range_get_value(GTK_RANGE(dialog->red_scale));
            gamma_values[1] = value;
            gamma_values[2] = gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
        }
        dialog->preview_callback(dialog, gamma_values, 3, dialog->preview_user_data);
    }
}

/**
 * Blue scale value changed callback
 */
static void on_blue_scale_changed(GtkRange* range, gpointer user_data) {
    GammaDialog* dialog = (GammaDialog*)user_data;
    gdouble value;

    if (!dialog) {
        return;
    }

    value = gtk_range_get_value(range);
    if (dialog->blue_spin) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->blue_spin), value);
    }

    /* If synced, update other channels */
    if (dialog->synced && dialog->red_scale && dialog->green_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->red_scale), value);
        gtk_range_set_value(GTK_RANGE(dialog->green_scale), value);
    }

    /* Redraw gamma curve */
    gtk_widget_queue_draw(dialog->gamma_curve_area);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        gdouble gamma_values[3];
        if (dialog->synced) {
            gamma_values[0] = value;
            gamma_values[1] = value;
            gamma_values[2] = value;
        } else {
            gamma_values[0] = gtk_range_get_value(GTK_RANGE(dialog->red_scale));
            gamma_values[1] = gtk_range_get_value(GTK_RANGE(dialog->green_scale));
            gamma_values[2] = value;
        }
        dialog->preview_callback(dialog, gamma_values, 3, dialog->preview_user_data);
    }
}

/**
 * Sync checkbox toggled callback
 */
static void on_sync_toggled(GtkToggleButton* button, gpointer user_data) {
    GammaDialog* dialog = (GammaDialog*)user_data;

    if (!dialog) {
        return;
    }

    dialog->synced = gtk_toggle_button_get_active(button);

    /* If syncing, copy red value to green and blue */
    if (dialog->synced && dialog->red_scale && dialog->green_scale && dialog->blue_scale) {
        gdouble red_value = gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        gtk_range_set_value(GTK_RANGE(dialog->green_scale), red_value);
        gtk_range_set_value(GTK_RANGE(dialog->blue_scale), red_value);
    }

    /* Redraw gamma curve */
    gtk_widget_queue_draw(dialog->gamma_curve_area);
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    GammaDialog* dialog = (GammaDialog*)user_data;
    (void)widget;
    gamma_dialog_reset(dialog);
}

/**
 * Create a new gamma correction dialog
 */
GammaDialog* gamma_dialog_new(const gchar* title) {
    GammaDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* curve_label;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adjustment;
    GtkWidget* sync_hbox;
    GtkWidget* sync_label;
    GtkWidget* reset_button;
    GtkWidget* button_box;

    if (!title) {
        return NULL;
    }

    dialog = (GammaDialog*)g_malloc(sizeof(GammaDialog));
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
    dialog->sync_checkbox = NULL;
    dialog->red_gamma = 1.0f;
    dialog->green_gamma = 1.0f;
    dialog->blue_gamma = 1.0f;
    dialog->synced = FALSE;
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
    gtk_widget_set_margin_start(right_vbox, 0);
    gtk_widget_set_margin_end(right_vbox, 0);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, FALSE, FALSE, 0);

    /* Create gamma curve preview area */
    curve_label = gtk_label_new("new gamma curve:");
    gtk_widget_set_halign(curve_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(curve_label, 3);
    gtk_box_pack_start(GTK_BOX(right_vbox), curve_label, FALSE, FALSE, 0);

    dialog->gamma_curve_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(dialog->gamma_curve_area, -1, 200);
    gtk_widget_set_margin_bottom(dialog->gamma_curve_area, 10);
    g_signal_connect(dialog->gamma_curve_area, "draw", G_CALLBACK(on_gamma_curve_draw), dialog);
    gtk_box_pack_start(GTK_BOX(right_vbox), dialog->gamma_curve_area, FALSE, FALSE, 0);

    /* Create red control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("red");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(1.0, 0.10, 3.00, 0.01, 0.1, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->red_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_red_scale_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 0.01, 2);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->red_spin = spin;

    /* Create green control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("green");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(1.0, 0.10, 3.00, 0.01, 0.1, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->green_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_green_scale_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 0.01, 2);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->green_spin = spin;

    /* Create blue control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("blue");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(1.0, 0.10, 3.00, 0.01, 0.1, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->blue_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_blue_scale_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 0.01, 2);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->blue_spin = spin;

    /* Create sync checkbox */
    sync_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(right_vbox), sync_hbox, FALSE, FALSE, 0);

    dialog->sync_checkbox = gtk_check_button_new();
    gtk_box_pack_start(GTK_BOX(sync_hbox), dialog->sync_checkbox, FALSE, FALSE, 0);
    g_signal_connect(dialog->sync_checkbox, "toggled", G_CALLBACK(on_sync_toggled), dialog);

    sync_label = gtk_label_new("keep all colors in sync");
    gtk_box_pack_start(GTK_BOX(sync_hbox), sync_label, FALSE, FALSE, 0);

/* Get button box from dialog (for OK/Cancel) */
/* Note: OK/Cancel buttons are already added by gtk_dialog_new_with_buttons */
/* gtk_dialog_get_action_area is deprecated but still works in GTK3 */
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

        /* Create reset button with reset.svg icon and add it to the left side of action area */
        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.svg");
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
 * Free gamma dialog
 */
void gamma_dialog_free(GammaDialog* dialog) {
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
GtkWindow* gamma_dialog_get_window(GammaDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }

    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void gamma_dialog_set_layers(GammaDialog* dialog, ImageLayer* original, ImageLayer* temp) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get composite surfaces from layers - pass full unmasked surfaces
       The preview widget will handle masking display based on selection */
    if (original && original->surface) {
        before_surface = cairo_surface_reference(original->surface);
    }

    if (temp && temp->surface) {
        after_surface = cairo_surface_reference(temp->surface);
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

    /* Trigger initial preview update after layers are set */
    update_preview(dialog);
}

/**
 * Run the dialog and get gamma values
 */
gint gamma_dialog_run(GammaDialog* dialog, GtkWindow* parent, gfloat* gamma_values) {
    gint response;

    if (!dialog || !gamma_values) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(gamma_dialog_get_window(dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        gamma_values[0] = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->red_scale));
        gamma_values[1] = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->green_scale));
        gamma_values[2] = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->blue_scale));
    }

    return response;
}

/**
 * Update the after layer in preview
 */
void gamma_dialog_update_after_layer(GammaDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;
    struct ImageDocument* doc = NULL;
    struct ImageLayer* original_layer = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get document and layer from dialog if available */
    GtkWindow* window = gamma_dialog_get_window(dialog);
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
 * Set preview callback
 */
void gamma_dialog_set_preview_callback(GammaDialog* dialog,
                                       GammaDialogPreviewCallback callback,
                                       gpointer user_data) {
    if (!dialog) {
        return;
    }

    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

/**
 * Reset all controls to default values (1.0 for all channels)
 */
void gamma_dialog_reset(GammaDialog* dialog) {
    const gdouble default_gamma = 1.0;

    if (!dialog) {
        return;
    }

    /* Reset all scales to default value (1.0) */
    if (dialog->red_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->red_scale), default_gamma);
    }
    if (dialog->green_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->green_scale), default_gamma);
    }
    if (dialog->blue_scale) {
        gtk_range_set_value(GTK_RANGE(dialog->blue_scale), default_gamma);
    }

    /* Redraw gamma curve */
    if (dialog->gamma_curve_area) {
        gtk_widget_queue_draw(dialog->gamma_curve_area);
    }

    /* Trigger preview update if callback is set */
    if (dialog->preview_callback) {
        gdouble gamma_values[3] = {default_gamma, default_gamma, default_gamma};
        dialog->preview_callback(dialog, gamma_values, 3, dialog->preview_user_data);
    }
}
