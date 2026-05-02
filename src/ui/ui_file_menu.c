/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/ui_file_menu.h"
#include "app/autosave.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "command.h"
#include "commands/command_revert.h"
#include "document.h"
#include "document_revert_diff.h"
#include "filters/filter_export_palette.h"
#include "filters/filter_lut3d.h"
#include "ui/widgets/swatches_widget.h"
#include "io/image_io.h"
#include "io/lut3d_io.h"
#include "plugins/format_registry.h"
#include "render/compositor.h"
#include "ui.h"
#include "ui/dialogs/new_image_dialog.h"
#include "ui/dialogs/save_options_dialog.h"
#include "ui/layers_panel.h"
#include "ui/swatches.h"
#include "ui/ui_edit_menu.h"
#include "ui/ui_utils.h"
#include "i18n.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include "debug_logger.h"

/* Header probe size for format detection (must be >= 132 for DICOM) */
#define FILE_HEADER_PROBE_SIZE 256

#define LUT_CHOOSER_FORMAT_KEY "rasterlab-lut-export-fmt"
enum { LUT_CHOOSER_FMT_CUBE = 0, LUT_CHOOSER_FMT_LOOK, LUT_CHOOSER_FMT_ANY };

static void on_canvas_viewport_drag_data_received(GtkWidget* widget, GdkDragContext* context,
                                                  gint x, gint y, GtkSelectionData* sel_data,
                                                  guint info, guint time, gpointer user_data);
static void on_notebook_drag_data_received(GtkWidget* widget, GdkDragContext* context,
                                           gint x, gint y, GtkSelectionData* sel_data,
                                           guint info, guint time, gpointer user_data);

/**
 * Timeout callback to pulse progress bar while saving (see ui_filter.c).
 */
static gboolean pulse_save_progress_bar(gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (ctx) {
        ui_update_progress(ctx);
    }
    return G_SOURCE_CONTINUE;
}

/**
 * Save via plugin pipeline with status-bar progress (same pattern as filters).
 */
static gboolean document_save_as_with_progress(AppContext* ctx, ImageDocument* doc,
                                               const gchar* file_path, const SaveOptions* opts,
                                               PluginError* save_error) {
    guint pulse_timeout_id = 0;
    gboolean success;

    if (!ctx || !doc || !file_path) {
        if (save_error) {
            *save_error = PLUGIN_ERROR_INVALID_PARAMETERS;
        }
        return FALSE;
    }

    {
        gchar* base = g_path_get_basename(file_path);
        gchar* progress_message = g_strdup_printf(_("Saving %s..."), base ? base : file_path);
        g_free(base);
        ui_show_progress(ctx, progress_message);
        g_free(progress_message);
    }

    pulse_timeout_id = g_timeout_add(50, pulse_save_progress_bar, ctx);

    success = document_save_as_with_error(doc, file_path, opts, save_error);

    if (pulse_timeout_id > 0) {
        g_source_remove(pulse_timeout_id);
    }

    ui_hide_progress(ctx);

    return success;
}

gboolean ui_file_menu_open_path_as_new_document(AppContext* ctx, const gchar* file_path) {
    gchar* basename;
    ImageDocument* doc;

    if (!ctx || !file_path) {
        return FALSE;
    }

    if (!image_io_is_supported_file(file_path)) {
        return FALSE;
    }

    basename = g_path_get_basename(file_path);
    if (!basename) {
        return FALSE;
    }

    doc = ui_create_document_without_tab(ctx, basename);
    if (!doc) {
        g_free(basename);
        return FALSE;
    }

    {
        PluginError load_error = PLUGIN_ERROR_NONE;
        gboolean load_result = image_io_load(doc, file_path, &load_error, ctx->settings);

        if (!load_result) {
            if (load_error == PLUGIN_ERROR_USER_CANCELLED) {
                document_free(doc);
                g_free(basename);
                return FALSE;
            }
            {
                const char* error_message = image_io_get_error_message(load_error, file_path);
                gchar* msg = g_strdup_printf(_("Failed to load image: %s\n\n%s"),
                                             file_path, error_message);
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
                g_free(msg);
            }
            document_free(doc);
            g_free(basename);
            return FALSE;
        }
    }

    ui_add_document_to_notebook(ctx, doc);
    autosave_register_document(doc);

    if (!document_init_rendering_structures(doc)) {
        gchar* msg = g_strdup_printf(_("Failed to initialize document rendering for: %s"),
                                     file_path);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        g_free(msg);

        if (doc->scrolled_window && ctx->notebook) {
            gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), doc->scrolled_window);
            if (page_num >= 0) {
                ui_close_document_tab(ctx, doc);
            } else {
                document_free(doc);
            }
        } else {
            document_free(doc);
        }
        g_free(basename);
        return FALSE;
    }

    {
        ImageLayer* layer_0 = document_get_layer(doc, 0);
        if (layer_0) {
            document_set_selected_layer(doc, layer_0);
        }
    }

    document_invalidate_composite(doc);

    /* Capture the unmodified image state for the undo history "Original image" entry */
    document_queue_initial_thumbnail(doc);

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    ui_update_status_bar_message(ctx, _("Image successfully loaded"));
    recent_files_add(file_path);
    recent_files_save();
    if (ctx->settings && ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }
    ui_update_status_bar(ctx, NULL);

    {
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_recent_files_menu(ctx);

    g_free(basename);
    return TRUE;
}

void ui_file_menu_setup_viewport_drag_drop(ImageDocument* doc, AppContext* ctx) {
    if (!doc || !ctx) {
        return;
    }

    if (doc->viewport) {
        g_object_set_data(G_OBJECT(doc->viewport), "image_document", doc);
        gtk_drag_dest_set(doc->viewport, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
        gtk_drag_dest_add_uri_targets(doc->viewport);
        g_signal_connect(doc->viewport, "drag-data-received",
                         G_CALLBACK(on_canvas_viewport_drag_data_received), ctx);
    }

    if (doc->drawing_area) {
        g_object_set_data(G_OBJECT(doc->drawing_area), "image_document", doc);
        gtk_drag_dest_set(doc->drawing_area, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
        gtk_drag_dest_add_uri_targets(doc->drawing_area);
        g_signal_connect(doc->drawing_area, "drag-data-received",
                         G_CALLBACK(on_canvas_viewport_drag_data_received), ctx);
    }
}

void ui_file_menu_setup_notebook_drag_drop(GtkWidget* notebook, AppContext* ctx) {
    if (!notebook || !ctx) {
        return;
    }

    gtk_drag_dest_set(notebook, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(notebook);
    g_signal_connect(notebook, "drag-data-received",
                     G_CALLBACK(on_notebook_drag_data_received), ctx);
}

static void on_canvas_viewport_drag_data_received(GtkWidget* widget, GdkDragContext* context,
                                                  gint x, gint y, GtkSelectionData* sel_data,
                                                  guint info, guint time, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = (ImageDocument*)g_object_get_data(G_OBJECT(widget), "image_document");
    gboolean success = FALSE;
    gchar** uris;
    gint u;
    ImageLayer* last_imported = NULL;
    LayersPanel* layers_panel;

    (void)x;
    (void)y;
    (void)info;

    if (!ctx || !doc) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    if (!sel_data || gtk_selection_data_get_length(sel_data) < 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    uris = gtk_selection_data_get_uris(sel_data);
    if (!uris) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    layers_panel = ctx->layers_panel;
    if (!layers_panel) {
        layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    }

    if (!layers_panel) {
        g_strfreev(uris);
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    for (u = 0; uris[u]; u++) {
        gchar* file_path = g_filename_from_uri(uris[u], NULL, NULL);
        ImageLayer* added;

        if (!file_path) {
            continue;
        }

        added = layers_panel_import_path_into_document(layers_panel, doc, file_path);
        g_free(file_path);

        if (added) {
            last_imported = added;
            success = TRUE;
        }
    }

    g_strfreev(uris);

    if (success && doc) {
        layers_panel_update(layers_panel, doc);
        if (last_imported) {
            layers_panel_select_layer(layers_panel, doc, last_imported);
        }
        layers_panel_update_opacity_controls(layers_panel);
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
    }

    gtk_drag_finish(context, success, FALSE, time);
}

static void on_notebook_drag_data_received(GtkWidget* widget, GdkDragContext* context,
                                           gint x, gint y, GtkSelectionData* sel_data,
                                           guint info, guint time, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    gboolean success = FALSE;
    gchar** uris;
    gint u;

    (void)widget;
    (void)x;
    (void)y;
    (void)info;

    if (!ctx || !ctx->notebook) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(ctx->notebook)) > 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    if (!sel_data || gtk_selection_data_get_length(sel_data) < 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    uris = gtk_selection_data_get_uris(sel_data);
    if (!uris) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    for (u = 0; uris[u]; u++) {
        gchar* file_path = g_filename_from_uri(uris[u], NULL, NULL);
        if (!file_path) {
            continue;
        }
        if (ui_file_menu_open_path_as_new_document(ctx, file_path)) {
            success = TRUE;
        }
        g_free(file_path);
    }

    g_strfreev(uris);
    gtk_drag_finish(context, success, FALSE, time);
}

/**
 * Callback for activating a recent file
 */
void on_recent_file_activate(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;
    gchar* file_path = (gchar*)g_object_get_data(G_OBJECT(menu_item), "recent_file_path");
    FormatHandler* handler;
    uint8_t header[FILE_HEADER_PROBE_SIZE];
    size_t header_size = 0;
    FILE* file;

    if (!file_path) {
        return;
    }

    /* Check if file still exists */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        /* Show warning dialog */
        gchar* msg = g_strdup_printf(
            _("File not found: %s\n\nThe file has been removed from the recent files list."),
            file_path);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_WARNING,
                                    msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        g_free(msg);

        /* Remove from recent files */
        recent_files_remove(file_path);
        recent_files_save(); /* This syncs to settings if connected */
        ui_update_recent_files_menu(ctx);

        return;
    }

    /* Check if a plugin can handle this file format */
    file = g_fopen(file_path, "rb");
    if (file) {
        header_size = fread(header, 1, sizeof(header), file);
        fclose(file);
    }

    handler = format_registry_find_loader(file_path, header, header_size);
    if (!handler) {
        /* No plugin can handle this file format */
        gchar* msg = g_strdup_printf(
            _("Unsupported file format: %s\n\nNo plugin is available to load this file type.\n\nThe file has been removed from the recent files list."),
            file_path);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        g_free(msg);

        /* Remove from recent files */
        recent_files_remove(file_path);
        recent_files_save(); /* This syncs to settings if connected */
        ui_update_recent_files_menu(ctx);

        return;
    }

    ui_file_menu_open_path_as_new_document(ctx, file_path);
}

/**
 * Callback for clearing recent files
 */
void on_clear_recent_files(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused */
    AppContext* ctx = (AppContext*)user_data;

    /* Clear recent files */
    recent_files_clear();
    recent_files_save(); /* This syncs to settings if connected */

    ui_update_recent_files_menu(ctx);
}

/**
 * Reload document from disk and record as a single undo step.
 */
static void on_file_revert(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item;
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc;
    ImageDocument* temp;
    DocumentRevertDiff* diff;
    Command* cmd;
    gchar* basename;
    guint undo_levels = 10;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc || !doc->file_path || doc->file_path[0] == '\0') {
        return;
    }

    {
        gint confirm;
        confirm = ui_utils_message_dialog_run(
            GTK_WINDOW(ctx->window),
            GTK_MESSAGE_WARNING,
            _("Revert to saved file?"),
            _("The document will be reloaded from disk. Undo history for this document "
            "will be cleared. You can undo the revert from the Edit menu."),
            GTK_RESPONSE_CANCEL,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Revert"), GTK_RESPONSE_OK,
            NULL);
        if (confirm != GTK_RESPONSE_OK) {
            return;
        }
    }

    basename = g_path_get_basename(doc->file_path);
    if (ctx->settings) {
        undo_levels = (guint)settings_get_undo_levels(ctx->settings);
    }
    temp = document_new(basename ? basename : _("revert"), TRUE, undo_levels);
    g_free(basename);
    if (!temp) {
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    _("Out of memory preparing revert."), NULL,
                                    GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        return;
    }

    {
        PluginError load_error = PLUGIN_ERROR_NONE;
        gboolean load_ok = image_io_load(temp, doc->file_path, &load_error, ctx->settings);

        if (!load_ok) {
            if (load_error == PLUGIN_ERROR_USER_CANCELLED) {
                document_free(temp);
                return;
            }
            {
                const char* err_msg = image_io_get_error_message(load_error, doc->file_path);
                gchar* msg = g_strdup_printf(_("Could not reload the file for revert:\n\n%s"), err_msg);
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
                g_free(msg);
            }
            document_free(temp);
            return;
        }

        if (!document_init_rendering_structures(temp)) {
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        _("Could not initialize rendering after reload."), NULL,
                                        GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
            document_free(temp);
            return;
        }

        {
            ImageLayer* l0 = document_get_layer(temp, 0);
            if (l0) {
                document_set_selected_layer(temp, l0);
            }
        }
        document_invalidate_composite(temp);
    }

    diff = document_revert_diff_build(doc, temp);
    if (!diff) {
        document_free(temp);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    _("Could not prepare revert undo data."), NULL,
                                    GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        return;
    }

    /* Drop prior undo/redo before replacing document content. If we cleared the stacks after
     * applying new content, command destroy callbacks would see stale layer pointers
     * (already freed by the apply) and could double-free. */
    if (doc->undo_stack) {
        command_stack_clear(doc->undo_stack);
    }
    if (doc->redo_stack) {
        command_stack_clear(doc->redo_stack);
    }

    if (!document_revert_apply_loaded_document(doc, temp)) {
        document_revert_diff_free(diff);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    _("Could not apply revert."), NULL,
                                    GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        return;
    }

    cmd = command_create_document_revert(doc, diff, doc->file_path, ctx->settings);
    if (!cmd) {
        if (!document_revert_diff_apply_undo(doc, diff)) {
            debug_log("WRN", "Revert: failed to restore document after command creation failure");
        }
        document_revert_diff_free(diff);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    _("Could not create revert undo step."), NULL,
                                    GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        return;
    }

    document_push_undo_command(doc, cmd);

    document_mark_saved(doc);
    autosave_mark_saved(doc);

    ui_update_document_tab_label(ctx, doc);
    ui_update_window_title(ctx, doc);
    ui_update_status_bar(ctx, doc);
    ui_update_status_bar_message(ctx, _("Reverted to saved file"));

    {
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }
    }
    ui_update_menu_and_button_states(ctx);
    ui_update_recent_files_menu(ctx);
}

static void on_lut_quality_value_changed(GtkAdjustment* adj, gpointer user_data) {
    GtkLabel* lbl = GTK_LABEL(user_data);
    gint n = CLAMP((gint)gtk_adjustment_get_value(adj),
                   FILTER_LUT3D_PHOTO_GRID_MIN,
                   FILTER_LUT3D_PHOTO_GRID_MAX);
    gchar* text = g_strdup_printf("(%d x %d x %d)", n, n, n);
    gtk_label_set_text(lbl, text);
    g_free(text);
}

static void on_lut_dlg_button_clicked(GtkButton* btn, gpointer dlg) {
    gint rid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "response-id"));
    gtk_dialog_response(GTK_DIALOG(dlg), rid);
}

/**
 * Show the Color lookup options dialog
 *
 * @param parent        Transient parent window.
 * @param out_title     Filled with g_strdup'd title string on GTK_RESPONSE_OK (caller frees).
 * @param out_copyright Filled with g_strdup'd description/copyright string (caller frees).
 * @param out_grid      Filled with chosen grid points (2–64).
 * @return GTK_RESPONSE_OK if the user confirmed, any other value on cancel/close.
 */
static gint run_color_lookup_options_dialog(GtkWindow* parent,
                                            gchar**    out_title,
                                            gchar**    out_copyright,
                                            gint*      out_grid) {
    GtkBuilder*    builder;
    GtkWidget*     dlg;
    GtkWidget*     title_entry;
    GtkWidget*     desc_entry;
    GtkAdjustment* adj;
    GtkWidget*     quality_label;
    GtkWidget*     cancel_btn;
    GtkWidget*     export_btn;
    GError*        err = NULL;
    gint           response;

    if (out_title)     *out_title     = NULL;
    if (out_copyright) *out_copyright = NULL;
    if (out_grid)      *out_grid      = 17;

    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/dialogs/3dlut_export_dialog.glade", &err)) {
        debug_log("WRN", "3dlut_export_dialog.glade load failed: %s",
                  err ? err->message : "unknown");
        if (err) g_error_free(err);
        g_object_unref(builder);
        return GTK_RESPONSE_CANCEL;
    }

    dlg = GTK_WIDGET(gtk_builder_get_object(builder, "3dlut_export_dialog"));
    if (!dlg) {
        debug_log("WRN", "3dlut_export_dialog widget not found in builder");
        g_object_unref(builder);
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);
        gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);
    }
    gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
    ui_utils_set_header_bar(GTK_WINDOW(dlg), _("Color lookup options"));

    title_entry   = GTK_WIDGET(gtk_builder_get_object(builder, "3dlut_export_title_entry"));
    desc_entry    = GTK_WIDGET(gtk_builder_get_object(builder, "3dlut_export_desc_entry"));
    adj           = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "quality_scale"));
    quality_label = GTK_WIDGET(gtk_builder_get_object(builder, "quality_value_label"));
    cancel_btn    = GTK_WIDGET(gtk_builder_get_object(builder, "3dlut_cancel_button"));
    export_btn    = GTK_WIDGET(gtk_builder_get_object(builder, "3dlut_export_button"));

    if (adj && quality_label) {
        /* Clamp adjustment bounds to filter limits. */
        gtk_adjustment_set_lower(adj, (gdouble)FILTER_LUT3D_PHOTO_GRID_MIN);
        gtk_adjustment_set_upper(adj, (gdouble)FILTER_LUT3D_PHOTO_GRID_MAX);
        if (gtk_adjustment_get_value(adj) < (gdouble)FILTER_LUT3D_PHOTO_GRID_MIN)
            gtk_adjustment_set_value(adj, 17.0);
        /* Wire live update. */
        g_signal_connect(adj, "value-changed",
                         G_CALLBACK(on_lut_quality_value_changed), quality_label);
        /* Trigger once to set initial text. */
        on_lut_quality_value_changed(adj, quality_label);
    }

    if (cancel_btn) {
        g_object_set_data(G_OBJECT(cancel_btn), "response-id",
                          GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_btn, "clicked",
                         G_CALLBACK(on_lut_dlg_button_clicked), dlg);
    }
    if (export_btn) {
        g_object_set_data(G_OBJECT(export_btn), "response-id",
                          GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(export_btn, "clicked",
                         G_CALLBACK(on_lut_dlg_button_clicked), dlg);
    }

    gtk_widget_show_all(dlg);
    response = gtk_dialog_run(GTK_DIALOG(dlg));

    if (response == GTK_RESPONSE_OK) {
        if (out_title && title_entry) {
            const gchar* t = gtk_entry_get_text(GTK_ENTRY(title_entry));
            *out_title = g_strdup(t ? t : "");
        }
        if (out_copyright && desc_entry) {
            const gchar* d = gtk_entry_get_text(GTK_ENTRY(desc_entry));
            *out_copyright = g_strdup(d ? d : "");
        }
        if (out_grid && adj) {
            *out_grid = CLAMP((gint)gtk_adjustment_get_value(adj),
                              FILTER_LUT3D_PHOTO_GRID_MIN,
                              FILTER_LUT3D_PHOTO_GRID_MAX);
        }
    }

    gtk_widget_destroy(dlg);
    g_object_unref(builder);
    return response;
}

/* ──────────────────────────────────────────────────────────────
 * Palette export dialog helpers
 * ────────────────────────────────────────────────────────────── */

typedef struct {
    GtkWidget*              dlg;
    GtkWidget*              auto_toggle;
    GtkWidget*              custom_toggle;
    GtkWidget*              controls_box;
    GtkAdjustment*          count_adj;
    SwatchesWidget*         swatch_widget;
    cairo_surface_t*        export_surface; /* composite snapshot, borrowed lifetime = dialog */
    gboolean                updating;       /* guard against re-entrant toggle callbacks */
    gboolean                count_held;     /* TRUE while a button is held on scale or spin */
    /* Snapshotted before gtk_widget_destroy so they survive after the dialog is gone */
    FilterPaletteExportCountMode last_mode;
    gint                    last_count;
} PaletteExportDlgState;

static void palette_dlg_refresh_swatches(PaletteExportDlgState* st) {
    OcPalette palette = {0};
    FilterPaletteExportCountMode mode;
    gint custom_count;
    gint i;

    if (!st || !st->export_surface || !st->swatch_widget) {
        return;
    }

    mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->custom_toggle))
               ? FILTER_PALETTE_EXPORT_COUNT_CUSTOM
               : FILTER_PALETTE_EXPORT_COUNT_AUTO;
    custom_count = (gint)gtk_adjustment_get_value(st->count_adj);

    if (!filter_build_preview_palette(st->export_surface, mode, custom_count, &palette)) {
        swatches_widget_clear(st->swatch_widget);
        return;
    }

    swatches_widget_clear(st->swatch_widget);
    for (i = 0; i < palette.num_colors; i++) {
        GdkRGBA c;
        c.red   = palette.colors[i].r / 255.0;
        c.green = palette.colors[i].g / 255.0;
        c.blue  = palette.colors[i].b / 255.0;
        c.alpha = 1.0;
        swatches_widget_add_swatch(st->swatch_widget, &c, palette.colors[i].name[0] ? palette.colors[i].name : NULL);
    }
    ocularFreePalette(&palette);
    gtk_widget_queue_draw(GTK_WIDGET(st->swatch_widget));
}

static void on_palette_dlg_auto_toggled(GtkToggleButton* btn, gpointer user_data) {
    PaletteExportDlgState* st = (PaletteExportDlgState*)user_data;
    if (st->updating) return;
    st->updating = TRUE;
    if (gtk_toggle_button_get_active(btn)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->custom_toggle), FALSE);
        gtk_widget_hide(st->controls_box);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->custom_toggle), TRUE);
        gtk_widget_show(st->controls_box);
    }
    st->updating = FALSE;
    palette_dlg_refresh_swatches(st);
}

static void on_palette_dlg_custom_toggled(GtkToggleButton* btn, gpointer user_data) {
    PaletteExportDlgState* st = (PaletteExportDlgState*)user_data;
    if (st->updating) return;
    st->updating = TRUE;
    if (gtk_toggle_button_get_active(btn)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->auto_toggle), FALSE);
        gtk_widget_show(st->controls_box);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->auto_toggle), TRUE);
        gtk_widget_hide(st->controls_box);
    }
    st->updating = FALSE;
    palette_dlg_refresh_swatches(st);
}

/* Set the held flag as soon as any mouse button goes down on scale or spin. */
static gboolean on_palette_dlg_count_press(GtkWidget* widget, GdkEventButton* event,
                                           gpointer user_data) {
    PaletteExportDlgState* st = (PaletteExportDlgState*)user_data;
    (void)widget;
    (void)event;
    st->count_held = TRUE;
    return FALSE;
}

/* Clear the held flag and flush the preview once the button is released. */
static gboolean on_palette_dlg_count_release(GtkWidget* widget, GdkEventButton* event,
                                             gpointer user_data) {
    PaletteExportDlgState* st = (PaletteExportDlgState*)user_data;
    (void)widget;
    (void)event;
    st->count_held = FALSE;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->custom_toggle)))
        palette_dlg_refresh_swatches(st);
    return FALSE;
}

/* value-changed fires for every intermediate step (drag, auto-repeat, keyboard).
 * Only act when no button is held; the release handler covers the mouse cases. */
static void on_palette_dlg_count_changed(GtkAdjustment* adj, gpointer user_data) {
    PaletteExportDlgState* st = (PaletteExportDlgState*)user_data;
    (void)adj;
    if (st->count_held) return;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->custom_toggle))) return;
    palette_dlg_refresh_swatches(st);
}

static void on_palette_dlg_button_clicked(GtkButton* btn, gpointer dlg) {
    gint rid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "response-id"));
    gtk_dialog_response(GTK_DIALOG(dlg), rid);
}

static void on_export_menu_palette(GtkWidget* widget, gpointer user_data) {
    AppContext*              ctx = (AppContext*)user_data;
    ImageDocument*           doc;
    cairo_surface_t*         export_surface;
    GtkFileChooserNative*    native;
    GtkFileFilter*           f;
    gchar*                   save_path = NULL;
    gint                     fc_response;
    GtkBuilder*              builder;
    GtkWidget*               dlg;
    GtkWidget*               alignment;
    GtkWidget*               swatches_w;
    GtkWidget*               ok_btn;
    GtkWidget*               cancel_btn;
    GtkWidget*               controls_box;
    GtkWidget*               auto_toggle;
    GtkWidget*               custom_toggle;
    GtkWidget*               count_scale;
    GtkWidget*               count_spin;
    GtkAdjustment*           count_adj;
    PaletteExportDlgState    st = {0};
    GError*                  err = NULL;
    gint                     response;

    (void)widget;

    if (!ctx || !ctx->window) return;

    doc = ui_get_active_document(ctx);
    if (!doc) return;

    native = gtk_file_chooser_native_new(
        _("Export Palette"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Save"),
        _("_Cancel"));
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);

    /* Default filename: strip the image extension and add .gpl */
    {
        const gchar* base = (doc->filename && doc->filename[0]) ? doc->filename : "palette";
        gchar* stem = g_strdup(base);
        gchar* dot  = strrchr(stem, '.');
        if (dot) *dot = '\0';
        gchar* suggested = g_strdup_printf("%s.gpl", stem);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), suggested);
        g_free(stem);
        g_free(suggested);

        /* If the document already has a saved directory, open there */
        if (doc->file_path && doc->file_path[0]) {
            gchar* dir = g_path_get_dirname(doc->file_path);
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), dir);
            g_free(dir);
        }
    }

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("GIMP Palette (*.gpl)"));
    gtk_file_filter_add_pattern(f, "*.gpl");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("RIFF Palette (*.pal)"));
    gtk_file_filter_add_pattern(f, "*.pal");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Adobe Color Swatch (*.aco)"));
    gtk_file_filter_add_pattern(f, "*.aco");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Paint.NET Palette (*.txt)"));
    gtk_file_filter_add_pattern(f, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Adobe Color Table (*.act)"));
    gtk_file_filter_add_pattern(f, "*.act");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    fc_response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (fc_response == GTK_RESPONSE_ACCEPT) {
        save_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
    }
    g_object_unref(native);

    if (!save_path) return;

    /* Guarantee a palette extension is present */
    if (!strrchr(save_path, '.')) {
        gchar* with_ext = g_strconcat(save_path, ".gpl", NULL);
        g_free(save_path);
        save_path = with_ext;
    }

    /* ── Step 2: composite snapshot for preview ─────────────────── */
    export_surface = document_export_composite_surface(doc);
    if (!export_surface) {
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    _("Could not prepare the image for export."),
                                    NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        g_free(save_path);
        return;
    }

    /* ── Step 3: options / preview dialog ───────────────────────── */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/dialogs/palette_export_dialog.glade", &err)) {
        debug_log("WRN", "palette_export_dialog.glade load failed: %s",
                  err ? err->message : "unknown");
        if (err) g_error_free(err);
        g_object_unref(builder);
        cairo_surface_destroy(export_surface);
        g_free(save_path);
        return;
    }

    dlg          = GTK_WIDGET(gtk_builder_get_object(builder, "palette_export_dialog"));
    alignment    = GTK_WIDGET(gtk_builder_get_object(builder, "palette_swatches_alignment"));
    controls_box = GTK_WIDGET(gtk_builder_get_object(builder, "color_count_controls_box"));
    auto_toggle  = GTK_WIDGET(gtk_builder_get_object(builder, "auto_count_toggle"));
    custom_toggle= GTK_WIDGET(gtk_builder_get_object(builder, "custom_count_toggle"));
    count_adj    = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "color_count_adjustment"));
    count_scale  = GTK_WIDGET(gtk_builder_get_object(builder, "color_count_scale"));
    count_spin   = GTK_WIDGET(gtk_builder_get_object(builder, "color_count_spin"));
    ok_btn       = GTK_WIDGET(gtk_builder_get_object(builder, "palette_export_ok_button"));
    cancel_btn   = GTK_WIDGET(gtk_builder_get_object(builder, "palette_export_cancel_button"));

    if (!dlg || !alignment || !controls_box || !auto_toggle || !custom_toggle ||
        !count_adj || !count_scale || !count_spin || !ok_btn || !cancel_btn) {
        debug_log("WRN", "palette_export_dialog: missing widget(s) in builder");
        g_object_unref(builder);
        cairo_surface_destroy(export_surface);
        g_free(save_path);
        return;
    }

    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(ctx->window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);
    ui_utils_set_header_bar(GTK_WINDOW(dlg), _("Palette export options"));

    /* Inject SwatchesWidget inside a GtkScrolledWindow, matching the main swatches panel style:
     * shadow-type=in, h-policy=never, v-policy=automatic, propagate-natural-height=true.
     * Size is capped to the frame interior (frame: 285x350) so the widget never overflows. */
    swatches_w = swatches_widget_new();
    swatches_widget_set_spacing(SWATCHES_WIDGET(swatches_w), 1.0);
    swatches_widget_set_padding(SWATCHES_WIDGET(swatches_w), 4.0);
    gtk_widget_set_size_request(swatches_w, -1, -1);

    {
        GtkWidget* scroll_w = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_w),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll_w), GTK_SHADOW_NONE);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll_w), TRUE);
        gtk_widget_set_size_request(scroll_w, 260, 310);
        gtk_widget_set_hexpand(scroll_w, FALSE);
        gtk_widget_set_vexpand(scroll_w, FALSE);
        gtk_container_add(GTK_CONTAINER(scroll_w), swatches_w);
        gtk_container_add(GTK_CONTAINER(alignment), scroll_w);
        gtk_widget_show_all(scroll_w);
    }

    /* Wire dialog state */
    st.dlg            = dlg;
    st.auto_toggle    = auto_toggle;
    st.custom_toggle  = custom_toggle;
    st.controls_box   = controls_box;
    st.count_adj      = count_adj;
    st.swatch_widget  = SWATCHES_WIDGET(swatches_w);
    st.export_surface = export_surface;
    st.updating       = FALSE;
    st.count_held     = FALSE;

    g_signal_connect(auto_toggle,   "toggled", G_CALLBACK(on_palette_dlg_auto_toggled),   &st);
    g_signal_connect(custom_toggle, "toggled", G_CALLBACK(on_palette_dlg_custom_toggled), &st);

    g_signal_connect(count_scale, "button-press-event",   G_CALLBACK(on_palette_dlg_count_press),   &st);
    g_signal_connect(count_scale, "button-release-event", G_CALLBACK(on_palette_dlg_count_release), &st);
    g_signal_connect(count_spin,  "button-press-event",   G_CALLBACK(on_palette_dlg_count_press),   &st);
    g_signal_connect(count_spin,  "button-release-event", G_CALLBACK(on_palette_dlg_count_release), &st);
    g_signal_connect(count_adj,   "value-changed",        G_CALLBACK(on_palette_dlg_count_changed), &st);

    g_object_set_data(G_OBJECT(ok_btn),     "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
    g_object_set_data(G_OBJECT(cancel_btn), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(on_palette_dlg_button_clicked), dlg);
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_palette_dlg_button_clicked), dlg);

    /* Initial palette preview */
    palette_dlg_refresh_swatches(&st);

    gtk_widget_show_all(dlg);
    gtk_widget_hide(controls_box); /* no-show-all: keep hidden in auto mode */

    response = gtk_dialog_run(GTK_DIALOG(dlg));

    /* Read widget state before the dialog is destroyed */
    {
        FilterPaletteExportCountMode dlg_mode;
        gint dlg_count;
        dlg_mode  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st.custom_toggle))
                        ? FILTER_PALETTE_EXPORT_COUNT_CUSTOM
                        : FILTER_PALETTE_EXPORT_COUNT_AUTO;
        dlg_count = (gint)gtk_adjustment_get_value(st.count_adj);
        st.last_mode  = dlg_mode;
        st.last_count = dlg_count;
    }

    gtk_widget_destroy(dlg);
    g_object_unref(builder);

    /* ── Step 4: export ─────────────────────────────────────────── */
    if (response == GTK_RESPONSE_OK) {
        FilterPaletteExportCountMode mode;
        gint   custom_count;
        gboolean ok;

        mode         = st.last_mode;
        custom_count = st.last_count;

        ok = filter_export_palette_from_surface(export_surface, save_path, mode,
                                                custom_count, NULL);
        if (ok) {
            ui_update_status_bar_message(ctx, _("Palette exported"));
        } else {
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        _("Could not export the palette. "
                                          "Check the file path and that the image has "
                                          "at least one non-transparent pixel."),
                                        NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        }
    }

    cairo_surface_destroy(export_surface);
    g_free(save_path);
}

static void on_export_menu_color_profile(GtkWidget* widget, gpointer user_data) {
    AppContext*           ctx = (AppContext*)user_data;
    ImageDocument*        doc;
    GtkFileChooserNative* native;
    GtkFileFilter*        f;
    gchar*                save_path = NULL;
    gint                  fc_response;
    FILE*                 fp;

    (void)widget;

    if (!ctx || !ctx->window) return;

    doc = ui_get_active_document(ctx);
    if (!doc) return;
    if (!doc->original_icc_data || doc->original_icc_size == 0) return;

    native = gtk_file_chooser_native_new(
        _("Export color profile"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Save"),
        _("_Cancel"));
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);

    /* Default filename: strip image extension and add .icc */
    {
        const gchar* base = (doc->filename && doc->filename[0]) ? doc->filename : "profile";
        gchar*       stem = g_strdup(base);
        gchar*       dot  = strrchr(stem, '.');
        if (dot) *dot = '\0';
        gchar* suggested = g_strdup_printf("%s.icc", stem);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), suggested);
        g_free(stem);
        g_free(suggested);

        if (doc->file_path && doc->file_path[0]) {
            gchar* dir = g_path_get_dirname(doc->file_path);
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), dir);
            g_free(dir);
        }
    }

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("ICC Color Profile (*.icc)"));
    gtk_file_filter_add_pattern(f, "*.icc");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("ICM Color Profile (*.icm)"));
    gtk_file_filter_add_pattern(f, "*.icm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f);

    fc_response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (fc_response == GTK_RESPONSE_ACCEPT) {
        save_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
    }
    g_object_unref(native);

    if (!save_path) return;

    fp = g_fopen(save_path, "wb");
    if (fp) {
        fwrite(doc->original_icc_data, 1, doc->original_icc_size, fp);
        fclose(fp);
    } else {
        GtkWidget* err_dlg = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            _("Could not write color profile to \"%s\"."),
            save_path);
        gtk_dialog_run(GTK_DIALOG(err_dlg));
        gtk_widget_destroy(err_dlg);
    }

    g_free(save_path);
}

static void on_export_menu_color_lookup(GtkWidget* widget, gpointer user_data) {
    GtkFileChooserNative* native;
    GtkFileFilter* f_cube;
    GtkFileFilter* f_look;
    GtkFileFilter* f_all;
    GtkFileFilter* sel_filter;
    gint fmt_choice;
    gint response;
    gchar* path;
    gpointer fmt_ptr;

    (void)widget;

    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->window) {
        return;
    }

    {
        ImageDocument* active = ui_get_active_document(ctx);
        if (!active) {
            return;
        }
    }

    native = gtk_file_chooser_native_new(
        _("Export color lookup"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Save"),
        _("_Cancel"));
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), "color-lookup.cube");

    f_cube = gtk_file_filter_new();
    gtk_file_filter_set_name(f_cube, _("Adobe/Resolve cube LUT (*.cube)"));
    gtk_file_filter_add_pattern(f_cube, "*.cube");
    g_object_set_data(G_OBJECT(f_cube), LUT_CHOOSER_FORMAT_KEY, GINT_TO_POINTER((gint)LUT_CHOOSER_FMT_CUBE));
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f_cube);

    f_look = gtk_file_filter_new();
    gtk_file_filter_set_name(f_look, _("SpeedGrade / XML LUT (*.look)"));
    gtk_file_filter_add_pattern(f_look, "*.look");
    g_object_set_data(G_OBJECT(f_look), LUT_CHOOSER_FORMAT_KEY, GINT_TO_POINTER((gint)LUT_CHOOSER_FMT_LOOK));
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f_look);

    f_all = gtk_file_filter_new();
    gtk_file_filter_set_name(f_all, _("All 3D LUTs (*.cube; *.look)"));
    gtk_file_filter_add_pattern(f_all, "*.cube");
    gtk_file_filter_add_pattern(f_all, "*.look");
    g_object_set_data(G_OBJECT(f_all), LUT_CHOOSER_FORMAT_KEY, GINT_TO_POINTER((gint)LUT_CHOOSER_FMT_ANY));
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), f_all);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(native), f_cube);

    response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (response != GTK_RESPONSE_ACCEPT) {
        g_object_unref(native);
        return;
    }

    sel_filter = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(native));
    fmt_ptr = sel_filter ? g_object_get_data(G_OBJECT(sel_filter), LUT_CHOOSER_FORMAT_KEY) : NULL;
    fmt_choice = fmt_ptr ? GPOINTER_TO_INT(fmt_ptr) : LUT_CHOOSER_FMT_ANY;

    path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
    g_object_unref(native);
    if (!path) {
        return;
    }

    if (!lut3d_io_is_supported(path)) {
        const gchar* ext = (fmt_choice == (gint)LUT_CHOOSER_FMT_LOOK) ? ".look" : ".cube";
        if (fmt_choice == (gint)LUT_CHOOSER_FMT_ANY) {
            ext = ".cube";
        }
        {
            gchar* with_ext = g_strconcat(path, ext, NULL);
            g_free(path);
            path = with_ext;
        }
    }

    if (!lut3d_io_is_supported(path)) {
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    _("Choose a .cube or .look filename to export a 3D LUT."),
                                    NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        g_free(path);
        return;
    }

    {
        gchar* dlg_title     = NULL;
        gchar* dlg_copyright = NULL;
        gint   dlg_grid      = 17;

        if (run_color_lookup_options_dialog(GTK_WINDOW(ctx->window),
                                            &dlg_title, &dlg_copyright, &dlg_grid)
                != GTK_RESPONSE_OK) {
            g_free(path);
            g_free(dlg_title);
            g_free(dlg_copyright);
            return;
        }

        {
            ImageDocument* doc = ui_get_active_document(ctx);
            cairo_surface_t* export_surface;
            gboolean ok;

            if (!doc) {
                g_free(path);
                g_free(dlg_title);
                g_free(dlg_copyright);
                return;
            }
            export_surface = document_export_composite_surface(doc);
            if (!export_surface) {
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            _("Could not prepare the image for export."),
                                            NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
                g_free(path);
                g_free(dlg_title);
                g_free(dlg_copyright);
                return;
            }
            {
                /*
                 * LUT export strategy:
                 *
                 *  A) Multi-layer document: treat bottom layer as the "before" image
                 *     and the composite as the "after" image.  The pixel-sampling
                 *     two-surface builder captures the exact per-pixel transform
                 *     applied by all layers above the base.
                 *
                 *  B) Single-layer (or mismatched sizes): use the voxel-bin
                 *     nearest-colour builder on the composite alone.  For each LUT
                 *     grid vertex (its identity colour) the nearest colour that
                 *     actually exists in the reference image is stored as the LUT
                 *     output.  This reproduces the image's colour palette when the
                 *     LUT is applied to any other image — no original file needed.
                 */
                guint  nlayers = document_get_layer_count(doc);
                gint   cw      = cairo_image_surface_get_width(export_surface);
                gint   ch      = cairo_image_surface_get_height(export_surface);

                if (nlayers > 1) {
                    /* Strategy A: two-surface pixel sampling */
                    ImageLayer*      base_layer = document_get_layer(doc, 0);
                    cairo_surface_t* base_surf  = (base_layer && base_layer->surface)
                                                  ? base_layer->surface : NULL;
                    gboolean sizes_ok = base_surf
                        && cairo_image_surface_get_width(base_surf)  == cw
                        && cairo_image_surface_get_height(base_surf) == ch;
                    if (sizes_ok) {
                        ok = filter_save_3d_lut_from_two_surfaces(
                                 base_surf, export_surface, path,
                                 dlg_title, dlg_copyright, dlg_grid);
                    } else {
                        ok = filter_save_3d_lut_from_image(
                                 export_surface, path,
                                 dlg_title, dlg_copyright, dlg_grid);
                    }
                } else {
                    /* Strategy B: voxel-bin nearest-colour from single image */
                    ok = filter_save_3d_lut_from_image(
                             export_surface, path,
                             dlg_title, dlg_copyright, dlg_grid);
                }
            }
            cairo_surface_destroy(export_surface);
            if (ok) {
                ui_update_status_bar_message(ctx, _("Color lookup exported"));
            } else {
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            _("Could not export the color lookup file. "
                                              "The image may need at least one non-transparent pixel, "
                                              "or the path may be invalid."),
                                            NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
            }
        }

        g_free(dlg_title);
        g_free(dlg_copyright);
    }

    g_free(path);
}

void ui_file_menu_update_sensitivity(AppContext* ctx) {
    ImageDocument* doc;
    gboolean has_document;
    gboolean can_save;
    gboolean can_save_as;
    gboolean can_close;
    gboolean can_close_all;
    gboolean can_open_recent;
    const GList* recent_files;

    if (!ctx || !ctx->window) {
        return;
    }

    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);

    can_save = has_document && document_is_dirty(doc);
    can_save_as = has_document;
    can_close = has_document;
    can_close_all = (ctx->documents != NULL && g_list_length(ctx->documents) > 0);

    recent_files = recent_files_get();
    can_open_recent = (recent_files != NULL && g_list_length((GList*)recent_files) > 0);

    if (ctx->file_menu_new && GTK_IS_WIDGET(ctx->file_menu_new)) {
        gtk_widget_set_sensitive(ctx->file_menu_new, TRUE);
    }
    if (ctx->file_menu_import_clipboard && GTK_IS_WIDGET(ctx->file_menu_import_clipboard)) {
        GtkClipboard* clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gboolean clipboard_has_image = clip && gtk_clipboard_wait_is_image_available(clip);
        gtk_widget_set_sensitive(ctx->file_menu_import_clipboard, clipboard_has_image);
    }
    if (ctx->file_menu_open && GTK_IS_WIDGET(ctx->file_menu_open)) {
        gtk_widget_set_sensitive(ctx->file_menu_open, TRUE);
    }
    if (ctx->file_menu_open_recent && GTK_IS_WIDGET(ctx->file_menu_open_recent)) {
        gtk_widget_set_sensitive(ctx->file_menu_open_recent, can_open_recent);
    }
    if (ctx->file_menu_save && GTK_IS_WIDGET(ctx->file_menu_save)) {
        gtk_widget_set_sensitive(ctx->file_menu_save, can_save);
    }
    if (ctx->file_menu_save_as && GTK_IS_WIDGET(ctx->file_menu_save_as)) {
        gtk_widget_set_sensitive(ctx->file_menu_save_as, can_save_as);
    }
    if (ctx->file_menu_revert && GTK_IS_WIDGET(ctx->file_menu_revert)) {
        gboolean can_revert = has_document && doc->file_path && doc->file_path[0] != '\0';
        gtk_widget_set_sensitive(ctx->file_menu_revert, can_revert);
    }
    if (ctx->file_menu_export && GTK_IS_WIDGET(ctx->file_menu_export)) {
        gtk_widget_set_sensitive(ctx->file_menu_export, can_save_as);
    }
    if (ctx->export_menu_color_lookup && GTK_IS_WIDGET(ctx->export_menu_color_lookup)) {
        gtk_widget_set_sensitive(ctx->export_menu_color_lookup, can_save_as);
    }
    if (ctx->export_menu_color_profile && GTK_IS_WIDGET(ctx->export_menu_color_profile)) {
        gboolean has_icc = has_document && doc->original_icc_data && doc->original_icc_size > 0;
        gtk_widget_set_sensitive(ctx->export_menu_color_profile, has_icc);
    }
    if (ctx->export_menu_palette && GTK_IS_WIDGET(ctx->export_menu_palette)) {
        gtk_widget_set_sensitive(ctx->export_menu_palette, can_save_as);
    }
    if (ctx->file_menu_close && GTK_IS_WIDGET(ctx->file_menu_close)) {
        gtk_widget_set_sensitive(ctx->file_menu_close, can_close);
    }
    if (ctx->file_menu_close_all && GTK_IS_WIDGET(ctx->file_menu_close_all)) {
        gtk_widget_set_sensitive(ctx->file_menu_close_all, can_close_all);
    }
    if (ctx->file_menu_exit && GTK_IS_WIDGET(ctx->file_menu_exit)) {
        gtk_widget_set_sensitive(ctx->file_menu_exit, TRUE);
    }
}

/**
 * Update the "Open Recent" submenu with current recent files
 */
void ui_update_recent_files_menu(AppContext* ctx) {
    if (!ctx || !ctx->window) {
        return;
    }

    /* Get the file menu from the window */
    GtkBuilder* builder = (GtkBuilder*)g_object_get_data(G_OBJECT(ctx->window), "main_builder");
    if (!builder) {
        return;
    }

    GtkWidget* file_menu = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu"));
    if (!file_menu) {
        return;
    }

    GtkWidget* recent_submenu = (GtkWidget*)g_object_get_data(G_OBJECT(file_menu), "recent_files_submenu");
    if (!recent_submenu) {
        /* Submenu not created yet - this is okay, just return */
        return;
    }

    if (!GTK_IS_MENU(recent_submenu)) {
        debug_log("WRN", "recent_files_submenu is not a GtkMenu");
        return;
    }

    /* Clear existing menu items and free stored file paths */
    if (GTK_IS_CONTAINER(recent_submenu)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(recent_submenu));
        for (GList* iter = children; iter; iter = iter->next) {
            GtkWidget* widget = GTK_WIDGET(iter->data);
            if (widget) {
                gchar* stored_path = (gchar*)g_object_get_data(G_OBJECT(widget), "recent_file_path");
                if (stored_path) {
                    g_free(stored_path);
                }
                gtk_widget_destroy(widget);
            }
        }
        g_list_free(children);
    }

    /* Get recent files list from recent_files system */
    const GList* recent_files = recent_files_get();

    if (!recent_files || g_list_length((GList*)recent_files) == 0) {
        /* No recent files - disable the menu item */
        GtkWidget* file_menu_open_recent = ctx->file_menu_open_recent;
        if (!file_menu_open_recent) {
            file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
        }
        if (file_menu_open_recent) {
            gtk_widget_set_sensitive(file_menu_open_recent, FALSE);
        }
        return;
    }

    /* Enable the menu item */
    {
        GtkWidget* file_menu_open_recent = ctx->file_menu_open_recent;
        if (!file_menu_open_recent) {
            file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
        }
        if (file_menu_open_recent) {
            gtk_widget_set_sensitive(file_menu_open_recent, TRUE);
        }
    }

    /* Add menu items for each recent file */
    for (const GList* iter = recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        FormatHandler* handler;
        gchar* basename;
        uint8_t header[FILE_HEADER_PROBE_SIZE];
        size_t header_size = 0;
        FILE* file;
        gboolean file_exists;

        if (!rf || !rf->path) {
            continue;
        }

        /* Check if file exists */
        file_exists = g_file_test(rf->path, G_FILE_TEST_EXISTS);

        /* Try to detect format if file exists */
        handler = NULL;
        if (file_exists) {
            file = g_fopen(rf->path, "rb");
            if (file) {
                header_size = fread(header, 1, sizeof(header), file);
                fclose(file);
            }

            /* Find plugin handler for this file */
            handler = format_registry_find_loader(rf->path, header, header_size);
        }

        /* Get filename for display */
        basename = g_path_get_basename(rf->path);
        GtkWidget* menu_item = gtk_menu_item_new_with_label(basename);
        g_free(basename);

        /* Set tooltip to full path */
        gtk_widget_set_tooltip_text(menu_item, rf->path);

        /* Disable menu item if file doesn't exist or no plugin can handle it */
        if (!file_exists || !handler) {
            gtk_widget_set_sensitive(menu_item, FALSE);
        }

        /* Store file path in menu item data */
        g_object_set_data(G_OBJECT(menu_item), "recent_file_path", g_strdup(rf->path));

        /* Connect activate signal */
        g_signal_connect(menu_item, "activate", G_CALLBACK(on_recent_file_activate), ctx);

        gtk_menu_shell_append(GTK_MENU_SHELL(recent_submenu), menu_item);
        gtk_widget_show(menu_item);
    }

    /* Add separator */
    GtkWidget* separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(recent_submenu), separator);
    gtk_widget_show(separator);

    /* Add "Clear Recent Files" item */
    GtkWidget* clear_item = gtk_menu_item_new_with_label(_("Clear Recent Files"));
    g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear_recent_files), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(recent_submenu), clear_item);
    gtk_widget_show(clear_item);
}

/**
 * Setup File menu from Glade builder
 */
void ui_file_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* file_menu = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu"));
    GtkWidget* file_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_item"));

    if (file_menu && file_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
    }

    /* Connect File menu signals */
    ctx->file_menu_new = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_new"));
    ctx->file_menu_import = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_import"));
    ctx->file_menu_import_clipboard = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_import_clipboard"));
    ctx->file_menu_open = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open"));
    ctx->file_menu_open_recent = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_open_recent"));
    ctx->file_menu_save = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_save"));
    ctx->file_menu_save_as = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_save_as"));
    ctx->file_menu_revert = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_revert"));
    ctx->file_menu_export          = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_export"));
    ctx->export_menu_color_lookup  = GTK_WIDGET(gtk_builder_get_object(builder, "export_menu_color_lookup"));
    ctx->export_menu_color_profile = GTK_WIDGET(gtk_builder_get_object(builder, "export_menu_color_profile"));
    ctx->export_menu_palette       = GTK_WIDGET(gtk_builder_get_object(builder, "export_menu_palette"));
    ctx->file_menu_close = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_close"));
    ctx->file_menu_close_all = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_close_all"));
    ctx->file_menu_exit = GTK_WIDGET(gtk_builder_get_object(builder, "file_menu_exit"));

    if (ctx->file_menu_new) {
        g_signal_connect(ctx->file_menu_new, "activate", G_CALLBACK(on_file_new), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_new, "activate", accel_group,
                                   GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    if (ctx->file_menu_import_clipboard) {
        g_signal_connect(ctx->file_menu_import_clipboard, "activate",
                         G_CALLBACK(on_edit_paste_new_image), ctx);
    }

    if (ctx->file_menu_open) {
        g_signal_connect(ctx->file_menu_open, "activate", G_CALLBACK(on_file_open), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_open, "activate", accel_group,
                                   GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    /* Setup "Open Recent" submenu */
    if (ctx->file_menu_open_recent) {
        if (!file_menu) {
            debug_log("WRN", "file_menu is NULL, cannot setup Open Recent submenu");
        } else {
            GtkWidget* recent_submenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(ctx->file_menu_open_recent), recent_submenu);
            g_object_set_data(G_OBJECT(file_menu), "recent_files_submenu", recent_submenu);
            /* Don't update menu here - it will be updated after window is fully created */
        }
    }

    if (ctx->file_menu_save) {
        g_signal_connect(ctx->file_menu_save, "activate", G_CALLBACK(on_file_save), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_save, "activate", accel_group,
                                   GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }

    if (ctx->file_menu_save_as) {
        g_signal_connect(ctx->file_menu_save_as, "activate", G_CALLBACK(on_file_save_as), ctx);
        gtk_widget_add_accelerator(ctx->file_menu_save_as, "activate", accel_group,
                                   GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    }
    if (ctx->file_menu_revert) {
        g_signal_connect(ctx->file_menu_revert, "activate", G_CALLBACK(on_file_revert), ctx);
    }
    if (ctx->export_menu_color_lookup) {
        g_signal_connect(ctx->export_menu_color_lookup, "activate", G_CALLBACK(on_export_menu_color_lookup), ctx);
    }
    if (ctx->export_menu_color_profile) {
        g_signal_connect(ctx->export_menu_color_profile, "activate", G_CALLBACK(on_export_menu_color_profile), ctx);
    }
    if (ctx->export_menu_palette) {
        g_signal_connect(ctx->export_menu_palette, "activate", G_CALLBACK(on_export_menu_palette), ctx);
    }
    if (ctx->file_menu_close) {
        g_signal_connect(ctx->file_menu_close, "activate", G_CALLBACK(on_file_close), ctx);
    }
    if (ctx->file_menu_close_all) {
        g_signal_connect(ctx->file_menu_close_all, "activate", G_CALLBACK(on_file_close_all), ctx);
    }
    if (ctx->file_menu_exit) {
        g_signal_connect(ctx->file_menu_exit, "activate", G_CALLBACK(on_file_exit), ctx);
    }
}

/**
 * File Open dialog response callback
 */
void on_file_open_response(GtkNativeDialog* dialog, gint response_id, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar* file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (file_path) {
            /* Check if a plugin can handle this file format */
            FormatHandler* handler = NULL;
            uint8_t header[FILE_HEADER_PROBE_SIZE];
            size_t header_size = 0;
            FILE* file = g_fopen(file_path, "rb");
            if (file) {
                header_size = fread(header, 1, sizeof(header), file);
                fclose(file);
            }

            handler = format_registry_find_loader(file_path, header, header_size);
            if (!handler) {
                /* No plugin can handle this file format */
                gchar* msg = g_strdup_printf(_("Unsupported file format: %s"), file_path);
                ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                            msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
                g_free(msg);
                g_free(file_path);
                g_object_unref(dialog);
                return;
            }

            ui_file_menu_open_path_as_new_document(ctx, file_path);
            g_free(file_path);
        }
    }

    g_object_unref(dialog);
}

/**
 * File > Open callback
 */
void on_file_open(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    if (!ctx) {
        debug_log("WRN", "Invalid context in on_file_open");
        return;
    }
    if (!ctx->window || !GTK_IS_WINDOW(ctx->window)) {
        debug_log("WRN", "Invalid window in on_file_open");
        return;
    }
    GtkFileChooserNative* native_dialog;
    GtkFileFilter* filter;
    GList* handlers;
    GHashTable* seen_formats;
    gchar* all_patterns;

    /* Create native file chooser dialog */
    native_dialog = gtk_file_chooser_native_new(
        _("Open Image"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Open"),
        _("_Cancel"));

    /* Get all registered format handlers from plugin system */
    handlers = format_registry_get_all_handlers();
    seen_formats = g_hash_table_new(g_str_hash, g_str_equal);

    /* Create "All Supported Images" filter with all patterns */
    all_patterns = format_registry_get_file_filter_patterns();
    if (all_patterns && strlen(all_patterns) > 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("All Supported Images"));
        /* Parse patterns (semicolon-separated) and add each */
        gchar** patterns = g_strsplit(all_patterns, ";", -1);
        for (gint i = 0; patterns[i]; i++) {
            g_strstrip(patterns[i]);
            if (strlen(patterns[i]) > 0) {
                gtk_file_filter_add_pattern(filter, patterns[i]);
            }
        }
        g_strfreev(patterns);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
        g_free(all_patterns);
    }

    /* Create individual filters for each format */
    if (handlers) {
        for (GList* iter = handlers; iter; iter = iter->next) {
            FormatHandler* handler = (FormatHandler*)iter->data;
            if (!handler || !handler->format_info.name || !handler->format_info.extensions) {
                continue;
            }

            /* Skip if we've already added a filter for this format name */
            if (g_hash_table_contains(seen_formats, handler->format_info.name)) {
                continue;
            }
            g_hash_table_insert(seen_formats, (gpointer)handler->format_info.name, GINT_TO_POINTER(1));

            /* Create filter for this format */
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, handler->format_info.name);

            /* Parse extensions (comma-separated) and add patterns */
            gchar** exts = g_strsplit(handler->format_info.extensions, ",", -1);
            for (gint i = 0; exts[i]; i++) {
                g_strstrip(exts[i]);
                if (strlen(exts[i]) > 0) {
                    gchar* pattern = g_strdup_printf("*.%s", exts[i]);
                    gtk_file_filter_add_pattern(filter, pattern);
                    g_free(pattern);
                }
            }
            g_strfreev(exts);

            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
        }
    }

    g_hash_table_destroy(seen_formats);

    /* Add fallback "All Files" filter if no formats registered */
    if (!handlers || g_list_length(handlers) == 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("All Files"));
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
    }

    /* Connect response signal */
    g_signal_connect(native_dialog, "response", G_CALLBACK(on_file_open_response), ctx);

    /* Show dialog */
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native_dialog));
}

/**
 * File > Save callback
 */
void on_file_save(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (!doc) {
        debug_log("WRN", "No document open");
        return;
    }

    /* Check if document is dirty - if not, nothing to save */
    if (!document_is_dirty(doc)) {
        return; /* Nothing to save */
    }

    /* If document doesn't have a filename, trigger Save As */
    if (!doc->file_path || strlen(doc->file_path) == 0) {
        on_file_save_as(NULL, ctx);
        return;
    }

    /* Save using existing filename with default options */
    SaveOptions default_opts;
    FormatHandler* handler;
    void* plugin_data = NULL;
    size_t plugin_options_size = 0;

    memset(&default_opts, 0, sizeof(SaveOptions));
    default_opts.quality = -1;           /* Use default */
    default_opts.compression_level = -1; /* Use default */
    default_opts.preserve_alpha = doc->has_alpha ? true : false;
    default_opts.flatten_layers = FALSE;

    /* Find handler to determine if we need plugin-specific options */
    handler = format_registry_find_saver(doc->file_path);
    if (handler && handler->plugin && handler->plugin->callbacks.get_save_options_size) {
        plugin_options_size = handler->plugin->callbacks.get_save_options_size();
        if (plugin_options_size > 0) {
            plugin_data = g_malloc0(plugin_options_size);
            if (plugin_data && handler->plugin->callbacks.init_save_options) {
                /* Initialize plugin-specific options with defaults */
                /* For PNG, init_png_save_options already sets transparency_format to PNG_TRANSPARENCY_AUTO */
                handler->plugin->callbacks.init_save_options(plugin_data);
            }
            default_opts.plugin_data = plugin_data;
        }
    } else {
        default_opts.plugin_data = NULL;
    }

    PluginError save_error = PLUGIN_ERROR_NONE;
    if (document_save_as_with_progress(ctx, doc, doc->file_path, &default_opts, &save_error)) {
        /* Mark document as saved */
        document_mark_saved(doc);

        /* Update window title and status bar */
        ui_update_document_tab_label(ctx, doc);
        ui_update_window_title(ctx, NULL);
        ui_update_status_bar(ctx, NULL);
        ui_update_status_bar_message(ctx, _("Image successfully saved"));

        /* Update menu states */
        ui_update_menu_and_button_states(ctx);
    } else {
        /* Get user-friendly error message */
        const char* error_message = image_io_get_error_message(save_error, doc->file_path);

        /* Show error dialog */
        gchar* msg = g_strdup_printf(_("Failed to save image: %s\n\n%s"),
                                     doc->file_path, error_message);
        ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                    msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
        g_free(msg);
    }

    /* Clean up plugin data */
    if (plugin_data) {
        g_free(plugin_data);
    }
}

/**
 * Process save as result after native file chooser dialog
 */
static void process_save_as_result(GtkNativeDialog* native_dialog, AppContext* ctx) {
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(native_dialog);
    gchar* file_path = gtk_file_chooser_get_filename(chooser);

    if (file_path) {
        ImageDocument* doc = ui_get_active_document(ctx);

        if (doc) {
            SaveOptions opts;
            FormatHandler* handler;
            void* plugin_data = NULL;
            size_t plugin_options_size = 0;
            gboolean dialog_result;

            /* Initialize save options */
            memset(&opts, 0, sizeof(SaveOptions));
            opts.quality = -1;
            opts.compression_level = -1;
            opts.preserve_alpha = doc->has_alpha ? true : false;
            opts.flatten_layers = FALSE;

            /* Find handler to determine if we need plugin-specific options */
            handler = format_registry_find_saver(file_path);
            if (handler && handler->plugin && handler->plugin->callbacks.get_save_options_size) {
                plugin_options_size = handler->plugin->callbacks.get_save_options_size();
                if (plugin_options_size > 0) {
                    plugin_data = g_malloc0(plugin_options_size);
                    if (plugin_data && handler->plugin->callbacks.init_save_options) {
                        handler->plugin->callbacks.init_save_options(plugin_data);
                    }
                    opts.plugin_data = plugin_data;
                }
            }

            /* Show save options dialog if needed (after file chooser is closed) */
            dialog_result = save_options_dialog_show(GTK_WINDOW(ctx->window), file_path, &opts, doc);

            if (dialog_result) {
                /* User clicked OK, proceed with save */
                PluginError save_error = PLUGIN_ERROR_NONE;
                if (document_save_as_with_progress(ctx, doc, file_path, &opts, &save_error)) {
                    /* Update document filename and path */
                    if (doc->file_path) {
                        g_free(doc->file_path);
                    }
                    if (doc->filename) {
                        g_free(doc->filename);
                    }
                    doc->file_path = g_strdup(file_path);
                    doc->filename = g_path_get_basename(file_path);

                    /* Mark document as saved */
                    document_mark_saved(doc);

                    /* Add to recent files */
                    recent_files_add(file_path);
                    recent_files_save(); /* This syncs to settings if connected */

                    /* Update tab label to reflect new filename */
                    ui_update_document_tab_label(ctx, doc);

                    /* Update window title to reflect new filename */
                    ui_update_window_title(ctx, NULL);
                    ui_update_status_bar(ctx, NULL);
                    ui_update_status_bar_message(ctx, _("Image successfully saved"));
                    ui_update_recent_files_menu(ctx);

                    /* Update menu states */
                    ui_update_menu_and_button_states(ctx);
                } else {
                    /* Get user-friendly error message */
                    const char* error_message = image_io_get_error_message(save_error, file_path);

                    /* Show error dialog */
                    gchar* msg = g_strdup_printf(_("Failed to save image: %s\n\n%s"),
                                                 file_path, error_message);
                    ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                                msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
                    g_free(msg);
                }
            }
            /* If dialog_result is FALSE, user cancelled, so don't save */

            /* Clean up plugin data */
            if (plugin_data) {
                g_free(plugin_data);
            }
        }

        g_free(file_path);
    }
}

static gchar* format_handler_primary_extension(const FormatHandler* handler) {
    if (!handler || !handler->format_info.extensions) {
        return NULL;
    }
    gchar** exts = g_strsplit(handler->format_info.extensions, ",", -1);
    gchar* out = NULL;
    for (gint i = 0; exts[i]; i++) {
        g_strstrip(exts[i]);
        if (exts[i][0]) {
            out = g_strdup(exts[i]);
            break;
        }
    }
    g_strfreev(exts);
    return out;
}

/**
 * File > Save As callback
 */
void on_file_save_as(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    GtkFileChooserNative* native_dialog;
    GtkFileFilter* filter;
    GList* handlers;
    GHashTable* seen_formats;
    gint response;

    if (!doc) {
        debug_log("WRN", "No document open");
        return;
    }

    /* Create native file chooser dialog */
    native_dialog = gtk_file_chooser_native_new(
        _("Save Image As"),
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Save"),
        _("_Cancel"));

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native_dialog), TRUE);

    /* Get all registered format handlers from plugin system */
    handlers = format_registry_get_all_handlers();
    seen_formats = g_hash_table_new(g_str_hash, g_str_equal);

    /* Track filters for default selection */
    GtkFileFilter* first_filter = NULL;
    GtkFileFilter* png_filter = NULL;
    FormatHandler* first_handler = NULL;
    FormatHandler* png_handler = NULL;

    /* Create filters for each format */
    if (handlers) {
        for (GList* iter = handlers; iter; iter = iter->next) {
            FormatHandler* handler = (FormatHandler*)iter->data;
            if (!handler || !handler->format_info.name || !handler->format_info.extensions) {
                continue;
            }

            /* Skip if plugin doesn't support saving */
            if (!handler->plugin || !handler->plugin->callbacks.save ||
                !handler->plugin->callbacks.can_save) {
                continue;
            }

            /* Skip if we've already added a filter for this format name */
            if (g_hash_table_contains(seen_formats, handler->format_info.name)) {
                continue;
            }
            g_hash_table_insert(seen_formats, (gpointer)handler->format_info.name, GINT_TO_POINTER(1));

            /* Create filter for this format */
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, handler->format_info.name);

            /* Parse extensions (comma-separated) and add patterns */
            gchar** exts = g_strsplit(handler->format_info.extensions, ",", -1);
            for (gint i = 0; exts[i]; i++) {
                g_strstrip(exts[i]);
                if (strlen(exts[i]) > 0) {
                    gchar* pattern = g_strdup_printf("*.%s", exts[i]);
                    gtk_file_filter_add_pattern(filter, pattern);
                    g_free(pattern);
                }
            }
            g_strfreev(exts);

            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);

            /* Track first filter */
            if (!first_filter) {
                first_filter = filter;
                first_handler = handler;
            }

            /* Track PNG filter if found (prefer as default) */
            if (!png_filter && handler->format_info.name &&
                g_str_has_prefix(handler->format_info.name, "PNG")) {
                png_filter = filter;
                png_handler = handler;
            }
        }
    }

    g_hash_table_destroy(seen_formats);

    /* Add fallback "All Files" filter if no formats registered */
    if (!handlers || g_list_length(handlers) == 0) {
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, _("All Files"));
        gtk_file_filter_add_pattern(filter, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native_dialog), filter);
        first_filter = filter;
    }

    /* Determine default filter (prefer PNG, otherwise use first filter) */
    GtkFileFilter* default_filter = png_filter ? png_filter : first_filter;
    FormatHandler* default_handler = png_handler ? png_handler : first_handler;
    gchar* default_ext = NULL;

    if (default_handler) {
        default_ext = format_handler_primary_extension(default_handler);
        if (!default_ext) {
            default_ext = g_strdup("png");
        }
    }

    /* Set default filter (repeat after filename below: native dialogs often snap type to extension) */
    if (default_filter) {
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(native_dialog), default_filter);
    }

    /* Suggested name must use the same extension as the default filter, or save uses the path
     * extension (format_registry_find_saver) and native UI shows the source format. */
    if (doc->file_path) {
        if (default_ext) {
            gchar* dir = g_path_get_dirname(doc->file_path);
            gchar* base = g_path_get_basename(doc->file_path);
            gchar* dot = strrchr(base, '.');
            if (dot) {
                *dot = '\0';
            }
            gchar* new_name = g_strdup_printf("%s.%s", base, default_ext);
            gchar* full = g_build_filename(dir, new_name, NULL);
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(native_dialog), full);
            g_free(dir);
            g_free(base);
            g_free(new_name);
            g_free(full);
        } else {
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(native_dialog), doc->file_path);
        }
    } else if (doc->filename) {
        if (default_ext) {
            gchar* base = g_strdup(doc->filename);
            gchar* dot = strrchr(base, '.');
            if (dot) {
                *dot = '\0';
            }
            gchar* suggested = g_strdup_printf("%s.%s", base, default_ext);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native_dialog), suggested);
            g_free(base);
            g_free(suggested);
        } else {
            gchar* suggested = g_strdup_printf("%s.png", doc->filename);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native_dialog), suggested);
            g_free(suggested);
        }
    }

    if (default_filter) {
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(native_dialog), default_filter);
    }

    g_free(default_ext);

    /* Show dialog and get response */
    response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native_dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        /* User clicked Save */
        process_save_as_result(GTK_NATIVE_DIALOG(native_dialog), ctx);
    }

    /* Clean up native dialog */
    g_object_unref(native_dialog);
}

/**
 * File > Close callback
 */
void on_file_close(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* active_doc = ui_get_active_document(ctx);

    if (active_doc) {
        ui_close_document_tab(ctx, active_doc);
    }
}

/**
 * File > Close All callback
 * Closes all document tabs. Stops if user cancels a save prompt on any document.
 */
void on_file_close_all(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;

    while (ctx->documents != NULL) {
        ImageDocument* doc = (ImageDocument*)ctx->documents->data;
        guint n_before = g_list_length(ctx->documents);

        ui_close_document_tab(ctx, doc);

        /* If list length unchanged, user cancelled save prompt - stop closing */
        if (g_list_length(ctx->documents) == n_before) {
            break;
        }
    }
}

/**
 * Window delete event callback
 */
gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data) {
    (void)widget; /* Unused */
    (void)event;  /* Unused */

    AppContext* ctx = (AppContext*)data;

    /* SAVE SETTINGS FIRST - before any cleanup */
    if (ctx && ctx->settings && ctx->app_dir) {
        /* Sync recent files to settings before saving */
        recent_files_save(); /* This will sync to settings if connected */

        /* Save all current tool options to settings before final save */
        if (ctx->tool_registry) {
            ui_save_all_tool_options_to_settings(ctx);
        }

        /* Sync widgets to swatches data before saving */
        GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
        GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
        swatches_sync_from_widgets(&ctx->swatches, main_widget, recent_widget);
        /* Save swatches to file */
        swatches_save(&ctx->swatches, ctx->app_dir);

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
    gtk_main_quit();

    return FALSE; /* Allow window to close */
}

/**
 * File > New callback
 */
void on_file_new(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    NewImageDialog* dialog;
    NewImageDialogResult* result;
    gint response;
    ImageDocument* doc;
    ImageLayer* background_layer;
    LayersPanel* layers_panel;
    const gdouble* custom_color = NULL;

    if (!ctx) {
        debug_log("WRN", "Invalid application context");
        return;
    }

    if (!ctx->window || !GTK_IS_WINDOW(ctx->window)) {
        debug_log("WRN", "Invalid main window");
        return;
    }

    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");

    /* Create and show new image dialog */
    dialog = new_image_dialog_new();
    if (!dialog) {
        debug_log("WRN", "Failed to create new image dialog");
        return;
    }

    response = new_image_dialog_run(dialog, GTK_WINDOW(ctx->window), &result);

    if (response == GTK_RESPONSE_OK && result) {
        /* Validate dimensions */
        if (result->width == 0 || result->height == 0) {
            gchar* msg = g_strdup_printf(
                _("Invalid image dimensions: %u x %u\n\nWidth and height must be greater than 0."),
                result->width, result->height);
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        msg, NULL, GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
            g_free(msg);
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Get custom color if needed */
        if (result->background == LAYER_BACKGROUND_CUSTOM) {
            custom_color = result->custom_color;
        }

        /* Create new document */
        doc = ui_create_document_without_tab(ctx, _("Untitled"));
        if (!doc) {
            debug_log("WRN", "Failed to create document");
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Set document dimensions */
        doc->width = result->width;
        doc->height = result->height;
        doc->has_alpha = (result->background == LAYER_BACKGROUND_TRANSPARENT) ? TRUE : FALSE;
        doc->channels = doc->has_alpha ? 4 : 3;
        doc->bit_depth = 8;

        /* Initialize rendering structures */
        if (!document_init_rendering_structures(doc)) {
            ui_utils_message_dialog_run(GTK_WINDOW(ctx->window), GTK_MESSAGE_ERROR,
                                        _("Failed to initialize document rendering structures"), NULL,
                                        GTK_RESPONSE_OK, _("_OK"), GTK_RESPONSE_OK, NULL);
            document_free(doc);
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Create background layer */
        background_layer = layer_new(_("Background"), doc->width, doc->height, TRUE,
                                     result->background, LAYER_POSITION_ABOVE_CURRENT,
                                     custom_color, doc);
        if (!background_layer) {
            debug_log("WRN", "Failed to create background layer");
            document_free(doc);
            new_image_dialog_result_free(result);
            new_image_dialog_free(dialog);
            return;
        }

        /* Add layer to document */
        doc->layers = g_list_append(doc->layers, background_layer);
        document_set_selected_layer(doc, background_layer);

        /* Mark composite as needing re-render */
        document_invalidate_composite(doc);

        /* Add document to notebook */
        ui_add_document_to_notebook(ctx, doc);

        /* Register document for autosave */
        autosave_register_document(doc);

        /* Update drawing area size to match image dimensions */
        if (doc->drawing_area) {
            gint display_width = (gint)(doc->width * doc->zoom_factor);
            gint display_height = (gint)(doc->height * doc->zoom_factor);
            gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
            gtk_widget_queue_draw(doc->drawing_area);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        ui_update_status_bar(ctx, NULL);
        ui_update_status_bar_message(ctx, _("New image created"));

        /* Free dialog result */
        new_image_dialog_result_free(result);
    }

    /* Free dialog */
    new_image_dialog_free(dialog);
}

/**
 * File > Exit callback
 */
void on_file_exit(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;

    /* SAVE SETTINGS FIRST - before any cleanup */
    if (ctx && ctx->settings && ctx->app_dir) {
        /* Sync recent files to settings before saving */
        recent_files_save(); /* This will sync to settings if connected */

        /* Save all current tool options to settings before final save */
        if (ctx->tool_registry) {
            ui_save_all_tool_options_to_settings(ctx);
        }

        /* Sync widgets to swatches data before saving */
        GtkWidget* main_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "main_swatches_widget");
        GtkWidget* recent_widget = (GtkWidget*)g_object_get_data(G_OBJECT(ctx->window), "recent_colors_widget");
        swatches_sync_from_widgets(&ctx->swatches, main_widget, recent_widget);
        /* Save swatches to file */
        swatches_save(&ctx->swatches, ctx->app_dir);

        /* Save all settings to file */
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Exit GTK main loop - let main() handle cleanup */
    /* Don't free context here - main() will handle it */
    gtk_main_quit();
}
