#include "ui/dialogs/formats/bmp_options_dialog.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include "i18n.h"
#include "debug_logger.h"

/* Forward declare BMP types from plugin_bmp.c */
typedef enum {
    BMP_COLOR_MODEL_AUTO = 0,
    BMP_COLOR_MODEL_COLOR_TRANSPARENCY = 1,
    BMP_COLOR_MODEL_COLOR_ONLY = 2,
    BMP_COLOR_MODEL_GRAYSCALE = 3
} BMPColorModel;

typedef enum {
    BMP_COLOR_DEPTH_32BPP = 0,
    BMP_COLOR_DEPTH_24BPP = 1,
    BMP_COLOR_DEPTH_16BPP = 2,
    BMP_COLOR_DEPTH_8BPP = 3
} BMPColorDepth;

typedef enum {
    BMP_GRAYSCALE_DEPTH_8BPP = 0,
    BMP_GRAYSCALE_DEPTH_4BPP = 1,
    BMP_GRAYSCALE_DEPTH_1BPP = 2
} BMPGrayscaleDepth;

typedef struct {
    bool flip_row_order;
    BMPColorModel color_model;
    BMPColorDepth color_depth;
    BMPGrayscaleDepth grayscale_depth;
    bool use_rle_compression;
    bool restrict_palette_size;
    int32_t palette_size;
    bool use_legacy_15bit;
    uint32_t reserved[1];
} BMPSaveOptions;

/* Update visibility of widgets based on current selections */
static void update_widget_visibility(GtkWidget* color_model_auto_button,
                                     GtkWidget* color_model_color_transparency_button,
                                     GtkWidget* color_model_color_button,
                                     GtkWidget* color_model_grayscale_button,
                                     GtkWidget* flip_row_order_checkbox,
                                     GtkWidget* color_depth_box,
                                     GtkWidget* grayscale_depth_box,
                                     GtkWidget* color_legacy_15bit_checkbox,
                                     GtkWidget* color_8bpp_option_box,
                                     GtkWidget* color_depth_32bpp_button,
                                     GtkWidget* color_depth_24bpp_button,
                                     GtkWidget* color_depth_16bpp_button,
                                     GtkWidget* color_dept_8bpp_button) {
    BMPColorModel color_model = BMP_COLOR_MODEL_AUTO;

    /* Determine current color model */
    if (color_model_auto_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_auto_button))) {
        color_model = BMP_COLOR_MODEL_AUTO;
    } else if (color_model_color_transparency_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_color_transparency_button))) {
        color_model = BMP_COLOR_MODEL_COLOR_TRANSPARENCY;
    } else if (color_model_color_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_color_button))) {
        color_model = BMP_COLOR_MODEL_COLOR_ONLY;
    } else if (color_model_grayscale_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_grayscale_button))) {
        color_model = BMP_COLOR_MODEL_GRAYSCALE;
    }

    /* Determine current color depth (if color only) */
    BMPColorDepth color_depth = BMP_COLOR_DEPTH_24BPP;
    if (color_depth_32bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_depth_32bpp_button))) {
        color_depth = BMP_COLOR_DEPTH_32BPP;
    } else if (color_depth_24bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_depth_24bpp_button))) {
        color_depth = BMP_COLOR_DEPTH_24BPP;
    } else if (color_depth_16bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_depth_16bpp_button))) {
        color_depth = BMP_COLOR_DEPTH_16BPP;
    } else if (color_dept_8bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_dept_8bpp_button))) {
        color_depth = BMP_COLOR_DEPTH_8BPP;
    }

    /* Rule 1: Always display color_model_box - no action needed */

    /* Rule 2: Always display flip_row_order_checkbox, except when auto color model is selected */
    if (flip_row_order_checkbox) {
        gtk_widget_set_visible(flip_row_order_checkbox, (color_model != BMP_COLOR_MODEL_AUTO));
    }

    /* Rule 3: If color model is set to auto, hide: grayscale_depth_box, color_depth_box, color_legacy_15bit_checkbox, color_8bpp_option_box */
    if (color_model == BMP_COLOR_MODEL_AUTO) {
        if (grayscale_depth_box) {
            gtk_widget_set_visible(grayscale_depth_box, FALSE);
        }
        if (color_depth_box) {
            gtk_widget_set_visible(color_depth_box, FALSE);
        }
        if (color_legacy_15bit_checkbox) {
            gtk_widget_set_visible(color_legacy_15bit_checkbox, FALSE);
        }
        if (color_8bpp_option_box) {
            gtk_widget_set_visible(color_8bpp_option_box, FALSE);
        }
    }
    /* Rule 4: If color model is set to color with transparency, hide everything but flip_row_order_checkbox */
    else if (color_model == BMP_COLOR_MODEL_COLOR_TRANSPARENCY) {
        if (grayscale_depth_box) {
            gtk_widget_set_visible(grayscale_depth_box, FALSE);
        }
        if (color_depth_box) {
            gtk_widget_set_visible(color_depth_box, FALSE);
        }
        if (color_legacy_15bit_checkbox) {
            gtk_widget_set_visible(color_legacy_15bit_checkbox, FALSE);
        }
        if (color_8bpp_option_box) {
            gtk_widget_set_visible(color_8bpp_option_box, FALSE);
        }
    }
    /* Rule 5: If color model is set to color only, show color_depth_box and flip_row_order_checkbox */
    else if (color_model == BMP_COLOR_MODEL_COLOR_ONLY) {
        if (color_depth_box) {
            gtk_widget_set_visible(color_depth_box, TRUE);
        }
        if (grayscale_depth_box) {
            gtk_widget_set_visible(grayscale_depth_box, FALSE);
        }

        /* Rule 6: If color only with 16-bpp is selected, show color_legacy_15bit_checkbox */
        if (color_legacy_15bit_checkbox) {
            gtk_widget_set_visible(color_legacy_15bit_checkbox, (color_depth == BMP_COLOR_DEPTH_16BPP));
        }

        /* Rule 7: If color only with 8-bpp is selected, show color_8bpp_option_box */
        if (color_8bpp_option_box) {
            gtk_widget_set_visible(color_8bpp_option_box, (color_depth == BMP_COLOR_DEPTH_8BPP));
        }
    }
    /* Rule 8: If grayscale color model is selected, show grayscale_depth_box */
    else if (color_model == BMP_COLOR_MODEL_GRAYSCALE) {
        if (grayscale_depth_box) {
            gtk_widget_set_visible(grayscale_depth_box, TRUE);
        }
        if (color_depth_box) {
            gtk_widget_set_visible(color_depth_box, FALSE);
        }
        if (color_legacy_15bit_checkbox) {
            gtk_widget_set_visible(color_legacy_15bit_checkbox, FALSE);
        }
        if (color_8bpp_option_box) {
            gtk_widget_set_visible(color_8bpp_option_box, FALSE);
        }
    }
}

/* Structure to hold both button group and all widgets for visibility updates */
typedef struct {
    GtkWidget** button_group;
    GtkWidget** all_widgets;
} ButtonGroupData;

/* Callback when color model button is toggled - ensures only one is active */
static void on_color_model_toggled(GtkToggleButton* button, gpointer user_data) {
    ButtonGroupData* data = (ButtonGroupData*)user_data;
    GtkWidget** buttons = data->button_group;

    if (gtk_toggle_button_get_active(button)) {
        /* Deactivate other buttons in the group */
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_color_model_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_color_model_toggled), user_data);
            }
        }
        /* Update visibility after selection changes */
        if (data->all_widgets) {
            update_widget_visibility(data->all_widgets[0], data->all_widgets[1], data->all_widgets[2], data->all_widgets[3],
                                     data->all_widgets[4], data->all_widgets[5], data->all_widgets[6], data->all_widgets[7],
                                     data->all_widgets[8], data->all_widgets[9], data->all_widgets[10], data->all_widgets[11], data->all_widgets[12]);
        }
    }
}

/* Callback when color depth button is toggled - ensures only one is active */
static void on_color_depth_toggled(GtkToggleButton* button, gpointer user_data) {
    ButtonGroupData* data = (ButtonGroupData*)user_data;
    GtkWidget** buttons = data->button_group;

    if (gtk_toggle_button_get_active(button)) {
        /* Deactivate other buttons in the group */
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_color_depth_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_color_depth_toggled), user_data);
            }
        }
        /* Update visibility after selection changes */
        if (data->all_widgets) {
            update_widget_visibility(data->all_widgets[0], data->all_widgets[1], data->all_widgets[2], data->all_widgets[3],
                                     data->all_widgets[4], data->all_widgets[5], data->all_widgets[6], data->all_widgets[7],
                                     data->all_widgets[8], data->all_widgets[9], data->all_widgets[10], data->all_widgets[11], data->all_widgets[12]);
        }
    }
}

/* Callback when grayscale depth button is toggled - ensures only one is active */
static void on_grayscale_depth_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget** buttons = (GtkWidget**)user_data;
    if (gtk_toggle_button_get_active(button)) {
        /* Deactivate other buttons in the group */
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_grayscale_depth_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_grayscale_depth_toggled), user_data);
            }
        }
    }
}

/* Callback for button clicks to emit dialog response */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/**
 * Show BMP save options dialog
 */
gboolean bmp_options_dialog_show(GtkWindow* parent, SaveOptions* opts) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* color_model_auto_button;
    GtkWidget* color_model_color_transparency_button;
    GtkWidget* color_model_color_button;
    GtkWidget* color_model_grayscale_button;
    GtkWidget* flip_row_order_checkbox;
    GtkWidget* color_depth_box;
    GtkWidget* color_depth_32bpp_button;
    GtkWidget* color_depth_24bpp_button;
    GtkWidget* color_depth_16bpp_button;
    GtkWidget* color_dept_8bpp_button;
    GtkWidget* grayscale_depth_box;
    GtkWidget* grayscale_depth_8bpp_button;
    GtkWidget* grayscale_depth_4bpp_button;
    GtkWidget* grayscale_depth_1bpp_button;
    GtkWidget* color_legacy_15bit_checkbox;
    GtkWidget* color_8bpp_option_box;
    GtkWidget* rle_compression_checkbox;
    GtkWidget* restrict_palette_size_checkbox;
    GtkWidget* palette_size_box;
    GtkWidget* palette_size_scale;
    GtkWidget* palette_size_spin;
    GtkAdjustment* palette_size_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    BMPSaveOptions* bmp_opts = NULL;

    if (!opts) {
        return FALSE;
    }

    /* Get BMP options from plugin_data */
    if (opts->plugin_data) {
        bmp_opts = (BMPSaveOptions*)opts->plugin_data;
    }

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/bmp_options_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load bmp_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    /* Get dialog widget */
    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "bmp_options_dialog"));
    if (!dialog) {
        debug_log("WRN", "Failed to get bmp_options_dialog from builder");
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
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : "BMP Options");
    }

    /* Get all widgets */
    color_model_auto_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_auto_button"));
    color_model_color_transparency_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_color_transparency_button"));
    color_model_color_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_color_button"));
    color_model_grayscale_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_grayscale_button"));
    flip_row_order_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "flip_row_order_checkbox"));
    color_depth_box = GTK_WIDGET(gtk_builder_get_object(builder, "color_depth_box"));
    color_depth_32bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_depth_32bpp_button"));
    color_depth_24bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_depth_24bpp_button"));
    color_depth_16bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_depth_16bpp_button"));
    color_dept_8bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "color_dept_8bpp_button"));
    grayscale_depth_box = GTK_WIDGET(gtk_builder_get_object(builder, "grayscale_depth_box"));
    grayscale_depth_8bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "grayscale_depth_8bpp_button"));
    grayscale_depth_4bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "grayscale_depth_4bpp_button"));
    grayscale_depth_1bpp_button = GTK_WIDGET(gtk_builder_get_object(builder, "grayscale_depth_1bpp_button"));
    color_legacy_15bit_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "color_legacy_15bit_checkbox"));
    color_8bpp_option_box = GTK_WIDGET(gtk_builder_get_object(builder, "color_8bpp_option_box"));
    rle_compression_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "rle_compression_checkbox"));
    restrict_palette_size_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "restrict_palette_size_checkbox"));
    palette_size_box = GTK_WIDGET(gtk_builder_get_object(builder, "palette_size_box"));
    palette_size_scale = GTK_WIDGET(gtk_builder_get_object(builder, "palette_size_scale"));
    palette_size_spin = GTK_WIDGET(gtk_builder_get_object(builder, "palette_size_spin"));
    palette_size_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "palette_size_adjustment"));

    if (!color_model_auto_button || !flip_row_order_checkbox) {
        debug_log("WRN", "Failed to get required widgets from bmp_options_dialog");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return FALSE;
    }

    /* Initialize values from options if available */
    /* First, ensure all toggle buttons start in inactive state to prevent multiple selections */
    if (color_model_auto_button && color_model_color_transparency_button &&
        color_model_color_button && color_model_grayscale_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_auto_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_color_transparency_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_color_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_grayscale_button), FALSE);
    }

    if (color_depth_32bpp_button && color_depth_24bpp_button &&
        color_depth_16bpp_button && color_dept_8bpp_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_32bpp_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_24bpp_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_16bpp_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_dept_8bpp_button), FALSE);
    }

    if (grayscale_depth_8bpp_button && grayscale_depth_4bpp_button && grayscale_depth_1bpp_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_8bpp_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_4bpp_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_1bpp_button), FALSE);
    }

    if (bmp_opts) {
        /* Set color model - first deactivate all, then activate the correct one */
        if (color_model_auto_button && color_model_color_transparency_button &&
            color_model_color_button && color_model_grayscale_button) {
            /* Set all to inactive first */
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_auto_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_color_transparency_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_color_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_grayscale_button), FALSE);

            /* Then activate the correct one */
            switch (bmp_opts->color_model) {
                case BMP_COLOR_MODEL_AUTO:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_auto_button), TRUE);
                    break;
                case BMP_COLOR_MODEL_COLOR_TRANSPARENCY:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_color_transparency_button), TRUE);
                    break;
                case BMP_COLOR_MODEL_COLOR_ONLY:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_color_button), TRUE);
                    break;
                case BMP_COLOR_MODEL_GRAYSCALE:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_model_grayscale_button), TRUE);
                    break;
            }
        }

        /* Set flip row order */
        if (flip_row_order_checkbox) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(flip_row_order_checkbox), bmp_opts->flip_row_order);
        }

        /* Set color depth - first deactivate all, then activate the correct one */
        if (color_depth_32bpp_button && color_depth_24bpp_button &&
            color_depth_16bpp_button && color_dept_8bpp_button) {
            /* Set all to inactive first */
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_32bpp_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_24bpp_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_16bpp_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_dept_8bpp_button), FALSE);

            /* Then activate the correct one */
            switch (bmp_opts->color_depth) {
                case BMP_COLOR_DEPTH_32BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_32bpp_button), TRUE);
                    break;
                case BMP_COLOR_DEPTH_24BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_24bpp_button), TRUE);
                    break;
                case BMP_COLOR_DEPTH_16BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_depth_16bpp_button), TRUE);
                    break;
                case BMP_COLOR_DEPTH_8BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_dept_8bpp_button), TRUE);
                    break;
            }
        }

        /* Set grayscale depth - first deactivate all, then activate the correct one */
        if (grayscale_depth_8bpp_button && grayscale_depth_4bpp_button && grayscale_depth_1bpp_button) {
            /* Set all to inactive first */
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_8bpp_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_4bpp_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_1bpp_button), FALSE);

            /* Then activate the correct one */
            switch (bmp_opts->grayscale_depth) {
                case BMP_GRAYSCALE_DEPTH_8BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_8bpp_button), TRUE);
                    break;
                case BMP_GRAYSCALE_DEPTH_4BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_4bpp_button), TRUE);
                    break;
                case BMP_GRAYSCALE_DEPTH_1BPP:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grayscale_depth_1bpp_button), TRUE);
                    break;
            }
        }

        /* Set legacy 15-bit */
        if (color_legacy_15bit_checkbox) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(color_legacy_15bit_checkbox), bmp_opts->use_legacy_15bit);
        }

        /* Set RLE compression */
        if (rle_compression_checkbox) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rle_compression_checkbox), bmp_opts->use_rle_compression);
        }

        /* Set restrict palette size */
        if (restrict_palette_size_checkbox) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(restrict_palette_size_checkbox), bmp_opts->restrict_palette_size);
        }

        /* Set palette size */
        if (palette_size_adjustment) {
            gtk_adjustment_set_value(palette_size_adjustment, (gdouble)bmp_opts->palette_size);
        }
    }

    /* Set up all widgets array for visibility updates */
    GtkWidget* all_widgets[13];
    all_widgets[0] = color_model_auto_button;
    all_widgets[1] = color_model_color_transparency_button;
    all_widgets[2] = color_model_color_button;
    all_widgets[3] = color_model_grayscale_button;
    all_widgets[4] = flip_row_order_checkbox;
    all_widgets[5] = color_depth_box;
    all_widgets[6] = grayscale_depth_box;
    all_widgets[7] = color_legacy_15bit_checkbox;
    all_widgets[8] = color_8bpp_option_box;
    all_widgets[9] = color_depth_32bpp_button;
    all_widgets[10] = color_depth_24bpp_button;
    all_widgets[11] = color_depth_16bpp_button;
    all_widgets[12] = color_dept_8bpp_button;

    /* Set up color model button group */
    GtkWidget** color_model_buttons = g_malloc(sizeof(GtkWidget*) * 5);
    color_model_buttons[0] = NULL;
    color_model_buttons[1] = NULL;
    color_model_buttons[2] = NULL;
    color_model_buttons[3] = NULL;
    color_model_buttons[4] = NULL;

    ButtonGroupData* color_model_data = g_malloc(sizeof(ButtonGroupData));
    color_model_data->button_group = color_model_buttons;
    color_model_data->all_widgets = all_widgets;

    /* Store data pointer on dialog for cleanup */
    g_object_set_data_full(G_OBJECT(dialog), "color-model-data", color_model_data, (GDestroyNotify)g_free);
    g_object_set_data_full(G_OBJECT(dialog), "color-model-buttons", color_model_buttons, (GDestroyNotify)g_free);

    if (color_model_auto_button && color_model_color_transparency_button &&
        color_model_color_button && color_model_grayscale_button) {
        color_model_buttons[0] = color_model_auto_button;
        color_model_buttons[1] = color_model_color_transparency_button;
        color_model_buttons[2] = color_model_color_button;
        color_model_buttons[3] = color_model_grayscale_button;
        color_model_buttons[4] = NULL;

        g_signal_connect(color_model_auto_button, "toggled", G_CALLBACK(on_color_model_toggled), color_model_data);
        g_signal_connect(color_model_color_transparency_button, "toggled", G_CALLBACK(on_color_model_toggled), color_model_data);
        g_signal_connect(color_model_color_button, "toggled", G_CALLBACK(on_color_model_toggled), color_model_data);
        g_signal_connect(color_model_grayscale_button, "toggled", G_CALLBACK(on_color_model_toggled), color_model_data);
    }

    /* Set up color depth button group */
    GtkWidget** color_depth_buttons = g_malloc(sizeof(GtkWidget*) * 5);
    color_depth_buttons[0] = NULL;
    color_depth_buttons[1] = NULL;
    color_depth_buttons[2] = NULL;
    color_depth_buttons[3] = NULL;
    color_depth_buttons[4] = NULL;

    ButtonGroupData* color_depth_data = g_malloc(sizeof(ButtonGroupData));
    color_depth_data->button_group = color_depth_buttons;
    color_depth_data->all_widgets = all_widgets;

    /* Store data pointer on dialog for cleanup */
    g_object_set_data_full(G_OBJECT(dialog), "color-depth-data", color_depth_data, (GDestroyNotify)g_free);
    g_object_set_data_full(G_OBJECT(dialog), "color-depth-buttons", color_depth_buttons, (GDestroyNotify)g_free);

    if (color_depth_32bpp_button && color_depth_24bpp_button &&
        color_depth_16bpp_button && color_dept_8bpp_button) {
        color_depth_buttons[0] = color_depth_32bpp_button;
        color_depth_buttons[1] = color_depth_24bpp_button;
        color_depth_buttons[2] = color_depth_16bpp_button;
        color_depth_buttons[3] = color_dept_8bpp_button;
        color_depth_buttons[4] = NULL;

        g_signal_connect(color_depth_32bpp_button, "toggled", G_CALLBACK(on_color_depth_toggled), color_depth_data);
        g_signal_connect(color_depth_24bpp_button, "toggled", G_CALLBACK(on_color_depth_toggled), color_depth_data);
        g_signal_connect(color_depth_16bpp_button, "toggled", G_CALLBACK(on_color_depth_toggled), color_depth_data);
        g_signal_connect(color_dept_8bpp_button, "toggled", G_CALLBACK(on_color_depth_toggled), color_depth_data);
    }

    /* Set up grayscale depth button group */
    GtkWidget** grayscale_depth_buttons = g_malloc(sizeof(GtkWidget*) * 4);
    grayscale_depth_buttons[0] = NULL;
    grayscale_depth_buttons[1] = NULL;
    grayscale_depth_buttons[2] = NULL;
    grayscale_depth_buttons[3] = NULL;

    /* Store buttons array on dialog for cleanup */
    g_object_set_data_full(G_OBJECT(dialog), "grayscale-depth-buttons", grayscale_depth_buttons, (GDestroyNotify)g_free);

    if (grayscale_depth_8bpp_button && grayscale_depth_4bpp_button && grayscale_depth_1bpp_button) {
        grayscale_depth_buttons[0] = grayscale_depth_8bpp_button;
        grayscale_depth_buttons[1] = grayscale_depth_4bpp_button;
        grayscale_depth_buttons[2] = grayscale_depth_1bpp_button;
        grayscale_depth_buttons[3] = NULL;

        g_signal_connect(grayscale_depth_8bpp_button, "toggled", G_CALLBACK(on_grayscale_depth_toggled), grayscale_depth_buttons);
        g_signal_connect(grayscale_depth_4bpp_button, "toggled", G_CALLBACK(on_grayscale_depth_toggled), grayscale_depth_buttons);
        g_signal_connect(grayscale_depth_1bpp_button, "toggled", G_CALLBACK(on_grayscale_depth_toggled), grayscale_depth_buttons);
    }

    /* Initialize restrict palette size checkbox */
    /* Note: palette_size_box is a child of color_8bpp_option_box, so it will be
     * automatically hidden/shown with its parent. The checkbox only controls the
     * option value, not the visibility of palette_size_box. */
    if (restrict_palette_size_checkbox) {
        gboolean restrict_active = bmp_opts ? bmp_opts->restrict_palette_size : FALSE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(restrict_palette_size_checkbox), restrict_active);
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "bmp_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "bmp_options_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
    }

    /* Initial visibility update */
    update_widget_visibility(color_model_auto_button, color_model_color_transparency_button,
                             color_model_color_button, color_model_grayscale_button,
                             flip_row_order_checkbox, color_depth_box, grayscale_depth_box,
                             color_legacy_15bit_checkbox, color_8bpp_option_box,
                             color_depth_32bpp_button, color_depth_24bpp_button,
                             color_depth_16bpp_button, color_dept_8bpp_button);

    /* Show dialog and get response */
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        /* Read values from dialog */
        if (bmp_opts) {
            /* Get color model */
            if (color_model_auto_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_auto_button))) {
                bmp_opts->color_model = BMP_COLOR_MODEL_AUTO;
            } else if (color_model_color_transparency_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_color_transparency_button))) {
                bmp_opts->color_model = BMP_COLOR_MODEL_COLOR_TRANSPARENCY;
            } else if (color_model_color_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_color_button))) {
                bmp_opts->color_model = BMP_COLOR_MODEL_COLOR_ONLY;
            } else if (color_model_grayscale_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_model_grayscale_button))) {
                bmp_opts->color_model = BMP_COLOR_MODEL_GRAYSCALE;
            }

            /* Get flip row order */
            if (flip_row_order_checkbox) {
                bmp_opts->flip_row_order = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(flip_row_order_checkbox)) ? true : false;
            }

            /* Get color depth */
            if (color_depth_32bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_depth_32bpp_button))) {
                bmp_opts->color_depth = BMP_COLOR_DEPTH_32BPP;
            } else if (color_depth_24bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_depth_24bpp_button))) {
                bmp_opts->color_depth = BMP_COLOR_DEPTH_24BPP;
            } else if (color_depth_16bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_depth_16bpp_button))) {
                bmp_opts->color_depth = BMP_COLOR_DEPTH_16BPP;
            } else if (color_dept_8bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_dept_8bpp_button))) {
                bmp_opts->color_depth = BMP_COLOR_DEPTH_8BPP;
            }

            /* Get grayscale depth */
            if (grayscale_depth_8bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grayscale_depth_8bpp_button))) {
                bmp_opts->grayscale_depth = BMP_GRAYSCALE_DEPTH_8BPP;
            } else if (grayscale_depth_4bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grayscale_depth_4bpp_button))) {
                bmp_opts->grayscale_depth = BMP_GRAYSCALE_DEPTH_4BPP;
            } else if (grayscale_depth_1bpp_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grayscale_depth_1bpp_button))) {
                bmp_opts->grayscale_depth = BMP_GRAYSCALE_DEPTH_1BPP;
            }

            /* Get legacy 15-bit */
            if (color_legacy_15bit_checkbox) {
                bmp_opts->use_legacy_15bit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(color_legacy_15bit_checkbox)) ? true : false;
            }

            /* Get RLE compression */
            if (rle_compression_checkbox) {
                bmp_opts->use_rle_compression = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rle_compression_checkbox)) ? true : false;
            }

            /* Get restrict palette size */
            if (restrict_palette_size_checkbox) {
                bmp_opts->restrict_palette_size = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(restrict_palette_size_checkbox)) ? true : false;
            }

            /* Get palette size */
            if (palette_size_adjustment) {
                bmp_opts->palette_size = (int32_t)gtk_adjustment_get_value(palette_size_adjustment);
            }
        }
        result = TRUE;
    }

    /* Memory cleanup is handled by g_object_set_data_full with destroy notify */
    gtk_widget_destroy(dialog);
    g_object_unref(builder);

    return result;
}
