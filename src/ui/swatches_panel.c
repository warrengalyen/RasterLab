#include "ui/swatches_panel.h"
#include "i18n.h"
#include "ocular.h"
#include "ui.h"
#include "ui/swatches.h"
#include "ui/ui_utils.h"
#include "ui/tools_panel.h"
#include "ui/widgets/swatches_widget.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "debug_logger.h"

/* CLAMP macro if not defined */
#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

/* Static references to swatches widgets */
static SwatchesWidget* g_recent_colors_widget = NULL;
static SwatchesWidget* g_main_swatches_widget = NULL;

/* Forward declarations */
static void on_recent_color_selected(SwatchesWidget* widget, gint index, gpointer user_data);
static void on_swatch_selected(SwatchesWidget* widget, gint index, gpointer user_data);
static void on_swatches_add_button_clicked(GtkButton* button, gpointer user_data);
static void on_swatches_delete_button_clicked(GtkButton* button, gpointer user_data);
static void on_swatches_reset_button_clicked(GtkButton* button, gpointer user_data);
static void on_swatches_import_button_clicked(GtkButton* button, gpointer user_data);
static void on_swatches_export_button_clicked(GtkButton* button, gpointer user_data);

/**
 * Callback when a recent color swatch is selected
 */
static void on_recent_color_selected(SwatchesWidget* widget, gint index, gpointer user_data) {
    if (!widget || index < 0) {
        return;
    }

    /* Get the selected color */
    GdkRGBA color;
    if (swatches_widget_get_swatch(widget, index, &color, NULL)) {
        /* Set foreground color using tools_panel function */
        tools_panel_set_foreground_color(&color);

        /* Also add to recent colors directly if we have AppContext */
        /* Note: tools_panel_set_foreground_color should handle this, but if it fails,
         * we do it here as a fallback */
        AppContext* ctx = (AppContext*)user_data;
        if (ctx) {
            swatches_add_recent(&ctx->swatches, &color);
            /* Sync to widget */
            GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
            if (recent_widget && SWATCHES_IS_WIDGET(recent_widget)) {
                swatches_sync_to_widgets(&ctx->swatches, NULL, recent_widget);
            }
        }
    }
}

/**
 * Callback when a swatch is selected in the main swatches widget
 */
static void on_swatch_selected(SwatchesWidget* widget, gint index, gpointer user_data) {
    if (!widget || index < 0) {
        return;
    }

    /* Get the selected color */
    GdkRGBA color;
    if (swatches_widget_get_swatch(widget, index, &color, NULL)) {
        /* Set foreground color using tools_panel function */
        /* This will also add to recent colors */
        tools_panel_set_foreground_color(&color);

        /* Also add to recent colors directly if we have AppContext */
        AppContext* ctx = (AppContext*)user_data;
        if (ctx) {
            swatches_add_recent(&ctx->swatches, &color);
            /* Sync to widget */
            GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
            if (recent_widget && SWATCHES_IS_WIDGET(recent_widget)) {
                swatches_sync_to_widgets(&ctx->swatches, NULL, recent_widget);
            }
        }
    }
}

/**
 * Callback for swatches add button click
 */
static void on_swatches_add_button_clicked(GtkButton* button, gpointer user_data) {
    (void)button; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        debug_log("WRN", "on_swatches_add_button_clicked: AppContext not found");
        return;
    }

    /* Get current foreground color */
    GdkRGBA fg_color;
    if (!tools_panel_get_foreground_color(&fg_color)) {
        debug_log("WRN", "on_swatches_add_button_clicked: Failed to get foreground color");
        return;
    }

    /* Add to main swatches */
    swatches_add_main(&ctx->swatches, &fg_color, NULL);

    /* Sync to widget */
    GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
    if (main_widget && SWATCHES_IS_WIDGET(main_widget)) {
        swatches_sync_to_widgets(&ctx->swatches, main_widget, NULL);
    } else {
        debug_log("WRN", "on_swatches_add_button_clicked: main_swatches_widget not found");
    }
}

/**
 * Callback for reset button click - reload swatches from file
 */
static void on_swatches_reset_button_clicked(GtkButton* button, gpointer user_data) {
    (void)button; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        debug_log("WRN", "on_swatches_reset_button_clicked: AppContext not found");
        return;
    }

    /* Reload swatches from file */
    swatches_load(&ctx->swatches, ctx->app_dir);

    /* Sync to widgets */
    GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
    GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
    swatches_sync_to_widgets(&ctx->swatches, main_widget, recent_widget);
}

/**
 * Callback for import button click - load swatches from palette file
 */
static void on_swatches_import_button_clicked(GtkButton* button, gpointer user_data) {
    (void)button; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->window) {
        debug_log("WRN", "on_swatches_import_button_clicked: AppContext or window not found");
        return;
    }

    GtkFileChooserNative* native_dialog;
    GtkFileFilter* filter;
    gint response;
    gchar* filename;

    /* Create native file chooser dialog */
    native_dialog = gtk_file_chooser_native_new(
        _("Import Palette File"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Open"),
        _("_Cancel"));

    /* Add file filters for all supported palette formats */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("GIMP Palette (*.gpl)"));
    gtk_file_filter_add_pattern(filter, "*.gpl");
    gtk_file_filter_add_pattern(filter, "*.GPL");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("RIFF Palette (*.pal)"));
    gtk_file_filter_add_pattern(filter, "*.pal");
    gtk_file_filter_add_pattern(filter, "*.PAL");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Adobe Color Swatch (*.aco)"));
    gtk_file_filter_add_pattern(filter, "*.aco");
    gtk_file_filter_add_pattern(filter, "*.ACO");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Paint.NET Palette (*.txt)"));
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.TXT");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Adobe Color Table (*.act)"));
    gtk_file_filter_add_pattern(filter, "*.act");
    gtk_file_filter_add_pattern(filter, "*.ACT");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Adobe Swatch Exchange (*.ase)");
    gtk_file_filter_add_pattern(filter, "*.ase");
    gtk_file_filter_add_pattern(filter, "*.ASE");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All Palette Files"));
    gtk_file_filter_add_pattern(filter, "*.gpl");
    gtk_file_filter_add_pattern(filter, "*.pal");
    gtk_file_filter_add_pattern(filter, "*.aco");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.act");
    gtk_file_filter_add_pattern(filter, "*.ase");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All Files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native_dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native_dialog));
        if (filename) {
            /* Load palette using ocular */
            OcPalette palette = {0};
            OC_STATUS status = ocularLoadPalette(filename, &palette);

            if (status == OC_STATUS_OK && palette.num_colors > 0) {
                /* Clear existing main swatches */
                swatches_clear_main(&ctx->swatches);

                /* Convert OcPalette to SwatchesData */
                for (int i = 0; i < palette.num_colors; i++) {
                    GdkRGBA color;
                    color.red = palette.colors[i].r / 255.0;
                    color.green = palette.colors[i].g / 255.0;
                    color.blue = palette.colors[i].b / 255.0;
                    color.alpha = 1.0;

                    /* Add swatch with name if available */
                    gchar* name = NULL;
                    if (palette.colors[i].name[0] != '\0') {
                        name = g_strdup(palette.colors[i].name);
                    }
                    swatches_add_main(&ctx->swatches, &color, name);
                    if (name) {
                        g_free(name);
                    }
                }

                /* Free palette */
                ocularFreePalette(&palette);

                /* Sync to widgets */
                GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
                GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
                swatches_sync_to_widgets(&ctx->swatches, main_widget, recent_widget);
            } else {
                /* Show error dialog */
                gchar* msg = g_strdup_printf(_("Failed to load palette file: %s"), filename);
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                    msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
                g_free(msg);
            }

            g_free(filename);
        }
    }

    g_object_unref(native_dialog);
}

/**
 * Callback for export button click - save swatches to palette file
 */
static void on_swatches_export_button_clicked(GtkButton* button, gpointer user_data) {
    (void)button; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->window) {
        debug_log("WRN", "on_swatches_export_button_clicked: AppContext or window not found");
        return;
    }

    if (ctx->swatches.main_swatch_count == 0) {
        /* Show error dialog */
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_WARNING,
            _("No swatches to export"), NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        return;
    }

    GtkFileChooserNative* native_dialog;
    GtkFileFilter* filter;
    gint response;
    gchar* filename;

    /* Create native file chooser dialog */
    native_dialog = gtk_file_chooser_native_new(
        _("Export Palette File"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Save"),
        _("_Cancel"));

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native_dialog), TRUE);

    /* Add file filters for all supported palette formats */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("GIMP Palette (*.gpl)"));
    gtk_file_filter_add_pattern(filter, "*.gpl");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("RIFF Palette (*.pal)"));
    gtk_file_filter_add_pattern(filter, "*.pal");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Adobe Color Swatch (*.aco)"));
    gtk_file_filter_add_pattern(filter, "*.aco");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Paint.NET Palette (*.txt)"));
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Adobe Color Table (*.act)"));
    gtk_file_filter_add_pattern(filter, "*.act");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All Files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

    /* Set default filename */
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native_dialog), "swatches.gpl");

    response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native_dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native_dialog));
        if (filename) {
            /* Convert SwatchesData to OcPalette */
            OcPalette palette = {0};
            palette.num_colors = ctx->swatches.main_swatch_count;
            palette.capacity = ctx->swatches.main_swatch_count;
            palette.colors = (OcPaletteColor*)g_malloc(sizeof(OcPaletteColor) * palette.num_colors);

            if (palette.colors) {
                /* Copy palette name if available */
                strncpy(palette.name, _("RasterLab Swatches"), sizeof(palette.name) - 1);
                palette.name[sizeof(palette.name) - 1] = '\0';

                /* Convert swatches to palette colors */
                for (int i = 0; i < palette.num_colors; i++) {
                    palette.colors[i].r = (int)(ctx->swatches.main_swatches[i].color.red * 255.0 + 0.5);
                    palette.colors[i].g = (int)(ctx->swatches.main_swatches[i].color.green * 255.0 + 0.5);
                    palette.colors[i].b = (int)(ctx->swatches.main_swatches[i].color.blue * 255.0 + 0.5);
                    palette.colors[i].r = CLAMP(palette.colors[i].r, 0, 255);
                    palette.colors[i].g = CLAMP(palette.colors[i].g, 0, 255);
                    palette.colors[i].b = CLAMP(palette.colors[i].b, 0, 255);

                    /* Copy name if available */
                    if (ctx->swatches.main_swatches[i].name) {
                        strncpy(palette.colors[i].name, ctx->swatches.main_swatches[i].name, sizeof(palette.colors[i].name) - 1);
                        palette.colors[i].name[sizeof(palette.colors[i].name) - 1] = '\0';
                    } else {
                        palette.colors[i].name[0] = '\0';
                    }
                }

                /* Determine file format from extension and save */
                gchar* extension = g_strrstr(filename, ".");
                gboolean saved = FALSE;

                if (extension) {
                    if (g_ascii_strcasecmp(extension, ".gpl") == 0) {
                        save_gimp_palette(filename, &palette);
                        saved = TRUE;
                    } else if (g_ascii_strcasecmp(extension, ".pal") == 0) {
                        save_riff_palette(filename, &palette);
                        saved = TRUE;
                    } else if (g_ascii_strcasecmp(extension, ".aco") == 0) {
                        save_aco_palette(filename, &palette);
                        saved = TRUE;
                    } else if (g_ascii_strcasecmp(extension, ".txt") == 0) {
                        save_paintnet_palette(filename, &palette);
                        saved = TRUE;
                    } else if (g_ascii_strcasecmp(extension, ".act") == 0) {
                        save_act_palette(filename, &palette);
                        saved = TRUE;
                    }
                }

                /* If no extension matched, default to GIMP palette */
                if (!saved) {
                    gchar* gpl_filename = g_strconcat(filename, ".gpl", NULL);
                    save_gimp_palette(gpl_filename, &palette);
                    g_free(gpl_filename);
                }

                /* Free palette */
                g_free(palette.colors);
            }

            g_free(filename);
        }
    }

    g_object_unref(native_dialog);
}

/**
 * Callback for delete button click - removes selected swatch
 */
static void on_swatches_delete_button_clicked(GtkButton* button, gpointer user_data) {
    (void)button; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        debug_log("WRN", "on_swatches_delete_button_clicked: AppContext not found");
        return;
    }

    /* Get the main swatches widget */
    GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
    if (!main_widget || !SWATCHES_IS_WIDGET(main_widget)) {
        debug_log("WRN", "on_swatches_delete_button_clicked: main_swatches_widget not found");
        return;
    }

    /* Get selected swatch index */
    gint selected_index = swatches_widget_get_selected(SWATCHES_WIDGET(main_widget));
    if (selected_index < 0 || selected_index >= ctx->swatches.main_swatch_count) {
        /* No swatch selected or invalid index */
        return;
    }

    /* Remove the swatch */
    if (ctx->swatches.main_swatches[selected_index].name) {
        g_free(ctx->swatches.main_swatches[selected_index].name);
    }

    /* Shift remaining swatches */
    for (gint i = selected_index; i < ctx->swatches.main_swatch_count - 1; i++) {
        ctx->swatches.main_swatches[i] = ctx->swatches.main_swatches[i + 1];
    }
    ctx->swatches.main_swatch_count--;

    /* Reallocate array */
    if (ctx->swatches.main_swatch_count > 0) {
        ctx->swatches.main_swatches = g_realloc(ctx->swatches.main_swatches,
                                                sizeof(SwatchData) * ctx->swatches.main_swatch_count);
    } else {
        g_free(ctx->swatches.main_swatches);
        ctx->swatches.main_swatches = NULL;
    }

    /* Clear selection */
    swatches_widget_set_selected(SWATCHES_WIDGET(main_widget), -1);

    /* Sync to widget */
    swatches_sync_to_widgets(&ctx->swatches, main_widget, NULL);
}

/**
 * Create the swatches panel widget
 */
GtkWidget* swatches_panel_create(AppContext* ctx) {
    GtkBuilder* swatches_builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(swatches_builder);
    GtkWidget* swatches_panel = NULL;
    GError* error = NULL;

    if (gtk_builder_add_from_resource(swatches_builder, "/ui/swatches_panel.glade", &error)) {
        swatches_panel = GTK_WIDGET(gtk_builder_get_object(swatches_builder, "swatches_panel"));
        if (swatches_panel) {
            /* Keep builder alive by storing it on the widget as object data */
            g_object_set_data_full(G_OBJECT(swatches_panel), "builder", swatches_builder, g_object_unref);
            /* Also store builder in window for drag-end handler */
            if (ctx && ctx->window) {
                g_object_ref(swatches_builder);
                g_object_set_data_full(G_OBJECT(ctx->window), "swatches_builder", swatches_builder, g_object_unref);
            }
            /* Ensure swatches panel content expands vertically */
            gtk_widget_set_vexpand(swatches_panel, TRUE);
            gtk_widget_set_hexpand(swatches_panel, TRUE);

            /* Get swatches_recent_colors_box and create recent colors widget */
            GtkWidget* swatches_recent_colors_box = GTK_WIDGET(gtk_builder_get_object(swatches_builder, "swatches_recent_colors_box"));
            if (swatches_recent_colors_box) {

                /* Create recent colors widget (up to 9 colors) */
                SwatchesWidget* recent_colors_widget = SWATCHES_WIDGET(swatches_widget_new());
                if (recent_colors_widget) {
                    swatches_widget_set_spacing(recent_colors_widget, 1.0);
                    swatches_widget_set_padding(recent_colors_widget, 2.0);
                    swatches_widget_set_max_swatch_size(recent_colors_widget, 20.0);

                    gtk_widget_set_size_request(GTK_WIDGET(recent_colors_widget), -1, 28);

                    /* Make widget expand horizontally but not vertically */
                    gtk_widget_set_hexpand(GTK_WIDGET(recent_colors_widget), TRUE);
                    gtk_widget_set_vexpand(GTK_WIDGET(recent_colors_widget), FALSE);

                    /* Store widget reference in swatches panel for color tracking */
                    g_object_set_data(G_OBJECT(swatches_panel), "recent_colors_widget", recent_colors_widget);

                    /* Store in static variable for easy access */
                    g_recent_colors_widget = recent_colors_widget;

                    /* Also store in main window for global access */
                    if (ctx && ctx->window) {
                        g_object_set_data(G_OBJECT(ctx->window), "recent_colors_widget", recent_colors_widget);
                        /* Sync recent colors from app context to widget */
                        swatches_sync_to_widgets(&ctx->swatches, NULL, GTK_WIDGET(recent_colors_widget));
                    }

                    /* Connect swatch selection signal to set foreground color */
                    g_signal_connect(recent_colors_widget, "swatch-selected",
                                     G_CALLBACK(on_recent_color_selected), ctx);

                    /* Add to swatches_recent_colors_box */
                    gtk_container_add(GTK_CONTAINER(swatches_recent_colors_box), GTK_WIDGET(recent_colors_widget));

                    /* Ensure the box expands horizontally */
                    gtk_widget_set_hexpand(swatches_recent_colors_box, TRUE);
                    gtk_widget_set_vexpand(swatches_recent_colors_box, FALSE);

                    /* Show all widgets in the hierarchy */
                    gtk_widget_show_all(swatches_recent_colors_box);

                    /* Force a queue resize to ensure proper layout */
                    gtk_widget_queue_resize(GTK_WIDGET(recent_colors_widget));
                    gtk_widget_queue_resize(swatches_recent_colors_box);
                } else {
                    debug_log("WRN", "Failed to create recent colors widget");
                }
            } else {
                debug_log("WRN", "Failed to get swatches_recent_colors_box from builder");
            }

            /* Apply CSS to remove padding from swatches buttons */
            /* Remove padding from all swatches control buttons */
            const gchar* button_ids[] = {
                "swatches_add_button",
                "swatches_delete_button",
                "swatches_reset_button",
                "swatches_import_button",
                "swatches_export_button",
            };

            GtkCssProvider* provider = gtk_css_provider_new();
            gtk_css_provider_load_from_data(provider, "button { padding: 0px; padding-right: 5px; padding-left: 5px; margin: 0px; }", -1, NULL);

            for (gint i = 0; i < 5; i++) {
                GtkWidget* button = GTK_WIDGET(gtk_builder_get_object(swatches_builder, button_ids[i]));
                if (button) {
                    GtkStyleContext* context = gtk_widget_get_style_context(button);
                    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

                    /* Connect add button click handler */
                    if (g_strcmp0(button_ids[i], "swatches_add_button") == 0) {
                        g_signal_connect(button, "clicked", G_CALLBACK(on_swatches_add_button_clicked), ctx);
                    }

                    /* Connect delete button click handler */
                    if (g_strcmp0(button_ids[i], "swatches_delete_button") == 0) {
                        g_signal_connect(button, "clicked", G_CALLBACK(on_swatches_delete_button_clicked), ctx);
                    }

                    /* Connect reset button click handler */
                    if (g_strcmp0(button_ids[i], "swatches_reset_button") == 0) {
                        g_signal_connect(button, "clicked", G_CALLBACK(on_swatches_reset_button_clicked), ctx);
                    }

                    /* Connect import button click handler */
                    if (g_strcmp0(button_ids[i], "swatches_import_button") == 0) {
                        g_signal_connect(button, "clicked", G_CALLBACK(on_swatches_import_button_clicked), ctx);
                    }

                    /* Connect export button click handler */
                    if (g_strcmp0(button_ids[i], "swatches_export_button") == 0) {
                        g_signal_connect(button, "clicked", G_CALLBACK(on_swatches_export_button_clicked), ctx);
                    }
                }
            }

            g_object_unref(provider);

            /* Get scrolled window and create swatches widget */
            GtkWidget* swatches_scroll = GTK_WIDGET(gtk_builder_get_object(swatches_builder, "swatches_scroll"));
            if (swatches_scroll) {
                SwatchesWidget* swatches_widget = SWATCHES_WIDGET(swatches_widget_new());
                if (swatches_widget) {
                    /* Configure widget */
                    /* Columns are now calculated automatically based on widget size */
                    swatches_widget_set_spacing(swatches_widget, 1.0);
                    swatches_widget_set_padding(swatches_widget, 4.0);

                    /* Store reference */
                    g_main_swatches_widget = swatches_widget;

                    /* Connect swatch selection signal to update foreground color */
                    g_signal_connect(swatches_widget, "swatch-selected",
                                     G_CALLBACK(on_swatch_selected), ctx);

                    /* Store reference */
                    g_main_swatches_widget = swatches_widget;

                    /* Store in main window for global access */
                    if (ctx && ctx->window) {
                        g_object_set_data(G_OBJECT(ctx->window), "main_swatches_widget", swatches_widget);

                        /* If no swatches loaded, add default gradient */
                        if (ctx->swatches.main_swatch_count == 0) {
                            /* Add default swatches - create a color gradient */
                            GdkRGBA color;
                            for (gint row = 0; row < 8; row++) {
                                for (gint col = 0; col < 16; col++) {
                                    /* Calculate hue (0-360) based on column */
                                    gdouble hue = (col / 15.0) * 360.0;
                                    /* Calculate saturation and value based on row */
                                    gdouble saturation = 0.3 + (row / 7.0) * 0.7; /* 0.3 to 1.0 */
                                    gdouble value = 1.0 - (row / 7.0) * 0.5;      /* 1.0 to 0.5 */

                                    /* Convert HSV to RGB */
                                    gdouble c = value * saturation;
                                    gdouble x = c * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
                                    gdouble m = value - c;

                                    gdouble r = 0.0, g = 0.0, b = 0.0;
                                    if (hue < 60.0) {
                                        r = c;
                                        g = x;
                                        b = 0.0;
                                    } else if (hue < 120.0) {
                                        r = x;
                                        g = c;
                                        b = 0.0;
                                    } else if (hue < 180.0) {
                                        r = 0.0;
                                        g = c;
                                        b = x;
                                    } else if (hue < 240.0) {
                                        r = 0.0;
                                        g = x;
                                        b = c;
                                    } else if (hue < 300.0) {
                                        r = x;
                                        g = 0.0;
                                        b = c;
                                    } else {
                                        r = c;
                                        g = 0.0;
                                        b = x;
                                    }

                                    color.red = r + m;
                                    color.green = g + m;
                                    color.blue = b + m;
                                    color.alpha = 1.0;

                                    swatches_add_main(&ctx->swatches, &color, NULL);
                                }
                            }
                        }
                        /* Always sync swatches data to widget (either loaded or defaults) */
                        swatches_sync_to_widgets(&ctx->swatches, GTK_WIDGET(swatches_widget), NULL);
                    }

                    /* Configure widget for scrolling - don't expand vertically */
                    gtk_widget_set_vexpand(GTK_WIDGET(swatches_widget), FALSE);
                    gtk_widget_set_hexpand(GTK_WIDGET(swatches_widget), TRUE);
                    gtk_widget_set_valign(GTK_WIDGET(swatches_widget), GTK_ALIGN_START);

                    /* Ensure scrolled window shows scrollbars when needed */
                    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swatches_scroll),
                                                   GTK_POLICY_AUTOMATIC,
                                                   GTK_POLICY_AUTOMATIC);

                    /* Add widget to scrolled window */
                    gtk_container_add(GTK_CONTAINER(swatches_scroll), GTK_WIDGET(swatches_widget));

                    /* Force widget to request its natural size (no fixed size request) */
                    gtk_widget_set_size_request(GTK_WIDGET(swatches_widget), -1, -1);

                    gtk_widget_show_all(GTK_WIDGET(swatches_widget));
                }
            }
        } else {
            debug_log("WRN", "Failed to get swatches_panel from builder");
            g_object_unref(swatches_builder);
        }
    } else {
        debug_log("WRN", "Failed to load swatches_panel.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(swatches_builder);
    }

    return swatches_panel;
}

/**
 * Cleanup swatches panel static references
 */
void swatches_panel_cleanup(void) {
    g_main_swatches_widget = NULL;
    g_recent_colors_widget = NULL;
}
