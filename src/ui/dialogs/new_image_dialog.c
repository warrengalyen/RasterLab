#include "ui/dialogs/new_image_dialog.h"
#include "i18n.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include "ui/widgets/vertical_spin_button.h"
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include "debug_logger.h"

/**
 * New image dialog structure
 */
struct _NewImageDialog {
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
    GtkWidget* bg_transparent_rb;
    GtkWidget* bg_black_rb;
    GtkWidget* bg_white_rb;
    GtkWidget* bg_custom_rb;
    GtkWidget* bg_custom_color;

    /* Current values (in pixels/PPI) */
    guint current_width;
    guint current_height;
    gdouble current_resolution;

    /* Default values (in pixels/PPI) */
    guint default_width;
    guint default_height;
    gdouble default_resolution;

    /* Aspect ratio */
    gdouble aspect_ratio;
    gboolean preserve_aspect;

    /* Unit types */
    gint width_unit;
    gint height_unit;
    gint resolution_unit;

    /* Custom background color */
    GdkRGBA custom_color;
};

/* Unit type enums */
typedef enum {
    UNIT_PIXELS = 0,
    UNIT_INCHES = 1,
    UNIT_CENTIMETERS = 2,
    UNIT_MILLIMETERS = 3,
    UNIT_POINTS = 4,
    UNIT_PICAS = 5
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
static void update_dimensions_label(NewImageDialog* dialog);
static void update_aspect_ratio_label(NewImageDialog* dialog);
static void update_preserve_ratio_icon(NewImageDialog* dialog);
static void convert_width_to_unit(NewImageDialog* dialog, DimensionUnit unit);
static void convert_height_to_unit(NewImageDialog* dialog, DimensionUnit unit);
static void convert_resolution_to_unit(NewImageDialog* dialog, ResolutionUnit unit);
static gdouble pixels_to_unit(guint pixels, DimensionUnit unit, gdouble resolution);
static guint unit_to_pixels(gdouble value, DimensionUnit unit, gdouble resolution);
static gdouble resolution_convert(gdouble value, ResolutionUnit from, ResolutionUnit to);
static gchar* format_file_size(guint64 bytes);
static void on_width_value_changed(GtkWidget* spin, gpointer user_data);
static void on_height_value_changed(GtkWidget* spin, gpointer user_data);
static void on_width_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_height_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_resolution_unit_changed(GtkComboBox* combo, gpointer user_data);
static void on_resolution_value_changed(GtkWidget* spin, gpointer user_data);
static void on_preserve_ratio_toggled(GtkToggleButton* button, gpointer user_data);
static void on_width_reset_clicked(GtkButton* button, gpointer user_data);
static void on_height_reset_clicked(GtkButton* button, gpointer user_data);
static void on_resolution_reset_clicked(GtkButton* button, gpointer user_data);
static void on_bg_changed(GtkRadioButton* button, gpointer user_data);
static void on_button_clicked(GtkButton* button, gpointer user_data);

/**
 * Width unit changed callback
 */
static void on_width_unit_changed(GtkComboBox* combo, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
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
 */
static void on_height_unit_changed(GtkComboBox* combo, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
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
    NewImageDialog* dialog = (NewImageDialog*)user_data;
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
static void on_width_value_changed(GtkWidget* spin, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
    gdouble value = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    guint new_width = unit_to_pixels(value, (DimensionUnit)dialog->width_unit, dialog->current_resolution);

    if (dialog->preserve_aspect && dialog->aspect_ratio > 0.0) {
        /* Calculate new height to maintain aspect ratio */
        guint new_height = (guint)(new_width / dialog->aspect_ratio + 0.5);
        dialog->current_height = new_height;

        /* Update height spin button (convert to current unit) */
        g_signal_handlers_block_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
        gdouble height_value = pixels_to_unit(new_height, (DimensionUnit)dialog->height_unit, dialog->current_resolution);
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->height_spin), height_value);
        g_signal_handlers_unblock_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
    }

    dialog->current_width = new_width;
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Height value changed callback
 */
static void on_height_value_changed(GtkWidget* spin, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
    gdouble value = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    guint new_height = unit_to_pixels(value, (DimensionUnit)dialog->height_unit, dialog->current_resolution);

    if (dialog->preserve_aspect && dialog->aspect_ratio > 0.0) {
        /* Calculate new width to maintain aspect ratio */
        guint new_width = (guint)(new_height * dialog->aspect_ratio + 0.5);
        dialog->current_width = new_width;

        /* Update width spin button (convert to current unit) */
        g_signal_handlers_block_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
        gdouble width_value = pixels_to_unit(new_width, (DimensionUnit)dialog->width_unit, dialog->current_resolution);
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->width_spin), width_value);
        g_signal_handlers_unblock_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
    }

    dialog->current_height = new_height;
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Resolution value changed callback
 */
static void on_resolution_value_changed(GtkWidget* spin, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
    gdouble value = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));

    /* Convert from current unit to PPI */
    ResolutionUnit unit = (ResolutionUnit)dialog->resolution_unit;
    if (unit == RES_UNIT_PPCM) {
        dialog->current_resolution = value * INCHES_TO_CM;
    } else {
        dialog->current_resolution = value;
    }

    /* When resolution changes, update width/height display if not in pixels */
    if (dialog->width_unit != UNIT_PIXELS) {
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }
    if (dialog->height_unit != UNIT_PIXELS) {
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
}

/**
 * Preserve ratio toggle callback
 */
static void on_preserve_ratio_toggled(GtkToggleButton* button, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
    dialog->preserve_aspect = gtk_toggle_button_get_active(button);

    /* Recalculate aspect ratio from current dimensions */
    if (dialog->current_height > 0) {
        dialog->aspect_ratio = (gdouble)dialog->current_width / (gdouble)dialog->current_height;
    } else {
        dialog->aspect_ratio = 1.0;
    }

    update_preserve_ratio_icon(dialog);
}

/**
 * Width reset button callback
 */
static void on_width_reset_clicked(GtkButton* button, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;

    dialog->current_width = dialog->default_width;
    convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);

    if (dialog->preserve_aspect) {
        dialog->current_height = dialog->default_height;
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }

    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Height reset button callback
 */
static void on_height_reset_clicked(GtkButton* button, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;

    dialog->current_height = dialog->default_height;
    convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);

    if (dialog->preserve_aspect) {
        dialog->current_width = dialog->default_width;
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }

    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
}

/**
 * Resolution reset button callback
 */
static void on_resolution_reset_clicked(GtkButton* button, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;

    dialog->current_resolution = dialog->default_resolution;
    convert_resolution_to_unit(dialog, (ResolutionUnit)dialog->resolution_unit);

    /* Update width/height display if not in pixels */
    if (dialog->width_unit != UNIT_PIXELS) {
        convert_width_to_unit(dialog, (DimensionUnit)dialog->width_unit);
    }
    if (dialog->height_unit != UNIT_PIXELS) {
        convert_height_to_unit(dialog, (DimensionUnit)dialog->height_unit);
    }
}

/**
 * Color update callback for custom color button
 */
static void on_custom_color_update(double r, double g, double b, gpointer user_data) {
    NewImageDialog* dialog = (NewImageDialog*)user_data;
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
    NewImageDialog* dialog = (NewImageDialog*)user_data;
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
    NewImageDialog* dialog = (NewImageDialog*)user_data;

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
 * Convert pixels to specified unit
 */
static gdouble pixels_to_unit(guint pixels, DimensionUnit unit, gdouble resolution) {
    switch (unit) {
        case UNIT_PIXELS:
            return (gdouble)pixels;
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
        return value / INCHES_TO_CM;
    } else if (from == RES_UNIT_PPCM && to == RES_UNIT_PPI) {
        return value * INCHES_TO_CM;
    }

    return value;
}

/**
 * Convert width to specified unit and update spin button
 */
static void convert_width_to_unit(NewImageDialog* dialog, DimensionUnit unit) {
    gdouble value = pixels_to_unit(dialog->current_width, unit, dialog->current_resolution);

    g_signal_handlers_block_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->width_spin), value);
    g_signal_handlers_unblock_by_func(dialog->width_spin, G_CALLBACK(on_width_value_changed), dialog);
}

/**
 * Convert height to specified unit and update spin button
 */
static void convert_height_to_unit(NewImageDialog* dialog, DimensionUnit unit) {
    gdouble value = pixels_to_unit(dialog->current_height, unit, dialog->current_resolution);

    g_signal_handlers_block_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->height_spin), value);
    g_signal_handlers_unblock_by_func(dialog->height_spin, G_CALLBACK(on_height_value_changed), dialog);
}

/**
 * Convert resolution to specified unit and update spin button
 */
static void convert_resolution_to_unit(NewImageDialog* dialog, ResolutionUnit unit) {
    gdouble value = resolution_convert(dialog->current_resolution, RES_UNIT_PPI, unit);

    g_signal_handlers_block_by_func(dialog->resolution_spin, G_CALLBACK(on_resolution_value_changed), dialog);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->resolution_spin), value);
    g_signal_handlers_unblock_by_func(dialog->resolution_spin, G_CALLBACK(on_resolution_value_changed), dialog);
}

/**
 * Update dimensions label
 */
static void update_dimensions_label(NewImageDialog* dialog) {
    gchar* text;
    guint64 current_size;
    gchar* current_size_str;

    /* Calculate memory size (assuming 4 bytes per pixel for RGBA) */
    current_size = (guint64)dialog->current_width * dialog->current_height * 4;
    current_size_str = format_file_size(current_size);

    text = g_strdup_printf("%u px X %u px (%s)",
                           dialog->current_width,
                           dialog->current_height,
                           current_size_str);

    gtk_label_set_text(GTK_LABEL(dialog->dimensions_text), text);
    g_free(text);
    g_free(current_size_str);
}

/**
 * Update aspect ratio label
 */
static void update_aspect_ratio_label(NewImageDialog* dialog) {
    gchar* text;
    guint width = dialog->current_width;
    guint height = dialog->current_height;
    guint gcd;
    gdouble ratio;

    /* Handle edge cases */
    if (width == 0 || height == 0) {
        text = g_strdup("1:1 (1.00)");
        gtk_label_set_text(GTK_LABEL(dialog->aspect_ratio_text), text);
        g_free(text);
        return;
    }

    /* Calculate GCD for simplified ratio using Euclidean algorithm */
    guint a = width;
    guint b = height;

    while (b != 0) {
        guint remainder = a % b;
        a = b;
        b = remainder;
    }
    gcd = a;

    if (gcd > 0 && gcd <= width && gcd <= height) {
        guint ratio_w = width / gcd;
        guint ratio_h = height / gcd;

        /* Verify the reduction is correct */
        guint check_a = ratio_w;
        guint check_b = ratio_h;
        while (check_b != 0) {
            guint check_remainder = check_a % check_b;
            check_a = check_b;
            check_b = check_remainder;
        }
        if (check_a == 1) {
            ratio = (gdouble)width / (gdouble)height;
            text = g_strdup_printf("%u:%u (%.2f)", ratio_w, ratio_h, ratio);
        } else {
            ratio = (gdouble)width / (gdouble)height;
            text = g_strdup_printf("%u:%u (%.2f)", width, height, ratio);
        }
    } else {
        ratio = (gdouble)width / (gdouble)height;
        text = g_strdup_printf("%u:%u (%.2f)", width, height, ratio);
    }

    gtk_label_set_text(GTK_LABEL(dialog->aspect_ratio_text), text);
    g_free(text);
}

/**
 * Update preserve ratio toggle icon
 */
static void update_preserve_ratio_icon(NewImageDialog* dialog) {
    GtkImage* image;
    GError* error = NULL;
    GdkPixbuf* pixbuf;
    const gchar* icon_resource;

    if (dialog->preserve_aspect) {
        icon_resource = "/icons/padlock-locked.png";
    } else {
        icon_resource = "/icons/padlock-unlocked.png";
    }

    pixbuf = gdk_pixbuf_new_from_resource(icon_resource, &error);
    if (!pixbuf) {
        debug_log("WRN", "Failed to load %s: %s", icon_resource, error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    /* Scale to 20x20 */
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 20, 20, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    pixbuf = scaled;
    if (!pixbuf) {
        return;
    }

    /* Get or create image widget */
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(dialog->preserve_ratio_toggle));
    if (child && GTK_IS_IMAGE(child)) {
        image = GTK_IMAGE(child);
        gtk_image_set_from_pixbuf(image, pixbuf);
    } else {
        if (child && GTK_IS_WIDGET(child)) {
            gtk_widget_destroy(child);
        }
        image = GTK_IMAGE(gtk_image_new_from_pixbuf(pixbuf));
        gtk_widget_show(GTK_WIDGET(image));
        gtk_container_add(GTK_CONTAINER(dialog->preserve_ratio_toggle), GTK_WIDGET(image));
    }

    g_object_unref(pixbuf);
}

/**
 * Helper function to set reset button icon from resource (to-pixdata format).
 */
static void set_reset_button_icon(GtkButton* button) {
    if (!button) {
        return;
    }

    GError* error = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_resource("/icons/reset.png", &error);
    if (!pixbuf) {
        debug_log("WRN", "Failed to load reset.png: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    /* Scale to 20x20 */
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 20, 20, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);

    if (scaled) {
        /* Remove any existing label */
        gtk_button_set_label(button, NULL);

        /* Set the image */
        GtkWidget* image = gtk_image_new_from_pixbuf(scaled);
        gtk_button_set_image(button, image);
        gtk_button_set_image_position(button, GTK_POS_LEFT);
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

/**
 * Create a new image dialog
 */
NewImageDialog* new_image_dialog_new(void) {
    NewImageDialog* dialog;
    GtkBuilder* builder;
    GError* error = NULL;
    GtkListStore *width_store, *height_store, *resolution_store;
    GtkTreeIter iter;
    GtkCellRenderer* cell;

    dialog = (NewImageDialog*)g_malloc(sizeof(NewImageDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize all pointers to NULL */
    memset(dialog, 0, sizeof(NewImageDialog));

    /* Get display resolution using modern monitor API */
    guint display_width = 1920; /* Fallback defaults */
    guint display_height = 1080;
    gdouble display_ppi = 96.0; /* Fallback default */

    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
        if (!monitor) {
            /* Fallback to first monitor if primary not available */
            gint n_monitors = gdk_display_get_n_monitors(display);
            if (n_monitors > 0) {
                monitor = gdk_display_get_monitor(display, 0);
            }
        }

        if (monitor) {
            GdkRectangle geometry;
            gdk_monitor_get_geometry(monitor, &geometry);
            display_width = (guint)geometry.width;
            display_height = (guint)geometry.height;

            /* Get monitor resolution (DPI) using scale factor */
            gint scale_factor = gdk_monitor_get_scale_factor(monitor);
            if (scale_factor > 0) {
                display_ppi = (gdouble)scale_factor * 96.0;
            }
        }
    }

    /* Set default values to display resolution */
    dialog->default_width = display_width;
    dialog->default_height = display_height;
    dialog->default_resolution = display_ppi;

    /* Initialize current values */
    dialog->current_width = dialog->default_width;
    dialog->current_height = dialog->default_height;
    dialog->current_resolution = dialog->default_resolution;

    /* Calculate aspect ratio */
    if (dialog->current_height > 0) {
        dialog->aspect_ratio = (gdouble)dialog->current_width / (gdouble)dialog->current_height;
    } else {
        dialog->aspect_ratio = 1.0;
    }

    dialog->preserve_aspect = FALSE;
    dialog->width_unit = UNIT_PIXELS;
    dialog->height_unit = UNIT_PIXELS;
    dialog->resolution_unit = RES_UNIT_PPI;

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/new_image_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load new_image_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Get dialog widget */
    dialog->dialog = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_dialog"));
    if (!dialog->dialog) {
        debug_log("WRN", "Failed to get new_image_dialog from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Get all widgets */
    dialog->width_spin = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_width_spin"));
    dialog->height_spin = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_height_spin"));
    dialog->resolution_spin = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_resolution_spin"));
    dialog->width_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_width_units_combo"));
    dialog->height_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_height_units_combo"));
    dialog->resolution_units_combo = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_resolution_units_combo"));
    dialog->width_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_width_reset_button"));
    dialog->height_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_height_reset_button"));
    dialog->resolution_reset_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_resolution_reset_button"));
    dialog->preserve_ratio_toggle = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_preserve_ratio_toggle"));
    dialog->dimensions_text = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_dimensions_text"));
    dialog->aspect_ratio_text = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_aspect_ratio_text"));
    dialog->bg_transparent_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_bg_transparent_rb"));
    dialog->bg_black_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_bg_black_rb"));
    dialog->bg_white_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_bg_white_rb"));
    dialog->bg_custom_rb = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_bg_custom_rb"));
    dialog->bg_custom_color = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_bg_custom_color"));

    dialog->width_spin = ui_utils_replace_spin_with_vertical(dialog->width_spin);
    dialog->height_spin = ui_utils_replace_spin_with_vertical(dialog->height_spin);
    dialog->resolution_spin = ui_utils_replace_spin_with_vertical(dialog->resolution_spin);

    if (!dialog->width_spin || !dialog->height_spin || !dialog->resolution_spin ||
        !dialog->width_units_combo || !dialog->height_units_combo || !dialog->resolution_units_combo ||
        !dialog->width_reset_button || !dialog->height_reset_button || !dialog->resolution_reset_button ||
        !dialog->preserve_ratio_toggle || !dialog->dimensions_text || !dialog->aspect_ratio_text ||
        !dialog->bg_transparent_rb || !dialog->bg_black_rb || !dialog->bg_white_rb ||
        !dialog->bg_custom_rb || !dialog->bg_custom_color) {
        debug_log("WRN", "Failed to get all widgets from builder");
        g_object_unref(builder);
        g_free(dialog);
        return NULL;
    }

    /* Keep all three rows visually aligned regardless of combo natural width. */
    {
        GtkSizeGroup* spin_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
        GtkSizeGroup* reset_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
        GtkSizeGroup* unit_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

        gtk_size_group_add_widget(spin_group, dialog->width_spin);
        gtk_size_group_add_widget(spin_group, dialog->height_spin);
        gtk_size_group_add_widget(spin_group, dialog->resolution_spin);

        gtk_size_group_add_widget(reset_group, dialog->width_reset_button);
        gtk_size_group_add_widget(reset_group, dialog->height_reset_button);
        gtk_size_group_add_widget(reset_group, dialog->resolution_reset_button);

        gtk_size_group_add_widget(unit_group, dialog->width_units_combo);
        gtk_size_group_add_widget(unit_group, dialog->height_units_combo);
        gtk_size_group_add_widget(unit_group, dialog->resolution_units_combo);

        g_object_unref(spin_group);
        g_object_unref(reset_group);
        g_object_unref(unit_group);
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

    /* Populate width/height units combo boxes */
    width_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, _("px"), -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, _("in"), -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, _("cm"), -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, _("mm"), -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, _("pt"), -1);
    gtk_list_store_append(width_store, &iter);
    gtk_list_store_set(width_store, &iter, 0, _("pc"), -1);

    height_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(height_store, &iter);
    gtk_list_store_set(height_store, &iter, 0, _("px"), -1);
    gtk_list_store_append(height_store, &iter);
    gtk_list_store_set(height_store, &iter, 0, _("in"), -1);
    gtk_list_store_append(height_store, &iter);
    gtk_list_store_set(height_store, &iter, 0, _("cm"), -1);
    gtk_list_store_append(height_store, &iter);
    gtk_list_store_set(height_store, &iter, 0, _("mm"), -1);
    gtk_list_store_append(height_store, &iter);
    gtk_list_store_set(height_store, &iter, 0, _("pt"), -1);
    gtk_list_store_append(height_store, &iter);
    gtk_list_store_set(height_store, &iter, 0, _("pc"), -1);

    /* Populate resolution units combo box */
    resolution_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(resolution_store, &iter);
    gtk_list_store_set(resolution_store, &iter, 0, _("PPI"), -1);
    gtk_list_store_append(resolution_store, &iter);
    gtk_list_store_set(resolution_store, &iter, 0, _("PPCM"), -1);

    /* Set models */
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->width_units_combo), GTK_TREE_MODEL(width_store));
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->height_units_combo), GTK_TREE_MODEL(height_store));
    gtk_combo_box_set_model(GTK_COMBO_BOX(dialog->resolution_units_combo), GTK_TREE_MODEL(resolution_store));
    g_object_unref(width_store);
    g_object_unref(height_store);
    g_object_unref(resolution_store);

    /* Set up cell renderers */
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->width_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->width_units_combo), cell, "text", 0, NULL);

    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->height_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->height_units_combo), cell, "text", 0, NULL);

    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dialog->resolution_units_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dialog->resolution_units_combo), cell, "text", 0, NULL);

    /* Set default units */
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->width_units_combo), UNIT_PIXELS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->height_units_combo), UNIT_PIXELS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->resolution_units_combo), RES_UNIT_PPI);

    /* Set up spin button adjustments */
    GtkAdjustment* width_adj = gtk_adjustment_new(dialog->current_width, 1.0, 100000.0, 1.0, 10.0, 0.0);
    GtkAdjustment* height_adj = gtk_adjustment_new(dialog->current_height, 1.0, 100000.0, 1.0, 10.0, 0.0);
    GtkAdjustment* resolution_adj = gtk_adjustment_new(dialog->current_resolution, 1.0, 10000.0, 0.1, 1.0, 0.0);

    vertical_spin_button_set_adjustment(VERTICAL_SPIN_BUTTON(dialog->width_spin), width_adj);
    vertical_spin_button_set_adjustment(VERTICAL_SPIN_BUTTON(dialog->height_spin), height_adj);
    vertical_spin_button_set_adjustment(VERTICAL_SPIN_BUTTON(dialog->resolution_spin), resolution_adj);

    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->width_spin), dialog->current_width);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->height_spin), dialog->current_height);
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->resolution_spin), dialog->current_resolution);

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
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_image_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog->dialog);
    }

    /* Replace default titlebar with header bar */
    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), _("New Image"));

    /* Update initial labels */
    update_dimensions_label(dialog);
    update_aspect_ratio_label(dialog);
    update_preserve_ratio_icon(dialog);

    /* Clean up builder */
    g_object_unref(builder);

    /* Ensure dialog is not shown yet - it will be shown by gtk_dialog_run */
    gtk_widget_hide(dialog->dialog);

    return dialog;
}

/**
 * Free new image dialog
 */
void new_image_dialog_free(NewImageDialog* dialog) {
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
GtkWindow* new_image_dialog_get_window(NewImageDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Run the dialog and get image parameters
 */
gint new_image_dialog_run(NewImageDialog* dialog, GtkWindow* parent, NewImageDialogResult** result) {
    gint response;
    LayerBackgroundType background;
    gdouble custom_color[4];

    if (!dialog || !result) {
        return GTK_RESPONSE_CANCEL;
    }

    if (!dialog->dialog || !GTK_IS_WIDGET(dialog->dialog)) {
        debug_log("WRN", "Invalid dialog widget");
        return GTK_RESPONSE_CANCEL;
    }

    *result = NULL;

    /* Set parent window for proper centering and modal behavior */
    if (parent && GTK_IS_WINDOW(parent) && GTK_IS_WINDOW(dialog->dialog)) {
        GtkWindow* dialog_window = GTK_WINDOW(dialog->dialog);
        if (dialog_window && GTK_IS_WINDOW(dialog_window)) {
            gtk_window_set_transient_for(dialog_window, parent);
            gtk_window_set_position(dialog_window, GTK_WIN_POS_CENTER_ON_PARENT);
        }
    } else if (GTK_IS_WINDOW(dialog->dialog)) {
        gtk_window_set_position(GTK_WINDOW(dialog->dialog), GTK_WIN_POS_CENTER);
    }

    /* Show the dialog */
    gtk_widget_show_all(dialog->dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Allocate result structure */
        *result = (NewImageDialogResult*)g_malloc(sizeof(NewImageDialogResult));
        if (!*result) {
            return GTK_RESPONSE_CANCEL;
        }

        (*result)->width = dialog->current_width;
        (*result)->height = dialog->current_height;
        (*result)->resolution = dialog->current_resolution;

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
    }

    return response;
}

/**
 * Free dialog result structure
 */
void new_image_dialog_result_free(NewImageDialogResult* result) {
    if (!result) {
        return;
    }
    g_free(result);
}
