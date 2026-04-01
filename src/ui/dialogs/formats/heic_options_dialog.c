#include "ui/dialogs/formats/heic_options_dialog.h"
#include "document.h"
#include "plugins/plugin_heic.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include "i18n.h"

#ifdef HAVE_LIBHEIF

/* Button clicked callback to emit dialog response */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/* Callback when lossless/lossy toggles change - update quality_controls_box visibility */
static void on_quality_method_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget* quality_controls_box = GTK_WIDGET(user_data);
    gboolean lossy_active = gtk_toggle_button_get_active(button);
    /* quality_controls_box visible when lossy is selected */
    gtk_widget_set_visible(quality_controls_box, lossy_active);
}

/* Callback for mutual exclusivity of lossless/lossy toggle buttons */
static void on_compression_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget** buttons = (GtkWidget**)user_data;
    if (gtk_toggle_button_get_active(button)) {
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_compression_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_compression_toggled), user_data);
            }
        }
    }
}

/* Callback for mutual exclusivity of single frame/multiframe toggle buttons */
static void on_format_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget** buttons = (GtkWidget**)user_data;
    if (gtk_toggle_button_get_active(button)) {
        for (int i = 0; buttons[i] != NULL; i++) {
            if (GTK_WIDGET(button) != buttons[i]) {
                g_signal_handlers_block_by_func(buttons[i], G_CALLBACK(on_format_toggled), user_data);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(buttons[i], G_CALLBACK(on_format_toggled), user_data);
            }
        }
    }
}

/**
 * Show HEIC save options dialog
 */
gboolean heic_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* quality_controls_box;
    GtkWidget* image_format_box;
    GtkWidget* compression_lossless;
    GtkWidget* compression_lossy_button;
    GtkWidget* image_format_single_frame_button;
    GtkWidget* image_format_multiframe_button;
    GtkWidget* quality_scale;
    GtkWidget* quality_spin;
    GtkAdjustment* quality_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    GtkWidget* icc_checkbox = NULL;
    HEICSaveOptions* heic_opts = NULL;
    guint layer_count = 0;

    if (!opts) {
        return FALSE;
    }

    if (opts->plugin_data) {
        heic_opts = (HEICSaveOptions*)opts->plugin_data;
    }

    if (doc) {
        layer_count = document_get_layer_count(doc);
    }

    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/heic_options_dialog.glade", &error)) {
        g_warning("Failed to load heic_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "heic_options_dialog"));
    if (!dialog) {
        g_warning("Failed to get heic_options_dialog from builder");
        g_object_unref(builder);
        return FALSE;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }

    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);

    if (GTK_IS_WINDOW(dialog)) {
        const gchar* title = gtk_window_get_title(GTK_WINDOW(dialog));
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : "HEIC Options");
    }

    /* Get widgets */
    quality_controls_box = GTK_WIDGET(gtk_builder_get_object(builder, "quality_controls_box"));
    image_format_box = GTK_WIDGET(gtk_builder_get_object(builder, "image_format_box"));
    compression_lossless = GTK_WIDGET(gtk_builder_get_object(builder, "compression_lossless"));
    compression_lossy_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_lossy_button"));
    image_format_single_frame_button = GTK_WIDGET(gtk_builder_get_object(builder, "image_format_single_frame_button"));
    image_format_multiframe_button = GTK_WIDGET(gtk_builder_get_object(builder, "image_format_multiframe_button"));
    quality_scale = GTK_WIDGET(gtk_builder_get_object(builder, "quality_scale"));
    quality_spin = GTK_WIDGET(gtk_builder_get_object(builder, "quality_spin"));
    quality_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "quality_adjustment"));

    if (!quality_controls_box || !compression_lossless || !compression_lossy_button) {
        g_warning("Failed to get required widgets from heic_options_dialog");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return FALSE;
    }

    /* Initialize quality (1-100, default 90) */
    if (quality_adjustment) {
        gtk_adjustment_configure(quality_adjustment, 90.0, 0.0, 100.0, 1.0, 10.0, 0.0);
        if (quality_scale) {
            gtk_range_set_adjustment(GTK_RANGE(quality_scale), quality_adjustment);
        }
        if (quality_spin) {
            gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(quality_spin), quality_adjustment);
        }
    }

    /* Set up compression toggle group */
    GtkWidget* compression_buttons[3] = {compression_lossless, compression_lossy_button, NULL};
    g_signal_connect(compression_lossless, "toggled", G_CALLBACK(on_compression_toggled), compression_buttons);
    g_signal_connect(compression_lossy_button, "toggled", G_CALLBACK(on_compression_toggled), compression_buttons);

    /* When lossless/lossy changes, update quality_controls_box visibility */
    g_signal_connect(compression_lossy_button, "toggled", G_CALLBACK(on_quality_method_toggled), quality_controls_box);

    /* Set up format toggle group */
    if (image_format_single_frame_button && image_format_multiframe_button) {
        GtkWidget* format_buttons[3] = {image_format_single_frame_button, image_format_multiframe_button, NULL};
        g_signal_connect(image_format_single_frame_button, "toggled", G_CALLBACK(on_format_toggled), format_buttons);
        g_signal_connect(image_format_multiframe_button, "toggled", G_CALLBACK(on_format_toggled), format_buttons);
    }

    /* Initialize from options */
    if (heic_opts) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossless), heic_opts->lossless);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossy_button), !heic_opts->lossless);
        if (quality_adjustment) {
            gdouble q = (gdouble)heic_opts->quality;
            if (q < 0)
                q = 90.0;
            if (q > 100)
                q = 100.0;
            gtk_adjustment_set_value(quality_adjustment, q);
        }
        if (image_format_single_frame_button && image_format_multiframe_button) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(image_format_single_frame_button), !heic_opts->multiframe);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(image_format_multiframe_button), heic_opts->multiframe);
        }
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossless), TRUE);
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "heic_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "heic_options_cancel_button"));
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

    gtk_widget_show_all(dialog);

    /* Set visibility after show_all (show_all overrides previous visibility) */
    gtk_widget_set_visible(quality_controls_box, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_lossless)));
    if (image_format_box) {
        gtk_widget_set_visible(image_format_box, layer_count > 1);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        if (heic_opts) {
            heic_opts->lossless = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_lossless));
            heic_opts->quality = (int)gtk_adjustment_get_value(quality_adjustment);
            if (heic_opts->quality < 0)
                heic_opts->quality = 0;
            if (heic_opts->quality > 100)
                heic_opts->quality = 100;
            if (layer_count > 1 && image_format_multiframe_button) {
                heic_opts->multiframe = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(image_format_multiframe_button));
            } else {
                heic_opts->multiframe = FALSE;
            }
        }
        if (icc_checkbox) {
            opts->preserve_icc_profile =
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(icc_checkbox)) ? true : false;
        }
        result = TRUE;
    }

    gtk_widget_destroy(dialog);
    g_object_unref(builder);

    return result;
}

#else /* HAVE_LIBHEIF not defined */

gboolean heic_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    (void)parent;
    (void)opts;
    (void)doc;
    return FALSE;
}

#endif /* HAVE_LIBHEIF */
