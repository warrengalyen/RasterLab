/*
 * Image resize dialog - layout and logic similar to canvas size dialog,
 * with resampling (interpolation) method selection.
 */

#include "ui/dialogs/resize_dialog.h"
#include "document.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

struct _ResizeDialog {
    GtkWidget* dialog;
    GtkWidget* width_spin;
    GtkWidget* height_spin;
    GtkWidget* resolution_spin;
    GtkWidget* width_units_combo;
    GtkWidget* height_units_combo;
    GtkWidget* resolution_units_combo;
    GtkWidget* width_reset_button;
    GtkWidget* height_reset_button;
    GtkWidget* resolution_reset_button;
    GtkWidget* preserve_ratio_toggle;
    GtkWidget* dimensions_text;
    GtkWidget* aspect_ratio_text;
    GtkWidget* resampling_combo;

    guint original_width;
    guint original_height;
    gdouble original_resolution;
    guint current_width;
    guint current_height;
    gdouble current_resolution;
    gdouble aspect_ratio;
    gboolean preserve_aspect;
    gint width_unit;
    gint height_unit;
    gint resolution_unit;
};

typedef enum {
    UNIT_PERCENT = 0,
    UNIT_PIXELS = 1,
    UNIT_INCHES = 2,
    UNIT_CENTIMETERS = 3,
    UNIT_MILLIMETERS = 4,
    UNIT_POINTS = 5,
    UNIT_PICAS = 6
} DimensionUnit;

typedef enum {
    RES_UNIT_PPI = 0,
    RES_UNIT_PPCM = 1
} ResolutionUnit;

#define INCHES_TO_CM 2.54
#define POINTS_TO_INCHES (1.0 / 72.0)
#define PICAS_TO_INCHES (1.0 / 6.0)

static void update_dimensions_label(ResizeDialog* dialog);
static void update_aspect_ratio_label(ResizeDialog* dialog);
static void update_preserve_ratio_icon(ResizeDialog* dialog);
static void convert_width_to_unit(ResizeDialog* dialog, DimensionUnit unit);
static void convert_height_to_unit(ResizeDialog* dialog, DimensionUnit unit);
static void convert_resolution_to_unit(ResizeDialog* dialog, ResolutionUnit unit);
static gdouble pixels_to_unit(guint pixels, DimensionUnit unit, gdouble resolution);
static guint unit_to_pixels(gdouble value, DimensionUnit unit, gdouble resolution);
static gdouble resolution_convert(gdouble value, ResolutionUnit from, ResolutionUnit to);
static gchar* format_file_size(guint64 bytes);
static void on_width_value_changed(GtkSpinButton* spin, gpointer user_data);
static void on_height_value_changed(GtkSpinButton* spin, gpointer user_data);
static void on_width_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_height_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_resolution_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_resolution_value_changed(GtkSpinButton* spin, gpointer user_data);
static void on_preserve_ratio_toggled(GtkToggleButton* button, gpointer user_data);
static void on_width_reset_clicked(GtkButton* button, gpointer user_data);
static void on_height_reset_clicked(GtkButton* button, gpointer user_data);
static void on_resolution_reset_clicked(GtkButton* button, gpointer user_data);
static void set_reset_button_icon(GtkButton* button);
static void on_button_clicked(GtkButton* button, gpointer user_data);

static void on_width_unit_changed(GtkComboBox* combo, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    gint active = gtk_combo_box_get_active(combo);
    if (active >= 0) {
        dialog->width_unit = active;
        dialog->height_unit = active;
        g_signal_handlers_block_by_func(dialog->height_units_combo, G_CALLBACK(on_height_unit_changed), dialog);
        gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->height_units_combo), active);
        g_signal_handlers_unblock_by_func(dialog->height_units_combo, G_CALLBACK(on_height_unit_changed), dialog);
        convert_width_to_unit(dialog, (DimensionUnit)active);
        convert_height_to_unit(dialog, (DimensionUnit)active);
    }
}

static void on_height_unit_changed(GtkComboBox* combo, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    gint active = gtk_combo_box_get_active(combo);
    if (active >= 0) {
        dialog->height_unit = active;
        dialog->width_unit = active;
        g_signal_handlers_block_by_func(dialog->width_units_combo, G_CALLBACK(on_width_unit_changed), dialog);
        gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->width_units_combo), active);
        g_signal_handlers_unblock_by_func(dialog->width_units_combo, G_CALLBACK(on_width_unit_changed), dialog);
        convert_width_to_unit(dialog, (DimensionUnit)active);
        convert_height_to_unit(dialog, (DimensionUnit)active);
    }
}

static void on_resolution_unit_changed(GtkComboBox* combo, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    gint active = gtk_combo_box_get_active(combo);
    if (active >= 0) {
        dialog->resolution_unit = active;
        convert_resolution_to_unit(dialog, (ResolutionUnit)active);
        if (dialog->width_unit != UNIT_PIXELS && dialog->width_unit != UNIT_PERCENT)
            convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
        if (dialog->height_unit != UNIT_PIXELS && dialog->height_unit != UNIT_PERCENT)
            convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
}

static void on_width_value_changed(GtkSpinButton* spin, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    gdouble value = gtk_spin_button_get_value(spin);
    guint new_width = (dialog->width_unit == UNIT_PERCENT)
                          ? (guint)(dialog->original_width * value / 100.0 + 0.5)
                          : unit_to_pixels(value, (DimensionUnit)dialog->width_unit, dialog->current_resolution);
    if (dialog->preserve_aspect && dialog->aspect_ratio > 0.0) {
        guint new_height = (guint)(new_width / dialog->aspect_ratio + 0.5);
        dialog->current_height = new_height;
        g_signal_handlers_block_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
        gdouble height_value = (dialog->height_unit == UNIT_PERCENT)
                                   ? (gdouble)new_height / (gdouble)dialog->original_height * 100.0
                                   : pixels_to_unit(new_height, (DimensionUnit)dialog->height_unit, dialog->current_resolution);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->height_spin), height_value);
        g_signal_handlers_unblock_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
    }
    dialog->current_width = new_width;
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

static void on_height_value_changed(GtkSpinButton* spin, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    gdouble value = gtk_spin_button_get_value(spin);
    guint new_height = (dialog->height_unit == UNIT_PERCENT)
                           ? (guint)(dialog->original_height * value / 100.0 + 0.5)
                           : unit_to_pixels(value, (DimensionUnit)dialog->height_unit, dialog->current_resolution);
    if (dialog->preserve_aspect && dialog->aspect_ratio > 0.0) {
        guint new_width = (guint)(new_height * dialog->aspect_ratio + 0.5);
        dialog->current_width = new_width;
        g_signal_handlers_block_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
        gdouble width_value = (dialog->width_unit == UNIT_PERCENT)
                                  ? (gdouble)new_width / (gdouble)dialog->original_width * 100.0
                                  : pixels_to_unit(new_width, (DimensionUnit)dialog->width_unit, dialog->current_resolution);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->width_spin), width_value);
        g_signal_handlers_unblock_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
    }
    dialog->current_height = new_height;
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

static void on_resolution_value_changed(GtkSpinButton* spin, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    gdouble value = gtk_spin_button_get_value(spin);
    if ((ResolutionUnit)dialog->resolution_unit == RES_UNIT_PPCM)
        dialog->current_resolution = value * INCHES_TO_CM;
    else
        dialog->current_resolution = value;
    if (dialog->width_unit != UNIT_PIXELS && dialog->width_unit != UNIT_PERCENT)
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    if (dialog->height_unit != UNIT_PIXELS && dialog->height_unit != UNIT_PERCENT)
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
}

static void on_preserve_ratio_toggled(GtkToggleButton* button, gpointer user_data) {
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    dialog->preserve_aspect = gtk_toggle_button_get_active(button);
    update_preserve_ratio_icon(dialog);
}

static void on_width_reset_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    dialog->current_width = dialog->original_width;
    convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    if (dialog->preserve_aspect) {
        dialog->current_height = dialog->original_height;
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

static void on_height_reset_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    dialog->current_height = dialog->original_height;
    convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    if (dialog->preserve_aspect) {
        dialog->current_width = dialog->original_width;
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

static void on_resolution_reset_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    ResizeDialog* dialog = (ResizeDialog*)user_data;
    dialog->current_resolution = dialog->original_resolution;
    convert_resolution_to_unit(dialog, (ResolutionUnit)dialog->resolution_unit);
    if (dialog->width_unit != UNIT_PIXELS && dialog->width_unit != UNIT_PERCENT)
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    if (dialog->height_unit != UNIT_PIXELS && dialog->height_unit != UNIT_PERCENT)
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
}

static gdouble pixels_to_unit(guint pixels, DimensionUnit unit, gdouble resolution) {
    switch (unit) {
        case UNIT_PIXELS:
            return (gdouble)pixels;
        case UNIT_PERCENT:
            return 100.0;
        case UNIT_INCHES:
            return (gdouble)pixels / resolution;
        case UNIT_CENTIMETERS:
            return (gdouble)pixels / resolution * INCHES_TO_CM;
        case UNIT_MILLIMETERS:
            return (gdouble)pixels / resolution * INCHES_TO_CM * 10.0;
        case UNIT_POINTS:
            return (gdouble)pixels / resolution / POINTS_TO_INCHES;
        case UNIT_PICAS:
            return (gdouble)pixels / resolution / PICAS_TO_INCHES;
        default:
            return (gdouble)pixels;
    }
}

static guint unit_to_pixels(gdouble value, DimensionUnit unit, gdouble resolution) {
    switch (unit) {
        case UNIT_PIXELS:
            return (guint)(value + 0.5);
        case UNIT_PERCENT:
            return (guint)(value + 0.5);
        case UNIT_INCHES:
            return (guint)(value * resolution + 0.5);
        case UNIT_CENTIMETERS:
            return (guint)(value * resolution / INCHES_TO_CM + 0.5);
        case UNIT_MILLIMETERS:
            return (guint)(value * resolution / INCHES_TO_CM / 10.0 + 0.5);
        case UNIT_POINTS:
            return (guint)(value * resolution * POINTS_TO_INCHES + 0.5);
        case UNIT_PICAS:
            return (guint)(value * resolution * PICAS_TO_INCHES + 0.5);
        default:
            return (guint)(value + 0.5);
    }
}

static gdouble resolution_convert(gdouble value, ResolutionUnit from, ResolutionUnit to) {
    if (from == to)
        return value;
    if (from == RES_UNIT_PPI && to == RES_UNIT_PPCM)
        return value / INCHES_TO_CM;
    if (from == RES_UNIT_PPCM && to == RES_UNIT_PPI)
        return value * INCHES_TO_CM;
    return value;
}

static void convert_width_to_unit(ResizeDialog* dialog, DimensionUnit unit) {
    gdouble value = (unit == UNIT_PERCENT)
                        ? (gdouble)dialog->current_width / (gdouble)dialog->original_width * 100.0
                        : pixels_to_unit(dialog->current_width, unit, dialog->current_resolution);
    g_signal_handlers_block_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->width_spin), value);
    g_signal_handlers_unblock_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
}

static void convert_height_to_unit(ResizeDialog* dialog, DimensionUnit unit) {
    gdouble value = (unit == UNIT_PERCENT)
                        ? (gdouble)dialog->current_height / (gdouble)dialog->original_height * 100.0
                        : pixels_to_unit(dialog->current_height, unit, dialog->current_resolution);
    g_signal_handlers_block_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->height_spin), value);
    g_signal_handlers_unblock_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
}

static void convert_resolution_to_unit(ResizeDialog* dialog, ResolutionUnit unit) {
    gdouble value = resolution_convert(dialog->current_resolution, RES_UNIT_PPI, unit);
    g_signal_handlers_block_by_func(dialog->resolution_spin, G_CALLBACK(on_resolution_value_changed), dialog);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->resolution_spin), value);
    g_signal_handlers_unblock_by_func(dialog->resolution_spin, G_CALLBACK(on_resolution_value_changed), dialog);
}

static gchar* format_file_size(guint64 bytes) {
    const gchar* units[] = {"B", "KB", "MB", "GB", "TB"};
    gint unit_index = 0;
    gdouble size = (gdouble)bytes;
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    return (unit_index == 0) ? g_strdup_printf("%.0f %s", size, units[unit_index])
                             : g_strdup_printf("%.2f %s", size, units[unit_index]);
}

static void update_dimensions_label(ResizeDialog* dialog) {
    guint64 current_size = (guint64)dialog->current_width * dialog->current_height * 4;
    guint64 original_size = (guint64)dialog->original_width * dialog->original_height * 4;
    gchar* current_size_str = format_file_size(current_size);
    gchar* original_size_str = format_file_size(original_size);
    gchar* text;
    if (dialog->current_width == dialog->original_width && dialog->current_height == dialog->original_height)
        text = g_strdup_printf("%u px X %u px (%s)", dialog->current_width, dialog->current_height, current_size_str);
    else
        text = g_strdup_printf("%u px X %u px (%s, was %s)", dialog->current_width, dialog->current_height, current_size_str, original_size_str);
    gtk_label_set_text(GTK_LABEL(dialog->dimensions_text), text);
    g_free(text);
    g_free(current_size_str);
    g_free(original_size_str);
}

static void update_aspect_ratio_label(ResizeDialog* dialog) {
    guint width = dialog->current_width;
    guint height = dialog->current_height;
    gchar* text;
    if (width == 0 || height == 0) {
        text = g_strdup("1:1 (1.00)");
    } else {
        guint a = width, b = height;
        while (b != 0) {
            guint r = a % b;
            a = b;
            b = r;
        }
        guint gcd = a;
        if (gcd > 0 && gcd <= width && gcd <= height) {
            guint rw = width / gcd, rh = height / gcd;
            gdouble ratio = (gdouble)width / (gdouble)height;
            text = g_strdup_printf("%u:%u (%.2f)", rw, rh, ratio);
        } else {
            text = g_strdup_printf("%u:%u (%.2f)", width, height, (gdouble)width / (gdouble)height);
        }
    }
    gtk_label_set_text(GTK_LABEL(dialog->aspect_ratio_text), text);
    g_free(text);
}

static void update_preserve_ratio_icon(ResizeDialog* dialog) {
    GError* error = NULL;
    const gchar* icon_resource = dialog->preserve_aspect ? "/icons/padlock-locked.svg" : "/icons/padlock-unlocked.svg";
    GBytes* icon_bytes = g_resources_lookup_data(icon_resource, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!icon_bytes) {
        if (error)
            g_error_free(error);
        return;
    }
    GInputStream* stream = g_memory_input_stream_new_from_data(g_bytes_get_data(icon_bytes, NULL), g_bytes_get_size(icon_bytes), NULL);
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    g_bytes_unref(icon_bytes);
    if (!pixbuf) {
        if (error)
            g_error_free(error);
        return;
    }
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(dialog->preserve_ratio_toggle));
    if (child && GTK_IS_IMAGE(child)) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(child), pixbuf);
    } else {
        if (child && GTK_IS_WIDGET(child))
            gtk_widget_destroy(child);
        GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);
        gtk_widget_show(image);
        gtk_container_add(GTK_CONTAINER(dialog->preserve_ratio_toggle), image);
    }
    g_object_unref(pixbuf);
}

static void set_reset_button_icon(GtkButton* button) {
    if (!button)
        return;
    GError* error = NULL;
    GBytes* icon_bytes = g_resources_lookup_data("/icons/reset.svg", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!icon_bytes) {
        if (error)
            g_error_free(error);
        return;
    }
    GInputStream* stream = g_memory_input_stream_new_from_data(g_bytes_get_data(icon_bytes, NULL), g_bytes_get_size(icon_bytes), NULL);
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    g_bytes_unref(icon_bytes);
    if (!pixbuf) {
        if (error)
            g_error_free(error);
        return;
    }
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 20, 20, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    if (scaled) {
        gtk_button_set_label(button, NULL);
        gtk_button_set_image(button, gtk_image_new_from_pixbuf(scaled));
        gtk_button_set_image_position(button, GTK_POS_LEFT);
        g_object_unref(scaled);
    }
}

static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

ResizeDialog* resize_dialog_new(ImageDocument* doc) {
    ResizeDialog* dialog;
    GtkBuilder* builder;
    GError* error = NULL;
    GtkListStore *width_store, *height_store, *resolution_store;
    GtkTreeIter iter;
    GtkCellRenderer* cell;

    if (!doc)
        return NULL;

    dialog = (ResizeDialog*)g_malloc0(sizeof(ResizeDialog));
    if (!dialog)
        return NULL;

    dialog->original_width = doc->width;
    dialog->original_height = doc->height;
    dialog->original_resolution = 72.0;
    dialog->current_resolution = 72.0;
    dialog->current_width = doc->width;
    dialog->current_height = doc->height;
    dialog->aspect_ratio = (doc->height > 0) ? (gdouble)doc->width / (gdouble)doc->height : 1.0;
    dialog->preserve_aspect = FALSE;
    dialog->width_unit = UNIT_PIXELS;
    dialog->height_unit = UNIT_PIXELS;
    dialog->resolution_unit = RES_UNIT_PPI;

    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/resize_dialog.glade", &error)) {
        g_warning("Failed to load resize_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error)
            g_error_free(error);
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    dialog->dialog = GTK_WIDGET(gtk_builder_get_object(builder, "resize_dialog"));
    if (!dialog->dialog) {
        g_warning("Failed to get resize_dialog from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    dialog->width_spin = GTK_WIDGET(gtk_builder_get_object(builder, "resize_width_spin"));
    dialog->height_spin = GTK_WIDGET(gtk_builder_get_object(builder, "resize_height_spin"));
    dialog->resolution_spin = GTK_WIDGET(gtk_builder_get_object(builder, "resize_resolution_spin"));
    dialog->width_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "resize_width_units_combo"));
    dialog->height_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "resize_height_units_combo"));
    dialog->resolution_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "resize_resolution_units_combo"));
    dialog->width_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "resize_width_reset_button"));
    dialog->height_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "resize_height_reset_button"));
    dialog->resolution_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "resize_resolution_reset_button"));
    dialog->preserve_ratio_toggle = GTK_WIDGET(gtk_builder_get_object(builder, "resize_preserve_ratio_toggle"));
    dialog->dimensions_text = GTK_WIDGET(gtk_builder_get_object(builder, "resize_dimensions_text"));
    dialog->aspect_ratio_text = GTK_WIDGET(gtk_builder_get_object(builder, "resize_aspect_ratio_text"));
    dialog->resampling_combo = GTK_WIDGET(gtk_builder_get_object(builder, "resampling_combo"));

    if (!dialog->width_spin || !dialog->height_spin || !dialog->resolution_spin ||
        !dialog->width_units_combo || !dialog->height_units_combo || !dialog->resolution_units_combo ||
        !dialog->width_reset_button || !dialog->height_reset_button || !dialog->resolution_reset_button ||
        !dialog->preserve_ratio_toggle || !dialog->dimensions_text || !dialog->aspect_ratio_text ||
        !dialog->resampling_combo) {
        g_warning("Failed to get all widgets from resize_dialog builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Unit combo boxes */
    width_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "percent", -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "pixels", -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "inches", -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "centimeters", -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "millimeters", -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "points", -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, "picas", -1);

    height_store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeIter wi;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(width_store), &wi);
    while (valid) {
        gchar* text = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(width_store), &wi, 0, &text, -1);
        if (text) {
            gtk_list_store_append(height_store, &iter);
            gtk_list_store_set(height_store, &iter, 0, text, -1);
            g_free(text);
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(width_store), &wi);
    }

    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->width_units_combo), GTK_TREE_MODEL(width_store));
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->height_units_combo), GTK_TREE_MODEL(height_store));
    g_object_unref(width_store);
    g_object_unref(height_store);

    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->width_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->width_units_combo), cell, "text", 0, NULL);
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->height_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->height_units_combo), cell, "text", 0, NULL);

    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->width_units_combo), UNIT_PIXELS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->height_units_combo), UNIT_PIXELS);

    resolution_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(resolution_store, &iter);
    gtk_list_store_set(resolution_store, &iter, 0, "pixels / inch (PPI)", -1);
    gtk_list_store_append(resolution_store, &iter);
    gtk_list_store_set(resolution_store, &iter, 0, "pixels / centimeter (PPCM)", -1);
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->resolution_units_combo), GTK_TREE_MODEL(resolution_store));
    g_object_unref(resolution_store);
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->resolution_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->resolution_units_combo), cell, "text", 0, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->resolution_units_combo), RES_UNIT_PPI);

    /* Resampling (interpolation) combo: OcInterpolationMode order */
    GtkListStore* resample_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(resample_store, &iter);
    gtk_list_store_set(resample_store, &iter, 0, "Nearest neighbor", -1);
    gtk_list_store_append(resample_store, &iter);
    gtk_list_store_set(resample_store, &iter, 0, "Bilinear", -1);
    gtk_list_store_append(resample_store, &iter);
    gtk_list_store_set(resample_store, &iter, 0, "Bicubic", -1);
    gtk_list_store_append(resample_store, &iter);
    gtk_list_store_set(resample_store, &iter, 0, "Lanczos", -1);
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->resampling_combo), GTK_TREE_MODEL(resample_store));
    g_object_unref(resample_store);
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->resampling_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->resampling_combo), cell, "text", 0, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->resampling_combo), 1); /* Bilinear default */

    GtkAdjustment* width_adj = gtk_adjustment_new(dialog->current_width, 1.0, 100000.0, 1.0, 10.0, 0.0);
    GtkAdjustment* height_adj = gtk_adjustment_new(dialog->current_height, 1.0, 100000.0, 1.0, 10.0, 0.0);
    GtkAdjustment* resolution_adj = gtk_adjustment_new(dialog->current_resolution, 1.0, 10000.0, 0.1, 1.0, 0.0);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(dialog->width_spin), width_adj);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(dialog->height_spin), height_adj);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(dialog->resolution_spin), resolution_adj);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->width_spin), dialog->current_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->height_spin), dialog->current_height);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->resolution_spin), dialog->current_resolution);

    set_reset_button_icon(GTK_BUTTON(dialog->width_reset_button));
    set_reset_button_icon(GTK_BUTTON(dialog->height_reset_button));
    set_reset_button_icon(GTK_BUTTON(dialog->resolution_reset_button));

    g_signal_connect(dialog->width_units_combo, "changed", G_CALLBACK(on_width_unit_changed), dialog);
    g_signal_connect(dialog->height_units_combo, "changed", G_CALLBACK(on_height_unit_changed), dialog);
    g_signal_connect(dialog->resolution_units_combo, "changed", G_CALLBACK(on_resolution_unit_changed), dialog);
    g_signal_connect(dialog->width_spin, "value-changed", G_CALLBACK(on_width_value_changed), dialog);
    g_signal_connect(dialog->height_spin, "value-changed", G_CALLBACK(on_height_value_changed), dialog);
    g_signal_connect(dialog->resolution_spin, "value-changed", G_CALLBACK(on_resolution_value_changed), dialog);
    g_signal_connect(dialog->preserve_ratio_toggle, "toggled", G_CALLBACK(on_preserve_ratio_toggled), dialog);
    g_signal_connect(dialog->width_reset_button, "clicked", G_CALLBACK(on_width_reset_clicked), dialog);
    g_signal_connect(dialog->height_reset_button, "clicked", G_CALLBACK(on_height_reset_clicked), dialog);
    g_signal_connect(dialog->resolution_reset_button, "clicked", G_CALLBACK(on_resolution_reset_clicked), dialog);

    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "resize_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "resize_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }

    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), "Resize image");
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
    update_preserve_ratio_icon(dialog);

    g_object_unref(builder);
    gtk_widget_hide(dialog->dialog);
    return dialog;
}

void resize_dialog_free(ResizeDialog* dialog) {
    if (!dialog)
        return;
    if (dialog->dialog)
        gtk_widget_destroy(dialog->dialog);
    g_free(dialog);
}

GtkWindow* resize_dialog_get_window(ResizeDialog* dialog) {
    if (!dialog || !dialog->dialog)
        return NULL;
    return GTK_WINDOW(dialog->dialog);
}

gint resize_dialog_run(ResizeDialog* dialog, GtkWindow* parent, ResizeDialogResult** result) {
    gint response;
    if (!dialog || !result)
        return GTK_RESPONSE_CANCEL;
    if (!dialog->dialog || !GTK_IS_WIDGET(dialog->dialog))
        return GTK_RESPONSE_CANCEL;
    *result = NULL;

    if (parent && GTK_IS_WINDOW(parent) && GTK_IS_WINDOW(dialog->dialog)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
        gtk_window_set_position(GTK_WINDOW(dialog->dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    } else if (GTK_IS_WINDOW(dialog->dialog)) {
        gtk_window_set_position(GTK_WINDOW(dialog->dialog), GTK_WIN_POS_CENTER);
    }

    gtk_widget_show_all(dialog->dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        *result = (ResizeDialogResult*)g_malloc(sizeof(ResizeDialogResult));
        if (!*result)
            return GTK_RESPONSE_CANCEL;
        (*result)->width = dialog->current_width;
        (*result)->height = dialog->current_height;
        (*result)->resolution = dialog->current_resolution;
        (*result)->interpolation_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->resampling_combo));
        if ((*result)->interpolation_mode < 0)
            (*result)->interpolation_mode = 1; /* Bilinear */
        if ((*result)->interpolation_mode > 3)
            (*result)->interpolation_mode = 3;
    }
    return response;
}

void resize_dialog_result_free(ResizeDialogResult* result) {
    if (result)
        g_free(result);
}
