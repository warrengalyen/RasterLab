#include "ui/dialogs/beeps_dialog.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_beeps.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * BEEPS dialog structure
 */
struct _BEEPSDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* range_filter_combo;
    GtkWidget* photometric_scale;
    GtkWidget* photometric_spin;
    GtkWidget* spatial_decay_scale;
    GtkWidget* spatial_decay_spin;
    gfloat photometric_std_dev;
    gfloat spatial_decay;
    gint range_filter;
};

/**
 * Helper function to wrap BEEPS filter for viewport system
 */
static cairo_surface_t* apply_beeps_filter_to_viewport_surface(cairo_surface_t* viewport_surface, gpointer params) {
    BEEPSParams* beeps_params = (BEEPSParams*)params;
    ImageLayer* temp_layer;
    cairo_surface_t* result;

    if (!viewport_surface || !beeps_params) {
        return NULL;
    }

    /* Get viewport dimensions */
    gint width = cairo_image_surface_get_width(viewport_surface);
    gint height = cairo_image_surface_get_height(viewport_surface);

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    /* Create a temporary layer with the viewport surface */
    temp_layer = layer_new("TempViewport", width, height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, NULL);
    if (!temp_layer) {
        return NULL;
    }

    /* Copy viewport surface to layer */
    cairo_t* cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, viewport_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Apply filter to the layer */
    if (!filter_beeps_apply(temp_layer, beeps_params->photometric_std_dev,
                            beeps_params->spatial_decay, beeps_params->range_filter)) {
        layer_free(temp_layer);
        return NULL;
    }

    /* Return a reference to the filtered surface */
    result = cairo_surface_reference(temp_layer->surface);
    layer_free(temp_layer);

    return result;
}

/**
 * Update preview callback
 */
static void update_preview(BEEPSDialog* dialog) {
    BEEPSParams* stored_params;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get current values from widgets */
    dialog->photometric_std_dev = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->photometric_scale));
    dialog->spatial_decay = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->spatial_decay_scale));

    /* Get range filter from combo */
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->range_filter_combo));
    dialog->range_filter = active;

    /* Get or create stored params */
    stored_params = (BEEPSParams*)g_object_get_data(G_OBJECT(dialog->dialog), "beeps_params");

    if (!stored_params) {
        stored_params = g_malloc(sizeof(BEEPSParams));
        g_object_set_data_full(G_OBJECT(dialog->dialog), "beeps_params",
                               stored_params, g_free);
    }

    /* Copy params */
    stored_params->photometric_std_dev = dialog->photometric_std_dev;
    stored_params->spatial_decay = dialog->spatial_decay;
    stored_params->range_filter = dialog->range_filter;

    /* Set filter function on preview to use viewport-based filtering */
    filter_preview_set_filter_function(dialog->preview, apply_beeps_filter_to_viewport_surface, stored_params);
    filter_preview_refresh(dialog->preview);
}

/**
 * Photometric value changed callback
 */
static void on_photometric_changed(GtkRange* range, gpointer user_data) {
    BEEPSDialog* dialog = (BEEPSDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->photometric_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->photometric_spin), value);
    update_preview(dialog);
}

/**
 * Photometric spin value changed callback
 */
static void on_photometric_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    BEEPSDialog* dialog = (BEEPSDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->photometric_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->photometric_scale), value);
}

/**
 * Spatial decay value changed callback
 */
static void on_spatial_decay_changed(GtkRange* range, gpointer user_data) {
    BEEPSDialog* dialog = (BEEPSDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->spatial_decay_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->spatial_decay_spin), value);
    update_preview(dialog);
}

/**
 * Spatial decay spin value changed callback
 */
static void on_spatial_decay_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    BEEPSDialog* dialog = (BEEPSDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->spatial_decay_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->spatial_decay_scale), value);
}

/**
 * Range filter combo changed callback
 */
static void on_range_filter_changed(GtkComboBox* combo, gpointer user_data) {
    BEEPSDialog* dialog = (BEEPSDialog*)user_data;
    (void)combo;
    update_preview(dialog);
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    BEEPSDialog* dialog = (BEEPSDialog*)user_data;
    (void)widget;
    beeps_dialog_reset(dialog);
}

/**
 * Create a new BEEPS dialog
 */
BEEPSDialog* beeps_dialog_new(const gchar* title) {
    BEEPSDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adjustment;
    GtkWidget* combo_vbox;
    GtkWidget* combo_label;
    GtkWidget* combo;
    GtkWidget* reset_button;
    GtkWidget* button_box;

    if (!title) {
        return NULL;
    }

    dialog = (BEEPSDialog*)g_malloc(sizeof(BEEPSDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->range_filter_combo = NULL;
    dialog->photometric_scale = NULL;
    dialog->photometric_spin = NULL;
    dialog->spatial_decay_scale = NULL;
    dialog->spatial_decay_spin = NULL;

    /* Set default parameters */
    dialog->photometric_std_dev = 255.0f;
    dialog->spatial_decay = 0.01f;
    dialog->range_filter = 1; /* balanced */

    /* Create dialog window */
    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK",
                                                 GTK_RESPONSE_OK,
                                                 "_Cancel",
                                                 GTK_RESPONSE_CANCEL,
                                                 NULL);

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

    /* Create range filter combo */
    combo_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(combo_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), combo_vbox, FALSE, FALSE, 0);

    combo_label = gtk_label_new("edge preservation mode");
    gtk_widget_set_halign(combo_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(combo_label, 3);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo_label, FALSE, FALSE, 0);

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "gentle");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "balanced");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "sharp");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 1); /* default: balanced */
    gtk_widget_set_hexpand(combo, TRUE);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo, FALSE, FALSE, 0);
    dialog->range_filter_combo = combo;
    g_signal_connect(combo, "changed", G_CALLBACK(on_range_filter_changed), dialog);

    /* Create photometric standard deviation control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("smoothing strength");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->photometric_std_dev, 1.0, 255.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->photometric_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_photometric_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->photometric_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_photometric_spin_changed), dialog);

    /* Create spatial decay control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("smoothing radius");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->spatial_decay, 0.01, 0.25, 0.01, 0.05, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->spatial_decay_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_spatial_decay_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 0.01, 2);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->spatial_decay_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_spatial_decay_spin_changed), dialog);

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

        gtk_widget_set_hexpand(button_box, TRUE);

        /* Create reset button with reset.svg icon */
        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.svg");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(reset_button), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
        }
        gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(button_box), reset_button, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(button_box), reset_button, 0);
        g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset_clicked), dialog);
    }

    /* Show all widgets */
    gtk_widget_show_all(content_area);

    return dialog;
}

/**
 * Free BEEPS dialog
 */
void beeps_dialog_free(BEEPSDialog* dialog) {
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
GtkWindow* beeps_dialog_get_window(BEEPSDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void beeps_dialog_set_layers(BEEPSDialog* dialog, ImageLayer* original, ImageLayer* temp) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get composite surfaces from layers if available */
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
 * Run the dialog and get BEEPS parameters
 */
gint beeps_dialog_run(BEEPSDialog* dialog, GtkWindow* parent, BEEPSParams* params) {
    gint response;

    if (!dialog || !params) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Get final values from widgets */
        params->photometric_std_dev = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->photometric_scale));
        params->spatial_decay = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->spatial_decay_scale));

        /* Get range filter */
        gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->range_filter_combo));
        params->range_filter = active;
    }

    return response;
}

/**
 * Reset all controls to default values
 */
void beeps_dialog_reset(BEEPSDialog* dialog) {
    if (!dialog) {
        return;
    }

    /* Reset to defaults */
    gtk_range_set_value(GTK_RANGE(dialog->photometric_scale), 255.0);
    gtk_range_set_value(GTK_RANGE(dialog->spatial_decay_scale), 0.01);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->range_filter_combo), 1); /* balanced */

    update_preview(dialog);
}
