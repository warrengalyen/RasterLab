#include "ui/widgets/filter_dialog.h"
#include "document.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_utils.h"
#include "ui/ui_utils.h"
#include <stdlib.h>
#include <string.h>

/**
 * Filter dialog structure
 */
struct _FilterDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* controls_box;
    GtkWidget** scale_widgets;    /* Array of scale widgets (for double controls) */
    GtkWidget** spin_widgets;     /* Array of spin button widgets (for double controls) */
    GtkWidget** checkbox_widgets; /* Array of checkbox widgets (for boolean controls) */
    GtkWidget** color_widgets;    /* Array of color button widgets (for RGB controls) */
    GtkWidget** combo_widgets;    /* Array of combo box widgets (for enum controls) */
    FilterControlParam* params;   /* Copy of control parameters */
    gint num_controls;
    FilterDialogPreviewCallback preview_callback; /* Callback for live preview */
    gpointer preview_user_data;                   /* User data for preview callback */
};

/**
 * Scale value changed callback - updates spin button and triggers preview
 */
static void on_scale_value_changed(GtkRange* range, gpointer user_data) {
    FilterDialog* dialog = (FilterDialog*)user_data;
    GtkWidget* spin_button;
    gdouble value;
    gdouble* values;
    gint i;
    gint total_values;

    if (!dialog) {
        return;
    }

    /* Find which control this scale belongs to */
    for (i = 0; i < dialog->num_controls; i++) {
        if (dialog->scale_widgets[i] == GTK_WIDGET(range)) {
            spin_button = dialog->spin_widgets[i];
            if (spin_button) {
                value = gtk_range_get_value(range);
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button), value);
            }

            /* Trigger preview update if callback is set */
            if (dialog->preview_callback) {
                /* Calculate total number of values needed */
                total_values = filter_dialog_get_total_values_count(dialog);
                values = (gdouble*)g_malloc(sizeof(gdouble) * total_values);
                if (values) {
                    filter_dialog_get_values(dialog, values, total_values);
                    dialog->preview_callback(dialog, values, total_values, dialog->preview_user_data);
                    g_free(values);
                }
            }
            break;
        }
    }
}

/**
 * Spin button value changed callback - updates scale and triggers preview
 */
static void on_spin_value_changed(GtkSpinButton* spin, gpointer user_data) {
    FilterDialog* dialog = (FilterDialog*)user_data;
    GtkWidget* scale;
    gdouble value;
    gdouble* values;
    gint i;
    gint total_values;

    if (!dialog) {
        return;
    }

    /* Find which control this spin button belongs to */
    for (i = 0; i < dialog->num_controls; i++) {
        if (dialog->spin_widgets[i] == GTK_WIDGET(spin)) {
            scale = dialog->scale_widgets[i];
            if (scale) {
                value = gtk_spin_button_get_value(spin);
                gtk_range_set_value(GTK_RANGE(scale), value);
            }

            /* Trigger preview update if callback is set */
            if (dialog->preview_callback) {
                total_values = filter_dialog_get_total_values_count(dialog);
                values = (gdouble*)g_malloc(sizeof(gdouble) * total_values);
                if (values) {
                    filter_dialog_get_values(dialog, values, total_values);
                    dialog->preview_callback(dialog, values, total_values, dialog->preview_user_data);
                    g_free(values);
                }
            }
            break;
        }
    }
}

/**
 * Checkbox toggled callback - triggers preview
 */
static void on_checkbox_toggled(GtkToggleButton* toggle, gpointer user_data) {
    FilterDialog* dialog = (FilterDialog*)user_data;
    gdouble* values;
    gint total_values;

    if (!dialog) {
        return;
    }

    /* Trigger preview update if callback is set */
    if (dialog->preview_callback) {
        total_values = filter_dialog_get_total_values_count(dialog);
        values = (gdouble*)g_malloc(sizeof(gdouble) * total_values);
        if (values) {
            filter_dialog_get_values(dialog, values, total_values);
            dialog->preview_callback(dialog, values, total_values, dialog->preview_user_data);
            g_free(values);
        }
    }
}

/**
 * Color button color-set callback - triggers preview
 */
static void on_color_set(GtkWidget* button, gpointer user_data) {
    FilterDialog* dialog = (FilterDialog*)user_data;
    gdouble* values;
    gint total_values;

    if (!dialog) {
        return;
    }

    /* Trigger preview update if callback is set */
    if (dialog->preview_callback) {
        total_values = filter_dialog_get_total_values_count(dialog);
        values = (gdouble*)g_malloc(sizeof(gdouble) * total_values);
        if (values) {
            filter_dialog_get_values(dialog, values, total_values);
            dialog->preview_callback(dialog, values, total_values, dialog->preview_user_data);
            g_free(values);
        }
    }
}

/**
 * Combo box changed callback - triggers preview
 */
static void on_combo_changed(GtkComboBox* combo, gpointer user_data) {
    FilterDialog* dialog = (FilterDialog*)user_data;
    gdouble* values;
    gint total_values;

    if (!dialog) {
        return;
    }

    /* Trigger preview update if callback is set */
    if (dialog->preview_callback) {
        total_values = filter_dialog_get_total_values_count(dialog);
        values = (gdouble*)g_malloc(sizeof(gdouble) * total_values);
        if (values) {
            filter_dialog_get_values(dialog, values, total_values);
            dialog->preview_callback(dialog, values, total_values, dialog->preview_user_data);
            g_free(values);
        }
    }
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    FilterDialog* dialog = (FilterDialog*)user_data;
    (void)widget;
    filter_dialog_reset(dialog);
}

/**
 * Create a new filter dialog
 */
FilterDialog* filter_dialog_new(const gchar* title,
                                const FilterControlParam* controls,
                                gint num_controls) {
    FilterDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* controls_vbox;
    GtkWidget* reset_button;
    GtkWidget* button_box;
    gint i;

    if (!title || !controls || num_controls <= 0) {
        return NULL;
    }

    dialog = (FilterDialog*)g_malloc(sizeof(FilterDialog));
    if (!dialog) {
        return NULL;
    }

    /* Allocate arrays for widgets */
    dialog->scale_widgets = (GtkWidget**)g_malloc(sizeof(GtkWidget*) * num_controls);
    dialog->spin_widgets = (GtkWidget**)g_malloc(sizeof(GtkWidget*) * num_controls);
    dialog->checkbox_widgets = (GtkWidget**)g_malloc(sizeof(GtkWidget*) * num_controls);
    dialog->color_widgets = (GtkWidget**)g_malloc(sizeof(GtkWidget*) * num_controls);
    dialog->combo_widgets = (GtkWidget**)g_malloc(sizeof(GtkWidget*) * num_controls);
    dialog->params = (FilterControlParam*)g_malloc(sizeof(FilterControlParam) * num_controls);

    if (!dialog->scale_widgets || !dialog->spin_widgets || !dialog->checkbox_widgets ||
        !dialog->color_widgets || !dialog->combo_widgets || !dialog->params) {
        g_free(dialog->scale_widgets);
        g_free(dialog->spin_widgets);
        g_free(dialog->checkbox_widgets);
        g_free(dialog->color_widgets);
        g_free(dialog->combo_widgets);
        g_free(dialog->params);
        g_free(dialog);
        return NULL;
    }

    /* Copy control parameters (avoid memcpy warnings) */
    for (i = 0; i < num_controls; i++) {
        dialog->params[i] = controls[i];
    }
    dialog->num_controls = num_controls;

    /* Initialize widget arrays */
    for (i = 0; i < num_controls; i++) {
        dialog->scale_widgets[i] = NULL;
        dialog->spin_widgets[i] = NULL;
        dialog->checkbox_widgets[i] = NULL;
        dialog->color_widgets[i] = NULL;
        dialog->combo_widgets[i] = NULL;
    }

    /* Create dialog window */
    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 (GtkDialogFlags)0,
                                                 "_OK",
                                                 GTK_RESPONSE_OK,
                                                 "_Cancel",
                                                 GTK_RESPONSE_CANCEL,
                                                 NULL);

    /* Replace default titlebar with header bar - must be done before other window properties */
    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), title);

    /* Apply modal + destroy-with-parent behavior without enum bitwise warnings */
    gtk_window_set_modal(GTK_WINDOW(dialog->dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog->dialog), TRUE);

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

    /* Create controls box (right side) */
    controls_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(controls_vbox, 320, -1);
    gtk_widget_set_margin_start(controls_vbox, 0);
    gtk_widget_set_margin_end(controls_vbox, 0);
    gtk_box_pack_start(GTK_BOX(main_hbox), controls_vbox, FALSE, FALSE, 0);

    dialog->controls_box = controls_vbox;

    /* Create control groups dynamically */
    for (i = 0; i < num_controls; i++) {
        GtkWidget* control_vbox;
        GtkWidget* label;

        /* Create vertical box for this control (no frame) */
        control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_bottom(control_vbox, 10);
        gtk_box_pack_start(GTK_BOX(controls_vbox), control_vbox, FALSE, FALSE, 0);

        /* Create label */
        label = gtk_label_new(controls[i].label);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_bottom(label, 3);
        gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

        /* Create control based on type */
        if (controls[i].type == FILTER_CONTROL_DOUBLE) {
            GtkWidget* scale_hbox;
            GtkWidget* scale;
            GtkWidget* spin;
            GtkAdjustment* adjustment;
            gdouble step;

            /* Create horizontal box for scale and spin */
            scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

            /* Determine step value */
            step = controls[i].step;
            if (step == 0.0) {
                /* Default step based on range */
                gdouble range = controls[i].max_value - controls[i].min_value;
                step = (range > 100.0) ? 1.0 : (range > 10.0) ? 0.1
                                                              : 0.01;
            }

            /* Create adjustment */
            adjustment = gtk_adjustment_new(controls[i].default_value,
                                            controls[i].min_value,
                                            controls[i].max_value,
                                            step,
                                            step * 10.0, /* Page step */
                                            0.0);

            /* Create scale (slider) */
            scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
            gtk_scale_set_digits(GTK_SCALE(scale), controls[i].decimals);
            gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
            gtk_widget_set_hexpand(scale, TRUE);
            gtk_widget_set_margin_end(scale, 5);
            gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);

            /* Create spin button */
            spin = gtk_spin_button_new(adjustment, step, controls[i].decimals);
            gtk_widget_set_size_request(spin, 70, -1);
            gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);

            /* Store widgets */
            dialog->scale_widgets[i] = scale;
            dialog->spin_widgets[i] = spin;

            /* Connect signals for synchronization and preview updates */
            g_signal_connect(scale, "value-changed",
                             G_CALLBACK(on_scale_value_changed), dialog);
            g_signal_connect(spin, "value-changed",
                             G_CALLBACK(on_spin_value_changed), dialog);
        } else if (controls[i].type == FILTER_CONTROL_BOOLEAN) {
            GtkWidget* checkbox;

            /* Create checkbox */
            checkbox = gtk_check_button_new();
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), controls[i].default_bool);
            gtk_widget_set_halign(checkbox, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(control_vbox), checkbox, FALSE, FALSE, 0);

            /* Store widget */
            dialog->checkbox_widgets[i] = checkbox;

            /* Connect signal for preview updates */
            g_signal_connect(checkbox, "toggled",
                             G_CALLBACK(on_checkbox_toggled), dialog);
        } else if (controls[i].type == FILTER_CONTROL_RGB) {
            GtkWidget* color_button;
            GdkRGBA color;

            /* Create custom color button */
            color.red = controls[i].default_r;
            color.green = controls[i].default_g;
            color.blue = controls[i].default_b;
            color.alpha = 1.0;

            color_button = create_custom_color_button(
                GTK_WINDOW(dialog->dialog),
                &color,
                on_color_set,
                dialog);

            gtk_widget_set_hexpand(color_button, TRUE);
            gtk_widget_set_size_request(color_button, -1, 35);
            gtk_box_pack_start(GTK_BOX(control_vbox), color_button, TRUE, TRUE, 0);

            /* Store widget */
            dialog->color_widgets[i] = color_button;
        } else if (controls[i].type == FILTER_CONTROL_ENUM) {
            GtkWidget* combo;
            gint active = 0;

            combo = gtk_combo_box_text_new();
            gtk_widget_set_hexpand(combo, TRUE);
            gtk_widget_set_size_request(combo, -1, 35);

            if (controls[i].enum_labels && controls[i].enum_n_labels > 0) {
                for (gint j = 0; j < controls[i].enum_n_labels; j++) {
                    const gchar* opt = controls[i].enum_labels[j] ? controls[i].enum_labels[j] : "";
                    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), opt);
                }
            }

            active = controls[i].default_enum_index;
            if (active < 0) {
                active = 0;
            }
            if (controls[i].enum_n_labels > 0 && active >= controls[i].enum_n_labels) {
                active = controls[i].enum_n_labels - 1;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);

            gtk_box_pack_start(GTK_BOX(control_vbox), combo, FALSE, FALSE, 0);

            dialog->combo_widgets[i] = combo;

            g_signal_connect(combo, "changed",
                             G_CALLBACK(on_combo_changed), dialog);
        }
    }

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
 * Set the image layers for the preview widget
 */
void filter_dialog_set_layers(FilterDialog* dialog,
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
 * Update the after layer preview
 */
void filter_dialog_update_after_layer(FilterDialog* dialog, ImageLayer* after_layer) {
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    if (after_layer && after_layer->surface) {
        /* Pass the full unmasked surface - preview widget will handle masking display */
        after_surface = cairo_surface_reference(after_layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);

    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Get the dialog window widget
 */
GtkWindow* filter_dialog_get_window(FilterDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }

    return GTK_WINDOW(dialog->dialog);
}

/**
 * Get the preview widget from the dialog
 */
FilterPreview* filter_dialog_get_preview(FilterDialog* dialog) {
    if (!dialog) {
        return NULL;
    }

    return dialog->preview;
}

/**
 * Run the dialog and get control values
 */
gint filter_dialog_run(FilterDialog* dialog,
                       GtkWindow* parent,
                       gdouble* values,
                       gint num_values) {
    gint response;

    if (!dialog || !values || num_values <= 0) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(filter_dialog_get_window(dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        gint total_values = filter_dialog_get_total_values_count(dialog);
        gint actual_count = (num_values < total_values) ? num_values : total_values;
        filter_dialog_get_values(dialog, values, actual_count);
    }

    return response;
}

/**
 * Get total number of values needed (accounts for RGB controls taking 3 values)
 */
gint filter_dialog_get_total_values_count(FilterDialog* dialog) {
    gint i;
    gint total = 0;

    if (!dialog) {
        return 0;
    }

    for (i = 0; i < dialog->num_controls; i++) {
        if (dialog->params[i].type == FILTER_CONTROL_RGB) {
            total += 3; /* RGB takes 3 values */
        } else {
            total += 1; /* Double, boolean, and enum take 1 value */
        }
    }

    return total;
}

/**
 * Get current control values
 * Note: RGB controls take 3 consecutive values (R, G, B in 0.0-1.0 range)
 * Boolean controls return 0.0 (false) or 1.0 (true)
 */
void filter_dialog_get_values(FilterDialog* dialog,
                              gdouble* values,
                              gint num_values) {
    gint i;
    gint value_index = 0;
    GdkRGBA color;

    if (!dialog || !values || num_values <= 0) {
        return;
    }

    for (i = 0; i < dialog->num_controls && value_index < num_values; i++) {
        if (dialog->params[i].type == FILTER_CONTROL_DOUBLE) {
            if (dialog->spin_widgets[i]) {
                values[value_index] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dialog->spin_widgets[i]));
            } else {
                values[value_index] = dialog->params[i].default_value;
            }
            value_index++;
        } else if (dialog->params[i].type == FILTER_CONTROL_BOOLEAN) {
            if (dialog->checkbox_widgets[i]) {
                values[value_index] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->checkbox_widgets[i])) ? 1.0 : 0.0;
            } else {
                values[value_index] = dialog->params[i].default_bool ? 1.0 : 0.0;
            }
            value_index++;
        } else if (dialog->params[i].type == FILTER_CONTROL_RGB) {
            if (dialog->color_widgets[i] && value_index + 2 < num_values) {
                get_custom_color_button_color(dialog->color_widgets[i], &color);
                values[value_index] = color.red;
                values[value_index + 1] = color.green;
                values[value_index + 2] = color.blue;
            } else {
                values[value_index] = dialog->params[i].default_r;
                values[value_index + 1] = dialog->params[i].default_g;
                values[value_index + 2] = dialog->params[i].default_b;
            }
            value_index += 3;
        } else if (dialog->params[i].type == FILTER_CONTROL_ENUM) {
            if (dialog->combo_widgets[i]) {
                values[value_index] = (gdouble)gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->combo_widgets[i]));
            } else {
                values[value_index] = (gdouble)dialog->params[i].default_enum_index;
            }
            value_index++;
        }
    }
}

/**
 * Set callback function for live preview updates
 */
void filter_dialog_set_preview_callback(FilterDialog* dialog,
                                        FilterDialogPreviewCallback callback,
                                        gpointer user_data) {
    if (!dialog) {
        return;
    }

    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

/**
 * Reset all controls to default values
 */
void filter_dialog_reset(FilterDialog* dialog) {
    gint i;
    GdkRGBA color;

    if (!dialog) {
        return;
    }

    for (i = 0; i < dialog->num_controls; i++) {
        if (dialog->params[i].type == FILTER_CONTROL_DOUBLE) {
            if (dialog->spin_widgets[i]) {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->spin_widgets[i]),
                                          dialog->params[i].default_value);
            }
        } else if (dialog->params[i].type == FILTER_CONTROL_BOOLEAN) {
            if (dialog->checkbox_widgets[i]) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->checkbox_widgets[i]),
                                             dialog->params[i].default_bool);
            }
        } else if (dialog->params[i].type == FILTER_CONTROL_RGB) {
            if (dialog->color_widgets[i]) {
                color.red = dialog->params[i].default_r;
                color.green = dialog->params[i].default_g;
                color.blue = dialog->params[i].default_b;
                color.alpha = 1.0;
                gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog->color_widgets[i]), &color);
            }
        } else if (dialog->params[i].type == FILTER_CONTROL_ENUM) {
            if (dialog->combo_widgets[i]) {
                gint active = dialog->params[i].default_enum_index;
                if (active < 0) {
                    active = 0;
                }
                if (dialog->params[i].enum_n_labels > 0 && active >= dialog->params[i].enum_n_labels) {
                    active = dialog->params[i].enum_n_labels - 1;
                }
                gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->combo_widgets[i]), active);
            }
        }
    }
}

/**
 * Free the filter dialog
 */
void filter_dialog_free(FilterDialog* dialog) {
    gint i;

    if (!dialog) {
        return;
    }

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    if (dialog->scale_widgets) {
        g_free(dialog->scale_widgets);
    }

    if (dialog->spin_widgets) {
        g_free(dialog->spin_widgets);
    }

    if (dialog->checkbox_widgets) {
        g_free(dialog->checkbox_widgets);
    }

    if (dialog->color_widgets) {
        g_free(dialog->color_widgets);
    }

    if (dialog->combo_widgets) {
        g_free(dialog->combo_widgets);
    }

    if (dialog->params) {
        g_free(dialog->params);
    }

    g_free(dialog);
}
