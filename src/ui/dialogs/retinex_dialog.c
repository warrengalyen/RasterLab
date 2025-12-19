#include "ui/dialogs/retinex_dialog.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_retinex.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * Retinex dialog structure
 */
struct _RetinexDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* mode_combo;
    GtkWidget* scale_scale;
    GtkWidget* scale_spin;
    GtkWidget* num_scales_scale;
    GtkWidget* num_scales_spin;
    GtkWidget* dynamic_scale;
    GtkWidget* dynamic_spin;
    OcRetinexMode mode;
    gint scale;
    gfloat num_scales;
    gfloat dynamic;
    RetinexDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/**
 * Retinex parameters structure for viewport filtering
 */
typedef struct {
    OcRetinexMode mode;
    gint scale;
    gfloat num_scales;
    gfloat dynamic;
} RetinexParams;

/**
 * Helper function to wrap retinex filter for viewport system
 */
static cairo_surface_t* apply_retinex_filter_to_viewport_surface(cairo_surface_t* viewport_surface, gpointer params) {
    RetinexParams* retinex_params = (RetinexParams*)params;
    ImageLayer* temp_layer;
    cairo_surface_t* result;

    if (!viewport_surface || !retinex_params) {
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
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
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
    if (!filter_retinex_apply(temp_layer, retinex_params->mode, retinex_params->scale,
                              retinex_params->num_scales, retinex_params->dynamic)) {
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
static void update_preview(RetinexDialog* dialog) {
    RetinexParams* stored_params;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get current values from widgets */
    dialog->scale = (gint)gtk_range_get_value(GTK_RANGE(dialog->scale_scale));
    dialog->num_scales = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->num_scales_scale));
    dialog->dynamic = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->dynamic_scale));

    /* Get mode from combo */
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->mode_combo));
    if (active == 0) {
        dialog->mode = OC_RETINEX_UNIFORM;
    } else if (active == 1) {
        dialog->mode = OC_RETINEX_LOW;
    } else {
        dialog->mode = OC_RETINEX_HIGH;
    }

    /* Get or create stored params */
    stored_params = (RetinexParams*)g_object_get_data(G_OBJECT(dialog->dialog), "retinex_params");

    if (!stored_params) {
        stored_params = g_malloc(sizeof(RetinexParams));
        g_object_set_data_full(G_OBJECT(dialog->dialog), "retinex_params",
                               stored_params, g_free);
    }

    /* Copy params */
    stored_params->mode = dialog->mode;
    stored_params->scale = dialog->scale;
    stored_params->num_scales = dialog->num_scales;
    stored_params->dynamic = dialog->dynamic;

    /* Set filter function on preview to use viewport-based filtering */
    filter_preview_set_filter_function(dialog->preview, apply_retinex_filter_to_viewport_surface, stored_params);
    filter_preview_refresh(dialog->preview);

    /* Call user callback if provided */
    if (dialog->preview_callback) {
        dialog->preview_callback(dialog, dialog->mode, dialog->scale, dialog->num_scales, dialog->dynamic, dialog->preview_user_data);
    }
}

/**
 * Scale value changed callback
 */
static void on_scale_changed(GtkRange* range, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->scale_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->scale_spin), value);
    update_preview(dialog);
}

/**
 * Scale spin value changed callback
 */
static void on_scale_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->scale_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->scale_scale), value);
}

/**
 * Num scales value changed callback
 */
static void on_num_scales_changed(GtkRange* range, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->num_scales_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->num_scales_spin), value);
    update_preview(dialog);
}

/**
 * Num scales spin value changed callback
 */
static void on_num_scales_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->num_scales_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->num_scales_scale), value);
}

/**
 * Dynamic value changed callback
 */
static void on_dynamic_changed(GtkRange* range, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->dynamic_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->dynamic_spin), value);
    update_preview(dialog);
}

/**
 * Dynamic spin value changed callback
 */
static void on_dynamic_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->dynamic_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->dynamic_scale), value);
}

/**
 * Mode combo changed callback
 */
static void on_mode_changed(GtkComboBox* combo, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    (void)combo;
    update_preview(dialog);
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    RetinexDialog* dialog = (RetinexDialog*)user_data;
    (void)widget;
    retinex_dialog_reset(dialog);
}

/**
 * Create a new retinex dialog
 */
RetinexDialog* retinex_dialog_new(const gchar* title) {
    RetinexDialog* dialog;
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

    dialog = (RetinexDialog*)g_malloc(sizeof(RetinexDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->mode_combo = NULL;
    dialog->scale_scale = NULL;
    dialog->scale_spin = NULL;
    dialog->num_scales_scale = NULL;
    dialog->num_scales_spin = NULL;
    dialog->dynamic_scale = NULL;
    dialog->dynamic_spin = NULL;
    dialog->preview_callback = NULL;
    dialog->preview_user_data = NULL;

    /* Set default parameters */
    dialog->mode = OC_RETINEX_UNIFORM;
    dialog->scale = 240;
    dialog->num_scales = 3.0f;
    dialog->dynamic = 1.2f;

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

    /* Create mode combo */
    combo_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(combo_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), combo_vbox, FALSE, FALSE, 0);

    combo_label = gtk_label_new("scale distribution");
    gtk_widget_set_halign(combo_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(combo_label, 3);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo_label, FALSE, FALSE, 0);

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Uniform");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Low");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "High");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_widget_set_hexpand(combo, TRUE);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo, FALSE, FALSE, 0);
    dialog->mode_combo = combo;
    g_signal_connect(combo, "changed", G_CALLBACK(on_mode_changed), dialog);

    /* Create scale control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("scale");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->scale, 16.0, 250.0, 1.0, 1.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->scale_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->scale_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_scale_spin_changed), dialog);

    /* Create num scales control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("number of scales");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->num_scales, 1.0, 8.0, 0.1, 1.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->num_scales_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_num_scales_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 0.1, 1);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->num_scales_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_num_scales_spin_changed), dialog);

    /* Create dynamic control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("dynamic range");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->dynamic, 0.05, 4.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->dynamic_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_dynamic_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->dynamic_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_dynamic_spin_changed), dialog);

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
 * Free retinex dialog
 */
void retinex_dialog_free(RetinexDialog* dialog) {
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
GtkWindow* retinex_dialog_get_window(RetinexDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void retinex_dialog_set_layers(RetinexDialog* dialog, ImageLayer* original, ImageLayer* temp) {
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
 * Run the dialog and get retinex parameters
 */
gint retinex_dialog_run(RetinexDialog* dialog, GtkWindow* parent,
                        OcRetinexMode* mode, gint* scale, gfloat* numScales, gfloat* dynamic) {
    gint response;

    if (!dialog || !mode || !scale || !numScales || !dynamic) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Get final values from widgets */
        *scale = (gint)gtk_range_get_value(GTK_RANGE(dialog->scale_scale));
        *numScales = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->num_scales_scale));
        *dynamic = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->dynamic_scale));

        /* Get mode */
        gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->mode_combo));
        if (active == 0) {
            *mode = OC_RETINEX_UNIFORM;
        } else if (active == 1) {
            *mode = OC_RETINEX_LOW;
        } else {
            *mode = OC_RETINEX_HIGH;
        }
    }

    return response;
}

/**
 * Update the after layer in preview
 */
void retinex_dialog_update_after_layer(RetinexDialog* dialog, ImageLayer* layer) {
    if (!dialog || !dialog->preview) {
        return;
    }
    filter_preview_set_after_surface(dialog->preview, layer ? layer->surface : NULL);
    filter_preview_refresh(dialog->preview);
}

/**
 * Set preview callback for live updates
 */
void retinex_dialog_set_preview_callback(RetinexDialog* dialog,
                                         RetinexDialogPreviewCallback callback,
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
void retinex_dialog_reset(RetinexDialog* dialog) {
    if (!dialog) {
        return;
    }

    /* Reset to defaults */
    gtk_range_set_value(GTK_RANGE(dialog->scale_scale), (gdouble)dialog->scale);
    gtk_range_set_value(GTK_RANGE(dialog->num_scales_scale), (gdouble)dialog->num_scales);
    gtk_range_set_value(GTK_RANGE(dialog->dynamic_scale), (gdouble)dialog->dynamic);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->mode_combo), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->mode_combo), 0);

    update_preview(dialog);
}
