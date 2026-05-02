/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/formats/jxl_options_dialog.h"
#include "plugins/plugin_jxl.h"
#include "ui/ui_utils.h"
#include "debug_logger.h"
#include "document.h"
#include "i18n.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

#ifdef HAVE_LIBJXL

/* Emit a dialog response when OK/Cancel is clicked */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/* Keep lossless/lossy buttons mutually exclusive */
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

/* Show/hide quality_controls_box depending on whether lossy is active */
static void on_lossy_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget* quality_controls_box = GTK_WIDGET(user_data);
    gtk_widget_set_visible(quality_controls_box, gtk_toggle_button_get_active(button));
}

/**
 * Show JPEG XL save options dialog
 */
gboolean jxl_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    GtkBuilder* builder = NULL;
    GtkWidget* dialog = NULL;
    GtkWidget* compression_lossless = NULL;
    GtkWidget* compression_lossy_button = NULL;
    GtkWidget* quality_controls_box = NULL;
    GtkWidget* quality_scale = NULL;
    GtkWidget* quality_spin = NULL;
    GtkWidget* effort_scale = NULL;
    GtkWidget* effort_spin = NULL;
    GtkAdjustment* quality_adjustment = NULL;
    GtkAdjustment* effort_adjustment = NULL;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    JXLSaveOptions* jxl_opts = NULL;

    (void)doc;

    if (!opts) {
        return FALSE;
    }

    if (opts->plugin_data) {
        jxl_opts = (JXLSaveOptions*)opts->plugin_data;
    }

    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/dialogs/jxl_options_dialog.glade", &error)) {
        debug_log("WRN", "JXL dialog: Failed to load jxl_options_dialog.glade: %s",
                  error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_object_unref(builder);
        return FALSE;
    }

    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "jxl_options_dialog"));
    if (!dialog) {
        debug_log("WRN", "JXL dialog: jxl_options_dialog widget not found");
        g_object_unref(builder);
        return FALSE;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }

    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);

    if (GTK_IS_WINDOW(dialog)) {
        ui_utils_set_header_bar(GTK_WINDOW(dialog), "JPEG XL Options");
    }

    /* Fetch widgets */
    compression_lossless    = GTK_WIDGET(gtk_builder_get_object(builder, "compression_lossless"));
    compression_lossy_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_lossy_button"));
    quality_controls_box    = GTK_WIDGET(gtk_builder_get_object(builder, "quality_controls_box"));
    quality_scale           = GTK_WIDGET(gtk_builder_get_object(builder, "quality_scale"));
    quality_spin            = GTK_WIDGET(gtk_builder_get_object(builder, "quality_spin"));
    effort_scale            = GTK_WIDGET(gtk_builder_get_object(builder, "effort_scale"));
    effort_spin             = GTK_WIDGET(gtk_builder_get_object(builder, "effect_spin")); /* glade uses "effect_spin" */
    quality_adjustment      = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "quality_adjustment"));
    effort_adjustment       = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "effort_adjustment"));

    if (!compression_lossless || !compression_lossy_button || !quality_controls_box) {
        debug_log("WRN", "JXL dialog: Required widgets missing");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return FALSE;
    }

    /* Wire adjustments to scale/spin widgets */
    if (quality_adjustment) {
        gtk_adjustment_configure(quality_adjustment, 90.0, 0.0, 100.0, 1.0, 10.0, 0.0);
        if (quality_scale) gtk_range_set_adjustment(GTK_RANGE(quality_scale), quality_adjustment);
        if (quality_spin)  gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(quality_spin), quality_adjustment);
    }
    if (effort_adjustment) {
        gtk_adjustment_configure(effort_adjustment, 7.0, 1.0, 9.0, 1.0, 1.0, 0.0);
        if (effort_scale) gtk_range_set_adjustment(GTK_RANGE(effort_scale), effort_adjustment);
        if (effort_spin)  gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(effort_spin), effort_adjustment);
    }

    /* Set up mutual exclusivity for lossless/lossy buttons */
    GtkWidget* compression_buttons[3] = {compression_lossless, compression_lossy_button, NULL};
    g_signal_connect(compression_lossless, "toggled",
                     G_CALLBACK(on_compression_toggled), compression_buttons);
    g_signal_connect(compression_lossy_button, "toggled",
                     G_CALLBACK(on_compression_toggled), compression_buttons);

    /* Lossy toggle controls quality_controls_box visibility */
    g_signal_connect(compression_lossy_button, "toggled",
                     G_CALLBACK(on_lossy_toggled), quality_controls_box);

    /* Populate from saved options */
    if (jxl_opts) {
        /* Block mutual-exclusivity callbacks to set both at once */
        g_signal_handlers_block_by_func(compression_lossless,
                                         G_CALLBACK(on_compression_toggled), compression_buttons);
        g_signal_handlers_block_by_func(compression_lossy_button,
                                         G_CALLBACK(on_compression_toggled), compression_buttons);

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossless),     jxl_opts->lossless);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossy_button), !jxl_opts->lossless);

        g_signal_handlers_unblock_by_func(compression_lossless,
                                           G_CALLBACK(on_compression_toggled), compression_buttons);
        g_signal_handlers_unblock_by_func(compression_lossy_button,
                                           G_CALLBACK(on_compression_toggled), compression_buttons);

        if (quality_adjustment) {
            gdouble q = (gdouble)jxl_opts->quality;
            if (q < 0.0)   q = 0.0;
            if (q > 100.0) q = 100.0;
            gtk_adjustment_set_value(quality_adjustment, q);
        }
        if (effort_adjustment) {
            gdouble e = (gdouble)jxl_opts->effort;
            if (e < 1.0) e = 1.0;
            if (e > 9.0) e = 9.0;
            gtk_adjustment_set_value(effort_adjustment, e);
        }
    } else {
        /* Defaults: lossless active */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossless), TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_lossy_button), FALSE);
        if (quality_adjustment) gtk_adjustment_set_value(quality_adjustment, 90.0);
        if (effort_adjustment)  gtk_adjustment_set_value(effort_adjustment, 7.0);
    }

    /* Connect OK / Cancel buttons */
    GtkWidget* ok_button     = GTK_WIDGET(gtk_builder_get_object(builder, "heic_options_ok_button"));
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

    /* Add "Embed ICC profile" checkbox when the document has a non-sRGB original profile */
    GtkWidget* icc_checkbox = NULL;
    if (doc && doc->original_icc_data && doc->original_icc_size > 0) {
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        icc_checkbox = gtk_check_button_new_with_label("Embed original ICC profile");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(icc_checkbox), FALSE);
        gtk_box_pack_end(GTK_BOX(content), icc_checkbox, FALSE, FALSE, 4);
    }

    gtk_widget_show_all(dialog);

    /* After show_all, apply visibility: quality_controls_box hidden in lossless mode */
    gboolean lossy_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_lossy_button));
    gtk_widget_set_visible(quality_controls_box, lossy_active);

    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        if (jxl_opts) {
            jxl_opts->lossless = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_lossless));
            if (quality_adjustment) {
                jxl_opts->quality = (int)gtk_adjustment_get_value(quality_adjustment);
                if (jxl_opts->quality < 0)   jxl_opts->quality = 0;
                if (jxl_opts->quality > 100) jxl_opts->quality = 100;
            }
            if (effort_adjustment) {
                jxl_opts->effort = (int)gtk_adjustment_get_value(effort_adjustment);
                if (jxl_opts->effort < 1) jxl_opts->effort = 1;
                if (jxl_opts->effort > 9) jxl_opts->effort = 9;
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

#else /* HAVE_LIBJXL not defined */

gboolean jxl_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    (void)parent;
    (void)opts;
    (void)doc;
    return FALSE;
}

#endif /* HAVE_LIBJXL */
