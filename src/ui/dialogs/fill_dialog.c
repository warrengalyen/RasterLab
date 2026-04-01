#include "ui/dialogs/fill_dialog.h"
#include "ui.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/tools_panel.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include "i18n.h"

/* Blend mode names matching BlendMode enum (document.h) */
static const char* const blend_mode_names[] = {
    "Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color",
    "Lighten", "Screen", "Color Dodge", "Linear Dodge", "Lighter Color",
    "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix",
    "Difference", "Exclusion", "Subtract", "Divide",
    "Hue", "Saturation", "Color", "Luminosity"};

typedef struct {
    GtkWidget* dialog;
    GtkWidget* custom_color_radio;
    GtkWidget* custom_color_button;
    GtkWidget* foreground_color_radio;
    GtkWidget* background_color_radio;
    GtkWidget* opacity_scale;
    GtkWidget* opacity_spin;
    GtkWidget* blend_mode_combo;
    GdkRGBA custom_color;
} FillDialogData;

static void on_custom_color_update(double r, double g, double b, gpointer user_data) {
    FillDialogData* data = (FillDialogData*)user_data;
    if (!data)
        return;
    data->custom_color.red = r;
    data->custom_color.green = g;
    data->custom_color.blue = b;
    data->custom_color.alpha = 1.0;
    update_color_button_appearance(data->custom_color_button, &data->custom_color);
}

static void on_custom_color_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    FillDialogData* data = (FillDialogData*)user_data;
    if (!data || !data->dialog)
        return;

    GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(data->dialog));
    if (!GTK_IS_WINDOW(parent))
        parent = NULL;

    GtkWidget* color_dialog = color_chooser_dialog_new(
        parent,
        "Choose Fill Color",
        &data->custom_color,
        on_custom_color_update,
        data,
        FALSE);

    gtk_dialog_run(GTK_DIALOG(color_dialog));
    double r, g, b;
    color_chooser_dialog_get_color(color_dialog, &r, &g, &b);
    data->custom_color.red = r;
    data->custom_color.green = g;
    data->custom_color.blue = b;
    data->custom_color.alpha = 1.0;
    update_color_button_appearance(data->custom_color_button, &data->custom_color);
    gtk_widget_destroy(color_dialog);
}

static void on_color_source_toggled(GtkToggleButton* btn, gpointer user_data) {
    FillDialogData* data = (FillDialogData*)user_data;
    if (!data || !gtk_toggle_button_get_active(btn))
        return;
    gtk_widget_set_sensitive(data->custom_color_button,
                             (GTK_WIDGET(btn) == data->custom_color_radio));
}

static void on_fill_dialog_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

gboolean fill_dialog_run(GtkWindow* parent, FillDialogResult* result) {
    GtkBuilder* builder;
    GError* error = NULL;
    FillDialogData data = {0};
    gint response;

    if (!result)
        return FALSE;

    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/fill_dialog.glade", &error)) {
        g_warning("Failed to load fill_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error)
            g_error_free(error);
        g_object_unref(builder);
        return FALSE;
    }

    data.dialog = GTK_WIDGET(gtk_builder_get_object(builder, "fill_dialog"));
    data.custom_color_radio = GTK_WIDGET(gtk_builder_get_object(builder, "custom_color_radio"));
    data.custom_color_button = GTK_WIDGET(gtk_builder_get_object(builder, "custom_color_button"));
    data.foreground_color_radio = GTK_WIDGET(gtk_builder_get_object(builder, "foreground_color_radio"));
    data.background_color_radio = GTK_WIDGET(gtk_builder_get_object(builder, "background_color_radio"));
    data.opacity_scale = GTK_WIDGET(gtk_builder_get_object(builder, "opacity_scale"));
    data.opacity_spin = GTK_WIDGET(gtk_builder_get_object(builder, "opacity_spin"));
    data.blend_mode_combo = GTK_WIDGET(gtk_builder_get_object(builder, "blend_mode_combo"));

    if (!data.dialog || !data.custom_color_radio || !data.custom_color_button ||
        !data.foreground_color_radio || !data.background_color_radio ||
        !data.opacity_spin || !data.blend_mode_combo) {
        g_warning("Failed to get fill dialog widgets from builder");
        g_object_unref(builder);
        return FALSE;
    }

    /* Group radio buttons */
    gtk_radio_button_join_group(GTK_RADIO_BUTTON(data.foreground_color_radio),
                                GTK_RADIO_BUTTON(data.custom_color_radio));
    gtk_radio_button_join_group(GTK_RADIO_BUTTON(data.background_color_radio),
                                GTK_RADIO_BUTTON(data.custom_color_radio));

    /* Default: custom color white, 100% opacity, Normal blend */
    data.custom_color.red = data.custom_color.green = data.custom_color.blue = 1.0;
    data.custom_color.alpha = 1.0;
    update_color_button_appearance(data.custom_color_button, &data.custom_color);
    ui_utils_widget_set_hand_cursor(data.custom_color_button);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data.custom_color_radio), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data.opacity_spin), 100.0);

    /* Blend mode list */
    ui_apply_list_combobox_style(data.blend_mode_combo);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(data.blend_mode_combo), TRUE);
    g_signal_connect(data.blend_mode_combo, "notify::popup-shown",
                     G_CALLBACK(ui_combo_popup_shown_fix), NULL);

    GtkListStore* blend_store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeIter iter;
    for (int i = 0; i < BLEND_MODE_COUNT; i++) {
        gtk_list_store_append(blend_store, &iter);
        gtk_list_store_set(blend_store, &iter, 0, blend_mode_names[i], -1);
    }
    gtk_combo_box_set_model(GTK_COMBO_BOX(data.blend_mode_combo), GTK_TREE_MODEL(blend_store));
    g_object_unref(blend_store);
    GtkCellRenderer* cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(data.blend_mode_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(data.blend_mode_combo), cell, "text", 0, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(data.blend_mode_combo), 0);

    g_signal_connect(data.custom_color_button, "clicked", G_CALLBACK(on_custom_color_clicked), &data);
    g_signal_connect(data.custom_color_radio, "toggled", G_CALLBACK(on_color_source_toggled), &data);
    g_signal_connect(data.foreground_color_radio, "toggled", G_CALLBACK(on_color_source_toggled), &data);
    g_signal_connect(data.background_color_radio, "toggled", G_CALLBACK(on_color_source_toggled), &data);

    /* Connect OK/Cancel so gtk_dialog_run returns the right response */
    GtkWidget* ok_btn = GTK_WIDGET(gtk_builder_get_object(builder, "fill_dialog_ok_button"));
    GtkWidget* cancel_btn = GTK_WIDGET(gtk_builder_get_object(builder, "fill_dialog_cancel_button"));
    if (ok_btn) {
        g_object_set_data(G_OBJECT(ok_btn), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_fill_dialog_button_clicked), data.dialog);
    }
    if (cancel_btn) {
        g_object_set_data(G_OBJECT(cancel_btn), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_fill_dialog_button_clicked), data.dialog);
    }

    gtk_window_set_transient_for(GTK_WINDOW(data.dialog), parent);
    gtk_window_set_modal(GTK_WINDOW(data.dialog), TRUE);

    response = gtk_dialog_run(GTK_DIALOG(data.dialog));

    if (response != GTK_RESPONSE_OK && response != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(data.dialog);
        g_object_unref(builder);
        return FALSE;
    }

    /* Read result from widgets before destroying the dialog */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data.custom_color_radio)))
        result->color_source = FILL_COLOR_CUSTOM;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data.foreground_color_radio)))
        result->color_source = FILL_COLOR_FOREGROUND;
    else
        result->color_source = FILL_COLOR_BACKGROUND;

    if (result->color_source == FILL_COLOR_CUSTOM) {
        result->color = data.custom_color;
    } else if (result->color_source == FILL_COLOR_FOREGROUND) {
        tools_panel_get_foreground_color(&result->color);
    } else {
        tools_panel_get_background_color(&result->color);
    }

    result->opacity = (gint)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(data.opacity_spin)) + 0.5);
    if (result->opacity < 0)
        result->opacity = 0;
    if (result->opacity > 100)
        result->opacity = 100;

    gint blend_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(data.blend_mode_combo));
    result->blend_mode = (blend_idx >= 0 && blend_idx < BLEND_MODE_COUNT)
                             ? (BlendMode)blend_idx
                             : BLEND_MODE_NORMAL;

    gtk_widget_destroy(data.dialog);
    g_object_unref(builder);
    return TRUE;
}
