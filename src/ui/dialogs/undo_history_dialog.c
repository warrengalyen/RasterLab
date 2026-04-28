/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/**
 * @file undo_history_dialog.c
 * @brief Undo history dialog — shows all undo/redo states with thumbnails.
 */

#include "ui/dialogs/undo_history_dialog.h"
#include "command.h" /* CommandStack, command_stack_size */
#include "debug_logger.h"
#include "document.h" /* ImageDocument, document_undo, document_redo */
#include "i18n.h"
#include "ui/ui_utils.h"

#include <cairo/cairo.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

static void on_response_button_clicked(GtkButton* button, gpointer user_data) {
    GtkDialog* dlg = GTK_DIALOG(user_data);
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    gtk_dialog_response(dlg, response_id);
}

/* Thumbnail display size in the list rows */
#define ROW_THUMB_SIZE 52

/* ──────────────────────────────────────────────────────────────────────────
 * Internal data structures
 * ────────────────────────────────────────────────────────────────────────── */

/* Per-row data attached to each GtkListBoxRow */
typedef struct {
    Command* cmd;        /* NULL for the synthetic "Original image" row */
    gboolean is_current; /* TRUE for the row matching the current doc state */
    guint index;         /* 0-based display index (used to compute navigation) */
} RowData;

/* State passed into draw callbacks */
typedef struct {
    Command* cmd;               /* Non-NULL for undo/redo rows; NULL for the "Original image" row */
    ImageDocument* doc_initial; /* Non-NULL ONLY for the "Original image" row.
                                  * Dereferenced at draw time (doc->initial_thumbnail) so we always
                                  * use the current live pointer rather than a potentially stale one
                                  * captured at dialog-build time — avoids use-after-free when the
                                  * worker replaces initial_thumbnail while the dialog is rendering. */
    gboolean is_current;
} DrawData;

/* ──────────────────────────────────────────────────────────────────────────
 * Drawing area callbacks
 * ────────────────────────────────────────────────────────────────────────── */

static gboolean on_thumb_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    (void)widget;
    DrawData* dd = (DrawData*)user_data;
    if (!dd) {
        return FALSE;
    }

    /* Resolve surface at draw time — never hold a stored pointer that can
     * become stale if document_process_thumbnail_completions replaces a surface. */
    cairo_surface_t* surf;
    if (dd->doc_initial) {
        surf = dd->doc_initial->initial_thumbnail; /* always the current live surface */
    } else {
        surf = dd->cmd ? dd->cmd->thumbnail : NULL;
    }

    if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
        /* Draw the thumbnail centred in the 52×52 box */
        gint w = cairo_image_surface_get_width(surf);
        gint h = cairo_image_surface_get_height(surf);
        gdouble ox = (ROW_THUMB_SIZE - w) / 2.0;
        gdouble oy = (ROW_THUMB_SIZE - h) / 2.0;
        cairo_set_source_surface(cr, surf, ox, oy);
        cairo_paint(cr);
    } else {
        /* Placeholder: light grey fill with a subtle border */
        cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
        cairo_rectangle(cr, 1, 1, ROW_THUMB_SIZE - 2, ROW_THUMB_SIZE - 2);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
        cairo_rectangle(cr, 0.5, 0.5, ROW_THUMB_SIZE - 1, ROW_THUMB_SIZE - 1);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    return FALSE;
}

static void draw_data_free(gpointer data) {
    g_free(data);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Row builder
 * ────────────────────────────────────────────────────────────────────────── */

/*
 * Build one GtkListBoxRow.
 *
 * @param display_num   1-based number shown in label ("1 – …", "2 – …")
 * @param title         Primary label text (command name or "Original image")
 * @param subtitle      Optional subtitle (layer name), or NULL
 * @param cmd           Command for thumbnail; NULL for the origin row
 * @param is_current    Whether to prepend "* " to the title
 */
static GtkWidget* build_row(guint display_num,
                            const gchar* title,
                            const gchar* subtitle,
                            Command* cmd,
                            ImageDocument* doc_initial,
                            gboolean is_current) {
    /* Outer row box (horizontal) */
    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(row_box, 6);
    gtk_widget_set_margin_end(row_box, 6);
    gtk_widget_set_margin_top(row_box, 4);
    gtk_widget_set_margin_bottom(row_box, 4);

    /* Thumbnail drawing area */
    GtkWidget* da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, ROW_THUMB_SIZE, ROW_THUMB_SIZE);

    /* Restrict the drawing area to expose events only so it does not absorb
     * button-press events — those must reach the GtkListBox to change selection. */
    gtk_widget_set_events(da, GDK_EXPOSURE_MASK);

    DrawData* dd = g_new0(DrawData, 1);
    dd->cmd = cmd;
    dd->doc_initial = doc_initial;
    dd->is_current = is_current;
    g_signal_connect_data(da, "draw", G_CALLBACK(on_thumb_draw),
                          dd, (GClosureNotify)draw_data_free, 0);

    gtk_box_pack_start(GTK_BOX(row_box), da, FALSE, FALSE, 0);

    /* Text column */
    GtkWidget* text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

    /* Primary label: "* N – Title"  or  "N – Title" */
    gchar* label_text;
    if (is_current) {
        label_text = g_strdup_printf("* %u \xe2\x80\x93 %s", display_num, title);
    } else {
        label_text = g_strdup_printf("%u \xe2\x80\x93 %s", display_num, title);
    }

    GtkWidget* lbl_name = gtk_label_new(label_text);
    g_free(label_text);
    gtk_label_set_xalign(GTK_LABEL(lbl_name), 0.0f);

    /* Bold via markup */
    gchar* markup = g_markup_printf_escaped("<b>%s</b>",
                                            gtk_label_get_text(GTK_LABEL(lbl_name)));
    gtk_label_set_markup(GTK_LABEL(lbl_name), markup);
    g_free(markup);

    gtk_box_pack_start(GTK_BOX(text_box), lbl_name, FALSE, FALSE, 0);

    /* Subtitle label (layer name), small grey */
    if (subtitle && subtitle[0] != '\0') {
        gchar* sub_text = g_strdup_printf("layer: %s", subtitle);
        GtkWidget* lbl_sub = gtk_label_new(NULL);
        gchar* sub_markup = g_markup_printf_escaped(
            "<small><span foreground=\"#000000\">%s</span></small>", sub_text);
        gtk_label_set_markup(GTK_LABEL(lbl_sub), sub_markup);
        g_free(sub_markup);
        g_free(sub_text);
        gtk_label_set_xalign(GTK_LABEL(lbl_sub), 0.0f);
        gtk_box_pack_start(GTK_BOX(text_box), lbl_sub, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(row_box), text_box, TRUE, TRUE, 0);

    /* Wrap in a GtkListBoxRow */
    GtkWidget* list_row = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(list_row), row_box);
    gtk_widget_show_all(list_row);

    return list_row;
}

void undo_history_dialog_show(GtkWindow* parent, ImageDocument* doc) {
    if (!doc) {
        return;
    }

    guint undo_size = doc->undo_stack ? command_stack_size(doc->undo_stack) : 0;
    guint redo_size = doc->redo_stack ? command_stack_size(doc->redo_stack) : 0;
    guint total = 1 + undo_size + redo_size; /* +1 for "Original image" */
    guint current_idx = undo_size;           /* 0-based index of current state */

    /* Collect Command pointers in display order (oldest → newest) */
    GPtrArray* entries = g_ptr_array_new(); /* Command* or NULL for origin */

    /* Row 0 — synthetic origin */
    g_ptr_array_add(entries, NULL);

    /* Rows 1..n — undo_stack from oldest (g_list_last) toward head */
    if (doc->undo_stack && doc->undo_stack->commands) {
        GList* node = g_list_last(doc->undo_stack->commands);
        while (node) {
            g_ptr_array_add(entries, node->data);
            node = node->prev;
        }
    }

    /* Rows n+1.. — redo_stack from head (next redo) toward tail */
    if (doc->redo_stack && doc->redo_stack->commands) {
        GList* node = doc->redo_stack->commands; /* head = next redo */
        while (node) {
            g_ptr_array_add(entries, node->data);
            node = node->next;
        }
    }

    GError* glade_err = NULL;
    GtkBuilder* builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/undo_history_dialog.glade", &glade_err)) {
        debug_log("WRN", "Failed to load undo_history_dialog.glade: %s",
                  glade_err ? glade_err->message : "Unknown error");
        if (glade_err)
            g_error_free(glade_err);
        g_object_unref(builder);
        g_ptr_array_free(entries, FALSE);
        return;
    }

    GtkWidget* dialog = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_dialog"));
    if (!dialog) {
        debug_log("WRN", "undo_history_dialog: missing undo_history_dialog");
        g_object_unref(builder);
        g_ptr_array_free(entries, FALSE);
        return;
    }

    GtkWidget* scroll = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_scrolled"));
    GtkWidget* list_box = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_list_box"));
    GtkWidget* hdr_label = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_subtitle_label"));
    GtkWidget* footer = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_footer_label"));
    GtkWidget* ok_button = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_ok_button"));
    GtkWidget* cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "undo_history_cancel_button"));

    if (!scroll || !list_box || !hdr_label || !footer || !ok_button || !cancel_button) {
        debug_log("WRN", "undo_history_dialog: missing one or more widgets from glade");
        g_object_unref(builder);
        g_ptr_array_free(entries, FALSE);
        return;
    }

    g_object_unref(builder);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

    ui_utils_set_header_bar(GTK_WINDOW(dialog), _("Undo history"));

    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    g_object_set_data(G_OBJECT(ok_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_response_button_clicked), dialog);
    g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_response_button_clicked), dialog);

    {
        gchar* hdr_markup = g_markup_printf_escaped(
            "<span foreground=\"#000000\">%s</span>", _("available image states"));
        gtk_label_set_markup(GTK_LABEL(hdr_label), hdr_markup);
        g_free(hdr_markup);
    }
    {
        gchar* footer_markup = g_markup_printf_escaped(
            "<small><i>%s</i></small>", _("* current image state"));
        gtk_label_set_markup(GTK_LABEL(footer), footer_markup);
        g_free(footer_markup);
    }

    GtkListBoxRow* current_row = NULL;

    for (guint i = 0; i < total; i++) {
        Command* cmd = (Command*)g_ptr_array_index(entries, i);
        gboolean is_current = (i == current_idx);

        const gchar* title = cmd ? cmd->name : _("Original image");
        const gchar* subtitle = cmd ? cmd->subtitle : NULL;

        /* Row 0 ("Original image"): pass doc so the draw callback can dereference
         * doc->initial_thumbnail live at paint time rather than capturing a pointer
         * that may be destroyed by the worker before the row is drawn. */
        ImageDocument* doc_initial_arg = (cmd == NULL) ? doc : NULL;

        GtkWidget* row = build_row(i + 1, title, subtitle, cmd, doc_initial_arg, is_current);

        /* Store row metadata for navigation */
        RowData* rd = g_new0(RowData, 1);
        rd->cmd = cmd;
        rd->is_current = is_current;
        rd->index = i;
        g_object_set_data_full(G_OBJECT(row), "row-data", rd, (GDestroyNotify)g_free);

        gtk_list_box_insert(GTK_LIST_BOX(list_box), row, -1);

        if (is_current) {
            current_row = GTK_LIST_BOX_ROW(row);
        }
    }

    /* Pre-select current state */
    if (current_row) {
        gtk_list_box_select_row(GTK_LIST_BOX(list_box), current_row);
    }

    gtk_widget_show_all(dialog);

    /* Scroll to current row after showing */
    if (current_row) {
        gtk_list_box_row_get_index(current_row); /* ensure layout */
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(scroll));
        GtkAllocation alloc;
        gtk_widget_get_allocation(GTK_WIDGET(current_row), &alloc);
        gdouble upper = gtk_adjustment_get_upper(adj);
        gdouble page = gtk_adjustment_get_page_size(adj);
        gdouble target = alloc.y - (page / 2.0) + (alloc.height / 2.0);
        if (target < 0)
            target = 0;
        if (target > upper - page)
            target = upper - page;
        if (target >= 0) {
            gtk_adjustment_set_value(adj, target);
        }
    }

    debug_log("DBG", "Undo history: current_idx=%u total=%u", current_idx, total);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));

    debug_log("DBG", "Undo history: dialog response=%d", response);

    if (response == GTK_RESPONSE_OK) {
        GtkListBoxRow* selected = gtk_list_box_get_selected_row(
            GTK_LIST_BOX(list_box));

        if (!selected) {
            debug_log("WRN", "Undo history: no row selected on OK");
        } else {
            /* Use gtk_list_box_row_get_index for reliable index lookup — avoids
             * any ambiguity with g_object_get_data across GTK row wrapping. */
            gint raw_idx = gtk_list_box_row_get_index(selected);
            debug_log("DBG", "Undo history: selected row index=%d", raw_idx);

            if (raw_idx >= 0) {
                guint selected_idx = (guint)raw_idx;

                if (selected_idx < current_idx) {
                    guint steps = current_idx - selected_idx;
                    debug_log("DBG", "Undo history: navigating back %u step(s)", steps);
                    for (guint i = 0; i < steps; i++) {
                        document_undo(doc);
                    }
                } else if (selected_idx > current_idx) {
                    guint steps = selected_idx - current_idx;
                    debug_log("DBG", "Undo history: navigating forward %u step(s)", steps);
                    for (guint i = 0; i < steps; i++) {
                        document_redo(doc);
                    }
                } else {
                    debug_log("DBG", "Undo history: current state selected, no navigation needed");
                }
            }
        }
    }

    gtk_widget_destroy(dialog);
    g_ptr_array_free(entries, FALSE); /* entries holds borrowed Command ptrs — no element free */
}
