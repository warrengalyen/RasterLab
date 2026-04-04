#include "ui/dialogs/formats/jpeg_options_dialog.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include "debug_logger.h"

#ifdef HAVE_LIBJPEG
#include <jpeglib.h>
#include "i18n.h"

/* Forward declare JPEG types from plugin_jpeg.c */

/* Callback functions for toggle button groups */
static void on_compression_method_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget** buttons = (GtkWidget**)user_data;
    if (gtk_toggle_button_get_active(button)) {
        /* Deactivate other buttons in the group */
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_compression_method_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_compression_method_toggled), user_data);
            }
        }
    }
}

static void on_subsampling_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget** buttons = (GtkWidget**)user_data;
    if (gtk_toggle_button_get_active(button)) {
        /* Deactivate other buttons in the group */
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_subsampling_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_subsampling_toggled), user_data);
            }
        }
    }
}

static void on_depth_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget** buttons = (GtkWidget**)user_data;
    if (gtk_toggle_button_get_active(button)) {
        /* Deactivate other buttons in the group */
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_depth_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_depth_toggled), user_data);
            }
        }
    }
}

/* Button clicked callback to emit dialog response */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/* Forward declarations for quality preset/value synchronization */
static void on_quality_value_changed(GtkAdjustment* adjustment, gpointer user_data);

/* Callback when quality preset combo selection changes */
static void on_quality_preset_changed(GtkComboBox* combo, gpointer user_data) {
    GtkAdjustment* adjustment = GTK_ADJUSTMENT(user_data);
    const gchar* active_id = gtk_combo_box_get_active_id(combo);
    gdouble quality = 92.0; /* default */

    if (active_id) {
        if (g_strcmp0(active_id, "perfect") == 0) {
            quality = 99.0;
        } else if (g_strcmp0(active_id, "excellent") == 0) {
            quality = 92.0;
        } else if (g_strcmp0(active_id, "good") == 0) {
            quality = 80.0;
        } else if (g_strcmp0(active_id, "average") == 0) {
            quality = 65.0;
        }
        /* "custom" doesn't change the value */
    }

    /* Block signals to prevent triggering the quality changed handler */
    g_signal_handlers_block_by_func(adjustment, G_CALLBACK(on_quality_value_changed), combo);
    gtk_adjustment_set_value(adjustment, quality);
    g_signal_handlers_unblock_by_func(adjustment, G_CALLBACK(on_quality_value_changed), combo);
}

/* Callback when quality value changes (from scale or spin button) */
static void on_quality_value_changed(GtkAdjustment* adjustment, gpointer user_data) {
    GtkComboBox* combo = GTK_COMBO_BOX(user_data);
    gdouble quality = gtk_adjustment_get_value(adjustment);

    /* Set to custom if value doesn't match any preset */
    gboolean is_preset = FALSE;
    if (quality >= 98.5 && quality <= 99.5) {
        /* perfect (99) */
        gtk_combo_box_set_active_id(combo, "perfect");
        is_preset = TRUE;
    } else if (quality >= 89.5 && quality <= 94.5) {
        /* excellent (92) */
        gtk_combo_box_set_active_id(combo, "excellent");
        is_preset = TRUE;
    } else if (quality >= 77.5 && quality <= 82.5) {
        /* good (80) */
        gtk_combo_box_set_active_id(combo, "good");
        is_preset = TRUE;
    } else if (quality >= 62.5 && quality <= 67.5) {
        /* average (65) */
        gtk_combo_box_set_active_id(combo, "average");
        is_preset = TRUE;
    }

    if (!is_preset) {
        /* Block signals to prevent triggering the preset changed handler */
        g_signal_handlers_block_by_func(combo, G_CALLBACK(on_quality_preset_changed), adjustment);
        gtk_combo_box_set_active_id(combo, "custom");
        g_signal_handlers_unblock_by_func(combo, G_CALLBACK(on_quality_preset_changed), adjustment);
    }
}

/* Forward declare JPEG types from plugin_jpeg.c */
typedef enum {
    JPEG_COMPRESSION_BASELINE = 0,
    JPEG_COMPRESSION_OPTIMIZED = 1,
    JPEG_COMPRESSION_PROGRESSIVE = 2
} JPEGCompressionMethod;

typedef enum {
    JPEG_SUBSAMPLING_NONE = 0,
    JPEG_SUBSAMPLING_LOW = 1,
    JPEG_SUBSAMPLING_MEDIUM = 2,
    JPEG_SUBSAMPLING_HIGH = 3
} JPEGChromaSubsampling;

typedef enum {
    JPEG_COLOR_AUTO = 0,
    JPEG_COLOR_RGB = 1,
    JPEG_COLOR_GRAYSCALE = 2
} JPEGColorDepth;

typedef struct {
    int32_t quality;
    JPEGCompressionMethod compression_method;
    JPEGChromaSubsampling chroma_subsampling;
    JPEGColorDepth color_depth;
    bool embed_thumbnail;
    uint32_t reserved[2];
} JPEGSaveOptions;

/**
 * Show JPEG save options dialog
 */
gboolean jpeg_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* quality_preset_combo;
    GtkWidget* quality_scale;
    GtkWidget* quality_spin;
    GtkWidget* compression_baseline_button;
    GtkWidget* compression_optimized_button;
    GtkWidget* compression_progressive_button;
    GtkWidget* subsampling_none_button;
    GtkWidget* subsampling_low_button;
    GtkWidget* subsampling_medium_button;
    GtkWidget* subsampling_high_button;
    GtkWidget* depth_auto_button;
    GtkWidget* depth_color_button;
    GtkWidget* depth_grayscale_button;
    GtkWidget* embed_thumbnail_checkbox;
    GtkWidget* icc_checkbox = NULL;
    GtkAdjustment* quality_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    JPEGSaveOptions* jpeg_opts = NULL;

    if (!opts) {
        return FALSE;
    }

    /* Get JPEG options from plugin_data */
    if (opts->plugin_data) {
        jpeg_opts = (JPEGSaveOptions*)opts->plugin_data;
    }

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/jpeg_options_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load jpeg_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    /* Get dialog widget */
    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "jpeg_options_dialog"));
    if (!dialog) {
        debug_log("WRN", "Failed to get jpeg_options_dialog from builder");
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
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : "JPEG Options");
    }

    /* Get all widgets */
    quality_preset_combo = GTK_WIDGET(gtk_builder_get_object(builder, "quality_preset_combo"));
    quality_scale = GTK_WIDGET(gtk_builder_get_object(builder, "quality_scale"));
    quality_spin = GTK_WIDGET(gtk_builder_get_object(builder, "quality_spin"));
    compression_baseline_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_baseline_button"));
    compression_optimized_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_optimized_button"));
    compression_progressive_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_progressive_button"));
    subsampling_none_button = GTK_WIDGET(gtk_builder_get_object(builder, "chroma_subsampling_none_button"));
    subsampling_low_button = GTK_WIDGET(gtk_builder_get_object(builder, "chroma_subsampling_low_button"));
    subsampling_medium_button = GTK_WIDGET(gtk_builder_get_object(builder, "chroma_subsampling_medium_button"));
    subsampling_high_button = GTK_WIDGET(gtk_builder_get_object(builder, "chroma_subsampling_high_button"));
    depth_auto_button = GTK_WIDGET(gtk_builder_get_object(builder, "depth_auto_button"));
    depth_color_button = GTK_WIDGET(gtk_builder_get_object(builder, "depth_color_button"));
    depth_grayscale_button = GTK_WIDGET(gtk_builder_get_object(builder, "depth_grayscale_button"));
    embed_thumbnail_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "embed_thumbnail_checkbox"));
    quality_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "quality_adjustment"));
    GtkWidget* notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
    GtkWidget* basic_content = GTK_WIDGET(gtk_builder_get_object(builder, "basic_content"));
    GtkWidget* advanced_content = GTK_WIDGET(gtk_builder_get_object(builder, "advanced_content"));

    if (!quality_scale || !quality_spin || !quality_adjustment) {
        debug_log("WRN", "Failed to get required widgets from jpeg_options_dialog");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return FALSE;
    }

    /* Set notebook tab labels */
    if (notebook && basic_content) {
        gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), basic_content, "basic");
    }
    if (notebook && advanced_content) {
        gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), advanced_content, "advanced");
    }

    /* Initialize quality adjustment (1-100) */
    gtk_adjustment_configure(quality_adjustment, 92.0, 1.0, 100.0, 1.0, 10.0, 0.0);
    gtk_range_set_adjustment(GTK_RANGE(quality_scale), quality_adjustment);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(quality_spin), quality_adjustment);

    /* Connect quality preset/value synchronization - connect AFTER setting initial value */
    if (quality_preset_combo && quality_adjustment) {
        /* Set initial combo box selection to match default quality (92 = excellent) */
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(quality_preset_combo), "excellent");

        /* Now connect signal handlers */
        g_signal_connect(quality_preset_combo, "changed", G_CALLBACK(on_quality_preset_changed), quality_adjustment);
        g_signal_connect(quality_adjustment, "value-changed", G_CALLBACK(on_quality_value_changed), quality_preset_combo);
    }

    /* Set up toggle button groups with signal handlers */
    GtkWidget* compression_buttons[4] = {NULL, NULL, NULL, NULL};
    GtkWidget* subsampling_buttons[5] = {NULL, NULL, NULL, NULL, NULL};
    GtkWidget* depth_buttons[4] = {NULL, NULL, NULL, NULL};

    if (compression_baseline_button && compression_optimized_button && compression_progressive_button) {
        compression_buttons[0] = compression_baseline_button;
        compression_buttons[1] = compression_optimized_button;
        compression_buttons[2] = compression_progressive_button;
        compression_buttons[3] = NULL;
        g_signal_connect(compression_baseline_button, "toggled", G_CALLBACK(on_compression_method_toggled), compression_buttons);
        g_signal_connect(compression_optimized_button, "toggled", G_CALLBACK(on_compression_method_toggled), compression_buttons);
        g_signal_connect(compression_progressive_button, "toggled", G_CALLBACK(on_compression_method_toggled), compression_buttons);
    }

    if (subsampling_none_button && subsampling_low_button && subsampling_medium_button && subsampling_high_button) {
        subsampling_buttons[0] = subsampling_none_button;
        subsampling_buttons[1] = subsampling_low_button;
        subsampling_buttons[2] = subsampling_medium_button;
        subsampling_buttons[3] = subsampling_high_button;
        subsampling_buttons[4] = NULL;
        g_signal_connect(subsampling_none_button, "toggled", G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
        g_signal_connect(subsampling_low_button, "toggled", G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
        g_signal_connect(subsampling_medium_button, "toggled", G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
        g_signal_connect(subsampling_high_button, "toggled", G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
    }

    if (depth_auto_button && depth_color_button && depth_grayscale_button) {
        depth_buttons[0] = depth_auto_button;
        depth_buttons[1] = depth_color_button;
        depth_buttons[2] = depth_grayscale_button;
        depth_buttons[3] = NULL;
        g_signal_connect(depth_auto_button, "toggled", G_CALLBACK(on_depth_toggled), depth_buttons);
        g_signal_connect(depth_color_button, "toggled", G_CALLBACK(on_depth_toggled), depth_buttons);
        g_signal_connect(depth_grayscale_button, "toggled", G_CALLBACK(on_depth_toggled), depth_buttons);
    }

    /* Initialize embed thumbnail checkbox */
    if (embed_thumbnail_checkbox) {
        if (jpeg_opts) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(embed_thumbnail_checkbox), jpeg_opts->embed_thumbnail ? TRUE : FALSE);
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(embed_thumbnail_checkbox), FALSE);
        }
    }

    /* Initialize dialog with current options if available */
    if (jpeg_opts) {
        /* Set quality - block signals to prevent combo box update during initialization */
        gdouble quality = (gdouble)jpeg_opts->quality;
        if (quality < 1.0)
            quality = 92.0; /* Default */
        if (quality > 100.0)
            quality = 100.0;
        if (quality_adjustment && quality_preset_combo) {
            g_signal_handlers_block_by_func(quality_adjustment, G_CALLBACK(on_quality_value_changed), quality_preset_combo);
            gtk_adjustment_set_value(quality_adjustment, quality);
            g_signal_handlers_unblock_by_func(quality_adjustment, G_CALLBACK(on_quality_value_changed), quality_preset_combo);
        }

        /* Set compression method (block signals to avoid triggering handlers during initialization) */
        if (compression_buttons[0]) {
            g_signal_handlers_block_by_func(compression_baseline_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_block_by_func(compression_optimized_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_block_by_func(compression_progressive_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);

            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_baseline_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_optimized_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_progressive_button), FALSE);

            switch (jpeg_opts->compression_method) {
                case JPEG_COMPRESSION_BASELINE:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_baseline_button), TRUE);
                    break;
                case JPEG_COMPRESSION_OPTIMIZED:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_optimized_button), TRUE);
                    break;
                case JPEG_COMPRESSION_PROGRESSIVE:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_progressive_button), TRUE);
                    break;
            }

            g_signal_handlers_unblock_by_func(compression_baseline_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_unblock_by_func(compression_optimized_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_unblock_by_func(compression_progressive_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
        }

        /* Set chroma subsampling */
        if (subsampling_buttons[0]) {
            g_signal_handlers_block_by_func(subsampling_none_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            g_signal_handlers_block_by_func(subsampling_low_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            g_signal_handlers_block_by_func(subsampling_medium_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            g_signal_handlers_block_by_func(subsampling_high_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);

            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_none_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_low_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_medium_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_high_button), FALSE);

            switch (jpeg_opts->chroma_subsampling) {
                case JPEG_SUBSAMPLING_NONE:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_none_button), TRUE);
                    break;
                case JPEG_SUBSAMPLING_LOW:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_low_button), TRUE);
                    break;
                case JPEG_SUBSAMPLING_MEDIUM:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_medium_button), TRUE);
                    break;
                case JPEG_SUBSAMPLING_HIGH:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_high_button), TRUE);
                    break;
            }

            g_signal_handlers_unblock_by_func(subsampling_none_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            g_signal_handlers_unblock_by_func(subsampling_low_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            g_signal_handlers_unblock_by_func(subsampling_medium_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            g_signal_handlers_unblock_by_func(subsampling_high_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
        }

        /* Set color depth */
        if (depth_buttons[0]) {
            g_signal_handlers_block_by_func(depth_auto_button, G_CALLBACK(on_depth_toggled), depth_buttons);
            g_signal_handlers_block_by_func(depth_color_button, G_CALLBACK(on_depth_toggled), depth_buttons);
            g_signal_handlers_block_by_func(depth_grayscale_button, G_CALLBACK(on_depth_toggled), depth_buttons);

            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_auto_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_color_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_grayscale_button), FALSE);

            switch (jpeg_opts->color_depth) {
                case JPEG_COLOR_AUTO:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_auto_button), TRUE);
                    break;
                case JPEG_COLOR_RGB:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_color_button), TRUE);
                    break;
                case JPEG_COLOR_GRAYSCALE:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_grayscale_button), TRUE);
                    break;
            }

            g_signal_handlers_unblock_by_func(depth_auto_button, G_CALLBACK(on_depth_toggled), depth_buttons);
            g_signal_handlers_unblock_by_func(depth_color_button, G_CALLBACK(on_depth_toggled), depth_buttons);
            g_signal_handlers_unblock_by_func(depth_grayscale_button, G_CALLBACK(on_depth_toggled), depth_buttons);
        }
    } else {
        /* Set defaults */
        gtk_adjustment_set_value(quality_adjustment, 92.0);
        if (compression_optimized_button && compression_buttons[0]) {
            g_signal_handlers_block_by_func(compression_optimized_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_optimized_button), TRUE);
            g_signal_handlers_unblock_by_func(compression_optimized_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
        }
        if (subsampling_low_button && subsampling_buttons[0]) {
            g_signal_handlers_block_by_func(subsampling_low_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(subsampling_low_button), TRUE);
            g_signal_handlers_unblock_by_func(subsampling_low_button, G_CALLBACK(on_subsampling_toggled), subsampling_buttons);
        }
        if (depth_auto_button && depth_buttons[0]) {
            g_signal_handlers_block_by_func(depth_auto_button, G_CALLBACK(on_depth_toggled), depth_buttons);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(depth_auto_button), TRUE);
            g_signal_handlers_unblock_by_func(depth_auto_button, G_CALLBACK(on_depth_toggled), depth_buttons);
        }
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "jpeg_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "jpeg_options_cancel_button"));
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

    /* All widgets are defined in Glade with visible=True, so just show the dialog */
    gtk_widget_show_all(dialog);

    /* Show dialog */
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        /* Read values from dialog */
        if (jpeg_opts) {
            /* Get quality */
            jpeg_opts->quality = (int32_t)gtk_adjustment_get_value(quality_adjustment);
            opts->quality = jpeg_opts->quality;

            /* Get compression method */
            if (compression_baseline_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_baseline_button))) {
                jpeg_opts->compression_method = JPEG_COMPRESSION_BASELINE;
            } else if (compression_progressive_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_progressive_button))) {
                jpeg_opts->compression_method = JPEG_COMPRESSION_PROGRESSIVE;
            } else {
                jpeg_opts->compression_method = JPEG_COMPRESSION_OPTIMIZED;
            }

            /* Get chroma subsampling */
            if (subsampling_none_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(subsampling_none_button))) {
                jpeg_opts->chroma_subsampling = JPEG_SUBSAMPLING_NONE;
            } else if (subsampling_medium_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(subsampling_medium_button))) {
                jpeg_opts->chroma_subsampling = JPEG_SUBSAMPLING_MEDIUM;
            } else if (subsampling_high_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(subsampling_high_button))) {
                jpeg_opts->chroma_subsampling = JPEG_SUBSAMPLING_HIGH;
            } else {
                jpeg_opts->chroma_subsampling = JPEG_SUBSAMPLING_LOW;
            }

            /* Get color depth */
            if (depth_color_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(depth_color_button))) {
                jpeg_opts->color_depth = JPEG_COLOR_RGB;
            } else if (depth_grayscale_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(depth_grayscale_button))) {
                jpeg_opts->color_depth = JPEG_COLOR_GRAYSCALE;
            } else {
                jpeg_opts->color_depth = JPEG_COLOR_AUTO;
            }

            /* Get thumbnail embedding setting */
            if (embed_thumbnail_checkbox) {
                jpeg_opts->embed_thumbnail = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(embed_thumbnail_checkbox)) ? true : false;
            } else {
                jpeg_opts->embed_thumbnail = FALSE;
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

#else /* HAVE_LIBJPEG not defined */

gboolean jpeg_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    (void)parent;
    (void)opts;
    (void)doc;
    /* JPEG support not available */
    return FALSE;
}

#endif /* HAVE_LIBJPEG */
