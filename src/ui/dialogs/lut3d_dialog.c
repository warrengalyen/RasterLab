/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/lut3d_dialog.h"
#include "debug_logger.h"
#include "i18n.h"
#include "io/lut3d_io.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/ui_utils.h"
#include "ui/widgets/filter_preview.h"
#include "ui/widgets/vertical_spin_button.h"
#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <string.h>

/* ---- blend mode names ------- */

static const char* const blend_mode_names[] = {
    N_("Normal"),        /* 0  BLEND_MODE_NORMAL      */
    N_("Dissolve"),      /* 1  BLEND_MODE_DISSOLVE    */
    N_("Darken"),        /* 2  BLEND_MODE_DARKEN      */
    N_("Multiply"),      /* 3  BLEND_MODE_MULTIPLY    */
    N_("Color Burn"),    /* 4  BLEND_MODE_COLOR_BURN  */
    N_("Linear Burn"),   /* 5  BLEND_MODE_LINEAR_BURN */
    N_("Darker Color"),  /* 6  BLEND_MODE_DARKER_COLOR */
    N_("Lighten"),       /* 7  BLEND_MODE_LIGHTEN     */
    N_("Screen"),        /* 8  BLEND_MODE_SCREEN      */
    N_("Color Dodge"),   /* 9  BLEND_MODE_COLOR_DODGE */
    N_("Linear Dodge"),  /* 10 BLEND_MODE_LINEAR_DODGE */
    N_("Lighter Color"), /* 11 BLEND_MODE_LIGHTER_COLOR */
    N_("Overlay"),       /* 12 BLEND_MODE_OVERLAY     */
    N_("Soft Light"),    /* 13 BLEND_MODE_SOFT_LIGHT  */
    N_("Hard Light"),    /* 14 BLEND_MODE_HARD_LIGHT  */
    N_("Vivid Light"),   /* 15 BLEND_MODE_VIVID_LIGHT */
    N_("Linear Light"),  /* 16 BLEND_MODE_LINEAR_LIGHT */
    N_("Pin Light"),     /* 17 BLEND_MODE_PIN_LIGHT   */
    N_("Hard Mix"),      /* 18 BLEND_MODE_HARD_MIX    */
    N_("Difference"),    /* 19 BLEND_MODE_DIFFERENCE  */
    N_("Exclusion"),     /* 20 BLEND_MODE_EXCLUSION   */
    N_("Subtract"),      /* 21 BLEND_MODE_SUBTRACT    */
    N_("Divide"),        /* 22 BLEND_MODE_DIVIDE      */
    N_("Hue"),           /* 23 BLEND_MODE_HUE         */
    N_("Saturation"),    /* 24 BLEND_MODE_SATURATION  */
    N_("Color"),         /* 25 BLEND_MODE_COLOR       */
    N_("Luminosity"),    /* 26 BLEND_MODE_LUMINOSITY  */
};

#define N_BLEND_MODES ((gint)(sizeof(blend_mode_names) / sizeof(blend_mode_names[0])))

/* ---- struct --------------------------------------------------------------- */

struct _Lut3dDialog {
    GtkWidget* dialog;
    FilterPreview* preview;

    /* widgets from color_lookup_dialog.glade */
    GtkWidget* listbox;           /* lut_intensity_listbox  */
    GtkWidget* import_button;     /* lut_import_button      */
    GtkWidget* intensity_scale;   /* lut_intensity_scale    */
    GtkWidget* intensity_spin;    /* VerticalSpinButton replacing lut_intensity_spin */
    GtkWidget* blend_combo;       /* lut_blendmode_combobox */
    GtkAdjustment* intensity_adj; /* lut_intensity_adustment */

    gchar* luts_dir;          /* full path to <app_dir>/3DLUTs */
    gchar* selected_lut_path; /* full path of the selected LUT  */

    Lut3dDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/* ---- helpers -------------------------------------------------------------- */

static gint get_intensity(Lut3dDialog* d) {
    gint v = (gint)(gtk_adjustment_get_value(d->intensity_adj) + 0.5);
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    return v;
}

static BlendMode get_blend_mode(Lut3dDialog* d) {
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(d->blend_combo));
    if (idx < 0 || idx >= N_BLEND_MODES)
        return BLEND_MODE_NORMAL;
    return (BlendMode)idx;
}

static void trigger_preview(Lut3dDialog* d) {
    if (d->preview_callback && d->selected_lut_path) {
        d->preview_callback(d, d->preview_user_data);
    }
}

/* ---- listbox -------------------------------------------------------------- */

static void listbox_clear(GtkListBox* lb) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(lb));
    GList* it;
    for (it = children; it; it = it->next) {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }
    g_list_free(children);
}

static void listbox_refresh(Lut3dDialog* d) {
    GDir* dir;
    const gchar* name;
    gchar* prev_selected;
    GtkListBoxRow* row_to_select = NULL;
    GtkListBoxRow* first_row = NULL;

    if (!d->listbox || !d->luts_dir)
        return;

    prev_selected = d->selected_lut_path ? g_strdup(d->selected_lut_path) : NULL;
    g_free(d->selected_lut_path);
    d->selected_lut_path = NULL;

    listbox_clear(GTK_LIST_BOX(d->listbox));
    g_mkdir_with_parents(d->luts_dir, 0755);

    dir = g_dir_open(d->luts_dir, 0, NULL);
    if (dir) {
        while ((name = g_dir_read_name(dir)) != NULL) {
            gchar* full_path = g_build_filename(d->luts_dir, name, NULL);
            if (lut3d_io_is_supported(full_path)) {
                GtkWidget* row = gtk_list_box_row_new();
                GtkWidget* label = gtk_label_new(name);
                gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
                gtk_widget_set_halign(label, GTK_ALIGN_START);
                gtk_widget_set_margin_start(label, 6);
                gtk_widget_set_margin_end(label, 6);
                gtk_widget_set_margin_top(label, 3);
                gtk_widget_set_margin_bottom(label, 3);
                gtk_container_add(GTK_CONTAINER(row), label);
                g_object_set_data_full(G_OBJECT(row), "lut-path", full_path, g_free);
                gtk_widget_show_all(row);
                gtk_list_box_insert(GTK_LIST_BOX(d->listbox), row, -1);

                if (!first_row) {
                    first_row = GTK_LIST_BOX_ROW(row);
                }
                if (prev_selected && strcmp(prev_selected, full_path) == 0) {
                    row_to_select = GTK_LIST_BOX_ROW(row);
                }
            } else {
                g_free(full_path);
            }
        }
        g_dir_close(dir);
    }

    /* Select the previously selected row, or fall back to the first one */
    if (!row_to_select && first_row) {
        row_to_select = first_row;
    }
    if (row_to_select) {
        gtk_list_box_select_row(GTK_LIST_BOX(d->listbox), row_to_select);
        d->selected_lut_path = g_strdup(
            (const gchar*)g_object_get_data(G_OBJECT(row_to_select), "lut-path"));
    }

    g_free(prev_selected);
}

/* ---- signal callbacks ----------------------------------------------------- */

static void on_row_selected(GtkListBox* lb, GtkListBoxRow* row, gpointer user_data) {
    Lut3dDialog* d = (Lut3dDialog*)user_data;
    (void)lb;

    g_free(d->selected_lut_path);
    d->selected_lut_path = NULL;

    if (row) {
        const gchar* path =
            (const gchar*)g_object_get_data(G_OBJECT(row), "lut-path");
        if (path) {
            d->selected_lut_path = g_strdup(path);
        }
    }
    trigger_preview(d);
}

static void on_intensity_changed(GtkAdjustment* adj, gpointer user_data) {
    Lut3dDialog* d = (Lut3dDialog*)user_data;
    (void)adj;
    trigger_preview(d);
}

static void on_blend_changed(GtkComboBox* combo, gpointer user_data) {
    Lut3dDialog* d = (Lut3dDialog*)user_data;
    (void)combo;
    trigger_preview(d);
}

static void on_import_clicked(GtkButton* button, gpointer user_data) {
    Lut3dDialog* d = (Lut3dDialog*)user_data;
    GtkWidget* chooser;
    gint response;
    (void)button;

    chooser = gtk_file_chooser_dialog_new(
        _("Import 3D LUT"),
        GTK_WINDOW(d->dialog),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Import"), GTK_RESPONSE_ACCEPT,
        NULL);

    GtkFileFilter* f_all = gtk_file_filter_new();
    gtk_file_filter_set_name(f_all, _("All 3D LUTs (*.cube; *.look)"));
    gtk_file_filter_add_pattern(f_all, "*.cube");
    gtk_file_filter_add_pattern(f_all, "*.look");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), f_all);

    GtkFileFilter* f_cube = gtk_file_filter_new();
    gtk_file_filter_set_name(f_cube, _("Adobe/Resolve cube LUT (*.cube)"));
    gtk_file_filter_add_pattern(f_cube, "*.cube");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), f_cube);

    GtkFileFilter* f_look = gtk_file_filter_new();
    gtk_file_filter_set_name(f_look, _("SpeedGrade / XML LUT (*.look)"));
    gtk_file_filter_add_pattern(f_look, "*.look");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), f_look);

    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), f_all);

    response = gtk_dialog_run(GTK_DIALOG(chooser));
    if (response == GTK_RESPONSE_ACCEPT) {
        gchar* src_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (src_path) {
            gchar* basename = g_path_get_basename(src_path);
            gchar* dest_path = g_build_filename(d->luts_dir, basename, NULL);

            GFile* src_file = g_file_new_for_path(src_path);
            GFile* dst_file = g_file_new_for_path(dest_path);
            GError* err = NULL;

            g_mkdir_with_parents(d->luts_dir, 0755);

            g_file_copy(src_file, dst_file,
                        G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err);
            if (err) {
                GtkWidget* msg = gtk_message_dialog_new(
                    GTK_WINDOW(d->dialog),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                    _("Failed to import LUT file: %s"), err->message);
                gtk_dialog_run(GTK_DIALOG(msg));
                gtk_widget_destroy(msg);
                g_error_free(err);
            } else {
                /* Pre-select the newly imported file on next refresh */
                g_free(d->selected_lut_path);
                d->selected_lut_path = g_strdup(dest_path);
                listbox_refresh(d);
            }

            g_object_unref(src_file);
            g_object_unref(dst_file);
            g_free(basename);
            g_free(dest_path);
            g_free(src_path);
        }
    }

    gtk_widget_destroy(chooser);
}

/* Block OK response when no LUT is selected. */
static void on_dialog_response(GtkDialog* dlg, gint response_id, gpointer user_data) {
    Lut3dDialog* d = (Lut3dDialog*)user_data;

    if (response_id == GTK_RESPONSE_OK && !d->selected_lut_path) {
        g_signal_stop_emission_by_name(dlg, "response");

        GtkWidget* msg = gtk_message_dialog_new(
            GTK_WINDOW(dlg),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
            _("Please select a 3D LUT file before applying."));
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
    }
}

/* ---- blend combo (GtkListStore style, like layers panel) ------------------ */

static void build_blend_combo(GtkWidget* combo) {
    GtkListStore* store;
    GtkTreeIter iter;
    GtkCellRenderer* cell;
    gint i;

    store = gtk_list_store_new(1, G_TYPE_STRING);
    for (i = 0; i < N_BLEND_MODES; i++) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, _(blend_mode_names[i]), -1);
    }
    gtk_combo_box_set_model(GTK_COMBO_BOX(combo), GTK_TREE_MODEL(store));
    g_object_unref(store);

    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", 0, NULL);

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

/* ---- construction --------------------------------------------------------- */

Lut3dDialog* lut3d_dialog_new(const gchar* title, const gchar* app_dir) {
    Lut3dDialog* d;
    GtkBuilder* builder;
    GError* err = NULL;
    GtkWidget* panel;
    GtkWidget* spin_parent;
    GtkWidget* old_spin;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* button_box;
    GtkWidget* reset_button;

    if (!title)
        return NULL;

    d = (Lut3dDialog*)g_malloc0(sizeof(Lut3dDialog));
    if (!d)
        return NULL;

    d->luts_dir = app_dir
                      ? g_build_filename(app_dir, "3DLUTs", NULL)
                      : g_strdup("3DLUTs");

    /* ---- Load right-panel layout from glade -------------------------------- */
    builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/dialogs/color_lookup_dialog.glade", &err)) {
        debug_log("WRN", "lut3d_dialog: failed to load glade: %s",
                  err ? err->message : "unknown");
        if (err)
            g_error_free(err);
        g_object_unref(builder);
        g_free(d->luts_dir);
        g_free(d);
        return NULL;
    }

    panel = GTK_WIDGET(gtk_builder_get_object(builder, "lut3d_apply_panel"));
    d->listbox = GTK_WIDGET(gtk_builder_get_object(builder, "lut_intensity_listbox"));
    d->import_button = GTK_WIDGET(gtk_builder_get_object(builder, "lut_import_button"));
    d->intensity_scale = GTK_WIDGET(gtk_builder_get_object(builder, "lut_intensity_scale"));
    d->blend_combo = GTK_WIDGET(gtk_builder_get_object(builder, "lut_blendmode_combobox"));
    d->intensity_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "lut_intensity_adustment"));

    if (!panel || !d->listbox || !d->import_button ||
        !d->intensity_scale || !d->blend_combo || !d->intensity_adj) {
        debug_log("WRN", "lut3d_dialog: missing required widgets in glade");
        g_object_unref(builder);
        g_free(d->luts_dir);
        g_free(d);
        return NULL;
    }

    /* Replace lut_intensity_spin (GtkSpinButton) with VerticalSpinButton.
     * The spin must NOT expand — only lut_intensity_scale expands in the row.
     * VerticalSpinButton's internal GtkEntry has hexpand=TRUE which propagates
     * upward, so we must forcibly suppress it with set_hexpand + size_request. */
    old_spin = GTK_WIDGET(gtk_builder_get_object(builder, "lut_intensity_spin"));
    if (old_spin) {
        spin_parent = gtk_widget_get_parent(old_spin);
        if (spin_parent) {
            d->intensity_spin = vertical_spin_button_new(d->intensity_adj, 1.0, 0);
            gtk_widget_set_name(d->intensity_spin, "lut_intensity_spin");
            gtk_widget_set_hexpand(d->intensity_spin, FALSE);
            gtk_widget_set_size_request(d->intensity_spin, 70, -1);
            gtk_container_remove(GTK_CONTAINER(spin_parent), old_spin);
            gtk_box_pack_start(GTK_BOX(spin_parent), d->intensity_spin, FALSE, FALSE, 0);
            gtk_box_reorder_child(GTK_BOX(spin_parent), d->intensity_spin, 1);
            gtk_widget_show(d->intensity_spin);
        }
    }

    /* Build blend mode list model */
    build_blend_combo(d->blend_combo);
    gtk_widget_set_hexpand(d->blend_combo, TRUE);
    ui_apply_list_combobox_style(d->blend_combo);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(d->blend_combo), TRUE);
    g_signal_connect(d->blend_combo, "notify::popup-shown",
                     G_CALLBACK(ui_combo_popup_shown_fix), NULL);

    /* ---- Create the top-level dialog window -------------------------------- */
    d->dialog = gtk_dialog_new_with_buttons(
        title, NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_OK", GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL,
        NULL);

    ui_utils_set_header_bar(GTK_WINDOW(d->dialog), title);
    gtk_window_set_resizable(GTK_WINDOW(d->dialog), FALSE);

    /* Validate OK: block response if no LUT is selected */
    g_signal_connect(d->dialog, "response",
                     G_CALLBACK(on_dialog_response), d);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(d->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

    /* Main horizontal box --------------------------------------------------- */
    main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    /* Left: FilterPreview --------------------------------------------------- */
    d->preview = FILTER_PREVIEW(filter_preview_new());
    gtk_widget_set_size_request(GTK_WIDGET(d->preview), 375, 338);
    gtk_widget_set_hexpand(GTK_WIDGET(d->preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(d->preview), FALSE);
    gtk_box_pack_start(GTK_BOX(main_hbox), GTK_WIDGET(d->preview), FALSE, FALSE, 0);

    /* Right: panel loaded from glade ---------------------------------------- */
    /* Reparent panel out of builder's ownership into the dialog hbox */
    g_object_ref(panel);
    if (gtk_widget_get_parent(panel)) {
        gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(panel)), panel);
    }
    gtk_widget_set_size_request(panel, 260, -1);
    gtk_box_pack_start(GTK_BOX(main_hbox), panel, FALSE, FALSE, 0);
    g_object_unref(panel);

    /* Action-area reset button --------------------------------------------- */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    button_box = gtk_dialog_get_action_area(GTK_DIALOG(d->dialog));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    if (button_box) {
        gtk_widget_set_margin_top(button_box, 5);
        gtk_widget_set_margin_bottom(button_box, 5);
        gtk_widget_set_margin_start(button_box, 5);
        gtk_widget_set_margin_end(button_box, 5);
        gtk_widget_set_hexpand(button_box, TRUE);

        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.png");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(reset_button), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
        }
        gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(button_box), reset_button, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(button_box), reset_button, 0);
        g_signal_connect_swapped(reset_button, "clicked",
                                 G_CALLBACK(gtk_list_box_unselect_all), d->listbox);
    }

    /* ---- Wire up signals --------------------------------------------------- */
    g_signal_connect(d->import_button, "clicked", G_CALLBACK(on_import_clicked), d);
    g_signal_connect(d->listbox, "row-selected", G_CALLBACK(on_row_selected), d);
    g_signal_connect(d->intensity_adj, "value-changed",
                     G_CALLBACK(on_intensity_changed), d);
    g_signal_connect(d->blend_combo, "changed", G_CALLBACK(on_blend_changed), d);

    gtk_widget_show_all(content_area);

    /* Populate listbox and auto-select first entry */
    listbox_refresh(d);

    g_object_unref(builder);
    return d;
}

/* ---- public API ----------------------------------------------------------- */

void lut3d_dialog_free(Lut3dDialog* dialog) {
    if (!dialog)
        return;

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    g_free(dialog->luts_dir);
    g_free(dialog->selected_lut_path);
    g_free(dialog);
}

GtkWindow* lut3d_dialog_get_window(Lut3dDialog* dialog) {
    if (!dialog || !dialog->dialog)
        return NULL;
    return GTK_WINDOW(dialog->dialog);
}

void lut3d_dialog_set_layers(Lut3dDialog* dialog, ImageLayer* original, ImageLayer* temp) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview)
        return;

    if (original && original->surface) {
        before_surface = cairo_surface_reference(original->surface);
    }
    if (temp && temp->surface) {
        after_surface = cairo_surface_reference(temp->surface);
    }

    filter_preview_set_before_surface(dialog->preview, before_surface);
    filter_preview_set_after_surface(dialog->preview, after_surface);

    if (before_surface)
        cairo_surface_destroy(before_surface);
    if (after_surface)
        cairo_surface_destroy(after_surface);
}

gint lut3d_dialog_run(Lut3dDialog* dialog,
                      GtkWindow* parent,
                      gchar** out_lut_path,
                      gint* out_intensity,
                      BlendMode* out_blend_mode) {
    gint response;

    if (!dialog)
        return GTK_RESPONSE_CANCEL;

    if (parent) {
        gtk_window_set_transient_for(lut3d_dialog_get_window(dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        if (out_lut_path) {
            *out_lut_path = dialog->selected_lut_path
                                ? g_strdup(dialog->selected_lut_path)
                                : NULL;
        }
        if (out_intensity) {
            *out_intensity = get_intensity(dialog);
        }
        if (out_blend_mode) {
            *out_blend_mode = get_blend_mode(dialog);
        }
    }

    return response;
}

void lut3d_dialog_update_after_layer(Lut3dDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview)
        return;

    if (layer && layer->surface) {
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);

    if (after_surface)
        cairo_surface_destroy(after_surface);
}

void lut3d_dialog_set_preview_callback(Lut3dDialog* dialog,
                                       Lut3dDialogPreviewCallback callback,
                                       gpointer user_data) {
    if (!dialog)
        return;
    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

const gchar* lut3d_dialog_get_selected_path(Lut3dDialog* dialog) {
    if (!dialog)
        return NULL;
    return dialog->selected_lut_path;
}

gint lut3d_dialog_get_intensity(Lut3dDialog* dialog) {
    if (!dialog)
        return 100;
    return get_intensity(dialog);
}

BlendMode lut3d_dialog_get_blend_mode(Lut3dDialog* dialog) {
    if (!dialog)
        return BLEND_MODE_NORMAL;
    return get_blend_mode(dialog);
}
