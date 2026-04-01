#include "ui/dialogs/new_layer_dialog.h"
#include "document.h"
#include "i18n.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

/**
 * New layer dialog structure
 */
struct _NewLayerDialog {
    GtkWidget* dialog;
    GtkWidget* name_entry;
    GtkWidget* bg_transparent_rb;
    GtkWidget* bg_black_rb;
    GtkWidget* bg_white_rb;
    GtkWidget* bg_custom_rb;
    GtkWidget* bg_custom_color;
    GtkWidget* position_combo;
    GtkWidget* set_active_cb;

    /* Custom background color */
    GdkRGBA custom_color;
};

/**
 * Color update callback for custom color button
 */
static void on_custom_color_update(double r, double g, double b, gpointer user_data) {
    NewLayerDialog* dialog = (NewLayerDialog*)user_data;
    if (!dialog) {
        return;
    }

    dialog->custom_color.red = r;
    dialog->custom_color.green = g;
    dialog->custom_color.blue = b;
    dialog->custom_color.alpha = 1.0;

    /* Update color button appearance */
    update_color_button_appearance(dialog->bg_custom_color, &dialog->custom_color);
}

/**
 * Custom color button clicked callback
 */
static void on_custom_color_clicked(GtkButton* button, gpointer user_data) {
    NewLayerDialog* dialog = (NewLayerDialog*)user_data;
    if (!dialog || !dialog->dialog) {
        return;
    }

    GtkWindow* parent = GTK_WINDOW(dialog->dialog);

    /* Create and show color chooser dialog */
    GtkWidget* color_dialog = color_chooser_dialog_new(
        parent,
        _("Choose Background Color"),
        &dialog->custom_color,
        on_custom_color_update,
        dialog,
        FALSE); /* Disable real-time updates */

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(color_dialog));

    /* Get final color */
    double r, g, b;
    color_chooser_dialog_get_color(color_dialog, &r, &g, &b);

    dialog->custom_color.red = r;
    dialog->custom_color.green = g;
    dialog->custom_color.blue = b;
    dialog->custom_color.alpha = 1.0;

    /* Update color button appearance */
    update_color_button_appearance(dialog->bg_custom_color, &dialog->custom_color);

    gtk_widget_destroy(color_dialog);
}

/**
 * Background radio button changed callback
 */
static void on_bg_changed(GtkRadioButton* button, gpointer user_data) {
    NewLayerDialog* dialog = (NewLayerDialog*)user_data;

    if (!dialog || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
        return;
    }

    /* Enable/disable custom color button based on selection */
    if (GTK_WIDGET(button) == dialog->bg_custom_rb) {
        gtk_widget_set_sensitive(dialog->bg_custom_color, TRUE);
    } else {
        gtk_widget_set_sensitive(dialog->bg_custom_color, FALSE);
    }
}

/**
 * Button clicked callback to emit dialog response
 */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/**
 * Create a new layer dialog
 */
NewLayerDialog* new_layer_dialog_new(void) {
    NewLayerDialog* dialog;
    GtkBuilder* builder;
    GError* error = NULL;
    static int layer_count = 1;
    gchar* default_name;

    dialog = (NewLayerDialog*)g_malloc(sizeof(NewLayerDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize all pointers to NULL */
    dialog->dialog = NULL;
    dialog->name_entry = NULL;
    dialog->bg_transparent_rb = NULL;
    dialog->bg_black_rb = NULL;
    dialog->bg_white_rb = NULL;
    dialog->bg_custom_rb = NULL;
    dialog->bg_custom_color = NULL;
    dialog->position_combo = NULL;
    dialog->set_active_cb = NULL;

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/new_layer_dialog.glade", &error)) {
        g_warning("Failed to load new_layer_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Get dialog widget */
    dialog->dialog = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_dialog"));
    if (!dialog->dialog) {
        g_warning("Failed to get new_layer_dialog from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Get all widgets */
    dialog->name_entry = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_name_text"));
    dialog->bg_transparent_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_bg_transparent_rb"));
    dialog->bg_black_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_bg_black_rb"));
    dialog->bg_white_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_bg_white_rb"));
    dialog->bg_custom_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_bg_custom_rb"));
    dialog->bg_custom_color = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_bg_custom_color"));
    dialog->position_combo = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_position_combo"));
    dialog->set_active_cb = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_set_active_cb"));

    if (!dialog->name_entry || !dialog->bg_transparent_rb || !dialog->bg_black_rb ||
        !dialog->bg_white_rb || !dialog->bg_custom_rb || !dialog->bg_custom_color ||
        !dialog->position_combo || !dialog->set_active_cb) {
        g_warning("Failed to get all widgets from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Set up radio button group */
    gtk_radio_button_join_group(GTK_RADIO_BUTTON(dialog->bg_black_rb),
                                GTK_RADIO_BUTTON(dialog->bg_transparent_rb));
    gtk_radio_button_join_group(GTK_RADIO_BUTTON(dialog->bg_white_rb),
                                GTK_RADIO_BUTTON(dialog->bg_transparent_rb));
    gtk_radio_button_join_group(GTK_RADIO_BUTTON(dialog->bg_custom_rb),
                                GTK_RADIO_BUTTON(dialog->bg_transparent_rb));

    /* Set default: transparent background */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->bg_transparent_rb), TRUE);
    gtk_widget_set_sensitive(dialog->bg_custom_color, FALSE);

    /* Set default custom color to RGB(60, 160, 255) */
    dialog->custom_color.red = 60.0 / 255.0;
    dialog->custom_color.green = 160.0 / 255.0;
    dialog->custom_color.blue = 255.0 / 255.0;
    dialog->custom_color.alpha = 1.0;
    update_color_button_appearance(dialog->bg_custom_color, &dialog->custom_color);
    ui_utils_widget_set_hand_cursor(dialog->bg_custom_color);

    /* Connect background radio button signals */
    g_signal_connect(dialog->bg_transparent_rb, "toggled", G_CALLBACK(on_bg_changed), dialog);
    g_signal_connect(dialog->bg_black_rb, "toggled", G_CALLBACK(on_bg_changed), dialog);
    g_signal_connect(dialog->bg_white_rb, "toggled", G_CALLBACK(on_bg_changed), dialog);
    g_signal_connect(dialog->bg_custom_rb, "toggled", G_CALLBACK(on_bg_changed), dialog);

    /* Connect custom color button click signal to open color chooser dialog */
    g_signal_connect(dialog->bg_custom_color, "clicked", G_CALLBACK(on_custom_color_clicked), dialog);

    /* Set default layer name */
    default_name = g_strdup_printf(_("Layer %d"), layer_count++);
    gtk_entry_set_text(GTK_ENTRY(dialog->name_entry), default_name);
    g_free(default_name);

    /* Populate position combo box with items */
    GtkListStore* position_store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeIter iter;

    gtk_list_store_append(position_store, &iter);
    gtk_list_store_set(position_store, &iter, 0, _("Above current layer"), -1);

    gtk_list_store_append(position_store, &iter);
    gtk_list_store_set(position_store, &iter, 0, _("Below current layer"), -1);

    gtk_list_store_append(position_store, &iter);
    gtk_list_store_set(position_store, &iter, 0, _("Top of layer stack"), -1);

    gtk_list_store_append(position_store, &iter);
    gtk_list_store_set(position_store, &iter, 0, _("Bottom of layer stack"), -1);

    /* Set the model */
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->position_combo), GTK_TREE_MODEL(position_store));
    g_object_unref(position_store);

    /* Set up cell renderer */
    GtkCellRenderer* cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->position_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->position_combo), cell, "text", 0, NULL);

    /* Set default: above current */
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->position_combo), 0);

    /* Set default: make active layer */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->set_active_cb), TRUE);

    /* Connect OK and Cancel buttons to emit dialog response */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_layer_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }

    /* Replace default titlebar with header bar */
    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), "Add New Layer");

    /* Clean up builder */
    g_object_unref(builder);

    return dialog;
}

/**
 * Free new layer dialog
 */
void new_layer_dialog_free(NewLayerDialog* dialog) {
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
GtkWindow* new_layer_dialog_get_window(NewLayerDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Run the dialog and get layer parameters
 */
gint new_layer_dialog_run(NewLayerDialog* dialog, GtkWindow* parent, NewLayerDialogResult** result) {
    gint response;
    const gchar* name_text;
    LayerBackgroundType background;
    LayerPosition position;
    gdouble custom_color[4];

    if (!dialog || !result) {
        return GTK_RESPONSE_CANCEL;
    }

    *result = NULL;

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Allocate result structure */
        *result = (NewLayerDialogResult*)g_malloc(sizeof(NewLayerDialogResult));
        if (!*result) {
            return GTK_RESPONSE_CANCEL;
        }

        /* Get layer name */
        name_text = gtk_entry_get_text(GTK_ENTRY(dialog->name_entry));
        if (name_text && strlen(name_text) > 0) {
            (*result)->name = g_strdup(name_text);
        } else {
            (*result)->name = g_strdup("Layer");
        }

        /* Get background type */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->bg_transparent_rb))) {
            background = LAYER_BACKGROUND_TRANSPARENT;
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->bg_black_rb))) {
            background = LAYER_BACKGROUND_BLACK;
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->bg_white_rb))) {
            background = LAYER_BACKGROUND_WHITE;
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->bg_custom_rb))) {
            background = LAYER_BACKGROUND_CUSTOM;
            /* Get custom color from dialog structure */
            custom_color[0] = dialog->custom_color.red;
            custom_color[1] = dialog->custom_color.green;
            custom_color[2] = dialog->custom_color.blue;
            custom_color[3] = dialog->custom_color.alpha;
            memcpy((*result)->custom_color, custom_color, sizeof(custom_color));
        } else {
            background = LAYER_BACKGROUND_TRANSPARENT;
        }
        (*result)->background = background;

        /* Get position */
        gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->position_combo));
        switch (active) {
            case 0:
                position = LAYER_POSITION_ABOVE_CURRENT;
                break;
            case 1:
                position = LAYER_POSITION_BELOW_CURRENT;
                break;
            case 2:
                position = LAYER_POSITION_TOP;
                break;
            case 3:
                position = LAYER_POSITION_BOTTOM;
                break;
            default:
                position = LAYER_POSITION_ABOVE_CURRENT;
                break;
        }
        (*result)->position = position;

        /* Get set active checkbox */
        (*result)->set_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->set_active_cb));
    }

    return response;
}

/**
 * Free dialog result structure
 */
void new_layer_dialog_result_free(NewLayerDialogResult* result) {
    if (!result) {
        return;
    }

    if (result->name) {
        g_free(result->name);
    }

    g_free(result);
}
