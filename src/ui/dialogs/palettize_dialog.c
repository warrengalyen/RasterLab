#include "ui/dialogs/palettize_dialog.h"
#include "document.h"
#include "filters.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_utils.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * Palettize dialog structure
 */
struct _PalettizeDialog {
    GtkWidget* dialog;
    FilterPreview* preview;

    /* Tab notebook */
    GtkWidget* notebook;

    /* Optional tab widgets */
    GtkWidget* quantize_method_combo;
    GtkWidget* max_colors_spin;
    GtkWidget* dither_method_combo;
    GtkWidget* dither_amount_scale;
    GtkWidget* dither_amount_spin;

    /* From File tab widgets */
    GtkWidget* palette_file_button;
    GtkWidget* palette_file_label;
    GtkWidget* file_dither_method_combo;
    GtkWidget* file_dither_amount_scale;
    GtkWidget* file_dither_amount_spin;

    PalettizeParams params;
};

/**
 * Helper function to wrap PalettizeParams filter for viewport system
 */
static cairo_surface_t* apply_palettize_filter_to_viewport_surface(cairo_surface_t* viewport_surface, gpointer params) {
    PalettizeParams* palettize_params = (PalettizeParams*)params;
    ImageLayer* temp_layer;
    cairo_surface_t* result;
    OC_STATUS status;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    gint channels = 4; /* RGBA format for Ocular (supports 3 or 4 channels) */

    if (!viewport_surface || !palettize_params) {
        return NULL;
    }

    /* NOTE: Selection masking is handled by the FilterPreview draw path. */

    /* Get viewport dimensions */
    width = cairo_image_surface_get_width(viewport_surface);
    height = cairo_image_surface_get_height(viewport_surface);

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    /* NOTE: Selection masking is handled by the FilterPreview draw path.
     * This viewport filter should operate purely on the viewport pixels to avoid
     * coordinate bugs when panning / 1:1 mode is enabled. */

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

    /* Allocate RGBA buffers */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_free(rgba_input);
        g_free(rgba_output);
        layer_free(temp_layer);
        return NULL;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(temp_layer->surface, rgba_input)) {
        g_free(rgba_input);
        g_free(rgba_output);
        layer_free(temp_layer);
        return NULL;
    }

    /* Apply palettize filter */
    if (palettize_params->use_file && palettize_params->palette_file) {
        status = ocularPalettetizeFromFile(
            rgba_input,
            rgba_output,
            width,
            height,
            channels,
            palettize_params->palette_file,
            palettize_params->dither_method,
            palettize_params->dither_amount);
    } else {
        status = ocularPalettetizeFromImage(
            rgba_input,
            rgba_output,
            width,
            height,
            channels,
            palettize_params->quantize_method,
            palettize_params->max_colors,
            palettize_params->dither_method,
            palettize_params->dither_amount);
    }

    if (status != OC_STATUS_OK) {
        g_free(rgba_input);
        g_free(rgba_output);
        layer_free(temp_layer);
        return NULL;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(temp_layer->surface, rgba_output)) {
        g_free(rgba_input);
        g_free(rgba_output);
        layer_free(temp_layer);
        return NULL;
    }

    g_free(rgba_input);
    g_free(rgba_output);

    /* Return a reference to the filtered surface */
    result = cairo_surface_reference(temp_layer->surface);
    layer_free(temp_layer);

    return result;
}

/**
 * Update preview callback
 */
static void update_preview(PalettizeDialog* dialog) {
    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get current tab */
    gint active_tab = gtk_notebook_get_current_page(GTK_NOTEBOOK(dialog->notebook));
    dialog->params.use_file = (active_tab == 1); /* Tab 1 is "From File" */

    if (dialog->params.use_file) {
        /* From File tab - get dither method and amount from file tab widgets */
        gint dither_index = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->file_dither_method_combo));
        dialog->params.dither_method = (OcDitherMethod)dither_index;
        dialog->params.dither_amount = (gint)gtk_range_get_value(GTK_RANGE(dialog->file_dither_amount_scale));
    } else {
        /* Optimal tab - get all parameters */
        gint quantize_index = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->quantize_method_combo));
        dialog->params.quantize_method = (OcQuantizeMethod)quantize_index;
        dialog->params.max_colors = (gint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(dialog->max_colors_spin));

        gint dither_index = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->dither_method_combo));
        dialog->params.dither_method = (OcDitherMethod)dither_index;
        dialog->params.dither_amount = (gint)gtk_range_get_value(GTK_RANGE(dialog->dither_amount_scale));
    }

    /* Set dialog pointer in params for selection masking */
    dialog->params.dialog = dialog;

    /* Set filter function on preview to use viewport-based filtering */
    filter_preview_set_filter_function(dialog->preview, apply_palettize_filter_to_viewport_surface, &dialog->params);
    filter_preview_refresh(dialog->preview);
}

/**
 * Quantize method changed callback
 */
static void on_quantize_method_changed(GtkComboBox* combo, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    (void)combo;
    update_preview(dialog);
}

/**
 * Max colors changed callback
 */
static void on_max_colors_changed(GtkSpinButton* spin, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    (void)spin;
    update_preview(dialog);
}

/**
 * Dither method changed callback
 */
static void on_dither_method_changed(GtkComboBox* combo, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    (void)combo;
    update_preview(dialog);
}

/**
 * Dither amount changed callback
 */
static void on_dither_amount_changed(GtkRange* range, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->dither_amount_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->dither_amount_spin), value);
    update_preview(dialog);
}

/**
 * Dither amount spin changed callback
 */
static void on_dither_amount_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->dither_amount_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->dither_amount_scale), value);
}

/**
 * File dither method changed callback
 */
static void on_file_dither_method_changed(GtkComboBox* combo, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    (void)combo;
    update_preview(dialog);
}

/**
 * File dither amount changed callback
 */
static void on_file_dither_amount_changed(GtkRange* range, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->file_dither_amount_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->file_dither_amount_spin), value);
    update_preview(dialog);
}

/**
 * File dither amount spin changed callback
 */
static void on_file_dither_amount_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->file_dither_amount_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->file_dither_amount_scale), value);
}

/**
 * Palette file button clicked callback
 */
static void on_palette_file_clicked(GtkButton* button, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    GtkWidget* file_dialog;
    GtkFileFilter* filter;
    gint response;
    gchar* filename;

    (void)button;

    /* Create file chooser dialog */
    file_dialog = gtk_file_chooser_dialog_new(
        "Select Palette File",
        palettize_dialog_get_window(dialog),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    /* Add file filters for all supported palette formats */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "GIMP Palette (*.gpl)");
    gtk_file_filter_add_pattern(filter, "*.gpl");
    gtk_file_filter_add_pattern(filter, "*.GPL");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "RIFF Palette (*.pal)");
    gtk_file_filter_add_pattern(filter, "*.pal");
    gtk_file_filter_add_pattern(filter, "*.PAL");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Adobe Color Swatch (*.aco)");
    gtk_file_filter_add_pattern(filter, "*.aco");
    gtk_file_filter_add_pattern(filter, "*.ACO");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Paint.NET Palette (*.txt)");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.TXT");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Adobe Color Table (*.act)");
    gtk_file_filter_add_pattern(filter, "*.act");
    gtk_file_filter_add_pattern(filter, "*.ACT");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Adobe Swatch Exchange (*.ase)");
    gtk_file_filter_add_pattern(filter, "*.ase");
    gtk_file_filter_add_pattern(filter, "*.ASE");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All Palette Files");
    gtk_file_filter_add_pattern(filter, "*.gpl");
    gtk_file_filter_add_pattern(filter, "*.pal");
    gtk_file_filter_add_pattern(filter, "*.aco");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.act");
    gtk_file_filter_add_pattern(filter, "*.ase");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    response = gtk_dialog_run(GTK_DIALOG(file_dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_dialog));
        if (filename) {
            /* Free old filename if exists */
            if (dialog->params.palette_file) {
                g_free(dialog->params.palette_file);
            }
            dialog->params.palette_file = g_strdup(filename);

            /* Update label */
            gchar* basename = g_path_get_basename(filename);
            gtk_label_set_text(GTK_LABEL(dialog->palette_file_label), basename);
            g_free(basename);
            g_free(filename);

            update_preview(dialog);
        }
    }

    gtk_widget_destroy(file_dialog);
}

/**
 * Tab switched callback
 */
static void on_tab_switched(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    (void)notebook;
    (void)page;
    (void)page_num;
    update_preview(dialog);
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    PalettizeDialog* dialog = (PalettizeDialog*)user_data;
    (void)widget;
    palettize_dialog_reset(dialog);
}

/**
 * Create optimal tab
 */
static GtkWidget* create_optimal_tab(PalettizeDialog* dialog) {
    GtkWidget* vbox;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* combo;
    GtkWidget* spin;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkAdjustment* adjustment;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    /* Quantize method combo */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Quantize Method");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Median Cut");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Octree");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 1);
    gtk_box_pack_start(GTK_BOX(control_vbox), combo, FALSE, FALSE, 0);
    dialog->quantize_method_combo = combo;
    g_signal_connect(combo, "changed", G_CALLBACK(on_quantize_method_changed), dialog);

    /* Max colors spin */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Palette Size");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    adjustment = gtk_adjustment_new(256.0, 2.0, 256.0, 1.0, 10.0, 0.0);
    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 100, -1);
    gtk_box_pack_start(GTK_BOX(control_vbox), spin, FALSE, FALSE, 0);
    dialog->max_colors_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_max_colors_changed), dialog);

    /* Dither method combo */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Dither Method");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "None");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Burkes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Floyd-Steinberg");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Stucki");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Atkinson");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sierra");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sierra Two Row");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sierra Lite");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "JJN");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Single Neighbor");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Bayer 4x4");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Bayer 8x8");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(control_vbox), combo, FALSE, FALSE, 0);
    dialog->dither_method_combo = combo;
    g_signal_connect(combo, "changed", G_CALLBACK(on_dither_method_changed), dialog);

    /* Dither amount */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Dither Amount");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(50.0, 0.0, 100.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->dither_amount_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_dither_amount_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->dither_amount_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_dither_amount_spin_changed), dialog);

    return vbox;
}

/**
 * Create from file tab
 */
static GtkWidget* create_from_file_tab(PalettizeDialog* dialog) {
    GtkWidget* vbox;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* combo;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adjustment;
    GtkWidget* button_hbox;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    /* Palette file button */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Palette File");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), button_hbox, FALSE, FALSE, 0);

    GtkWidget* button = gtk_button_new_with_label("Select File...");
    gtk_box_pack_start(GTK_BOX(button_hbox), button, FALSE, FALSE, 0);
    dialog->palette_file_button = button;
    g_signal_connect(button, "clicked", G_CALLBACK(on_palette_file_clicked), dialog);

    dialog->palette_file_label = gtk_label_new("No file selected");
    gtk_widget_set_halign(dialog->palette_file_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(button_hbox), dialog->palette_file_label, TRUE, TRUE, 0);

    /* Dither method combo */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Dither Method");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "None");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Burkes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Floyd-Steinberg");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Stucki");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Atkinson");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sierra");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sierra Two Row");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sierra Lite");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "JJN");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Single Neighbor");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Bayer 4x4");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Bayer 8x8");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(control_vbox), combo, FALSE, FALSE, 0);
    dialog->file_dither_method_combo = combo;
    g_signal_connect(combo, "changed", G_CALLBACK(on_file_dither_method_changed), dialog);

    /* Dither amount */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("Dither Amount");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(50.0, 0.0, 100.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->file_dither_amount_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_file_dither_amount_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->file_dither_amount_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_file_dither_amount_spin_changed), dialog);

    return vbox;
}

/**
 * Create a new palettize dialog
 */
PalettizeDialog* palettize_dialog_new(const gchar* title) {
    PalettizeDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* notebook;
    GtkWidget* optimal_tab;
    GtkWidget* from_file_tab;
    GtkWidget* reset_button;
    GtkWidget* button_box;

    if (!title) {
        return NULL;
    }

    dialog = (PalettizeDialog*)g_malloc(sizeof(PalettizeDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->notebook = NULL;
    dialog->quantize_method_combo = NULL;
    dialog->max_colors_spin = NULL;
    dialog->dither_method_combo = NULL;
    dialog->dither_amount_scale = NULL;
    dialog->dither_amount_spin = NULL;
    dialog->palette_file_button = NULL;
    dialog->palette_file_label = NULL;
    dialog->file_dither_method_combo = NULL;
    dialog->file_dither_amount_scale = NULL;
    dialog->file_dither_amount_spin = NULL;
    dialog->params.palette_file = NULL;

    /* Set default parameters */
    dialog->params.use_file = FALSE;
    dialog->params.quantize_method = OC_QUANTIZE_MEDIAN_CUT;
    dialog->params.max_colors = 256;
    dialog->params.dither_method = OC_DITHER_NONE;
    dialog->params.dither_amount = 50;

    /* Create dialog window */
    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 (GtkDialogFlags)((GtkDialogFlags)GTK_DIALOG_MODAL | (GtkDialogFlags)GTK_DIALOG_DESTROY_WITH_PARENT),
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

    /* Create notebook for tabs */
    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), notebook, TRUE, TRUE, 0);
    dialog->notebook = notebook;
    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_tab_switched), dialog);

    /* Create optimal tab */
    optimal_tab = create_optimal_tab(dialog);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), optimal_tab, gtk_label_new("Optimal"));

    /* Create from file tab */
    from_file_tab = create_from_file_tab(dialog);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), from_file_tab, gtk_label_new("From File"));

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
 * Free palettize dialog
 */
void palettize_dialog_free(PalettizeDialog* dialog) {
    if (!dialog) {
        return;
    }

    if (dialog->params.palette_file) {
        g_free(dialog->params.palette_file);
    }

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    g_free(dialog);
}

/**
 * Get the dialog window
 */
GtkWindow* palettize_dialog_get_window(PalettizeDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void palettize_dialog_set_layers(PalettizeDialog* dialog, ImageLayer* original, ImageLayer* temp) {
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
 * Run the dialog and get palettize parameters
 */
gint palettize_dialog_run(PalettizeDialog* dialog, GtkWindow* parent, PalettizeParams* params) {
    gint response;

    if (!dialog || !params) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Get current tab */
        gint active_tab = gtk_notebook_get_current_page(GTK_NOTEBOOK(dialog->notebook));
        dialog->params.use_file = (active_tab == 1);

        if (dialog->params.use_file) {
            /* From File tab */
            gint dither_index = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->file_dither_method_combo));
            params->dither_method = (OcDitherMethod)dither_index;
            params->dither_amount = (gint)gtk_range_get_value(GTK_RANGE(dialog->file_dither_amount_scale));

            /* Copy palette file path */
            if (dialog->params.palette_file) {
                params->palette_file = g_strdup(dialog->params.palette_file);
            } else {
                params->palette_file = NULL;
            }
        } else {
            /* Optional tab */
            gint quantize_index = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->quantize_method_combo));
            params->quantize_method = (OcQuantizeMethod)quantize_index;
            params->max_colors = (gint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(dialog->max_colors_spin));

            gint dither_index = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->dither_method_combo));
            params->dither_method = (OcDitherMethod)dither_index;
            params->dither_amount = (gint)gtk_range_get_value(GTK_RANGE(dialog->dither_amount_scale));

            params->palette_file = NULL;
        }

        params->use_file = dialog->params.use_file;
    }

    return response;
}

/**
 * Update the after layer in preview
 */
void palettize_dialog_update_after_layer(PalettizeDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    if (layer && layer->surface) {
        /* Pass the full unmasked surface - preview widget will handle masking display */
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);

    /* Clean up reference */
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Reset all controls to default values
 */
void palettize_dialog_reset(PalettizeDialog* dialog) {
    if (!dialog) {
        return;
    }

    /* Reset optional tab */
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->quantize_method_combo), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->max_colors_spin), 256.0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->dither_method_combo), 0);
    gtk_range_set_value(GTK_RANGE(dialog->dither_amount_scale), 50.0);

    /* Reset from file tab */
    if (dialog->params.palette_file) {
        g_free(dialog->params.palette_file);
        dialog->params.palette_file = NULL;
    }
    gtk_label_set_text(GTK_LABEL(dialog->palette_file_label), "No file selected");
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->file_dither_method_combo), 0);
    gtk_range_set_value(GTK_RANGE(dialog->file_dither_amount_scale), 50.0);

    /* Reset to optional tab */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(dialog->notebook), 0);

    update_preview(dialog);
}
