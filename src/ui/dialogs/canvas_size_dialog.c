#include "ui/dialogs/canvas_size_dialog.h"
#include "document.h"
#include "ui/widgets/anchor_position_widget.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

/**
 * Canvas size dialog structure
 */
struct _CanvasSizeDialog {
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
    GtkWidget* anchor_container;
    AnchorPositionWidget* anchor_widget;

    /* Original values (in pixels/PPI) */
    guint original_width;
    guint original_height;
    gdouble original_resolution;

    /* Current values (in pixels/PPI) */
    guint current_width;
    guint current_height;
    gdouble current_resolution;

    /* Aspect ratio */
    gdouble aspect_ratio;
    gboolean preserve_aspect;

    /* Unit types */
    gint width_unit;
    gint height_unit;
    gint resolution_unit;
};

/* Unit type enums */
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
    RES_UNIT_PPI = 0, /* Pixels per inch */
    RES_UNIT_PPCM = 1 /* Pixels per centimeter */
} ResolutionUnit;

/* Conversion constants */
#define INCHES_TO_CM 2.54
#define CM_TO_INCHES (1.0 / 2.54)
#define POINTS_TO_INCHES (1.0 / 72.0)
#define PICAS_TO_INCHES (1.0 / 6.0)

/* Forward declarations */
static void update_dimensions_label(CanvasSizeDialog* dialog);
static void update_aspect_ratio_label(CanvasSizeDialog* dialog);
static void update_preserve_ratio_icon(CanvasSizeDialog* dialog);
static void convert_width_to_unit(CanvasSizeDialog* dialog, DimensionUnit unit);
static void convert_height_to_unit(CanvasSizeDialog* dialog, DimensionUnit unit);
static void convert_resolution_to_unit(CanvasSizeDialog* dialog, ResolutionUnit unit);
static gdouble pixels_to_unit(guint pixels, DimensionUnit unit, gdouble resolution);
static guint unit_to_pixels(gdouble value, DimensionUnit unit, gdouble resolution);
static gdouble resolution_convert(gdouble value, ResolutionUnit from, ResolutionUnit to);
static gchar* format_file_size(guint64 bytes);
static void on_width_value_changed(GtkSpinButton* spin, gpointer user_data);
static void on_height_value_changed(GtkSpinButton* spin, gpointer user_data);
static void on_width_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_height_unit_changed(GtkComboBox* combo, gpointer user_data);

/**
 * Width unit changed callback
 * Updates both width and height to use the same unit type
 */
static void on_width_unit_changed(GtkComboBox* combo, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    gint active = gtk_combo_box_get_active(combo);

    if (active >= 0) {
        dialog->width_unit = active;
        dialog->height_unit = active; /* Keep them in sync */

        /* Update height combo box to match */
        g_signal_handlers_block_by_func(dialog->height_units_combo, G_CALLBACK(on_height_unit_changed), dialog);
        gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->height_units_combo), active);
        g_signal_handlers_unblock_by_func(dialog->height_units_combo, G_CALLBACK(on_height_unit_changed), dialog);

        /* Convert both to the new unit */
        convert_width_to_unit(dialog, (DimensionUnit)active);
        convert_height_to_unit(dialog, (DimensionUnit)active);
    }
}

/**
 * Height unit changed callback
 * Updates both width and height to use the same unit type
 */
static void on_height_unit_changed(GtkComboBox* combo, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    gint active = gtk_combo_box_get_active(combo);

    if (active >= 0) {
        dialog->height_unit = active;
        dialog->width_unit = active; /* Keep them in sync */

        /* Update width combo box to match */
        g_signal_handlers_block_by_func(dialog->width_units_combo, G_CALLBACK(on_width_unit_changed), dialog);
        gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->width_units_combo), active);
        g_signal_handlers_unblock_by_func(dialog->width_units_combo, G_CALLBACK(on_width_unit_changed), dialog);

        /* Convert both to the new unit */
        convert_width_to_unit(dialog, (DimensionUnit)active);
        convert_height_to_unit(dialog, (DimensionUnit)active);
    }
}

/**
 * Resolution unit changed callback
 */
static void on_resolution_unit_changed(GtkComboBox* combo, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    gint active = gtk_combo_box_get_active(combo);

    if (active >= 0) {
        ResolutionUnit old_unit = (ResolutionUnit)dialog->resolution_unit;
        dialog->resolution_unit = active;
        convert_resolution_to_unit(dialog, (ResolutionUnit)active);

        /* When resolution unit changes, we need to update width/height display */
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
}

/**
 * Width value changed callback
 */
static void on_width_value_changed(GtkSpinButton* spin, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    gdouble value = gtk_spin_button_get_value(spin);
    guint new_width;

    /* Convert from current unit to pixels */
    if (dialog->width_unit == UNIT_PERCENT) {
        new_width = (guint)(dialog->original_width * value / 100.0 + 0.5);
    } else {
        new_width = unit_to_pixels(value, (DimensionUnit)dialog->width_unit, dialog->current_resolution);
    }

    if (dialog->preserve_aspect && dialog->aspect_ratio > 0.0) {
        /* Calculate new height to maintain aspect ratio */
        guint new_height = (guint)(new_width / dialog->aspect_ratio + 0.5);
        dialog->current_height = new_height;

        /* Update height spin button (convert to current unit) */
        g_signal_handlers_block_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
        gdouble height_value;
        if (dialog->height_unit == UNIT_PERCENT) {
            height_value = (gdouble)new_height / (gdouble)dialog->original_height * 100.0;
        } else {
            height_value = pixels_to_unit(new_height, (DimensionUnit)dialog->height_unit, dialog->current_resolution);
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->height_spin), height_value);
        g_signal_handlers_unblock_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
    }

    dialog->current_width = new_width;
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Height value changed callback
 */
static void on_height_value_changed(GtkSpinButton* spin, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    gdouble value = gtk_spin_button_get_value(spin);
    guint new_height;

    /* Convert from current unit to pixels */
    if (dialog->height_unit == UNIT_PERCENT) {
        new_height = (guint)(dialog->original_height * value / 100.0 + 0.5);
    } else {
        new_height = unit_to_pixels(value, (DimensionUnit)dialog->height_unit, dialog->current_resolution);
    }

    if (dialog->preserve_aspect && dialog->aspect_ratio > 0.0) {
        /* Calculate new width to maintain aspect ratio */
        guint new_width = (guint)(new_height * dialog->aspect_ratio + 0.5);
        dialog->current_width = new_width;

        /* Update width spin button (convert to current unit) */
        g_signal_handlers_block_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
        gdouble width_value;
        if (dialog->width_unit == UNIT_PERCENT) {
            width_value = (gdouble)new_width / (gdouble)dialog->original_width * 100.0;
        } else {
            width_value = pixels_to_unit(new_width, (DimensionUnit)dialog->width_unit, dialog->current_resolution);
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->width_spin), width_value);
        g_signal_handlers_unblock_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
    }

    dialog->current_height = new_height;
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Resolution value changed callback
 */
static void on_resolution_value_changed(GtkSpinButton* spin, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    gdouble value = gtk_spin_button_get_value(spin);

    /* Convert from current unit to PPI */
    ResolutionUnit unit = (ResolutionUnit)dialog->resolution_unit;
    if (unit == RES_UNIT_PPCM) {
        /* PPCM to PPI: 1 cm = 0.393701 inches, so PPCM * 2.54 = PPI */
        dialog->current_resolution = value * INCHES_TO_CM;
    } else {
        dialog->current_resolution = value;
    }

    /* When resolution changes, update width/height display if not in pixels */
    if (dialog->width_unit != UNIT_PIXELS && dialog->width_unit != UNIT_PERCENT) {
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }
    if (dialog->height_unit != UNIT_PIXELS && dialog->height_unit != UNIT_PERCENT) {
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
}

/**
 * Preserve ratio toggle callback
 */
static void on_preserve_ratio_toggled(GtkToggleButton* button, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;
    dialog->preserve_aspect = gtk_toggle_button_get_active(button);
    update_preserve_ratio_icon(dialog);
}

/**
 * Width reset button callback
 */
static void on_width_reset_clicked(GtkButton* button, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;

    dialog->current_width = dialog->original_width;
    convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);

    if (dialog->preserve_aspect) {
        dialog->current_height = dialog->original_height;
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }

    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Height reset button callback
 */
static void on_height_reset_clicked(GtkButton* button, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;

    dialog->current_height = dialog->original_height;
    convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);

    if (dialog->preserve_aspect) {
        dialog->current_width = dialog->original_width;
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }

    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Resolution reset button callback
 */
static void on_resolution_reset_clicked(GtkButton* button, gpointer user_data) {
    CanvasSizeDialog* dialog = (CanvasSizeDialog*)user_data;

    dialog->current_resolution = dialog->original_resolution;
    convert_resolution_to_unit(dialog, (ResolutionUnit)dialog->resolution_unit);

    /* Update width/height display if not in pixels/percent */
    if (dialog->width_unit != UNIT_PIXELS && dialog->width_unit != UNIT_PERCENT) {
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }
    if (dialog->height_unit != UNIT_PIXELS && dialog->height_unit != UNIT_PERCENT) {
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
}

/**
 * Convert pixels to specified unit
 */
static gdouble pixels_to_unit(guint pixels, DimensionUnit unit, gdouble resolution) {
    switch (unit) {
        case UNIT_PIXELS:
            return (gdouble)pixels;
        case UNIT_PERCENT:
            /* This should not be called directly - handled separately with original value */
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

/**
 * Convert unit value to pixels
 */
static guint unit_to_pixels(gdouble value, DimensionUnit unit, gdouble resolution) {
    switch (unit) {
        case UNIT_PIXELS:
            return (guint)(value + 0.5);
        case UNIT_PERCENT:
            /* This should not be called directly - handled separately with original value */
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

/**
 * Convert resolution between units
 */
static gdouble resolution_convert(gdouble value, ResolutionUnit from, ResolutionUnit to) {
    if (from == to) {
        return value;
    }

    if (from == RES_UNIT_PPI && to == RES_UNIT_PPCM) {
        /* PPI to PPCM: divide by 2.54 */
        return value / INCHES_TO_CM;
    } else if (from == RES_UNIT_PPCM && to == RES_UNIT_PPI) {
        /* PPCM to PPI: multiply by 2.54 */
        return value * INCHES_TO_CM;
    }

    return value;
}

/**
 * Convert width to specified unit and update spin button
 */
static void convert_width_to_unit(CanvasSizeDialog* dialog, DimensionUnit unit) {
    gdouble value;

    if (unit == UNIT_PERCENT) {
        value = (gdouble)dialog->current_width / (gdouble)dialog->original_width * 100.0;
    } else {
        value = pixels_to_unit(dialog->current_width, unit, dialog->current_resolution);
    }

    g_signal_handlers_block_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->width_spin), value);
    g_signal_handlers_unblock_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
}

/**
 * Convert height to specified unit and update spin button
 */
static void convert_height_to_unit(CanvasSizeDialog* dialog, DimensionUnit unit) {
    gdouble value;

    if (unit == UNIT_PERCENT) {
        value = (gdouble)dialog->current_height / (gdouble)dialog->original_height * 100.0;
    } else {
        value = pixels_to_unit(dialog->current_height, unit, dialog->current_resolution);
    }

    g_signal_handlers_block_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->height_spin), value);
    g_signal_handlers_unblock_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
}

/**
 * Convert resolution to specified unit and update spin button
 */
static void convert_resolution_to_unit(CanvasSizeDialog* dialog, ResolutionUnit unit) {
    gdouble value = resolution_convert(dialog->current_resolution, RES_UNIT_PPI, unit);

    g_signal_handlers_block_by_func(dialog->resolution_spin, G_CALLBACK(on_resolution_value_changed), dialog);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->resolution_spin), value);
    g_signal_handlers_unblock_by_func(dialog->resolution_spin, G_CALLBACK(on_resolution_value_changed), dialog);
}

/**
 * Update dimensions label
 */
static void update_dimensions_label(CanvasSizeDialog* dialog) {
    gchar* text;
    guint64 current_size, original_size;
    gchar *current_size_str, *original_size_str;

    /* Calculate memory sizes (assuming 4 bytes per pixel for RGBA) */
    current_size = (guint64)dialog->current_width * dialog->current_height * 4;
    original_size = (guint64)dialog->original_width * dialog->original_height * 4;

    current_size_str = format_file_size(current_size);
    original_size_str = format_file_size(original_size);

    if (dialog->current_width == dialog->original_width &&
        dialog->current_height == dialog->original_height) {
        /* No change */
        text = g_strdup_printf("%u px X %u px (%s)",
                               dialog->current_width,
                               dialog->current_height,
                               current_size_str);
    } else {
        /* Changed */
        text = g_strdup_printf("%u px X %u px (%s, was %s)",
                               dialog->current_width,
                               dialog->current_height,
                               current_size_str,
                               original_size_str);
    }

    gtk_label_set_text(GTK_LABEL(dialog->dimensions_text), text);
    g_free(text);
    g_free(current_size_str);
    g_free(original_size_str);
}

/**
 * Update aspect ratio label
 */
static void update_aspect_ratio_label(CanvasSizeDialog* dialog) {
    gchar* text;
    guint gcd_w = dialog->current_width;
    guint gcd_h = dialog->current_height;
    guint gcd;
    gdouble ratio;

    /* Calculate GCD for simplified ratio */
    while (gcd_h != 0) {
        guint temp = gcd_h;
        gcd_h = gcd_w % gcd_h;
        gcd_w = temp;
    }
    gcd = gcd_w;

    if (gcd > 0) {
        guint ratio_w = dialog->current_width / gcd;
        guint ratio_h = dialog->current_height / gcd;
        ratio = (gdouble)dialog->current_width / (gdouble)dialog->current_height;
        text = g_strdup_printf("%u:%u (%.2f)", ratio_w, ratio_h, ratio);
    } else {
        text = g_strdup("1:1 (1.00)");
    }

    gtk_label_set_text(GTK_LABEL(dialog->aspect_ratio_text), text);
    g_free(text);
}

/**
 * Update preserve ratio toggle icon
 */
static void update_preserve_ratio_icon(CanvasSizeDialog* dialog) {
    GtkImage* image;
    GError* error = NULL;
    GdkPixbuf* pixbuf;
    GInputStream* stream;
    GBytes* icon_bytes;
    const gchar* icon_resource;

    if (dialog->preserve_aspect) {
        icon_resource = "/icons/padlock-locked.svg";
    } else {
        icon_resource = "/icons/padlock-unlocked.svg";
    }

    icon_bytes = g_resources_lookup_data(icon_resource, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!icon_bytes) {
        g_warning("Failed to load %s: %s", icon_resource, error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    stream = g_memory_input_stream_new_from_data(g_bytes_get_data(icon_bytes, NULL),
                                                 g_bytes_get_size(icon_bytes),
                                                 NULL);
    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    g_bytes_unref(icon_bytes);

    if (!pixbuf) {
        g_warning("Failed to create pixbuf from %s: %s", icon_resource, error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    /* Get or create image widget */
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(dialog->preserve_ratio_toggle));
    if (child && GTK_IS_IMAGE(child)) {
        image = GTK_IMAGE(child);
        gtk_image_set_from_pixbuf(image, pixbuf);
    } else {
        /* Remove existing child and add image - use destroy instead of unparent to avoid parent access */
        if (child && GTK_IS_WIDGET(child)) {
            /* Destroy the widget - this is safer than unparent which might access parent */
            gtk_widget_destroy(child);
        }
        image = GTK_IMAGE(gtk_image_new_from_pixbuf(pixbuf));
        gtk_widget_show(GTK_WIDGET(image));
        gtk_container_add(GTK_CONTAINER(dialog->preserve_ratio_toggle), GTK_WIDGET(image));
    }

    g_object_unref(pixbuf);
}

/**
 * Helper function to set reset button icon from SVG resource
 */
static void set_reset_button_icon(GtkButton* button) {
    if (!button) {
        return;
    }

    GError* error = NULL;
    GBytes* icon_bytes = g_resources_lookup_data("/icons/reset.svg", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!icon_bytes) {
        g_warning("Failed to load reset.svg: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    GInputStream* stream = g_memory_input_stream_new_from_data(g_bytes_get_data(icon_bytes, NULL),
                                                               g_bytes_get_size(icon_bytes),
                                                               NULL);
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    g_bytes_unref(icon_bytes);

    if (!pixbuf) {
        g_warning("Failed to create pixbuf from reset.svg: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    /* Scale to 16x16 */
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 16, 16, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);

    if (scaled) {
        /* gtk_button_set_image automatically replaces any existing image/label,
         * so we don't need to manually remove the child - this avoids parent access issues */
        GtkWidget* image = gtk_image_new_from_pixbuf(scaled);
        gtk_button_set_image(button, image);
        g_object_unref(scaled);
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
 * Create a new canvas size dialog
 */
CanvasSizeDialog* canvas_size_dialog_new(ImageDocument* doc) {
    CanvasSizeDialog* dialog;
    GtkBuilder* builder;
    GError* error = NULL;
    GtkListStore *width_store, *height_store, *resolution_store;
    GtkTreeIter iter;
    GtkCellRenderer* cell;

    if (!doc) {
        return NULL;
    }

    dialog = (CanvasSizeDialog*)g_malloc(sizeof(CanvasSizeDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize all pointers to NULL */
    memset(dialog, 0, sizeof(CanvasSizeDialog));

    /* Store original values */
    dialog->original_width = doc->width;
    dialog->original_height = doc->height;
    dialog->original_resolution = 72.0; /* Default 72 PPI */
    dialog->current_resolution = dialog->original_resolution;

    /* Initialize current values */
    dialog->current_width = doc->width;
    dialog->current_height = doc->height;

    /* Calculate aspect ratio */
    if (doc->height > 0) {
        dialog->aspect_ratio = (gdouble)doc->width / (gdouble)doc->height;
    } else {
        dialog->aspect_ratio = 1.0;
    }

    dialog->preserve_aspect = FALSE;
    dialog->width_unit = UNIT_PIXELS;
    dialog->height_unit = UNIT_PIXELS;
    dialog->resolution_unit = RES_UNIT_PPI;

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/canvas_size_dialog.glade", &error)) {
        g_warning("Failed to load canvas_size_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Get dialog widget */
    dialog->dialog = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_dialog"));
    if (!dialog->dialog) {
        g_warning("Failed to get canvas_size_dialog from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Get all widgets */
    dialog->width_spin = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_width_spin"));
    dialog->height_spin = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_height_spin"));
    dialog->resolution_spin = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_resolution_spin"));
    dialog->width_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_width_units_combo"));
    dialog->height_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_height_units_combo"));
    dialog->resolution_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_resolution_units_combo"));
    dialog->width_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_width_reset_button"));
    dialog->height_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_height_reset_button"));
    dialog->resolution_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_resolution_reset_button"));
    dialog->preserve_ratio_toggle = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_preserve_ratio_toggle"));
    dialog->dimensions_text = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_dimensions_text"));
    dialog->aspect_ratio_text = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_aspect_ratio_text"));
    dialog->anchor_container = GTK_WIDGET(gtk_builder_get_object(builder, "anchor_position_container"));

    if (!dialog->width_spin || !dialog->height_spin || !dialog->resolution_spin ||
        !dialog->width_units_combo || !dialog->height_units_combo || !dialog->resolution_units_combo ||
        !dialog->width_reset_button || !dialog->height_reset_button || !dialog->resolution_reset_button ||
        !dialog->preserve_ratio_toggle || !dialog->dimensions_text || !dialog->aspect_ratio_text ||
        !dialog->anchor_container) {
        g_warning("Failed to get all widgets from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Create anchor position widget */
    dialog->anchor_widget = anchor_position_widget_new();
    if (dialog->anchor_widget) {
        GtkWidget* anchor_widget = anchor_position_widget_get_widget(dialog->anchor_widget);
        if (anchor_widget && GTK_IS_WIDGET(anchor_widget)) {
            /* Remove placeholder if it exists - skip if container might have issues */
            if (GTK_IS_CONTAINER(dialog->anchor_container)) {
                /* Check if container has children - only remove if safe */
                GList* children = gtk_container_get_children(GTK_CONTAINER(dialog->anchor_container));
                if (children) {
                    gint child_count = g_list_length(children);
                    if (child_count > 0) {
                        /* Try to remove children - iterate backwards */
                        for (GList* iter = g_list_last(children); iter; iter = iter->prev) {
                            GtkWidget* child = GTK_WIDGET(iter->data);
                            if (child && GTK_IS_WIDGET(child)) {
                                /* Just hide the child instead of removing to avoid parent access */
                                gtk_widget_hide(child);
                            }
                        }
                    }
                    g_list_free(children);
                }
            }
            /* Ensure container is visible and valid */
            if (GTK_IS_WIDGET(dialog->anchor_container) && GTK_IS_BOX(dialog->anchor_container)) {
                gtk_widget_set_visible(dialog->anchor_container, TRUE);
                /* Add and show the anchor widget using gtk_box_pack_start since it's a GtkBox */
                gtk_box_pack_start(GTK_BOX(dialog->anchor_container), anchor_widget, FALSE, FALSE, 0);
                /* Ensure the anchor widget is visible and show all its children */
                gtk_widget_set_visible(anchor_widget, TRUE);
                gtk_widget_show_all(anchor_widget);
                /* Ensure container shows all children */
                gtk_widget_show_all(dialog->anchor_container);
            }
        } else {
            g_warning("Failed to get anchor widget from anchor_position_widget");
        }
    } else {
        g_warning("Failed to create anchor position widget");
    }

    /* Populate width/height unit combo boxes */
    width_store = gtk_list_store_new(1, G_TYPE_STRING);
    height_store = gtk_list_store_new(1, G_TYPE_STRING);

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

    /* Copy to height store - iterate through width_store properly */
    GtkTreeIter width_iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(width_store), &width_iter);
    while (valid) {
        gchar* text = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(width_store), &width_iter, 0, &text, -1);
        if (text) {
            gtk_list_store_append(height_store, &iter);
            gtk_list_store_set(height_store, &iter, 0, text, -1);
            g_free(text);
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(width_store), &width_iter);
    }

    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->width_units_combo), GTK_TREE_MODEL(width_store));
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->height_units_combo), GTK_TREE_MODEL(height_store));
    g_object_unref(width_store);
    g_object_unref(height_store);

    /* Set up cell renderers */
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->width_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->width_units_combo), cell, "text", 0, NULL);

    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->height_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->height_units_combo), cell, "text", 0, NULL);

    /* Set default: pixels */
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->width_units_combo), UNIT_PIXELS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->height_units_combo), UNIT_PIXELS);

    /* Populate resolution unit combo box */
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

    /* Set default: PPI */
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->resolution_units_combo), RES_UNIT_PPI);

    /* Set up spin button adjustments */
    GtkAdjustment* width_adj = gtk_adjustment_new(dialog->current_width, 1.0, 100000.0, 1.0, 10.0, 0.0);
    GtkAdjustment* height_adj = gtk_adjustment_new(dialog->current_height, 1.0, 100000.0, 1.0, 10.0, 0.0);
    GtkAdjustment* resolution_adj = gtk_adjustment_new(dialog->current_resolution, 1.0, 10000.0, 0.1, 1.0, 0.0);

    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(dialog->width_spin), width_adj);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(dialog->height_spin), height_adj);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(dialog->resolution_spin), resolution_adj);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->width_spin), dialog->current_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->height_spin), dialog->current_height);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->resolution_spin), dialog->current_resolution);

    /* Set reset button icons */
    set_reset_button_icon(GTK_BUTTON(dialog->width_reset_button));
    set_reset_button_icon(GTK_BUTTON(dialog->height_reset_button));
    set_reset_button_icon(GTK_BUTTON(dialog->resolution_reset_button));

    /* Connect signals */
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

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_size_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }

    /* Update initial labels */
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
    update_preserve_ratio_icon(dialog);

    /* Don't show dialog yet - it will be shown by gtk_dialog_run
     * This avoids parent access issues during setup */

    /* Clean up builder */
    g_object_unref(builder);

    /* Ensure dialog is not shown yet - it will be shown by gtk_dialog_run */
    gtk_widget_hide(dialog->dialog);

    return dialog;
}

/**
 * Free canvas size dialog
 */
void canvas_size_dialog_free(CanvasSizeDialog* dialog) {
    if (!dialog) {
        return;
    }

    if (dialog->anchor_widget) {
        anchor_position_widget_free(dialog->anchor_widget);
    }

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    g_free(dialog);
}

/**
 * Get the dialog window
 */
GtkWindow* canvas_size_dialog_get_window(CanvasSizeDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Run the dialog and get canvas size parameters
 */
gint canvas_size_dialog_run(CanvasSizeDialog* dialog, GtkWindow* parent, CanvasSizeDialogResult** result) {
    gint response;

    if (!dialog || !result) {
        return GTK_RESPONSE_CANCEL;
    }

    if (!dialog->dialog || !GTK_IS_WIDGET(dialog->dialog)) {
        g_warning("Invalid dialog widget");
        return GTK_RESPONSE_CANCEL;
    }

    *result = NULL;

    /* Set parent window for proper centering and modal behavior
     * Must be done before showing the dialog, but after dialog is fully constructed */
    if (parent && GTK_IS_WINDOW(parent) && GTK_IS_WINDOW(dialog->dialog)) {
        /* Ensure dialog widget is a valid window before setting transient */
        GtkWindow* dialog_window = GTK_WINDOW(dialog->dialog);
        if (dialog_window && GTK_IS_WINDOW(dialog_window)) {
            /* Set transient parent - this enables proper centering */
            gtk_window_set_transient_for(dialog_window, parent);
            /* Set window position to center on parent */
            gtk_window_set_position(dialog_window, GTK_WIN_POS_CENTER_ON_PARENT);
        }
    } else if (GTK_IS_WINDOW(dialog->dialog)) {
        /* Fallback to center on screen if no valid parent */
        gtk_window_set_position(GTK_WINDOW(dialog->dialog), GTK_WIN_POS_CENTER);
    }

    /* Show the dialog - must happen after setting transient parent for proper centering */
    gtk_widget_show_all(dialog->dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Allocate result structure */
        *result = (CanvasSizeDialogResult*)g_malloc(sizeof(CanvasSizeDialogResult));
        if (!*result) {
            return GTK_RESPONSE_CANCEL;
        }

        (*result)->width = dialog->current_width;
        (*result)->height = dialog->current_height;
        (*result)->resolution = dialog->current_resolution;
        (*result)->anchor = anchor_position_widget_get_position(dialog->anchor_widget);
    }

    return response;
}

/**
 * Free dialog result structure
 */
void canvas_size_dialog_result_free(CanvasSizeDialogResult* result) {
    if (!result) {
        return;
    }
    g_free(result);
}

/**
 * Format file size in human-readable format
 */
static gchar* format_file_size(guint64 bytes) {
    const gchar* units[] = {"B", "KB", "MB", "GB", "TB"};
    gint unit_index = 0;
    gdouble size = (gdouble)bytes;

    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }

    if (unit_index == 0) {
        return g_strdup_printf("%.0f %s", size, units[unit_index]);
    } else {
        return g_strdup_printf("%.2f %s", size, units[unit_index]);
    }
}
