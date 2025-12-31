#include "ui/dialogs/formats/png_options_dialog.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

#ifdef HAVE_LIBPNG

/* Forward declare PNG types from plugin_png.c */
typedef enum {
    PNG_COLOR_FORMAT_AUTO = 0,
    PNG_COLOR_FORMAT_COLOR = 1,
    PNG_COLOR_FORMAT_GRAYSCALE = 2
} PNGColorFormat;

typedef enum {
    PNG_TRANSPARENCY_AUTO = 0,
    PNG_TRANSPARENCY_FULL = 1,
    PNG_TRANSPARENCY_BINARY_CUTOFF = 2,
    PNG_TRANSPARENCY_BINARY_COLOR = 3,
    PNG_TRANSPARENCY_NONE = 4
} PNGTransparencyFormat;

typedef enum {
    PNG_DEPTH_HDR = 0,
    PNG_DEPTH_STANDARD = 1,
    PNG_DEPTH_INDEXED = 2
} PNGDepth;

typedef struct {
    int32_t compression_level;
    int32_t filter_type;
    int32_t compression_strategy;
    bool automatic_mode;
    bool embed_background_color;
    uint8_t background_color_r;
    uint8_t background_color_g;
    uint8_t background_color_b;
    PNGColorFormat color_format;
    PNGTransparencyFormat transparency_format;
    uint8_t transparency_cutoff;
    uint8_t transparency_color_r;
    uint8_t transparency_color_g;
    uint8_t transparency_color_b;
    PNGDepth depth;
    int palette_size;
    uint8_t compositing_color_r;
    uint8_t compositing_color_g;
    uint8_t compositing_color_b;
    uint32_t reserved[1];
} PNGSaveOptions;

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
                                     GtkWidget* transparency_cutoff_box,
                                     GtkWidget* transparent_color_box,
                                     GtkWidget* compositing_color_box) {
    gint color_format_active = gtk_combo_box_get_active(GTK_COMBO_BOX(color_format_combo));
    gint transparency_format_active = gtk_combo_box_get_active(GTK_COMBO_BOX(transparency_format_combo));
    gint depth_active = depth_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(depth_combo)) : PNG_DEPTH_STANDARD;

    /* Handle invalid selections (-1) by defaulting to AUTO */
    if (color_format_active < 0) {
        color_format_active = PNG_COLOR_FORMAT_AUTO;
    }
    if (transparency_format_active < 0) {
        transparency_format_active = PNG_TRANSPARENCY_AUTO;
    }
    if (depth_active < 0) {
        depth_active = PNG_DEPTH_STANDARD;
    }

    PNGColorFormat color_format = (PNGColorFormat)color_format_active;
    PNGTransparencyFormat transparency_format = (PNGTransparencyFormat)transparency_format_active;
    PNGDepth depth = (PNGDepth)depth_active;

    /* Depth: visible when color_format = color or grayscale */
    if (depth_box) {
        gtk_widget_set_visible(depth_box, (color_format == PNG_COLOR_FORMAT_COLOR || color_format == PNG_COLOR_FORMAT_GRAYSCALE));
    }

    /* Palette size: visible when color_format = grayscale AND depth = standard */
    if (palette_size_box) {
        gtk_widget_set_visible(palette_size_box, (color_format == PNG_COLOR_FORMAT_GRAYSCALE && depth == PNG_DEPTH_STANDARD));
    }

    /* Transparency cut-off: visible when transparency_format = binary cut-off */
    if (transparency_cutoff_box) {
        gtk_widget_set_visible(transparency_cutoff_box, (transparency_format == PNG_TRANSPARENCY_BINARY_CUTOFF));
    }

    /* Transparent color: visible when transparency_format = binary color */
    if (transparent_color_box) {
        gtk_widget_set_visible(transparent_color_box, (transparency_format == PNG_TRANSPARENCY_BINARY_COLOR));
    }

    /* Compositing color: visible when transparency_format = binary color, binary cut-off, or none */
    if (compositing_color_box) {
        gtk_widget_set_visible(compositing_color_box, (transparency_format == PNG_TRANSPARENCY_BINARY_COLOR ||
                                                       transparency_format == PNG_TRANSPARENCY_BINARY_CUTOFF ||
                                                       transparency_format == PNG_TRANSPARENCY_NONE));
    }
}

/* Callback when color format changes */
static void on_color_format_changed(GtkComboBox* combo, gpointer user_data) {
    GtkWidget** widgets = (GtkWidget**)user_data;
    update_widget_visibility(GTK_WIDGET(combo), widgets[0], widgets[1], widgets[2], widgets[3], widgets[4], widgets[5], widgets[6]);
}

/* Callback when transparency format changes */
static void on_transparency_format_changed(GtkComboBox* combo, gpointer user_data) {
    GtkWidget** widgets = (GtkWidget**)user_data;
    update_widget_visibility(widgets[0], GTK_WIDGET(combo), widgets[1], widgets[2], widgets[3], widgets[4], widgets[5], widgets[6]);
}

/* Callback when depth changes */
static void on_depth_changed(GtkComboBox* combo, gpointer user_data) {
    GtkWidget** widgets = (GtkWidget**)user_data;
    update_widget_visibility(widgets[0], widgets[1], GTK_WIDGET(combo), widgets[2], widgets[3], widgets[4], widgets[5], widgets[6]);
}

/**
 * Show PNG save options dialog
 */
gboolean png_options_dialog_show(GtkWindow* parent, SaveOptions* opts) {
    GtkBuilder* builder;
    GtkWidget* dialog;
    GtkWidget* compression_level_scale;
    GtkWidget* compression_level_spin;
    GtkWidget* compression_optimization_combo;
    GtkWidget* embed_bgcolor_checkbox;
    GtkWidget* embed_bgcolor_color;
    GtkWidget* color_format_combo;
    GtkWidget* color_format_depth_combo;
    GtkWidget* color_format_depth_box;
    GtkWidget* color_format_palette_size_box;
    GtkWidget* palette_size_scale;
    GtkWidget* palette_size_spin;
    GtkWidget* transparency_format_combo;
    GtkWidget* transparency_format_cutoff_box;
    GtkWidget* transparency_cutoff_scale;
    GtkWidget* transparency_cutoff_spin;
    GtkWidget* transparent_color;
    GtkWidget* transparent_color_box;
    GtkWidget* compositing_color;
    GtkWidget* compositing_color_box;
    GtkAdjustment* compression_level_adjustment;
    GtkAdjustment* palette_size_adjustment;
    GtkAdjustment* transparency_cutoff_adjustment;
    GError* error = NULL;
    gint response;
    gboolean result = FALSE;
    PNGSaveOptions* png_opts = NULL;
    GdkRGBA rgba;

    if (!opts) {
        return FALSE;
    }

    /* Get PNG options from plugin_data */
    if (opts->plugin_data) {
        png_opts = (PNGSaveOptions*)opts->plugin_data;
    }

    /* Load dialog from Glade resource */
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_resource(builder, "/ui/png_options_dialog.glade", &error)) {
        g_warning("Failed to load png_options_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return FALSE;
    }

    /* Get dialog widget */
    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "png_options_dialog"));
    if (!dialog) {
        g_warning("Failed to get png_options_dialog from builder");
        g_object_unref(builder);
        return FALSE;
    }

    /* Set parent window */
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }

    /* Set default size */
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, -1);

    /* Hide icon in titlebar */
    gtk_window_set_icon(GTK_WINDOW(dialog), NULL);

    /* Get all widgets */
    compression_level_scale = GTK_WIDGET(gtk_builder_get_object(builder, "compression_level_scale"));
    compression_level_spin = GTK_WIDGET(gtk_builder_get_object(builder, "compression_level_spin"));
    compression_level_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "compression_level_adjustment"));
    compression_optimization_combo = GTK_WIDGET(gtk_builder_get_object(builder, "compression_optimization_combo"));
    embed_bgcolor_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "embed_bgcolor_checkbox"));
    embed_bgcolor_color = GTK_WIDGET(gtk_builder_get_object(builder, "embed_bgcolor_color"));
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
    GtkWidget* notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
    GtkWidget* basic_content = GTK_WIDGET(gtk_builder_get_object(builder, "basic_content"));
    GtkWidget* advanced_content = GTK_WIDGET(gtk_builder_get_object(builder, "advanced_content"));

    if (!compression_level_scale || !compression_level_spin || !compression_level_adjustment) {
        g_warning("Failed to get required widgets from png_options_dialog");
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

    /* Set up compression level adjustment (0-12, but we'll use 0-9) */
    gtk_adjustment_configure(compression_level_adjustment, 9.0, 0.0, 9.0, 1.0, 10.0, 0.0);
    gtk_range_set_adjustment(GTK_RANGE(compression_level_scale), compression_level_adjustment);
    gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(compression_level_spin), compression_level_adjustment);

    /* Set up visibility update callbacks BEFORE setting any values */
    GtkWidget* visibility_widgets[7] = {
        GTK_WIDGET(transparency_format_combo), /* widgets[0] */
        color_format_depth_combo,              /* widgets[1] */
        color_format_depth_box,                /* widgets[2] */
        color_format_palette_size_box,         /* widgets[3] */
        transparency_format_cutoff_box,        /* widgets[4] */
        transparent_color_box,                 /* widgets[5] */
        compositing_color_box                  /* widgets[6] */
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
    if (png_opts) {
        /* Set compression level */
        gtk_adjustment_set_value(compression_level_adjustment, (gdouble)png_opts->compression_level);

        /* Set compression optimization */
        if (compression_optimization_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(compression_optimization_combo), png_opts->automatic_mode ? 0 : 1);
        }

        /* Set embed background color */
        if (embed_bgcolor_checkbox) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(embed_bgcolor_checkbox), png_opts->embed_background_color ? TRUE : FALSE);
        }
        if (embed_bgcolor_color) {
            rgba.red = (gdouble)png_opts->background_color_r / 255.0;
            rgba.green = (gdouble)png_opts->background_color_g / 255.0;
            rgba.blue = (gdouble)png_opts->background_color_b / 255.0;
            rgba.alpha = 1.0;
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(embed_bgcolor_color), &rgba);
        }

        /* Set color format */
        if (color_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_combo), (gint)png_opts->color_format);
        }

        /* Set depth */
        if (color_format_depth_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_depth_combo), (gint)png_opts->depth);
        }

        /* Set palette size */
        if (palette_size_adjustment) {
            gtk_adjustment_set_value(palette_size_adjustment, (gdouble)png_opts->palette_size);
        }

        /* Set transparency format */
        if (transparency_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(transparency_format_combo), (gint)png_opts->transparency_format);
        }

        /* Set transparency cutoff */
        if (transparency_cutoff_adjustment) {
            gtk_adjustment_set_value(transparency_cutoff_adjustment, (gdouble)png_opts->transparency_cutoff);
        }

        /* Set transparent color */
        if (transparent_color) {
            rgba.red = (gdouble)png_opts->transparency_color_r / 255.0;
            rgba.green = (gdouble)png_opts->transparency_color_g / 255.0;
            rgba.blue = (gdouble)png_opts->transparency_color_b / 255.0;
            rgba.alpha = 1.0;
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(transparent_color), &rgba);
        }

        /* Set compositing color */
        if (compositing_color) {
            rgba.red = (gdouble)png_opts->compositing_color_r / 255.0;
            rgba.green = (gdouble)png_opts->compositing_color_g / 255.0;
            rgba.blue = (gdouble)png_opts->compositing_color_b / 255.0;
            rgba.alpha = 1.0;
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(compositing_color), &rgba);
        }
    } else {
        /* Set defaults */
        gtk_adjustment_set_value(compression_level_adjustment, 9.0);
        if (compression_optimization_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(compression_optimization_combo), 0); /* automatic */
        }
        if (color_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_combo), 0); /* auto */
        }
        if (color_format_depth_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_format_depth_combo), 1); /* standard */
        }
        if (palette_size_adjustment) {
            gtk_adjustment_set_value(palette_size_adjustment, 256.0);
        }
        if (transparency_format_combo) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(transparency_format_combo), 0); /* auto */
        }
        if (transparency_cutoff_adjustment) {
            gtk_adjustment_set_value(transparency_cutoff_adjustment, 64.0);
        }
    }

    /* Unblock signals and update visibility after all values are set */
    if (color_format_combo) {
        g_signal_handlers_unblock_by_func(color_format_combo, G_CALLBACK(on_color_format_changed), visibility_widgets);
    }
    if (transparency_format_combo) {
        g_signal_handlers_unblock_by_func(transparency_format_combo, G_CALLBACK(on_transparency_format_changed), visibility_widgets);
    }
    if (color_format_depth_combo) {
        g_signal_handlers_unblock_by_func(color_format_depth_combo, G_CALLBACK(on_depth_changed), visibility_widgets);
    }

    /* Connect OK and Cancel buttons */
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "png_options_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "png_options_cancel_button"));
    if (ok_button) {
        g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    if (cancel_button) {
        g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), dialog);
    }

    /* Show the dialog first to ensure widgets are realized */
    gtk_widget_show_all(dialog);

    /* Explicitly update visibility after dialog is shown and widgets are realized */
    if (color_format_combo && transparency_format_combo) {
        update_widget_visibility(color_format_combo, transparency_format_combo,
                                 color_format_depth_combo, color_format_depth_box,
                                 color_format_palette_size_box, transparency_format_cutoff_box,
                                 transparent_color_box, compositing_color_box);
    }

    /* Show dialog */
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK) {
        /* Read values from dialog */
        if (png_opts) {
            /* Get compression level */
            png_opts->compression_level = (int32_t)gtk_adjustment_get_value(compression_level_adjustment);
            opts->compression_level = png_opts->compression_level;

            /* Get compression optimization */
            if (compression_optimization_combo) {
                gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(compression_optimization_combo));
                png_opts->automatic_mode = (active == 0);
                /* TODO: Map other optimization options to filter_type and compression_strategy */
            }

            /* Get embed background color */
            if (embed_bgcolor_checkbox) {
                png_opts->embed_background_color = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(embed_bgcolor_checkbox)) ? true : false;
            }
            if (embed_bgcolor_color) {
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(embed_bgcolor_color), &rgba);
                png_opts->background_color_r = (uint8_t)(rgba.red * 255.0 + 0.5);
                png_opts->background_color_g = (uint8_t)(rgba.green * 255.0 + 0.5);
                png_opts->background_color_b = (uint8_t)(rgba.blue * 255.0 + 0.5);
            }

            /* Get color format */
            if (color_format_combo) {
                png_opts->color_format = (PNGColorFormat)gtk_combo_box_get_active(GTK_COMBO_BOX(color_format_combo));
            }

            /* Get depth */
            if (color_format_depth_combo) {
                png_opts->depth = (PNGDepth)gtk_combo_box_get_active(GTK_COMBO_BOX(color_format_depth_combo));
            }

            /* Get palette size */
            if (palette_size_adjustment) {
                png_opts->palette_size = (int)gtk_adjustment_get_value(palette_size_adjustment);
            }

            /* Get transparency format */
            if (transparency_format_combo) {
                png_opts->transparency_format = (PNGTransparencyFormat)gtk_combo_box_get_active(GTK_COMBO_BOX(transparency_format_combo));
            }

            /* Get transparency cutoff */
            if (transparency_cutoff_adjustment) {
                png_opts->transparency_cutoff = (uint8_t)gtk_adjustment_get_value(transparency_cutoff_adjustment);
            }

            /* Get transparent color */
            if (transparent_color) {
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(transparent_color), &rgba);
                png_opts->transparency_color_r = (uint8_t)(rgba.red * 255.0 + 0.5);
                png_opts->transparency_color_g = (uint8_t)(rgba.green * 255.0 + 0.5);
                png_opts->transparency_color_b = (uint8_t)(rgba.blue * 255.0 + 0.5);
            }

            /* Get compositing color */
            if (compositing_color) {
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(compositing_color), &rgba);
                png_opts->compositing_color_r = (uint8_t)(rgba.red * 255.0 + 0.5);
                png_opts->compositing_color_g = (uint8_t)(rgba.green * 255.0 + 0.5);
                png_opts->compositing_color_b = (uint8_t)(rgba.blue * 255.0 + 0.5);
            }
        }
        result = TRUE;
    }

    /* Clean up */
    gtk_widget_destroy(dialog);
    g_object_unref(builder);

    return result;
}

#else /* HAVE_LIBPNG not defined */

gboolean png_options_dialog_show(GtkWindow* parent, SaveOptions* opts) {
    (void)parent;
    (void)opts;
    /* PNG support not available */
    return FALSE;
}

#endif /* HAVE_LIBPNG */
