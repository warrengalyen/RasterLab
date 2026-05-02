/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/formats/tiff_options_dialog.h"
#include "document.h"
#include "i18n.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include "debug_logger.h"

#ifdef HAVE_LIBTIFF

/* Forward declare TIFF types from plugin_tiff.c */
typedef enum {
    TIFF_COLOR_COMPRESSION_AUTO = 0,
    TIFF_COLOR_COMPRESSION_LZW = 1,
    TIFF_COLOR_COMPRESSION_ZIP = 2,
    TIFF_COLOR_COMPRESSION_NONE = 3
} TIFFColorCompression;

typedef enum {
    TIFF_MONO_COMPRESSION_AUTO = 0,
    TIFF_MONO_COMPRESSION_CCITT_FAX4 = 1,
    TIFF_MONO_COMPRESSION_CCITT_FAX3 = 2,
    TIFF_MONO_COMPRESSION_LZW = 3,
    TIFF_MONO_COMPRESSION_NONE = 4
} TIFFMonochromeCompression;

typedef enum {
    TIFF_COLOR_FORMAT_AUTO = 0,
    TIFF_COLOR_FORMAT_COLOR = 1,
    TIFF_COLOR_FORMAT_GRAYSCALE = 2
} TIFFColorFormat;

typedef enum {
    TIFF_TRANSPARENCY_AUTO = 0,
    TIFF_TRANSPARENCY_FULL = 1,
    TIFF_TRANSPARENCY_BINARY_CUTOFF = 2,
    TIFF_TRANSPARENCY_BINARY_COLOR = 3,
    TIFF_TRANSPARENCY_NONE = 4
} TIFFTransparencyFormat;

typedef enum {
    TIFF_PAGE_FORMAT_SINGLE_PAGE = 0,
    TIFF_PAGE_FORMAT_MULTIPAGE = 1
} TIFFPageFormat;

typedef struct {
    TIFFColorCompression color_compression;
    TIFFMonochromeCompression monochrome_compression;
    TIFFColorFormat color_format;
    TIFFTransparencyFormat transparency_format;
    uint8_t transparency_cutoff;
    uint8_t transparency_color_r;
    uint8_t transparency_color_g;
    uint8_t transparency_color_b;
    uint8_t compositing_color_r;
    uint8_t compositing_color_g;
    uint8_t compositing_color_b;
    TIFFPageFormat page_format;
    uint32_t reserved[2];
} TIFFSaveOptions;

typedef struct {
    GdkRGBA* color;
    GtkWidget* button;
} TIFFColorButtonData;

static void on_tiff_transparent_color_update(double r, double g, double b, gpointer user_data) {
    TIFFColorButtonData* data = (TIFFColorButtonData*)user_data;
    if (!data || !data->color) {
        return;
    }
    data->color->red = r;
    data->color->green = g;
    data->color->blue = b;
    data->color->alpha = 1.0;
    if (data->button) {
        update_color_button_appearance(data->button, data->color);
    }
}

static void on_tiff_compositing_color_update(double r, double g, double b, gpointer user_data) {
    TIFFColorButtonData* data = (TIFFColorButtonData*)user_data;
    if (!data || !data->color) {
        return;
    }
    data->color->red = r;
    data->color->green = g;
    data->color->blue = b;
    data->color->alpha = 1.0;
    if (data->button) {
        update_color_button_appearance(data->button, data->color);
    }
}

static void on_tiff_transparent_color_clicked(GtkButton* button, gpointer user_data) {
    TIFFColorButtonData* data = (TIFFColorButtonData*)user_data;
    if (!data || !data->color || !button) {
        return;
    }
    GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    GtkWidget* color_dialog = color_chooser_dialog_new(
        parent,
        _("Choose Transparent Color"),
        data->color,
        on_tiff_transparent_color_update,
        data,
        FALSE);
    gtk_dialog_run(GTK_DIALOG(color_dialog));
    double r, g, b;
    color_chooser_dialog_get_color(color_dialog, &r, &g, &b);
    data->color->red = r;
    data->color->green = g;
    data->color->blue = b;
    data->color->alpha = 1.0;
    update_color_button_appearance(data->button, data->color);
    gtk_widget_destroy(color_dialog);
}

static void on_tiff_compositing_color_clicked(GtkButton* button, gpointer user_data) {
    TIFFColorButtonData* data = (TIFFColorButtonData*)user_data;
    if (!data || !data->color || !button) {
        return;
    }
    GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    GtkWidget* color_dialog = color_chooser_dialog_new(
        parent,
        _("Choose Compositing Color"),
        data->color,
        on_tiff_compositing_color_update,
        data,
        FALSE);
    gtk_dialog_run(GTK_DIALOG(color_dialog));
    double r, g, b;
    color_chooser_dialog_get_color(color_dialog, &r, &g, &b);
    data->color->red = r;
    data->color->green = g;
    data->color->blue = b;
    data->color->alpha = 1.0;
    update_color_button_appearance(data->button, data->color);
    gtk_widget_destroy(color_dialog);
}

/* Button clicked callback to emit dialog response */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/* Update visibility of widgets based on current selections */
static void update_widget_visibility(GtkWidget* color_format_combo,
                                     GtkWidget* transparency_format_combo,
                                     GtkWidget* depth_combo,
                                     GtkWidget* depth_box,
                                     GtkWidget* palette_size_box,
                                     GtkWidget* transparency_format_cutoff_box,
                                     GtkWidget* transparent_color_box,
                                     GtkWidget* compositing_color_box) {
    if (!color_format_combo || !transparency_format_combo) {
        return; /* Can't update visibility without required combos */
    }

    gint color_format_active = gtk_combo_box_get_active(GTK_COMBO_BOX(color_format_combo));
    gint transparency_format_active = gtk_combo_box_get_active(GTK_COMBO_BOX(transparency_format_combo));
    gint depth_active = depth_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(depth_combo)) : 1; /* Default to standard */

    /* Handle invalid selections (-1) by defaulting to AUTO */
    if (color_format_active < 0) {
        color_format_active = TIFF_COLOR_FORMAT_AUTO;
    }
    if (transparency_format_active < 0) {
        transparency_format_active = TIFF_TRANSPARENCY_AUTO;
    }
    if (depth_active < 0) {
        depth_active = 1; /* Standard */
    }

    TIFFColorFormat color_format = (TIFFColorFormat)color_format_active;
    TIFFTransparencyFormat transparency_format = (TIFFTransparencyFormat)transparency_format_active;

    /* Depth: visible when color_format = color or grayscale (not auto) */
    if (depth_box) {
        gtk_widget_set_visible(depth_box, (color_format == TIFF_COLOR_FORMAT_COLOR || color_format == TIFF_COLOR_FORMAT_GRAYSCALE));
    }

    /* Palette size: visible when color_format = color or grayscale AND depth = indexed (2) */
    if (palette_size_box) {
        gtk_widget_set_visible(palette_size_box, ((color_format == TIFF_COLOR_FORMAT_COLOR || color_format == TIFF_COLOR_FORMAT_GRAYSCALE) && depth_active == 2));
    }

    /* Transparency cut-off: visible when transparency_format = binary cut-off */
    if (transparency_format_cutoff_box) {
        gtk_widget_set_visible(transparency_format_cutoff_box, (transparency_format == TIFF_TRANSPARENCY_BINARY_CUTOFF));
    }

    /* Transparent color: visible when transparency_format = binary color */
    if (transparent_color_box) {
        gtk_widget_set_visible(transparent_color_box, (transparency_format == TIFF_TRANSPARENCY_BINARY_COLOR));
    }

    /* Compositing color: visible when transparency_format = binary color, binary cut-off, or none */
    if (compositing_color_box) {
        gtk_widget_set_visible(compositing_color_box, (transparency_format == TIFF_TRANSPARENCY_BINARY_COLOR ||
                                                       transparency_format == TIFF_TRANSPARENCY_BINARY_CUTOFF ||
                                                       transparency_format == TIFF_TRANSPARENCY_NONE));
    }
}

/* Callback when color format changes */
static void on_color_format_changed(GtkComboBox* combo, gpointer user_data) {
    GtkWidget** widgets = (GtkWidget**)user_data;
    update_widget_visibility(GTK_WIDGET(combo), widgets[1], widgets[2], widgets[3], widgets[4], widgets[5], widgets[6], widgets[7]);
}

/* Callback when transparency format changes */
static void on_transparency_format_changed(GtkComboBox* combo, gpointer user_data) {
    GtkWidget** widgets = (GtkWidget**)user_data;
    update_widget_visibility(widgets[0], GTK_WIDGET(combo), widgets[2], widgets[3], widgets[4], widgets[5], widgets[6], widgets[7]);
}

/* Callback when depth changes */
static void on_depth_changed(GtkComboBox* combo, gpointer user_data) {
    GtkWidget** widgets = (GtkWidget**)user_data;
    update_widget_visibility(widgets[0], widgets[1], GTK_WIDGET(combo), widgets[3], widgets[4], widgets[5], widgets[6], widgets[7]);
}

/* Callback to handle toggle button group mutual exclusivity */
static void on_toggle_button_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkToggleButton** buttons = (GtkToggleButton**)user_data;
    int count = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "button-count"));
    int button_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "button-index"));

    if (!buttons || count <= 0 || button_index < 0 || button_index >= count) {
        return;
    }

    /* If this button was activated, deactivate all others in the group */
    if (gtk_toggle_button_get_active(button)) {
        for (int i = 0; i < count; i++) {
            if (i != button_index && buttons[i]) {
                /* Block signal to prevent recursion */
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_toggle_button_toggled), buttons);
                gtk_toggle_button_set_active(buttons[i], FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_toggle_button_toggled), buttons);
            }
        }
    } else {
        /* If this button was deactivated, ensure at least one button in the group is active */
        gboolean any_active = FALSE;
        for (int i = 0; i < count; i++) {
            if (buttons[i] && gtk_toggle_button_get_active(buttons[i])) {
                any_active = TRUE;
                break;
            }
        }

        /* If no button is active, reactivate this one */
        if (!any_active) {
            g_signal_handlers_block_by_func(button, G_CALLBACK(on_toggle_button_toggled), buttons);
            gtk_toggle_button_set_active(button, TRUE);
            g_signal_handlers_unblock_by_func(button, G_CALLBACK(on_toggle_button_toggled), buttons);
        }
    }
}

/* Helper function to set up toggle button group with mutual exclusivity */
static void setup_toggle_button_group(GtkToggleButton** buttons, int count) {
    if (!buttons || count <= 0) {
        return;
    }

    /* Connect toggled signal for each button */
    for (int i = 0; i < count; i++) {
        if (buttons[i]) {
            /* Store button index and count in button data */
            g_object_set_data(G_OBJECT(buttons[i]), "button-index", GINT_TO_POINTER(i));
            g_object_set_data(G_OBJECT(buttons[i]), "button-count", GINT_TO_POINTER(count));
            /* Connect signal handler */
            g_signal_connect(buttons[i], "toggled", G_CALLBACK(on_toggle_button_toggled), buttons);
        }
    }
}

/* Helper function to set toggle button in a radio group */
static void set_toggle_button_group(GtkToggleButton** buttons, int count, int active_index) {
    if (!buttons || count <= 0 || active_index < 0 || active_index >= count) {
        return;
    }

    /* Block signals while setting to avoid triggering handlers */
    for (int i = 0; i < count; i++) {
        if (buttons[i]) {
            g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_toggle_button_toggled), buttons);
        }
    }

    /* Set active state */
    for (int i = 0; i < count; i++) {
        if (buttons[i]) {
            gtk_toggle_button_set_active(buttons[i], (i == active_index));
        }
    }

    /* Unblock signals */
    for (int i = 0; i < count; i++) {
        if (buttons[i]) {
            g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_toggle_button_toggled), buttons);
        }
    }
}

/* Helper function to get active toggle button in a radio group */
static int get_active_toggle_button(GtkToggleButton** buttons, int count) {
    for (int i = 0; i < count; i++) {
        if (buttons[i] && gtk_toggle_button_get_active(buttons[i])) {
            return i;
        }
    }
    return 0; /* Default to first */
}

/**
 * Show TIFF save options dialog
 */
gboolean tiff_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* color_compression_auto_button;
    GtkWidget* color_compression_lzw_button;
    GtkWidget* color_compression_zip_button;
    GtkWidget* color_compression_none_button;
    GtkWidget* monochrome_compression_auto_button;
    GtkWidget* monochrome_compression_fax4_button;
    GtkWidget* monochrome_compression_fax3_button;
    GtkWidget* monochrome_compression_lzw_button;
    GtkWidget* monochrome_compression_none_button;
    GtkWidget* color_format_combo;
    GtkWidget* color_format_depth_combo;
    GtkWidget* color_format_depth_box;
    GtkWidget* color_format_palette_size_box;
    GtkWidget* palette_size_scale;
    GtkWidget* palette_size_spin;
    GtkAdjustment* palette_size_adjustment;
    GtkWidget* transparency_format_combo;
    GtkWidget* transparency_format_cutoff_box;
    GtkWidget* transparency_cutoff_scale;
    GtkWidget* transparency_cutoff_spin;
    GtkWidget* transparent_color;
    GtkWidget* transparent_color_box;
    GtkWidget* compositing_color;
    GtkWidget* compositing_color_box;
    GtkWidget* page_format_box;
    GtkWidget* page_format_single_page_button;
    GtkWidget* page_format_multipage_button;
    GtkAdjustment* transparency_cutoff_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    GtkWidget* icc_checkbox = NULL;
    TIFFSaveOptions* tiff_opts = NULL;
    GdkRGBA transparent_color_rgba;
    GdkRGBA compositing_color_rgba;
    TIFFColorButtonData transparent_color_data;
    TIFFColorButtonData compositing_color_data;
    GtkToggleButton* color_compression_buttons[4];
    GtkToggleButton* monochrome_compression_buttons[5];
    GtkToggleButton* page_format_buttons[2];
    guint layer_count = 0;

    if (!opts) {
        return FALSE;
    }

    /* Get layer count from document */
    if (doc) {
        layer_count = document_get_layer_count(doc);
    }

    /* Get TIFF options from plugin_data */
    if (opts->plugin_data) {
        tiff_opts = (TIFFSaveOptions*)opts->plugin_data;
    }

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/dialogs/tiff_options_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load tiff_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    /* Get dialog widget */
    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "tiff_options_dialog"));
    if (!dialog) {
        debug_log("WRN", "Failed to get tiff_options_dialog from builder");
        g_object_unref(builder);
        return FALSE;
    }

    /* Set parent window */
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }

    /* Set default size */
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, -1);

    /* Replace default titlebar with header bar */
    if (GTK_IS_WINDOW(dialog)) {
        const gchar* title = gtk_window_get_title(GTK_WINDOW(dialog));
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : "TIFF Options");
    }

    /* Get all widgets */
    /* Note: The glade file has incorrect IDs - the first button (labeled "auto") has ID "color_compression_none_button" */
    color_compression_auto_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_compression_none_button")); /* Actually "auto" button */
    color_compression_lzw_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_compression_lzw_button"));
    color_compression_zip_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_compression_zip_button"));
    color_compression_none_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_compression_none_butto")); /* Typo in glade: missing 'n' */

    monochrome_compression_auto_button = GTK_WIDGET(gtk_builder_get_object(builder, "monochrome_compression_auto_button"));
    monochrome_compression_fax4_button = GTK_WIDGET(gtk_builder_get_object(builder, "monochrome_compression_CCITFAX4_button"));
    monochrome_compression_fax3_button = GTK_WIDGET(gtk_builder_get_object(builder, "monochrome_compression_CCITFAX3_button"));
    monochrome_compression_lzw_button = GTK_WIDGET(gtk_builder_get_object(builder, "monochrome_compression_LZW_button"));
    monochrome_compression_none_button = GTK_WIDGET(gtk_builder_get_object(builder, "monochrome_compression_none_button"));

    color_format_combo = GTK_WIDGET(gtk_builder_get_object(builder, "color_format_combo"));
    color_format_depth_combo = GTK_WIDGET(gtk_builder_get_object(builder, "color_format_depth_combo"));
    color_format_depth_box = GTK_WIDGET(gtk_builder_get_object(builder, "color_format_depth_box"));
    color_format_palette_size_box = GTK_WIDGET(gtk_builder_get_object(builder, "color_format_palette_size_box"));
    palette_size_scale = GTK_WIDGET(gtk_builder_get_object(builder, "palette_size_scale"));
    palette_size_spin = GTK_WIDGET(gtk_builder_get_object(builder, "palette_size_spin"));
    palette_size_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "palette_size_adjustment"));
    transparency_format_combo = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_format_combo"));
    transparency_format_cutoff_box = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_format_cutoff_box"));
    transparency_cutoff_scale = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_cutoff_scale"));
    transparency_cutoff_spin = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_cutoff_spin"));
    transparency_cutoff_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "transparency_cutoff_adjustment"));
    transparent_color = GTK_WIDGET(gtk_builder_get_object(builder, "transparent_color"));
    transparent_color_box = GTK_WIDGET(gtk_builder_get_object(builder, "transparent_color_box"));
    compositing_color = GTK_WIDGET(gtk_builder_get_object(builder, "compositing_color"));
    compositing_color_box = GTK_WIDGET(gtk_builder_get_object(builder, "compositing_color_box"));
    page_format_box = GTK_WIDGET(gtk_builder_get_object(builder, "page_format_box"));
    page_format_single_page_button = GTK_WIDGET(gtk_builder_get_object(builder, "page_format_single_page_button"));
    page_format_multipage_button = GTK_WIDGET(gtk_builder_get_object(builder, "page_format_multipage_button"));

    /* Show page format options only if document has more than one layer */
    if (page_format_box) {
        gtk_widget_set_visible(page_format_box, (layer_count > 1));
    }

    /* Depth and palette size will be shown/hidden based on color format selection */

    /* Set up color compression radio group */
    color_compression_buttons[0] = GTK_TOGGLE_BUTTON(color_compression_auto_button);
    color_compression_buttons[1] = GTK_TOGGLE_BUTTON(color_compression_lzw_button);
    color_compression_buttons[2] = GTK_TOGGLE_BUTTON(color_compression_zip_button);
    color_compression_buttons[3] = GTK_TOGGLE_BUTTON(color_compression_none_button);
    setup_toggle_button_group(color_compression_buttons, 4);

    /* Set up monochrome compression radio group */
    monochrome_compression_buttons[0] = GTK_TOGGLE_BUTTON(monochrome_compression_auto_button);
    monochrome_compression_buttons[1] = GTK_TOGGLE_BUTTON(monochrome_compression_fax4_button);
    monochrome_compression_buttons[2] = GTK_TOGGLE_BUTTON(monochrome_compression_fax3_button);
    monochrome_compression_buttons[3] = GTK_TOGGLE_BUTTON(monochrome_compression_lzw_button);
    monochrome_compression_buttons[4] = GTK_TOGGLE_BUTTON(monochrome_compression_none_button);
    setup_toggle_button_group(monochrome_compression_buttons, 5);

    /* Set up page format radio group */
    page_format_buttons[0] = GTK_TOGGLE_BUTTON(page_format_single_page_button);
    page_format_buttons[1] = GTK_TOGGLE_BUTTON(page_format_multipage_button);
    setup_toggle_button_group(page_format_buttons, 2);

    /* Set up visibility update callbacks BEFORE setting any values */
    /* Note: Array order matches update_widget_visibility parameter order */
    GtkWidget* visibility_widgets[8] = {
        color_format_combo,                    /* widgets[0] - for color_format_combo parameter */
        GTK_WIDGET(transparency_format_combo), /* widgets[1] - for transparency_format_combo parameter */
        color_format_depth_combo,              /* widgets[2] - for depth_combo parameter */
        color_format_depth_box,                /* widgets[3] - for depth_box parameter */
        color_format_palette_size_box,         /* widgets[4] - for palette_size_box parameter */
        transparency_format_cutoff_box,        /* widgets[5] - for transparency_format_cutoff_box parameter */
        transparent_color_box,                 /* widgets[6] - for transparent_color_box parameter */
        compositing_color_box                  /* widgets[7] - for compositing_color_box parameter */
    };

    if (color_format_combo && transparency_format_combo) {
        g_signal_connect(color_format_combo, "changed", G_CALLBACK(on_color_format_changed), visibility_widgets);
        g_signal_connect(transparency_format_combo, "changed", G_CALLBACK(on_transparency_format_changed), visibility_widgets);
        if (color_format_depth_combo) {
            g_signal_connect(color_format_depth_combo, "changed", G_CALLBACK(on_depth_changed), visibility_widgets);
        }
    }

    /* Block signals while setting initial values to avoid triggering callbacks prematurely */
    if (color_format_combo) {
        g_signal_handlers_block_by_func(color_format_combo, G_CALLBACK(on_color_format_changed), visibility_widgets);
    }
    if (transparency_format_combo) {
        g_signal_handlers_block_by_func(transparency_format_combo, G_CALLBACK(on_transparency_format_changed), visibility_widgets);
    }
    if (color_format_depth_combo) {
        g_signal_handlers_block_by_func(color_format_depth_combo, G_CALLBACK(on_depth_changed), visibility_widgets);
    }

    /* Initialize dialog with current options if available */
    if (tiff_opts) {
        /* Set color compression */
        set_toggle_button_group(color_compression_buttons, 4, (int)tiff_opts->color_compression);

        /* Set monochrome compression */
        set_toggle_button_group(monochrome_compression_buttons, 5, (int)tiff_opts->monochrome_compression);

        /* Set color format */
        if (color_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_combo), (gint)tiff_opts->color_format);
        }

        /* Set depth (default to standard if not set) */
        if (color_format_depth_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_depth_combo), 1); /* standard */
        }

        /* Set palette size (default to 256 if not set) */
        if (palette_size_adjustment) {
            gtk_adjustment_set_value(palette_size_adjustment, 256.0);
        }

        /* Set transparency format */
        if (transparency_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(transparency_format_combo), (gint)tiff_opts->transparency_format);
        }

        /* Set transparency cutoff */
        if (transparency_cutoff_adjustment) {
            gtk_adjustment_set_value(transparency_cutoff_adjustment, (gdouble)tiff_opts->transparency_cutoff);
        }

        /* Set transparent color */
        if (transparent_color) {
            transparent_color_rgba.red = (gdouble)tiff_opts->transparency_color_r / 255.0;
            transparent_color_rgba.green = (gdouble)tiff_opts->transparency_color_g / 255.0;
            transparent_color_rgba.blue = (gdouble)tiff_opts->transparency_color_b / 255.0;
            transparent_color_rgba.alpha = 1.0;
            transparent_color_data.color = &transparent_color_rgba;
            transparent_color_data.button = transparent_color;
            update_color_button_appearance(transparent_color, &transparent_color_rgba);
            g_signal_connect(transparent_color, "clicked", G_CALLBACK(on_tiff_transparent_color_clicked), &transparent_color_data);
        }

        /* Set compositing color */
        if (compositing_color) {
            compositing_color_rgba.red = (gdouble)tiff_opts->compositing_color_r / 255.0;
            compositing_color_rgba.green = (gdouble)tiff_opts->compositing_color_g / 255.0;
            compositing_color_rgba.blue = (gdouble)tiff_opts->compositing_color_b / 255.0;
            compositing_color_rgba.alpha = 1.0;
            compositing_color_data.color = &compositing_color_rgba;
            compositing_color_data.button = compositing_color;
            update_color_button_appearance(compositing_color, &compositing_color_rgba);
            g_signal_connect(compositing_color, "clicked", G_CALLBACK(on_tiff_compositing_color_clicked), &compositing_color_data);
        }

        /* Set page format */
        if (page_format_single_page_button && page_format_multipage_button) {
            set_toggle_button_group(page_format_buttons, 2, (int)tiff_opts->page_format);
        }
    } else {
        /* Set defaults - matching PNG plugin defaults */
        set_toggle_button_group(color_compression_buttons, 4, 0);      /* auto */
        set_toggle_button_group(monochrome_compression_buttons, 5, 0); /* auto */
        if (color_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_combo), 0); /* auto */
        }
        if (transparency_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(transparency_format_combo), 0); /* auto */
        }
        if (transparency_cutoff_adjustment) {
            gtk_adjustment_set_value(transparency_cutoff_adjustment, 64.0); /* Default cutoff: 64 (same as PNG) */
        }
        if (color_format_depth_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_depth_combo), 1); /* standard */
        }
        if (palette_size_adjustment) {
            gtk_adjustment_set_value(palette_size_adjustment, 256.0);
        }
        if (page_format_single_page_button && page_format_multipage_button) {
            set_toggle_button_group(page_format_buttons, 2, 0); /* single page (default) */
        }

        if (transparent_color) {
            transparent_color_rgba.red = 0.0;
            transparent_color_rgba.green = 0.0;
            transparent_color_rgba.blue = 0.0;
            transparent_color_rgba.alpha = 1.0;
            transparent_color_data.color = &transparent_color_rgba;
            transparent_color_data.button = transparent_color;
            update_color_button_appearance(transparent_color, &transparent_color_rgba);
            g_signal_connect(transparent_color, "clicked", G_CALLBACK(on_tiff_transparent_color_clicked), &transparent_color_data);
        }

        if (compositing_color) {
            compositing_color_rgba.red = 0.0;
            compositing_color_rgba.green = 0.0;
            compositing_color_rgba.blue = 0.0;
            compositing_color_rgba.alpha = 1.0;
            compositing_color_data.color = &compositing_color_rgba;
            compositing_color_data.button = compositing_color;
            update_color_button_appearance(compositing_color, &compositing_color_rgba);
            g_signal_connect(compositing_color, "clicked", G_CALLBACK(on_tiff_compositing_color_clicked), &compositing_color_data);
        }
    }

    if (transparent_color) {
        ui_utils_widget_set_hand_cursor(transparent_color);
    }
    if (compositing_color) {
        ui_utils_widget_set_hand_cursor(compositing_color);
    }

    /* Unblock signals after all values are set */
    if (color_format_combo) {
        g_signal_handlers_unblock_by_func(color_format_combo, G_CALLBACK(on_color_format_changed), visibility_widgets);
    }
    if (transparency_format_combo) {
        g_signal_handlers_unblock_by_func(transparency_format_combo, G_CALLBACK(on_transparency_format_changed), visibility_widgets);
    }
    if (color_format_depth_combo) {
        g_signal_handlers_unblock_by_func(color_format_depth_combo, G_CALLBACK(on_depth_changed), visibility_widgets);
    }

    /* Update visibility BEFORE showing dialog to ensure correct initial state */
    if (color_format_combo && transparency_format_combo) {
        update_widget_visibility(visibility_widgets[0], visibility_widgets[1],
                                 visibility_widgets[2], visibility_widgets[3],
                                 visibility_widgets[4], visibility_widgets[5],
                                 visibility_widgets[6], visibility_widgets[7]);
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "tiff_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "tiff_options_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
    }

    /* Add "Embed ICC profile" checkbox when document has a non-sRGB original profile */
    if (doc && doc->original_icc_data && doc->original_icc_size > 0) {
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        icc_checkbox = gtk_check_button_new_with_label("Embed original ICC profile");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(icc_checkbox), FALSE);
        gtk_box_pack_end(GTK_BOX(content), icc_checkbox, FALSE, FALSE, 4);
    }

    /* Show the dialog */
    gtk_widget_show_all(dialog);

    /* Trigger changed signals to update visibility after show_all */
    /* This ensures all visibility logic runs properly with the current values */
    if (color_format_combo) {
        g_signal_emit_by_name(color_format_combo, "changed");
    }
    if (transparency_format_combo) {
        g_signal_emit_by_name(transparency_format_combo, "changed");
    }
    if (color_format_depth_combo) {
        g_signal_emit_by_name(color_format_depth_combo, "changed");
    }

    /* Set page format visibility again after show_all (which may override it) */
    if (page_format_box) {
        gtk_widget_set_visible(page_format_box, (layer_count > 1));
    }

    /* Run dialog */
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        /* Read values from dialog */
        if (tiff_opts) {
            /* Get color compression */
            tiff_opts->color_compression = (TIFFColorCompression)get_active_toggle_button(color_compression_buttons, 4);

            /* Get monochrome compression */
            tiff_opts->monochrome_compression = (TIFFMonochromeCompression)get_active_toggle_button(monochrome_compression_buttons, 5);

            /* Get color format */
            if (color_format_combo) {
                gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(color_format_combo));
                if (active >= 0) {
                    tiff_opts->color_format = (TIFFColorFormat)active;
                }
            }

            /* Get transparency format */
            if (transparency_format_combo) {
                gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(transparency_format_combo));
                if (active >= 0) {
                    tiff_opts->transparency_format = (TIFFTransparencyFormat)active;
                }
            }

            /* Get transparency cutoff */
            if (transparency_cutoff_adjustment) {
                tiff_opts->transparency_cutoff = (uint8_t)gtk_adjustment_get_value(transparency_cutoff_adjustment);
            }

            /* Get transparent color */
            if (transparent_color) {
                tiff_opts->transparency_color_r = (uint8_t)(transparent_color_rgba.red * 255.0 + 0.5);
                tiff_opts->transparency_color_g = (uint8_t)(transparent_color_rgba.green * 255.0 + 0.5);
                tiff_opts->transparency_color_b = (uint8_t)(transparent_color_rgba.blue * 255.0 + 0.5);
            }

            /* Get compositing color */
            if (compositing_color) {
                tiff_opts->compositing_color_r = (uint8_t)(compositing_color_rgba.red * 255.0 + 0.5);
                tiff_opts->compositing_color_g = (uint8_t)(compositing_color_rgba.green * 255.0 + 0.5);
                tiff_opts->compositing_color_b = (uint8_t)(compositing_color_rgba.blue * 255.0 + 0.5);
            }

            /* Get page format */
            if (page_format_single_page_button && page_format_multipage_button) {
                tiff_opts->page_format = (TIFFPageFormat)get_active_toggle_button(page_format_buttons, 2);
            }
        }
        if (icc_checkbox) {
            opts->preserve_icc_profile =
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(icc_checkbox)) ? true : false;
        }
        result = TRUE;
    }

    /* Clean up */
    gtk_widget_destroy(dialog);
    g_object_unref(builder);

    return result;
}

#else /* HAVE_LIBTIFF not defined */

gboolean tiff_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    (void)parent;
    (void)opts;
    (void)doc;
    /* TIFF support not available */
    return FALSE;
}

#endif /* HAVE_LIBTIFF */
