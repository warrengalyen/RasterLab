#include "ui/dialogs/formats/avif_options_dialog.h"
#include "plugins/plugin_avif.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include "i18n.h"
#include "debug_logger.h"

#if HAVE_LIBHEIF && HAVE_LIBAOM

static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

gboolean avif_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* quality_scale;
    GtkWidget* quality_spin;
    GtkAdjustment* quality_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    AVIFSaveOptions* avif_opts = NULL;
    GtkWidget* icc_checkbox = NULL;

    if (!opts) {
        return FALSE;
    }

    if (opts->plugin_data) {
        avif_opts = (AVIFSaveOptions*)opts->plugin_data;
    }

    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/avif_options_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load avif_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "avif_options_dialog"));
    if (!dialog) {
        debug_log("WRN", "Failed to get avif_options_dialog from builder");
        g_object_unref(builder);
        return FALSE;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }

    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);

    if (GTK_IS_WINDOW(dialog)) {
        const gchar* title = gtk_window_get_title(GTK_WINDOW(dialog));
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : "AVIF Options");
    }

    quality_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "quality_adjustment"));
    quality_scale = GTK_WIDGET(gtk_builder_get_object(builder, "quality_scale"));
    quality_spin = GTK_WIDGET(gtk_builder_get_object(builder, "quality_spin"));

    if (!quality_adjustment) {
        debug_log("WRN", "Failed to get quality_adjustment from avif_options_dialog");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return FALSE;
    }

    /* Ensure range 0-63, default 63 */
    gtk_adjustment_configure(quality_adjustment, 63.0, 0.0, 63.0, 1.0, 10.0, 0.0);
    if (quality_scale) {
        gtk_range_set_adjustment(GTK_RANGE(quality_scale), quality_adjustment);
    }
    if (quality_spin) {
        gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(quality_spin), quality_adjustment);
    }

    /* Initialize from options */
    if (avif_opts) {
        gdouble q = (gdouble)avif_opts->quality;
        if (q < 0)
            q = 63.0;
        if (q > 63)
            q = 63.0;
        gtk_adjustment_set_value(quality_adjustment, q);
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "avif_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "avif_options_cancel_button"));
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

    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        if (avif_opts) {
            avif_opts->quality = (int)gtk_adjustment_get_value(quality_adjustment);
            if (avif_opts->quality < 0)
                avif_opts->quality = 0;
            if (avif_opts->quality > 63)
                avif_opts->quality = 63;
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

#else

gboolean avif_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    (void)parent;
    (void)opts;
    (void)doc;
    return FALSE;
}

#endif /* HAVE_LIBHEIF && HAVE_LIBAOM */
