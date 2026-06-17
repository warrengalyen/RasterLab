/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifdef HAVE_LENSFUN

#include "ui/dialogs/lens_correction_dialog.h"
#include "debug_logger.h"
#include "document.h"
#include "i18n.h"
#include "lensfun.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/filters/filter_utils.h"
#include "ui/ui_utils.h"
#include "ui/widgets/filter_preview.h"
#include "ui/widgets/vertical_spin_button.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Database cache / LUT singleton
 * ---------------------------------------------------------------------------*/

typedef struct {
    lfDatabase* db;

    const lfCamera** sorted_cameras;
    gchar** camera_display_names;
    gint num_cameras;

    const lfLens* const* all_lenses;
    gchar** lens_display_names;
    gint num_lenses;

    const gchar** camera_mount_map;

    GHashTable* mount_to_lens_indices; /* mount_name -> GArray of gint */
} LensfunDBCache;

static LensfunDBCache* g_lensfun_cache = NULL;

static gint cmp_camera_display(gconstpointer a, gconstpointer b, gpointer user_data) {
    gchar** names = (gchar**)user_data;
    gint ia = *(const gint*)a;
    gint ib = *(const gint*)b;
    return g_ascii_strcasecmp(names[ia], names[ib]);
}

static gint cmp_lens_display(gconstpointer a, gconstpointer b, gpointer user_data) {
    gchar** names = (gchar**)user_data;
    gint ia = *(const gint*)a;
    gint ib = *(const gint*)b;
    return g_ascii_strcasecmp(names[ia], names[ib]);
}

static LensfunDBCache* lensfun_db_cache_build(const gchar* app_dir) {
    LensfunDBCache* cache;
    lfDatabase* db;
    gchar* db_path;
    const lfCamera* const* cameras;
    const lfLens* const* lenses;
    gint i, count;
    gint* sort_indices;

    cache = g_new0(LensfunDBCache, 1);
    db = lf_db_create();
    if (!db) {
        g_free(cache);
        return NULL;
    }

    db_path = g_build_filename(app_dir, "LensProfiles", NULL);
    if (lf_db_load_path(db, db_path) != LF_NO_ERROR) {
        debug_log("WRN", "Lensfun: failed to load database from %s, trying default", db_path);
        if (lf_db_load(db) != LF_NO_ERROR) {
            debug_log("ERR", "Lensfun: failed to load any database");
            g_free(db_path);
            lf_db_destroy(db);
            g_free(cache);
            return NULL;
        }
    }
    g_free(db_path);

    cache->db = db;

    /* Build sorted camera LUT */
    cameras = lf_db_get_cameras(db);
    count = 0;
    if (cameras) {
        while (cameras[count])
            count++;
    }
    cache->num_cameras = count;

    if (count > 0) {
        cache->camera_display_names = g_new(gchar*, count);
        cache->camera_mount_map = g_new(const gchar*, count);
        cache->sorted_cameras = g_new(const lfCamera*, count);

        gchar** raw_names = g_new(gchar*, count);
        sort_indices = g_new(gint, count);

        for (i = 0; i < count; i++) {
            const gchar* maker = cameras[i]->Maker ? cameras[i]->Maker : "";
            const gchar* model = cameras[i]->Model ? cameras[i]->Model : "";
            raw_names[i] = g_strdup_printf("%s %s", maker, model);
            sort_indices[i] = i;
        }

        g_qsort_with_data(sort_indices, (gsize)count, sizeof(gint),
                          cmp_camera_display, raw_names);

        for (i = 0; i < count; i++) {
            gint si = sort_indices[i];
            cache->sorted_cameras[i] = cameras[si];
            cache->camera_display_names[i] = raw_names[si];
            cache->camera_mount_map[i] = cameras[si]->Mount;
        }

        /* Mark transferred entries so we don't double-free */
        for (i = 0; i < count; i++)
            raw_names[i] = NULL;
        g_free(raw_names);
        g_free(sort_indices);
    }

    /* Build lens list and mount-to-lens index */
    lenses = lf_db_get_lenses(db);
    count = 0;
    if (lenses) {
        while (lenses[count])
            count++;
    }
    cache->num_lenses = count;
    cache->all_lenses = lenses;

    if (count > 0) {
        gchar** raw_names = g_new(gchar*, count);
        sort_indices = g_new(gint, count);

        for (i = 0; i < count; i++) {
            const gchar* maker = lenses[i]->Maker ? lenses[i]->Maker : "";
            const gchar* model = lenses[i]->Model ? lenses[i]->Model : "";
            raw_names[i] = g_strdup_printf("%s %s", maker, model);
            sort_indices[i] = i;
        }

        g_qsort_with_data(sort_indices, (gsize)count, sizeof(gint),
                          cmp_lens_display, raw_names);

        cache->lens_display_names = g_new(gchar*, count);
        for (i = 0; i < count; i++) {
            cache->lens_display_names[i] = raw_names[sort_indices[i]];
        }

        /* We need a reverse mapping from sorted index back to original index.
           But for mount lookup we use the original lens array, so build mount map
           using original indices. */
        cache->mount_to_lens_indices = g_hash_table_new_full(
            g_str_hash, g_str_equal, NULL, (GDestroyNotify)g_array_unref);

        for (i = 0; i < count; i++) {
            /* Use lf_db_find_lenses to get compatible lenses for each mount,
               but it's simpler to iterate lens mounts directly.
               Lenses list their compatible mounts via the lens->Mounts field (deprecated)
               but we can use lf_db_find_lenses(db, camera, NULL, NULL, 0) instead.
               For the index, just store ALL lenses and filter at lookup time by
               using lf_db_find_lenses with a camera. */
        }

        for (i = 0; i < count; i++)
            raw_names[i] = NULL;
        g_free(raw_names);
        g_free(sort_indices);
    }

    return cache;
}

static LensfunDBCache* lensfun_db_cache_get(const gchar* app_dir) {
    if (!g_lensfun_cache) {
        g_lensfun_cache = lensfun_db_cache_build(app_dir);
    }
    return g_lensfun_cache;
}

void lensfun_db_cache_free(void) {
    gint i;

    if (!g_lensfun_cache) {
        return;
    }

    if (g_lensfun_cache->camera_display_names) {
        for (i = 0; i < g_lensfun_cache->num_cameras; i++) {
            g_free(g_lensfun_cache->camera_display_names[i]);
        }
        g_free(g_lensfun_cache->camera_display_names);
    }
    g_free(g_lensfun_cache->sorted_cameras);
    g_free((gpointer)g_lensfun_cache->camera_mount_map);

    if (g_lensfun_cache->lens_display_names) {
        for (i = 0; i < g_lensfun_cache->num_lenses; i++) {
            g_free(g_lensfun_cache->lens_display_names[i]);
        }
        g_free(g_lensfun_cache->lens_display_names);
    }

    if (g_lensfun_cache->mount_to_lens_indices) {
        g_hash_table_destroy(g_lensfun_cache->mount_to_lens_indices);
    }

    if (g_lensfun_cache->db) {
        lf_db_destroy(g_lensfun_cache->db);
    }

    g_free(g_lensfun_cache);
    g_lensfun_cache = NULL;
}

/* ---------------------------------------------------------------------------
 * Dialog structure
 * ---------------------------------------------------------------------------*/

struct _LensCorrectionDialog {
    GtkWidget* dialog;
    FilterPreview* preview;

    GtkWidget* camera_combo;
    GtkWidget* lens_combo;

    GtkWidget* focal_length_scale;
    GtkWidget* focal_length_spin;
    GtkWidget* aperture_scale;
    GtkWidget* aperture_spin;
    GtkWidget* focal_distance_scale;
    GtkWidget* focal_distance_spin;

    GtkWidget* check_scale_to_fit;
    GtkWidget* check_distortion;
    GtkWidget* check_vignetting;
    GtkWidget* check_tca;

    LensfunDBCache* cache;

    /* Current filtered lens indices (sorted order) for the lens combo */
    GArray* filtered_lens_indices;

    LensCorrectionPreviewCallback preview_callback;
    gpointer preview_user_data;

    gboolean suppress_signals;
};

/* ---------------------------------------------------------------------------
 * EXIF autodetect placeholder
 * ---------------------------------------------------------------------------*/

/*
 * TODO: Future EXIF metadata autodetect support
 * When metadata support is added to RasterLab, this function should:
 * 1. Read EXIF data from the current image (camera make/model, lens info,
 *    focal length, aperture, focal distance)
 * 2. Use lf_db_find_cameras() to match camera
 * 3. Use lf_db_find_lenses() to match lens
 * 4. Pre-populate dialog controls with detected values
 * 5. Set focal length, aperture, and focal distance from EXIF
 */
static void lens_correction_autodetect_from_exif(LensCorrectionDialog* dialog,
                                                 void* doc) {
    (void)dialog;
    (void)doc;
}

/* ---------------------------------------------------------------------------
 * Helper: gather current params from dialog widgets
 * ---------------------------------------------------------------------------*/

static void dialog_get_params(LensCorrectionDialog* dialog, LensCorrectionParams* params) {
    if (!dialog || !params)
        return;

    gint cam_active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->camera_combo));
    params->camera_index = (cam_active <= 0) ? -1 : cam_active - 1;

    gint lens_active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->lens_combo));
    if (lens_active <= 0 || !dialog->filtered_lens_indices ||
        lens_active - 1 >= (gint)dialog->filtered_lens_indices->len) {
        params->lens_index = -1;
    } else {
        params->lens_index = g_array_index(dialog->filtered_lens_indices, gint, lens_active - 1);
    }

    params->focal_length = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->focal_length_scale));
    params->aperture = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->aperture_scale));
    params->focal_distance = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->focal_distance_scale));
    params->scale_to_fit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->check_scale_to_fit));
    params->distortion = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->check_distortion));
    params->vignetting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->check_vignetting));
    params->tca = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialog->check_tca));
}

/* ---------------------------------------------------------------------------
 * Preview update
 * ---------------------------------------------------------------------------*/

static void update_preview(LensCorrectionDialog* dialog) {
    LensCorrectionParams params;

    if (!dialog || dialog->suppress_signals)
        return;

    dialog_get_params(dialog, &params);

    if (dialog->preview_callback) {
        dialog->preview_callback(dialog, &params, dialog->preview_user_data);
    }
}

/* ---------------------------------------------------------------------------
 * Populate lens combo based on selected camera
 * ---------------------------------------------------------------------------*/

static void populate_lens_combo(LensCorrectionDialog* dialog) {
    LensfunDBCache* cache;
    gint cam_active;
    const lfCamera* camera = NULL;
    const lfLens** found_lenses = NULL;
    gint i;

    if (!dialog || !dialog->cache)
        return;
    cache = dialog->cache;

    dialog->suppress_signals = TRUE;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(dialog->lens_combo));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->lens_combo), _("(none)"));

    if (dialog->filtered_lens_indices) {
        g_array_set_size(dialog->filtered_lens_indices, 0);
    } else {
        dialog->filtered_lens_indices = g_array_new(FALSE, FALSE, sizeof(gint));
    }

    cam_active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->camera_combo));
    if (cam_active > 0 && cam_active - 1 < cache->num_cameras) {
        camera = cache->sorted_cameras[cam_active - 1];
    }

    if (camera) {
        found_lenses = lf_db_find_lenses(cache->db, camera, NULL, NULL,
                                         LF_SEARCH_SORT_AND_UNIQUIFY);
    }

    if (found_lenses) {
        /* Build sorted list from search results */
        GArray* temp_indices = g_array_new(FALSE, FALSE, sizeof(gint));
        GArray* temp_names = g_array_new(FALSE, FALSE, sizeof(gchar*));

        for (i = 0; found_lenses[i]; i++) {
            const gchar* maker = found_lenses[i]->Maker ? found_lenses[i]->Maker : "";
            const gchar* model = found_lenses[i]->Model ? found_lenses[i]->Model : "";
            gchar* name = g_strdup_printf("%s %s", maker, model);

            /* Find original index in the full lens list */
            gint orig_idx = -1;
            for (gint j = 0; j < cache->num_lenses; j++) {
                if (cache->all_lenses[j] == found_lenses[i]) {
                    orig_idx = j;
                    break;
                }
            }
            if (orig_idx >= 0) {
                g_array_append_val(temp_indices, orig_idx);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->lens_combo), name);
                g_array_append_val(dialog->filtered_lens_indices, orig_idx);
            }
            g_free(name);
        }
        g_array_free(temp_indices, TRUE);
        g_array_free(temp_names, TRUE);
        lf_free(found_lenses);
    } else {
        /* Show all lenses sorted */
        for (i = 0; i < cache->num_lenses; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->lens_combo),
                                           cache->lens_display_names[i]);
            g_array_append_val(dialog->filtered_lens_indices, i);
        }
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->lens_combo), 0);
    dialog->suppress_signals = FALSE;
}

/* ---------------------------------------------------------------------------
 * Pre-popup handler: constrain the list-mode popup's GtkScrolledWindow
 * height BEFORE UpdateLayeredWindow is called on Windows.
 * Connected to the "popup" action signal (G_SIGNAL_RUN_LAST), so this
 * runs before the default class handler that calls gtk_widget_show().
 * ---------------------------------------------------------------------------*/

static GtkWidget* find_descendant_tree_view_local(GtkWidget* widget) {
    if (!widget)
        return NULL;
    if (GTK_IS_TREE_VIEW(widget))
        return widget;
    if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* l = children; l; l = l->next) {
            GtkWidget* found = find_descendant_tree_view_local(GTK_WIDGET(l->data));
            if (found) {
                g_list_free(children);
                return found;
            }
        }
        g_list_free(children);
    }
    return NULL;
}

static GtkTreeModel* unwrap_model(GtkTreeModel* model) {
    GtkTreeModel* current = model;
    while (current) {
        if (GTK_IS_TREE_MODEL_FILTER(current)) {
            current = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(current));
            continue;
        }
        if (GTK_IS_TREE_MODEL_SORT(current)) {
            current = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(current));
            continue;
        }
        break;
    }
    return current;
}

static void on_combo_pre_popup(GtkComboBox* combo, gpointer user_data) {
    (void)user_data;

    GtkTreeModel* combo_model = gtk_combo_box_get_model(combo);
    GtkTreeModel* combo_base = unwrap_model(combo_model);
    if (!combo_base)
        return;

    GList* toplevels = gtk_window_list_toplevels();
    for (GList* l = toplevels; l; l = l->next) {
        GtkWidget* w = GTK_WIDGET(l->data);
        if (!GTK_IS_WINDOW(w))
            continue;

        GdkWindowTypeHint hint = gtk_window_get_type_hint(GTK_WINDOW(w));
        if (hint != GDK_WINDOW_TYPE_HINT_COMBO && hint != GDK_WINDOW_TYPE_HINT_POPUP_MENU)
            continue;

        GtkWidget* tree = find_descendant_tree_view_local(w);
        if (!tree || !GTK_IS_TREE_VIEW(tree))
            continue;

        GtkTreeModel* tree_model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
        GtkTreeModel* tree_base = unwrap_model(tree_model);
        if (tree_base != combo_base)
            continue;

        /* Walk up from tree view to find the GtkScrolledWindow ancestor */
        GtkWidget* parent = gtk_widget_get_parent(tree);
        while (parent && !GTK_IS_SCROLLED_WINDOW(parent))
            parent = gtk_widget_get_parent(parent);

        if (parent && GTK_IS_SCROLLED_WINDOW(parent)) {
            GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(parent);
            gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
            gtk_scrolled_window_set_max_content_height(sw, 400);
            gtk_scrolled_window_set_propagate_natural_height(sw, TRUE);
        }
        break;
    }
    g_list_free(toplevels);
}

/* ---------------------------------------------------------------------------
 * Signal callbacks
 * ---------------------------------------------------------------------------*/

static void on_camera_changed(GtkComboBox* combo, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    (void)combo;
    if (!dialog || dialog->suppress_signals)
        return;
    populate_lens_combo(dialog);
    update_preview(dialog);
}

static void on_lens_changed(GtkComboBox* combo, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    (void)combo;
    if (!dialog || dialog->suppress_signals)
        return;
    update_preview(dialog);
}

static void on_focal_length_changed(GtkRange* range, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    if (!dialog || dialog->suppress_signals)
        return;
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->focal_length_spin),
                                   gtk_range_get_value(range));
    update_preview(dialog);
}

static void on_focal_length_spin_changed(GtkWidget* spin, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    if (!dialog || dialog->suppress_signals)
        return;
    gtk_range_set_value(GTK_RANGE(dialog->focal_length_scale),
                        vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)));
}

static void on_aperture_changed(GtkRange* range, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    if (!dialog || dialog->suppress_signals)
        return;
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->aperture_spin),
                                   gtk_range_get_value(range));
    update_preview(dialog);
}

static void on_aperture_spin_changed(GtkWidget* spin, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    if (!dialog || dialog->suppress_signals)
        return;
    gtk_range_set_value(GTK_RANGE(dialog->aperture_scale),
                        vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)));
}

static void on_focal_distance_changed(GtkRange* range, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    if (!dialog || dialog->suppress_signals)
        return;
    vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->focal_distance_spin),
                                   gtk_range_get_value(range));
    update_preview(dialog);
}

static void on_focal_distance_spin_changed(GtkWidget* spin, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    if (!dialog || dialog->suppress_signals)
        return;
    gtk_range_set_value(GTK_RANGE(dialog->focal_distance_scale),
                        vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)));
}

static void on_check_toggled(GtkToggleButton* toggle, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    (void)toggle;
    if (!dialog || dialog->suppress_signals)
        return;
    update_preview(dialog);
}

static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    LensCorrectionDialog* dialog = (LensCorrectionDialog*)user_data;
    (void)widget;
    if (!dialog)
        return;

    dialog->suppress_signals = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->camera_combo), 0);
    gtk_range_set_value(GTK_RANGE(dialog->focal_length_scale), 1.0);
    gtk_range_set_value(GTK_RANGE(dialog->aperture_scale), 8.0);
    gtk_range_set_value(GTK_RANGE(dialog->focal_distance_scale), 10.0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_scale_to_fit), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_distortion), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_vignetting), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_tca), FALSE);
    dialog->suppress_signals = FALSE;

    populate_lens_combo(dialog);
    update_preview(dialog);
}

/* ---------------------------------------------------------------------------
 * Helper: create a slider + spin control row
 * ---------------------------------------------------------------------------*/

static void create_slider_row(GtkWidget* parent_vbox,
                              const gchar* label_text,
                              gdouble min_val, gdouble max_val,
                              gdouble default_val, gdouble step,
                              gint decimals,
                              GtkWidget** out_scale, GtkWidget** out_spin,
                              GCallback scale_cb, GCallback spin_cb,
                              gpointer cb_data) {
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* scale_hbox;
    GtkAdjustment* adjustment;

    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(control_vbox, TRUE);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(parent_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(scale_hbox, TRUE);
    gtk_widget_set_halign(scale_hbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(default_val, min_val, max_val, step, step * 10.0, 0.0);

    *out_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(*out_scale), FALSE);
    gtk_widget_set_hexpand(*out_scale, TRUE);
    gtk_widget_set_halign(*out_scale, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(scale_hbox), *out_scale, TRUE, TRUE, 0);
    g_signal_connect(*out_scale, "value-changed", scale_cb, cb_data);

    *out_spin = vertical_spin_button_new(adjustment, step, decimals);
    gtk_widget_set_size_request(*out_spin, 70, -1);
    gtk_widget_set_hexpand(*out_spin, FALSE);
    gtk_widget_set_halign(*out_spin, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(scale_hbox), *out_spin, FALSE, FALSE, 0);
    g_signal_connect(*out_spin, "value-changed", spin_cb, cb_data);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

LensCorrectionDialog* lens_correction_dialog_new(const gchar* title,
                                                 const gchar* app_dir) {
    LensCorrectionDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* combo_vbox;
    GtkWidget* combo_label;
    GtkWidget* frame;
    GtkWidget* frame_vbox;
    GtkWidget* reset_button;
    GtkWidget* button_box;
    gint i;

    if (!title)
        return NULL;

    dialog = g_new0(LensCorrectionDialog, 1);
    dialog->suppress_signals = TRUE;

    dialog->cache = lensfun_db_cache_get(app_dir);
    if (!dialog->cache) {
        debug_log("WRN", "Lens Correction: failed to load lensfun database");
        g_free(dialog);
        return NULL;
    }

    dialog->dialog = gtk_dialog_new_with_buttons(title, NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK", GTK_RESPONSE_OK,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 NULL);

    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), title);
    gtk_window_set_resizable(GTK_WINDOW(dialog->dialog), TRUE);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

    main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    /* Preview (left side) */
    dialog->preview = FILTER_PREVIEW(filter_preview_new());
    filter_preview_set_allow_zoom_pan(dialog->preview, FALSE);
    gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), 375, 338);
    gtk_widget_set_hexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_box_pack_start(GTK_BOX(main_hbox), GTK_WIDGET(dialog->preview), FALSE, FALSE, 0);

    /* Controls (right side) */
    right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right_vbox, 320, -1);
    gtk_widget_set_margin_top(right_vbox, 10);
    gtk_widget_set_margin_bottom(right_vbox, 10);
    gtk_widget_set_margin_start(right_vbox, 10);
    gtk_widget_set_margin_end(right_vbox, 10);
    gtk_widget_set_hexpand(right_vbox, TRUE);
    gtk_widget_set_halign(right_vbox, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, TRUE, TRUE, 0);

    /* Camera dropdown */
    combo_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(combo_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), combo_vbox, FALSE, FALSE, 0);

    combo_label = gtk_label_new(_("Camera"));
    gtk_widget_set_halign(combo_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(combo_label, 3);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo_label, FALSE, FALSE, 0);

    dialog->camera_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(dialog->camera_combo, TRUE);
    ui_apply_list_combobox_style(dialog->camera_combo);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(dialog->camera_combo), TRUE);
    g_signal_connect(dialog->camera_combo, "notify::popup-shown",
                     G_CALLBACK(ui_combo_popup_shown_fix), NULL);
    g_signal_connect(dialog->camera_combo, "popup",
                     G_CALLBACK(on_combo_pre_popup), NULL);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->camera_combo), _("(any)"));
    if (dialog->cache) {
        for (i = 0; i < dialog->cache->num_cameras; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->camera_combo),
                                           dialog->cache->camera_display_names[i]);
        }
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->camera_combo), 0);
    gtk_box_pack_start(GTK_BOX(combo_vbox), dialog->camera_combo, FALSE, FALSE, 0);
    g_signal_connect(dialog->camera_combo, "changed", G_CALLBACK(on_camera_changed), dialog);

    /* Lens dropdown */
    combo_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(combo_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), combo_vbox, FALSE, FALSE, 0);

    combo_label = gtk_label_new(_("Lens"));
    gtk_widget_set_halign(combo_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(combo_label, 3);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo_label, FALSE, FALSE, 0);

    dialog->lens_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(dialog->lens_combo, TRUE);
    ui_apply_list_combobox_style(dialog->lens_combo);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(dialog->lens_combo), TRUE);
    g_signal_connect(dialog->lens_combo, "notify::popup-shown",
                     G_CALLBACK(ui_combo_popup_shown_fix), NULL);
    g_signal_connect(dialog->lens_combo, "popup",
                     G_CALLBACK(on_combo_pre_popup), NULL);
    gtk_box_pack_start(GTK_BOX(combo_vbox), dialog->lens_combo, FALSE, FALSE, 0);
    g_signal_connect(dialog->lens_combo, "changed", G_CALLBACK(on_lens_changed), dialog);

    /* Focal Length slider */
    create_slider_row(right_vbox, _("Focal Length (mm)"),
                      1.0, 300.0, 1.0, 1.0, 0,
                      &dialog->focal_length_scale, &dialog->focal_length_spin,
                      G_CALLBACK(on_focal_length_changed),
                      G_CALLBACK(on_focal_length_spin_changed), dialog);

    /* Aperture slider */
    create_slider_row(right_vbox, _("Aperture (f/)"),
                      1.0, 64.0, 8.0, 0.1, 1,
                      &dialog->aperture_scale, &dialog->aperture_spin,
                      G_CALLBACK(on_aperture_changed),
                      G_CALLBACK(on_aperture_spin_changed), dialog);

    /* Focal Distance slider */
    create_slider_row(right_vbox, _("Focal Distance (m)"),
                      0.1, 100.0, 10.0, 0.1, 1,
                      &dialog->focal_distance_scale, &dialog->focal_distance_spin,
                      G_CALLBACK(on_focal_distance_changed),
                      G_CALLBACK(on_focal_distance_spin_changed), dialog);

    /* Processing checkboxes frame */
    frame = gtk_frame_new(_("Processing"));
    gtk_widget_set_margin_top(frame, 5);
    gtk_box_pack_start(GTK_BOX(right_vbox), frame, FALSE, FALSE, 0);

    frame_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(frame_vbox), 8);
    gtk_container_add(GTK_CONTAINER(frame), frame_vbox);

    dialog->check_scale_to_fit = gtk_check_button_new_with_label(_("Scale to fit"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_scale_to_fit), TRUE);
    gtk_box_pack_start(GTK_BOX(frame_vbox), dialog->check_scale_to_fit, FALSE, FALSE, 0);
    g_signal_connect(dialog->check_scale_to_fit, "toggled", G_CALLBACK(on_check_toggled), dialog);

    dialog->check_distortion = gtk_check_button_new_with_label(_("Distortion"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_distortion), TRUE);
    gtk_box_pack_start(GTK_BOX(frame_vbox), dialog->check_distortion, FALSE, FALSE, 0);
    g_signal_connect(dialog->check_distortion, "toggled", G_CALLBACK(on_check_toggled), dialog);

    dialog->check_vignetting = gtk_check_button_new_with_label(_("Vignetting"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_vignetting), FALSE);
    gtk_box_pack_start(GTK_BOX(frame_vbox), dialog->check_vignetting, FALSE, FALSE, 0);
    g_signal_connect(dialog->check_vignetting, "toggled", G_CALLBACK(on_check_toggled), dialog);

    dialog->check_tca = gtk_check_button_new_with_label(_("Chromatic Aberration"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->check_tca), FALSE);
    gtk_box_pack_start(GTK_BOX(frame_vbox), dialog->check_tca, FALSE, FALSE, 0);
    g_signal_connect(dialog->check_tca, "toggled", G_CALLBACK(on_check_toggled), dialog);

    /* Reset button in action area */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    button_box = gtk_dialog_get_action_area(GTK_DIALOG(dialog->dialog));
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
        g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset_clicked), dialog);
    }

    /* Populate lens combo with initial state */
    populate_lens_combo(dialog);

    gtk_widget_show_all(content_area);

    dialog->suppress_signals = FALSE;

    return dialog;
}

void lens_correction_dialog_free(LensCorrectionDialog* dialog) {
    if (!dialog)
        return;

    if (dialog->filtered_lens_indices) {
        g_array_free(dialog->filtered_lens_indices, TRUE);
    }

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    g_free(dialog);
}

GtkWindow* lens_correction_dialog_get_window(LensCorrectionDialog* dialog) {
    if (!dialog || !dialog->dialog)
        return NULL;
    return GTK_WINDOW(dialog->dialog);
}

void lens_correction_dialog_set_layers(LensCorrectionDialog* dialog,
                                       ImageLayer* original,
                                       ImageLayer* temp) {
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

    update_preview(dialog);
}

void lens_correction_dialog_update_after_layer(LensCorrectionDialog* dialog,
                                               ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview || !layer)
        return;

    if (layer->surface) {
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);
    filter_preview_refresh(dialog->preview);

    if (after_surface)
        cairo_surface_destroy(after_surface);
}

gint lens_correction_dialog_run(LensCorrectionDialog* dialog,
                                GtkWindow* parent,
                                LensCorrectionParams* params) {
    gint response;

    if (!dialog || !params)
        return GTK_RESPONSE_CANCEL;

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        dialog_get_params(dialog, params);
    }

    return response;
}

void lens_correction_dialog_set_preview_callback(LensCorrectionDialog* dialog,
                                                 LensCorrectionPreviewCallback callback,
                                                 gpointer user_data) {
    if (!dialog)
        return;
    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

#else /* !HAVE_LENSFUN */

#include "debug_logger.h"
#include "ui/dialogs/lens_correction_dialog.h"

LensCorrectionDialog* lens_correction_dialog_new(const gchar* title,
                                                 const gchar* app_dir) {
    (void)title;
    (void)app_dir;
    debug_log("WRN", "Lens Correction not available (HAVE_LENSFUN not defined)");
    return NULL;
}

void lens_correction_dialog_free(LensCorrectionDialog* dialog) {
    (void)dialog;
}
GtkWindow* lens_correction_dialog_get_window(LensCorrectionDialog* dialog) {
    (void)dialog;
    return NULL;
}
void lens_correction_dialog_set_layers(LensCorrectionDialog* d, ImageLayer* a, ImageLayer* b) {
    (void)d;
    (void)a;
    (void)b;
}
void lens_correction_dialog_update_after_layer(LensCorrectionDialog* d, ImageLayer* l) {
    (void)d;
    (void)l;
}
gint lens_correction_dialog_run(LensCorrectionDialog* d, GtkWindow* p, LensCorrectionParams* pr) {
    (void)d;
    (void)p;
    (void)pr;
    return GTK_RESPONSE_CANCEL;
}
void lens_correction_dialog_set_preview_callback(LensCorrectionDialog* d, LensCorrectionPreviewCallback c, gpointer u) {
    (void)d;
    (void)c;
    (void)u;
}
void lensfun_db_cache_free(void) {
}

#endif /* HAVE_LENSFUN */
