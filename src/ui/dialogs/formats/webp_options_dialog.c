#include "ui/dialogs/formats/webp_options_dialog.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

#ifdef HAVE_LIBWEBP
#include <webp/encode.h>

/* Forward declare WebP types from plugin_webp.c */
typedef enum {
    WEBP_COMPRESSION_FAST = 0,     /* Fast compression (method 0) */
    WEBP_COMPRESSION_BALANCED = 1, /* Balanced compression (method 3) */
    WEBP_COMPRESSION_BEST = 2      /* Best compression (method 6) */
} WebPCompressionMethod;

typedef struct {
    /* Image type hint (WebPImageHint parameter) */
    WebPImageHint image_hint;

    /* Quality setting (0-100, where 0 = lowest quality, 100 = highest quality) */
    int32_t quality;

    /* Compression method (fast, balanced, best) */
    WebPCompressionMethod compression_method;

    /* Reserved for future use */
    uint32_t reserved[2];
} WebPSaveOptions;

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

/* Button clicked callback to emit dialog response */
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dialog = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dialog, response_id);
}

/**
 * Show WebP save options dialog
 */
gboolean webp_options_dialog_show(GtkWindow* parent, SaveOptions* opts) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* image_type_combo;
    GtkWidget* quality_scale;
    GtkWidget* quality_spin;
    GtkWidget* compression_fast_button;
    GtkWidget* compression_balanced_button;
    GtkWidget* compression_best_button;
    GtkWidget* image_type_box;
    GtkAdjustment* quality_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    WebPSaveOptions* webp_opts = NULL;

    if (!opts) {
        return FALSE;
    }

    /* Get WebP options from plugin_data */
    if (opts->plugin_data) {
        webp_opts = (WebPSaveOptions*)opts->plugin_data;
    }

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/webp_options_dialog.glade", &error)) {
        g_warning("Failed to load webp_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    /* Get dialog widget */
    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "webp_options_dialog"));
    if (!dialog) {
        g_warning("Failed to get webp_options_dialog from builder");
        g_object_unref(builder);
        return FALSE;
    }

    /* Set parent window */
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }

    /* Set default size */
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);

    /* Hide icon in titlebar */
    gtk_window_set_icon(GTK_WINDOW(dialog), NULL);

    /* Get all widgets */
    image_type_box = GTK_WIDGET(gtk_builder_get_object(builder, "image_type_box"));
    quality_scale = GTK_WIDGET(gtk_builder_get_object(builder, "quality_scale"));
    quality_spin = GTK_WIDGET(gtk_builder_get_object(builder, "quality_spin"));
    compression_fast_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_fast_button"));
    compression_balanced_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_balanced_button"));
    compression_best_button = GTK_WIDGET(gtk_builder_get_object(builder, "compression_best_button"));
    quality_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "quality_adjustment"));
    image_type_combo = NULL;

    if (!quality_scale || !quality_spin || !quality_adjustment) {
        g_warning("Failed to get required widgets from webp_options_dialog");
        g_object_unref(builder);
        gtk_widget_destroy(dialog);
        return FALSE;
    }

    /* Get image type combo box (it's a child of image_type_box) */
    if (image_type_box) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(image_type_box));
        if (children && g_list_length(children) >= 2) {
            /* Second child should be the combo box */
            image_type_combo = GTK_WIDGET(g_list_nth_data(children, 1));
            /* Ensure combo box has a text renderer if it doesn't already have one */
            if (image_type_combo && GTK_IS_COMBO_BOX(image_type_combo)) {
                GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(image_type_combo));
                if (!cells) {
                    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
                    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(image_type_combo), renderer, TRUE);
                    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(image_type_combo), renderer, "text", 0, NULL);
                } else {
                    g_list_free(cells);
                }
            }
        }
        if (children) {
            g_list_free(children);
        }
    }

    /* Initialize quality adjustment (1-100, default 75) */
    gtk_adjustment_configure(quality_adjustment, 75.0, 1.0, 100.0, 1.0, 10.0, 0.0);
    gtk_range_set_adjustment(GTK_RANGE(quality_scale), quality_adjustment);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(quality_spin), quality_adjustment);

    /* Set up toggle button group for compression method */
    GtkWidget* compression_buttons[4] = {NULL, NULL, NULL, NULL};

    if (compression_fast_button && compression_balanced_button && compression_best_button) {
        compression_buttons[0] = compression_fast_button;
        compression_buttons[1] = compression_balanced_button;
        compression_buttons[2] = compression_best_button;
        compression_buttons[3] = NULL;
        g_signal_connect(compression_fast_button, "toggled", G_CALLBACK(on_compression_method_toggled), compression_buttons);
        g_signal_connect(compression_balanced_button, "toggled", G_CALLBACK(on_compression_method_toggled), compression_buttons);
        g_signal_connect(compression_best_button, "toggled", G_CALLBACK(on_compression_method_toggled), compression_buttons);
    }

    /* Initialize dialog with current options if available */
    if (webp_opts) {
        /* Set quality */
        gdouble quality = (gdouble)webp_opts->quality;
        if (quality < 1.0)
            quality = 75.0; /* Default */
        if (quality > 100.0)
            quality = 100.0;
        gtk_adjustment_set_value(quality_adjustment, quality);

        /* Set image type hint */
        if (image_type_combo) {
            gint active = 0; /* Default to WEBP_HINT_DEFAULT */
            switch (webp_opts->image_hint) {
                case WEBP_HINT_DEFAULT:
                    active = 0;
                    break;
                case WEBP_HINT_PICTURE:
                    active = 1; /* indoor photo */
                    break;
                case WEBP_HINT_PHOTO:
                    active = 2; /* outdoor photo */
                    break;
                case WEBP_HINT_GRAPH:
                    active = 3; /* chart (or graph, both map to GRAPH) */
                    break;
                default:
                    active = 0;
                    break;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(image_type_combo), active);
        }

        /* Set compression method */
        if (compression_buttons[0]) {
            g_signal_handlers_block_by_func(compression_fast_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_block_by_func(compression_balanced_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_block_by_func(compression_best_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);

            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_fast_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_balanced_button), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_best_button), FALSE);

            switch (webp_opts->compression_method) {
                case WEBP_COMPRESSION_FAST:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_fast_button), TRUE);
                    break;
                case WEBP_COMPRESSION_BALANCED:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_balanced_button), TRUE);
                    break;
                case WEBP_COMPRESSION_BEST:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_best_button), TRUE);
                    break;
            }

            g_signal_handlers_unblock_by_func(compression_fast_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_unblock_by_func(compression_balanced_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            g_signal_handlers_unblock_by_func(compression_best_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
        }
    } else {
        /* Set defaults */
        gtk_adjustment_set_value(quality_adjustment, 75.0);
        if (image_type_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(image_type_combo), 0); /* WEBP_HINT_DEFAULT */
        }
        if (compression_balanced_button && compression_buttons[0]) {
            g_signal_handlers_block_by_func(compression_balanced_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compression_balanced_button), TRUE);
            g_signal_handlers_unblock_by_func(compression_balanced_button, G_CALLBACK(on_compression_method_toggled), compression_buttons);
        }
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "webp_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "webp_options_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
    }

    /* All widgets are defined in Glade with visible=True, so just show the dialog */
    gtk_widget_show_all(dialog);

    /* Show dialog */
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        /* Read values from dialog */
        if (webp_opts) {
            /* Get quality */
            webp_opts->quality = (int32_t)gtk_adjustment_get_value(quality_adjustment);
            opts->quality = webp_opts->quality;

            /* Get image type hint */
            if (image_type_combo) {
                gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(image_type_combo));
                switch (active) {
                    case 0: /* default */
                        webp_opts->image_hint = WEBP_HINT_DEFAULT;
                        break;
                    case 1: /* indoor photo */
                        webp_opts->image_hint = WEBP_HINT_PICTURE;
                        break;
                    case 2: /* outdoor photo */
                        webp_opts->image_hint = WEBP_HINT_PHOTO;
                        break;
                    case 3: /* chart */
                    case 4: /* graph */
                        webp_opts->image_hint = WEBP_HINT_GRAPH;
                        break;
                    default:
                        webp_opts->image_hint = WEBP_HINT_DEFAULT;
                        break;
                }
            } else {
                webp_opts->image_hint = WEBP_HINT_DEFAULT;
            }

            /* Get compression method */
            if (compression_fast_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_fast_button))) {
                webp_opts->compression_method = WEBP_COMPRESSION_FAST;
            } else if (compression_best_button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compression_best_button))) {
                webp_opts->compression_method = WEBP_COMPRESSION_BEST;
            } else {
                webp_opts->compression_method = WEBP_COMPRESSION_BALANCED;
            }
        }
        result = TRUE;
    }

    /* Clean up */
    gtk_widget_destroy(dialog);
    g_object_unref(builder);

    return result;
}

#else /* HAVE_LIBWEBP not defined */

gboolean webp_options_dialog_show(GtkWindow* parent, SaveOptions* opts) {
    (void)parent;
    (void)opts;
    /* WebP support not available */
    return FALSE;
}

#endif /* HAVE_LIBWEBP */
