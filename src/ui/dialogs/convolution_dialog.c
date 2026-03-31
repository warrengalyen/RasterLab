#include "ui/dialogs/convolution_dialog.h"
#include "document.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_convolution.h"
#include "ui/filters/filter_utils.h"
#include "ui/ui_utils.h"
#include "ui/widgets/filter_preview.h"
#include "ui/widgets/vertical_spin_button.h"
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

#define MATRIX_SIZE 5
#define MATRIX_ELEMENTS (MATRIX_SIZE * MATRIX_SIZE)

/**
 * Convolution dialog structure
 */
struct _ConvolutionDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* matrix_spins[MATRIX_ELEMENTS]; /* 5x5 matrix of spin buttons */
    GtkWidget* auto_normalize_check;
    GtkWidget* divisor_spin;
    GtkWidget* divisor_reset_button;
    GtkWidget* bias_spin;
    GtkWidget* bias_reset_button;
    float kernel[MATRIX_ELEMENTS];
    unsigned char divisor;
    unsigned char bias;
    gboolean auto_normalize;
    ConvolutionDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/**
 * Convolution parameters structure for viewport filtering
 */
typedef struct {
    float kernel[MATRIX_ELEMENTS];
    unsigned char divisor;
    unsigned char bias;
    gboolean auto_normalize;
    ConvolutionDialog* dialog; /* Dialog pointer for accessing document/layer */
} ConvolutionParams;

/**
 * Calculate sum of kernel for normalization
 */
static float calculate_kernel_sum(float* kernel) {
    float sum = 0.0f;
    int i;
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        sum += kernel[i];
    }
    return sum;
}

/**
 * Normalize kernel and calculate divisor/bias
 * When auto-normalize is enabled, the divisor is set to the sum of kernel values,
 * and the kernel is normalized so that the sum equals the divisor.
 */
static void normalize_kernel(float* kernel, unsigned char* divisor, unsigned char* bias) {
    float sum = calculate_kernel_sum(kernel);

    if (sum == 0.0f) {
        *divisor = 1;
        *bias = 127;
    } else if (sum > 0.0f) {
        *divisor = (unsigned char)fminf(sum, 255.0f); /* Clamp to 255 */
        *bias = 0;
    } else {
        *divisor = (unsigned char)fminf(fabsf(sum), 255.0f); /* Clamp to 255 */
        *bias = 255;
    }
}

/**
 * Update preview callback
 */
static void update_preview(ConvolutionDialog* dialog) {
    ConvolutionParams* stored_params;
    int i;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get current values from widgets */
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        if (dialog->matrix_spins[i]) {
            dialog->kernel[i] = (float)vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(dialog->matrix_spins[i]));
        }
    }

    dialog->auto_normalize = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->auto_normalize_check));

    /* Always calculate divisor from kernel sum (divisor should always be sum of kernel) */
    normalize_kernel(dialog->kernel, &dialog->divisor, &dialog->bias);

    /* Update UI widgets to show calculated values */
    if (dialog->divisor_spin) {
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->divisor_spin), (gdouble)dialog->divisor);
    }
    if (dialog->bias_spin) {
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->bias_spin), (gdouble)dialog->bias);
    }

    /* Call user callback if provided */
    if (dialog->preview_callback) {
        dialog->preview_callback(dialog, dialog->kernel, dialog->divisor, dialog->bias, dialog->auto_normalize, dialog->preview_user_data);
    }
}

/**
 * Matrix spin value changed callback
 */
static void on_matrix_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    (void)spin;
    update_preview(dialog);
}

/**
 * Divisor spin value changed callback
 */
static void on_divisor_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    (void)spin;
    update_preview(dialog);
}

/**
 * Offset spin value changed callback
 */
static void on_bias_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    (void)spin;
    update_preview(dialog);
}

/**
 * Auto normalize checkbox toggled callback
 */
static void on_auto_normalize_toggled(GtkToggleButton* toggle, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    gboolean active = gtk_toggle_button_get_active(toggle);

    /* Enable/disable divisor and bias controls based on auto_normalize state */
    if (dialog->divisor_spin) {
        gtk_widget_set_sensitive(dialog->divisor_spin, !active);
    }
    if (dialog->divisor_reset_button) {
        gtk_widget_set_sensitive(dialog->divisor_reset_button, !active);
    }
    if (dialog->bias_spin) {
        gtk_widget_set_sensitive(dialog->bias_spin, !active);
    }
    if (dialog->bias_reset_button) {
        gtk_widget_set_sensitive(dialog->bias_reset_button, !active);
    }

    update_preview(dialog);
}

/**
 * Reset matrix element button clicked callback
 */
static void on_matrix_reset_clicked(GtkButton* button, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "matrix-index"));

    if (index >= 0 && index < MATRIX_ELEMENTS && dialog->matrix_spins[index]) {
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->matrix_spins[index]), 0.0);
        update_preview(dialog);
    }
}

/**
 * Reset divisor button clicked callback
 */
static void on_divisor_reset_clicked(GtkButton* button, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    (void)button;
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->divisor_spin), 0.0);
    update_preview(dialog);
}

/**
 * Reset offset button clicked callback
 */
static void on_offset_reset_clicked(GtkButton* button, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    (void)button;
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->bias_spin), 0.0);
    update_preview(dialog);
}

/**
 * Helper function to set scaled reset button icon
 */
static void set_scaled_reset_button_icon(GtkButton* button, gint size) {
    if (!button) {
        return;
    }

    GError* error = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_resource("/icons/reset.png", &error);
    if (!pixbuf) {
        if (error) {
            g_error_free(error);
        }
        return;
    }

    /* Scale to specified size */
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, size, size, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);

    if (scaled) {
        GtkWidget* image = gtk_image_new_from_pixbuf(scaled);
        gtk_button_set_image(button, image);
        g_object_unref(scaled);
    }
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    ConvolutionDialog* dialog = (ConvolutionDialog*)user_data;
    (void)widget;
    convolution_dialog_reset(dialog);
}

/**
 * Create a new convolution dialog
 */
ConvolutionDialog* convolution_dialog_new(const gchar* title) {
    ConvolutionDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* matrix_vbox;
    GtkWidget* matrix_grid;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* spin_hbox;
    GtkWidget* spin;
    GtkWidget* reset_button;
    GtkAdjustment* adjustment;
    GtkWidget* checkbox_vbox;
    GtkWidget* checkbox;
    GtkWidget* button_box;
    int i, row, col;

    if (!title) {
        return NULL;
    }

    dialog = (ConvolutionDialog*)g_malloc(sizeof(ConvolutionDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        dialog->matrix_spins[i] = NULL;
    }
    dialog->auto_normalize_check = NULL;
    dialog->divisor_spin = NULL;
    dialog->divisor_reset_button = NULL;
    dialog->bias_spin = NULL;
    dialog->bias_reset_button = NULL;
    dialog->preview_callback = NULL;
    dialog->preview_user_data = NULL;

    /* Set default parameters */
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        dialog->kernel[i] = 0.0f;
    }
    /* Center element is 1.0 (identity) */
    dialog->kernel[12] = 1.0f; /* Row 2, Col 2 (0-indexed: row 2*5 + 2 = 12) */
    dialog->divisor = 0;
    dialog->bias = 0;
    dialog->auto_normalize = TRUE;

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
    gtk_widget_set_margin_start(right_vbox, 10);
    gtk_widget_set_margin_end(right_vbox, 10);
    gtk_widget_set_margin_top(right_vbox, 10);
    gtk_widget_set_margin_bottom(right_vbox, 10);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, FALSE, FALSE, 0);

    /* Create matrix grid */
    matrix_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(matrix_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), matrix_vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("convolution matrix"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(matrix_vbox), label, FALSE, FALSE, 0);

    matrix_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(matrix_grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(matrix_grid), 3);
    gtk_box_pack_start(GTK_BOX(matrix_vbox), matrix_grid, FALSE, FALSE, 0);

    /* Create 5x5 matrix of spin buttons */
    for (row = 0; row < MATRIX_SIZE; row++) {
        for (col = 0; col < MATRIX_SIZE; col++) {
            i = row * MATRIX_SIZE + col;

            spin_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
            gtk_widget_set_size_request(spin_hbox, 82, 25);

            adjustment = gtk_adjustment_new((gdouble)dialog->kernel[i], -100.0, 100.0, 0.1, 1.0, 0.0);
            spin = vertical_spin_button_new(adjustment, 0.1, 1);
            gtk_widget_set_vexpand(spin, FALSE);
            gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
            gtk_box_pack_start(GTK_BOX(spin_hbox), spin, TRUE, TRUE, 0);
            dialog->matrix_spins[i] = spin;
            g_signal_connect(spin, "value-changed", G_CALLBACK(on_matrix_spin_changed), dialog);

            /* Create reset button for this matrix element */
            reset_button = gtk_button_new();
            set_scaled_reset_button_icon(GTK_BUTTON(reset_button), 12);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
            gtk_widget_set_size_request(reset_button, 16, 16);
            gtk_widget_set_vexpand(reset_button, FALSE);
            gtk_widget_set_valign(reset_button, GTK_ALIGN_CENTER);
            g_object_set_data(G_OBJECT(reset_button), "matrix-index", GINT_TO_POINTER(i));
            g_signal_connect(reset_button, "clicked", G_CALLBACK(on_matrix_reset_clicked), dialog);
            gtk_box_pack_start(GTK_BOX(spin_hbox), reset_button, FALSE, FALSE, 0);

            gtk_grid_attach(GTK_GRID(matrix_grid), spin_hbox, col, row, 1, 1);
        }
    }

    /* Create auto normalize checkbox */
    checkbox_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(checkbox_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), checkbox_vbox, FALSE, FALSE, 0);

    checkbox = gtk_check_button_new_with_label("automatically normalize divisor and offset");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), dialog->auto_normalize);
    gtk_box_pack_start(GTK_BOX(checkbox_vbox), checkbox, FALSE, FALSE, 0);
    dialog->auto_normalize_check = checkbox;
    g_signal_connect(checkbox, "toggled", G_CALLBACK(on_auto_normalize_toggled), dialog);

    /* Create horizontal container for divisor and bias controls */
    GtkWidget* params_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_bottom(params_hbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), params_hbox, FALSE, FALSE, 0);

    /* Create divisor control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(params_hbox), control_vbox, TRUE, TRUE, 0);

    label = gtk_label_new(_("divisor"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    spin_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(spin_hbox, FALSE);
    gtk_widget_set_size_request(spin_hbox, 82, 25);
    gtk_box_pack_start(GTK_BOX(control_vbox), spin_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->divisor, 0.0, 255.0, 1.0, 1.0, 0.0);
    spin = vertical_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(spin_hbox), spin, TRUE, TRUE, 0);
    dialog->divisor_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_divisor_spin_changed), dialog);

    reset_button = gtk_button_new();
    set_scaled_reset_button_icon(GTK_BUTTON(reset_button), 12);
    gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
    gtk_widget_set_size_request(reset_button, 16, 16);
    gtk_widget_set_vexpand(reset_button, FALSE);
    gtk_widget_set_valign(reset_button, GTK_ALIGN_CENTER);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(on_divisor_reset_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(spin_hbox), reset_button, FALSE, FALSE, 0);
    dialog->divisor_reset_button = reset_button;

    /* Create bias control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(params_hbox), control_vbox, TRUE, TRUE, 0);

    label = gtk_label_new(_("offset"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    spin_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(spin_hbox, FALSE);
    gtk_widget_set_size_request(spin_hbox, 82, 25);
    gtk_box_pack_start(GTK_BOX(control_vbox), spin_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->bias, 0.0, 255.0, 1.0, 1.0, 0.0);
    spin = vertical_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(spin_hbox), spin, TRUE, TRUE, 0);
    dialog->bias_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_bias_spin_changed), dialog);

    reset_button = gtk_button_new();
    set_scaled_reset_button_icon(GTK_BUTTON(reset_button), 12);
    gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
    gtk_widget_set_size_request(reset_button, 16, 16);
    gtk_widget_set_vexpand(reset_button, FALSE);
    gtk_widget_set_valign(reset_button, GTK_ALIGN_CENTER);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(on_offset_reset_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(spin_hbox), reset_button, FALSE, FALSE, 0);
    dialog->bias_reset_button = reset_button;

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

        /* Create reset button with icon */
        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.png");
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

    /* Set initial state of divisor and bias controls based on auto_normalize */
    if (dialog->auto_normalize) {
        if (dialog->divisor_spin) {
            gtk_widget_set_sensitive(dialog->divisor_spin, FALSE);
        }
        if (dialog->divisor_reset_button) {
            gtk_widget_set_sensitive(dialog->divisor_reset_button, FALSE);
        }
        if (dialog->bias_spin) {
            gtk_widget_set_sensitive(dialog->bias_spin, FALSE);
        }
        if (dialog->bias_reset_button) {
            gtk_widget_set_sensitive(dialog->bias_reset_button, FALSE);
        }
    }

    return dialog;
}

/**
 * Free convolution dialog
 */
void convolution_dialog_free(ConvolutionDialog* dialog) {
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
GtkWindow* convolution_dialog_get_window(ConvolutionDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void convolution_dialog_set_layers(ConvolutionDialog* dialog, ImageLayer* original, ImageLayer* temp) {
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
 * Run the dialog and get convolution parameters
 */
gint convolution_dialog_run(ConvolutionDialog* dialog, GtkWindow* parent,
                            float* kernel, unsigned char* divisor, unsigned char* bias) {
    gint response;
    int i;

    if (!dialog || !kernel || !divisor || !bias) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Get final values from widgets */
        for (i = 0; i < MATRIX_ELEMENTS; i++) {
            if (dialog->matrix_spins[i]) {
                kernel[i] = (float)vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(dialog->matrix_spins[i]));
            }
        }
        /* Always calculate divisor from kernel sum (not from UI widget) */
        normalize_kernel(kernel, divisor, bias);
    }

    return response;
}

/**
 * Update the after layer in preview
 */
void convolution_dialog_update_after_layer(ConvolutionDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    if (layer && layer->surface) {
        /* Pass the full unmasked surface - preview widget will handle masking display */
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);
    filter_preview_refresh(dialog->preview);

    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Set preview callback for live updates
 */
void convolution_dialog_set_preview_callback(ConvolutionDialog* dialog,
                                             ConvolutionDialogPreviewCallback callback,
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
void convolution_dialog_reset(ConvolutionDialog* dialog) {
    int i;

    if (!dialog) {
        return;
    }

    /* Reset matrix to identity (center = 1.0, others = 0.0) */
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        if (dialog->matrix_spins[i]) {
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->matrix_spins[i]), (i == 12) ? 1.0 : 0.0);
        }
    }

    /* Reset divisor and bias */
    if (dialog->divisor_spin) {
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->divisor_spin), 0.0);
    }
    if (dialog->bias_spin) {
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->bias_spin), 0.0);
    }

    /* Reset auto normalize checkbox */
    if (dialog->auto_normalize_check) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->auto_normalize_check), TRUE);
    }

    update_preview(dialog);
}
