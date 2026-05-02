/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "debug_logger.h"
/*
 * Settings dialog 
 * Uses layout from resources/ui/dialogs/settings_dialog.glade
 */
#include "ui/dialogs/settings_dialog.h"
#include "app/autosave.h"
#include "app/settings.h"
#include "render/gpu_compositor.h"
#include "render/render_utils.h"
#include "ui.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#if HAVE_LCMS2
#include "color_manager/icc_utils.h"
#include "i18n.h"
#endif

enum {
    RENDERER_COLUMN_LABEL,
    RENDERER_COLUMN_DEVICE_ID,
    RENDERER_N_COLUMNS
};

enum {
    CMS_DISPLAY_COLUMN_LABEL,
    CMS_DISPLAY_COLUMN_ID,
    CMS_DISPLAY_N_COLUMNS
};

#define SETTINGS_TAB_ICON_SIZE 30

static gboolean temp_path_is_valid(const gchar* path);

typedef enum {
    CMS_PROFILE_VALID,
    CMS_PROFILE_INVALID_FILE,
    CMS_PROFILE_INVALID_NON_RGB
} CmsProfileValidationResult;
static CmsProfileValidationResult cms_icc_path_validate(const gchar* path);

/* Set a notebook tab to show icon (from resource) before the existing label. */
static void set_tab_icon_label(GtkNotebook* notebook, GtkWidget* page_child, GtkWidget* tab_label,
                               const gchar* icon_resource) {
    if (!notebook || !page_child || !tab_label || !icon_resource) {
        return;
    }
    GError* err = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_resource(icon_resource, &err);
    if (!pixbuf) {
        if (err) {
            debug_log("WRN", "Settings tab icon %s: %s", icon_resource, err->message);
            g_error_free(err);
        }
        return;
    }
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, SETTINGS_TAB_ICON_SIZE, SETTINGS_TAB_ICON_SIZE, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    if (!scaled) {
        return;
    }
    GtkWidget* image = gtk_image_new_from_pixbuf(scaled);
    g_object_unref(scaled);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_add(GTK_CONTAINER(box), image);
    /* Set box as tab label first so the notebook releases tab_label, then add label to box */
    gtk_notebook_set_tab_label(notebook, page_child, box);
    gtk_container_add(GTK_CONTAINER(box), tab_label);
    gtk_widget_show_all(box);
}

/* Pixels per check: Small=4 (12×12 in 48px), Medium=8 (6×6), Large=16 (3×3). */
static gint alpha_check_square_size_from_index(gint index) {
    switch (index) {
        case 0:
            return 4; /* Small: 12 squares per row in 48px */
        case 1:
            return 8; /* Medium: 6 squares per row */
        case 2:
            return 16; /* Large: 3 squares per row */
        default:
            return 8;
    }
}

/* Grid color presets: Highlights (0), Midtones (1), Shadows (2). Values in 0-1 for Cairo. */
typedef struct {
    double r1, g1, b1;
    double r2, g2, b2;
} AlphaCheckPreset;

static const AlphaCheckPreset alpha_check_presets[] = {
    {1.0, 1.0, 1.0, 204.0 / 255.0, 204.0 / 255.0, 204.0 / 255.0},                               /* Highlights: White, Light Gray */
    {153.0 / 255.0, 153.0 / 255.0, 153.0 / 255.0, 102.0 / 255.0, 102.0 / 255.0, 102.0 / 255.0}, /* Midtones */
    {51.0 / 255.0, 51.0 / 255.0, 51.0 / 255.0, 0.0, 0.0, 0.0},                                  /* Shadows: Very Dark Gray, Black */
};
#define ALPHA_CHECK_N_PRESETS (sizeof(alpha_check_presets) / sizeof(alpha_check_presets[0]))

static gint alpha_check_preset_from_settings(Settings* s) {
    gdouble r1, g1, b1, r2, g2, b2;
    settings_get_alpha_color_one(s, &r1, &g1, &b1);
    settings_get_alpha_color_two(s, &r2, &g2, &b2);
    for (size_t i = 0; i < ALPHA_CHECK_N_PRESETS; i++) {
        const AlphaCheckPreset* p = &alpha_check_presets[i];
        if (fabs(r1 - p->r1) < 0.02 && fabs(g1 - p->g1) < 0.02 && fabs(b1 - p->b1) < 0.02 &&
            fabs(r2 - p->r2) < 0.02 && fabs(g2 - p->g2) < 0.02 && fabs(b2 - p->b2) < 0.02) {
            return (gint)i;
        }
    }
    return 0;
}

/* Apply dialog values to settings and save; then update UI (e.g. canvas bg).
 * Returns TRUE on success, FALSE if validation failed (e.g. invalid CMS profile path). */
static gboolean settings_dialog_apply_and_save(GtkDialog* dialog, AppContext* ctx) {
    GtkBuilder* builder = (GtkBuilder*)g_object_get_data(G_OBJECT(dialog), "settings_builder");
    if (!builder || !ctx || !ctx->settings) {
        return FALSE;
    }

    /* Canvas background color is already in settings when user picked it via custom color chooser */

    /* Undo limit (undo_levels 1-100) - spin/scale share adjustment */
    GtkAdjustment* undo_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "undo_limit_adjustment"));
    if (undo_adj) {
        gint val = (gint)gtk_adjustment_get_value(undo_adj);
        settings_set_undo_levels(ctx->settings, val);
    }

    /* Undo compression level 1-9 */
    GtkAdjustment* comp_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "undo_compression_level_adjustment"));
    if (comp_adj) {
        gint val = (gint)gtk_adjustment_get_value(comp_adj);
        settings_set_undo_compression_level(ctx->settings, val);
    }

    /* Worker threads */
    GtkAdjustment* threads_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "threads_adjustment"));
    if (threads_adj) {
        gint val = (gint)gtk_adjustment_get_value(threads_adj);
        settings_set_worker_threads(ctx->settings, val);
    }

    /* Max recent files */
    GtkAdjustment* recent_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "recent_files_max_adjustment"));
    if (recent_adj) {
        guint val = (guint)gtk_adjustment_get_value(recent_adj);
        settings_set_max_recent_files(ctx->settings, val);
    }

    /* File recovery interval (30-2700 seconds) */
    GtkAdjustment* recovery_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "file_recovery_interval_adjustment"));
    if (recovery_adj) {
        gint sec = (gint)gtk_adjustment_get_value(recovery_adj);
        settings_set_file_recovery_interval_seconds(ctx->settings, sec);
        autosave_set_interval((guint)sec);
    }

    /* Temp file directory (Advanced tab): validate; if invalid, save default */
    GtkEntry* temp_entry = GTK_ENTRY(gtk_builder_get_object(builder, "temp_location_entry"));
    if (temp_entry) {
        const gchar* text = gtk_entry_get_text(temp_entry);
        gchar* trimmed = text ? g_strstrip(g_strdup(text)) : g_strdup("");
        if (trimmed && trimmed[0] != '\0' && !temp_path_is_valid(trimmed)) {
            settings_set_undo_temp_directory(ctx->settings, NULL); /* Fallback to system temp */
        } else {
            settings_set_undo_temp_directory(ctx->settings, (trimmed && trimmed[0] != '\0') ? trimmed : NULL);
        }
        g_free(trimmed);
    }

    /* Renderer (GPU device): empty string = system default */
    GtkComboBox* renderer_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "renderer_combobox"));
    if (renderer_combo) {
        GtkTreeIter iter;
        if (gtk_combo_box_get_active_iter(renderer_combo, &iter)) {
            GtkTreeModel* model = gtk_combo_box_get_model(renderer_combo);
            gchar* device_id = NULL;
            gtk_tree_model_get(model, &iter, RENDERER_COLUMN_DEVICE_ID, &device_id, -1);
            settings_set_gpu_device_name(ctx->settings, device_id && device_id[0] != '\0' ? device_id : NULL);
            g_free(device_id);
        }
    }

    /* Hardware acceleration */
    GtkToggleButton* gpu_check = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "hardware_acceleration_checkbox"));
    if (gpu_check) {
        settings_set_gpu_acceleration_enabled(ctx->settings, gtk_toggle_button_get_active(gpu_check));
    }

    /* Mouse: snap distance (1-255 px) */
    GtkAdjustment* snap_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "snap_distance_adjustment"));
    if (snap_adj) {
        gint snap_px = (gint)gtk_adjustment_get_value(snap_adj);
        settings_set_mouse_snap_distance(ctx->settings, snap_px);
    }

    /* Checkerboard: grid size (0=Small, 1=Medium, 2=Large) */
    GtkComboBox* grid_size_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "transparency_grid_size_combo"));
    if (grid_size_combo) {
        gint idx = gtk_combo_box_get_active(grid_size_combo);
        if (idx >= 0 && idx <= 2) {
            settings_set_alpha_check_size(ctx->settings, idx);
        }
    }

    /* Checkerboard: grid colors preset -> set both alpha colors */
    GtkComboBox* grid_colors_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "transparency_grid_colors_combo"));
    if (grid_colors_combo) {
        gint idx = gtk_combo_box_get_active(grid_colors_combo);
        if (idx >= 0 && (size_t)idx < ALPHA_CHECK_N_PRESETS) {
            const AlphaCheckPreset* p = &alpha_check_presets[idx];
            settings_set_alpha_color_one(ctx->settings, p->r1, p->g1, p->b1);
            settings_set_alpha_color_two(ctx->settings, p->r2, p->g2, p->b2);
        }
    }

    /* Apply checkerboard to global render state so canvas updates immediately */
    render_utils_set_alpha_check_from_settings(ctx->settings);

    /* CMS: validate profile path before saving; only when custom mode and path non-empty */
    GtkComboBox* cms_display_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "cms_available_displays_combo"));
    GtkEntry* cms_profile_entry = GTK_ENTRY(gtk_builder_get_object(builder, "cms_color_profile_text"));
    GtkToggleButton* cms_custom_radio = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "cms_mode_custom_radio"));
    if (cms_custom_radio && gtk_toggle_button_get_active(cms_custom_radio) && cms_profile_entry) { /* custom mode only */
        const gchar* profile_path = gtk_entry_get_text(cms_profile_entry);
        gchar* trimmed = profile_path ? g_strstrip(g_strdup(profile_path)) : g_strdup("");
        if (trimmed && trimmed[0] != '\0') {
            CmsProfileValidationResult r = cms_icc_path_validate(trimmed);
            if (r != CMS_PROFILE_VALID) {
                GtkWidget* parent = gtk_widget_get_toplevel(GTK_WIDGET(dialog));
                const gchar* msg = (r == CMS_PROFILE_INVALID_NON_RGB)
                                       ? "The color profile is not an RGB profile.\nDisplay profiles must be RGB (e.g. sRGB, Adobe RGB)."
                                       : "The color profile path is invalid or the file is not a valid ICC/ICM profile.\nPlease choose a valid profile or clear the field.";
                GtkWidget* err_dlg = gtk_message_dialog_new(GTK_WINDOW(parent),
                                                            GTK_DIALOG_MODAL,
                                                            GTK_MESSAGE_ERROR,
                                                            GTK_BUTTONS_OK,
                                                            "%s", msg);
                gtk_dialog_run(GTK_DIALOG(err_dlg));
                gtk_widget_destroy(err_dlg);
                gtk_entry_set_text(cms_profile_entry, "");
                g_free(trimmed);
                return FALSE;
            }
        }
        g_free(trimmed);
    }

    /* CMS: apply mode (none=2, custom=1, system=0) */
    {
        GtkToggleButton* cms_none_radio = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "cms_mode_none_radio"));
        GtkToggleButton* cms_system_radio = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "cms_mode_system_radio"));
        if (cms_none_radio && gtk_toggle_button_get_active(cms_none_radio)) {
            settings_set_cm_mode(ctx->settings, 2); /* CM_MODE_NONE */
        } else if (cms_custom_radio && gtk_toggle_button_get_active(cms_custom_radio)) {
            settings_set_cm_mode(ctx->settings, 1); /* CM_MODE_CUSTOM_PROFILE */
        } else if (cms_system_radio) {
            settings_set_cm_mode(ctx->settings, 0); /* CM_MODE_SYSTEM_PROFILE */
        }
    }
    GtkComboBox* cms_intent_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "cms_rendering_intent_combo"));
    if (cms_intent_combo) {
        gint idx = gtk_combo_box_get_active(cms_intent_combo);
        if (idx >= 0 && idx <= 3) {
            settings_set_cm_rendering_intent(ctx->settings, idx);
        }
    }
    GtkCheckButton* cms_bpc = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "cms_use_black_point_checkbox"));
    if (cms_bpc) {
        settings_set_cm_black_point_compensation(ctx->settings, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cms_bpc)));
    }
    GtkCheckButton* cms_embedded = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "cms_use_embedded_icc_checkbox"));
    if (cms_embedded) {
        settings_set_cm_use_embedded_icc(ctx->settings, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cms_embedded)));
    }
    if (cms_custom_radio && gtk_toggle_button_get_active(cms_custom_radio) && cms_display_combo && cms_profile_entry) {
        GtkTreeIter iter;
        if (gtk_combo_box_get_active_iter(cms_display_combo, &iter)) {
            GtkTreeModel* model = gtk_combo_box_get_model(cms_display_combo);
            gchar* display_id = NULL;
            gtk_tree_model_get(model, &iter, CMS_DISPLAY_COLUMN_ID, &display_id, -1);
            if (display_id) {
                const gchar* path = gtk_entry_get_text(cms_profile_entry);
                gchar* path_trimmed = path ? g_strstrip(g_strdup(path)) : g_strdup("");
                settings_set_cm_display_profile(ctx->settings, display_id,
                                                (path_trimmed && path_trimmed[0] != '\0') ? path_trimmed : NULL);
                g_free(path_trimmed);
                g_free(display_id);
            }
        }
    }

    if (ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Refresh canvas background on all documents */
    ui_update_canvas_background_color(ctx);
    return TRUE;
}

static void on_settings_ok_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    AppContext* ctx = (AppContext*)user_data;
    GtkWidget* dialog = (GtkWidget*)g_object_get_data(G_OBJECT(button), "settings_dialog");
    if (dialog && ctx && settings_dialog_apply_and_save(GTK_DIALOG(dialog), ctx)) {
        gtk_widget_destroy(dialog);
    }
}

static void on_settings_cancel_clicked(GtkButton* button, gpointer user_data) {
    (void)user_data;
    GtkWidget* dialog = (GtkWidget*)g_object_get_data(G_OBJECT(button), "settings_dialog");
    if (dialog) {
        gtk_widget_destroy(dialog);
    }
}

/* Draw checkerboard preview: tile 2×2 squares across 48×48. Small=12×12, Medium=6×6, Large=3×3 squares. */
static gboolean on_transparency_preview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    (void)user_data;
    GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
    GtkBuilder* builder = (GtkBuilder*)g_object_get_data(G_OBJECT(toplevel), "settings_builder");
    if (!builder) {
        return FALSE;
    }
    GtkComboBox* size_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "transparency_grid_size_combo"));
    GtkComboBox* colors_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "transparency_grid_colors_combo"));
    if (!size_combo || !colors_combo) {
        return FALSE;
    }
    gint size_index = gtk_combo_box_get_active(size_combo);
    gint colors_index = gtk_combo_box_get_active(colors_combo);
    if (size_index < 0)
        size_index = 1;
    if (colors_index < 0 || (size_t)colors_index >= ALPHA_CHECK_N_PRESETS)
        colors_index = 0;

    gint square_size = alpha_check_square_size_from_index(size_index);
    const AlphaCheckPreset* p = &alpha_check_presets[colors_index];

    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);
    if (w <= 0)
        w = 48;
    if (h <= 0)
        h = 48;

    for (gint y = 0; y < h; y += square_size) {
        for (gint x = 0; x < w; x += square_size) {
            gint cell_x = x / square_size;
            gint cell_y = y / square_size;
            gboolean use_first = ((cell_x + cell_y) & 1) == 0;
            if (use_first) {
                cairo_set_source_rgb(cr, p->r1, p->g1, p->b1);
            } else {
                cairo_set_source_rgb(cr, p->r2, p->g2, p->b2);
            }
            cairo_rectangle(cr, (double)x, (double)y, (double)square_size, (double)square_size);
            cairo_fill(cr);
        }
    }
    /* 1 px black border */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, (double)w - 1.0, (double)h - 1.0);
    cairo_stroke(cr);
    return TRUE;
}

static void on_transparency_combo_changed(GtkComboBox* combo, gpointer user_data) {
    (void)combo;
    GtkWidget* preview = (GtkWidget*)user_data;
    if (preview) {
        gtk_widget_queue_draw(preview);
    }
}

static void on_settings_dialog_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
}

/* Validate ICC/ICM profile path for display use. Empty is valid (no custom profile).
 * Non-empty must be a valid RGB ICC profile. Returns rejection reason if invalid. */
static CmsProfileValidationResult cms_icc_path_validate(const gchar* path) {
    if (!path || path[0] == '\0') {
        return CMS_PROFILE_VALID;
    }
    if (!g_file_test(path, G_FILE_TEST_EXISTS) || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        return CMS_PROFILE_INVALID_FILE;
    }
#if HAVE_LCMS2
    {
        cmsHPROFILE prof = icc_profile_from_file(path);
        if (prof) {
            icc_destroy(prof);
            return CMS_PROFILE_VALID;
        }
        /* icc_profile_from_file returns NULL for non-RGB; check if it's valid ICC at all */
        if (icc_profile_file_is_valid(path)) {
            return CMS_PROFILE_INVALID_NON_RGB;
        }
        return CMS_PROFILE_INVALID_FILE;
    }
#else
    {
        const gchar* ext = strrchr(path, '.');
        return (ext && (g_ascii_strcasecmp(ext, ".icc") == 0 || g_ascii_strcasecmp(ext, ".icm") == 0))
                   ? CMS_PROFILE_VALID
                   : CMS_PROFILE_INVALID_FILE;
    }
#endif
}

/* Check if temp path is valid: empty (use default) or existing directory */
static gboolean temp_path_is_valid(const gchar* path) {
    if (!path || path[0] == '\0') {
        return TRUE;
    }
    return g_file_test(path, G_FILE_TEST_EXISTS) && g_file_test(path, G_FILE_TEST_IS_DIR);
}

/* Update temp_location_msg_label visibility based on path validity */
static void temp_location_update_msg_label_visibility(GtkBuilder* builder, const gchar* path) {
    GtkLabel* msg_label = GTK_LABEL(gtk_builder_get_object(builder, "temp_location_msg_label"));
    if (!msg_label) {
        return;
    }
    if (temp_path_is_valid(path)) {
        gtk_widget_set_visible(GTK_WIDGET(msg_label), FALSE);
    } else {
        gtk_widget_set_visible(GTK_WIDGET(msg_label), TRUE);
    }
}

/* Real-time validation when user types in temp_location_entry */
static void on_temp_location_changed(GtkEntry* entry, gpointer user_data) {
    GtkBuilder* builder = (GtkBuilder*)user_data;
    if (!builder || !entry) {
        return;
    }
    const gchar* text = gtk_entry_get_text(entry);
    gchar* trimmed = text ? g_strstrip(g_strdup(text)) : g_strdup("");
    temp_location_update_msg_label_visibility(builder, trimmed);
    g_free(trimmed);
}

/* Open folder chooser dialog and update temp_location_entry with selected path */
static void on_temp_location_browse_clicked(GtkButton* button, gpointer user_data) {
    GtkEntry* entry = GTK_ENTRY(user_data);
    if (!entry) {
        return;
    }
    GtkWidget* parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
    if (!parent || !GTK_IS_WINDOW(parent)) {
        return;
    }

    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(_("Select Temporary File Location"),
                                                                GTK_WINDOW(parent),
                                                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                                _("Select"),
                                                                _("Cancel"));
    /* Avoid set_current_folder - it triggers GFileInfo bugs in GLib 2.84+ on Windows */

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar* uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(chooser));
        if (uri) {
            gchar* path = g_filename_from_uri(uri, NULL, NULL);
            if (path) {
                gtk_entry_set_text(entry, path);
                g_free(path);
            }
            g_free(uri);
        }
    }
    g_object_unref(chooser);
}

/* CMS: browse for ICC/ICM profile and set path into text entry */
static void on_cms_profile_browse_clicked(GtkButton* button, gpointer user_data) {
    GtkEntry* entry = GTK_ENTRY(user_data);
    if (!entry) {
        return;
    }
    GtkWidget* parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
    if (!parent || !GTK_IS_WINDOW(parent)) {
        return;
    }
    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(_("Select a color profile"),
                                                                GTK_WINDOW(parent),
                                                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                                                _("Select"),
                                                                _("Cancel"));
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("ICC profile (.icc, .icm)"));
    gtk_file_filter_add_pattern(filter, "*.icc");
    gtk_file_filter_add_pattern(filter, "*.icm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar* uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(chooser));
        if (uri) {
            gchar* path = g_filename_from_uri(uri, NULL, NULL);
            if (path) {
                gtk_entry_set_text(entry, path);
                g_free(path);
            }
            g_free(uri);
        }
    }
    g_object_unref(chooser);
}

/* CMS: when display combo changes, load profile path for that display into text entry */
static void on_cms_display_changed(GtkComboBox* combo, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->settings) {
        return;
    }
    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(combo));
    GtkBuilder* builder = (GtkBuilder*)g_object_get_data(G_OBJECT(toplevel), "settings_builder");
    if (!builder) {
        return;
    }
    GtkEntry* entry = GTK_ENTRY(gtk_builder_get_object(builder, "cms_color_profile_text"));
    if (!entry) {
        return;
    }
    GtkTreeIter iter;
    if (!gtk_combo_box_get_active_iter(combo, &iter)) {
        gtk_entry_set_text(entry, "");
        return;
    }
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    gchar* display_id = NULL;
    gtk_tree_model_get(model, &iter, CMS_DISPLAY_COLUMN_ID, &display_id, -1);
    if (display_id) {
        const gchar* path = settings_get_cm_display_profile(ctx->settings, display_id);
        gtk_entry_set_text(entry, path ? path : "");
        g_free(display_id);
    } else {
        gtk_entry_set_text(entry, "");
    }
}

/* CMS: when mode changes, enable/disable custom profile controls */
static void on_cms_mode_toggled(GtkToggleButton* button, gpointer user_data) {
    GtkWidget* custom_box = (GtkWidget*)user_data;
    if (custom_box) {
        gtk_widget_set_sensitive(custom_box, gtk_toggle_button_get_active(button));
    }
}

/* Open custom color chooser for canvas background; update settings and button appearance */
static void on_canvas_bgcolor_clicked(GtkButton* button, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || !ctx->settings) {
        return;
    }
    GtkWidget* settings_dialog = gtk_widget_get_toplevel(GTK_WIDGET(button));
    if (!settings_dialog || !GTK_IS_WINDOW(settings_dialog)) {
        return;
    }

    gdouble r, g, b;
    settings_get_canvas_background(ctx->settings, &r, &g, &b);
    GdkRGBA initial = {(float)r, (float)g, (float)b, 1.0f};

    GtkWidget* color_dialog = color_chooser_dialog_new(
        GTK_WINDOW(settings_dialog),
        _("Canvas Background Color"),
        &initial,
        NULL,
        NULL,
        FALSE);

    gtk_dialog_run(GTK_DIALOG(color_dialog));

    double out_r, out_g, out_b;
    color_chooser_dialog_get_color(color_dialog, &out_r, &out_g, &out_b);
    settings_set_canvas_background(ctx->settings, out_r, out_g, out_b);

    initial.red = (float)out_r;
    initial.green = (float)out_g;
    initial.blue = (float)out_b;
    initial.alpha = 1.0f;
    update_color_button_appearance(GTK_WIDGET(button), &initial);

    gtk_widget_destroy(color_dialog);
}

void settings_dialog_show(AppContext* ctx) {
    if (!ctx || !ctx->settings) {
        return;
    }

    GError* error = NULL;
    GtkBuilder* builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/dialogs/settings_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load settings_dialog.glade: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(builder);
        return;
    }

    GtkWidget* dialog = GTK_WIDGET(gtk_builder_get_object(builder, "settings_dialog"));
    if (!dialog) {
        g_object_unref(builder);
        return;
    }

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(ctx->window));
    g_object_set_data_full(G_OBJECT(dialog), "settings_builder", g_object_ref(builder), (GDestroyNotify)g_object_unref);
    g_object_unref(builder); /* dialog owns one ref via data */

    /* Prefix labels for settings that require an app restart to take effect */
    {
        const char* restart_label_ids[] = {
            "undo_limit_label",
            "undo_compression_level_label",
            "threads_label",
            "renderer_label",
            "hardware_acceleration_label",
            "temp_location_label",
            NULL};
        for (const char** id = restart_label_ids; *id != NULL; id++) {
            GtkLabel* label = GTK_LABEL(gtk_builder_get_object(builder, *id));
            if (label) {
                const gchar* current = gtk_label_get_text(label);
                gchar* prefixed = g_strdup_printf("*%s", current ? current : "");
                gtk_label_set_text(label, prefixed);
                g_free(prefixed);
            }
        }
    }

    /* Notebook tab icons (icon before label) */
    {
        GtkNotebook* notebook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "settings_notebook"));
        if (notebook) {
            GtkWidget* general_page = GTK_WIDGET(gtk_builder_get_object(builder, "general_tab_content_box"));
            GtkWidget* general_label = GTK_WIDGET(gtk_builder_get_object(builder, "general_tab_label"));
            if (general_page && general_label) {
                set_tab_icon_label(notebook, general_page, general_label, "/icons/settings-general.png");
            }
            GtkWidget* ui_page = GTK_WIDGET(gtk_builder_get_object(builder, "ui_tab_content_box"));
            GtkWidget* ui_label = GTK_WIDGET(gtk_builder_get_object(builder, "ui_tab_label"));
            if (ui_page && ui_label) {
                set_tab_icon_label(notebook, ui_page, ui_label, "/icons/settings-ui.png");
            }
            GtkWidget* perf_page = GTK_WIDGET(gtk_builder_get_object(builder, "performance_tab_content_box"));
            GtkWidget* perf_label = GTK_WIDGET(gtk_builder_get_object(builder, "performance_tab_label"));
            if (perf_page && perf_label) {
                set_tab_icon_label(notebook, perf_page, perf_label, "/icons/settings-performance.png");
            }
            GtkWidget* advanced_page = GTK_WIDGET(gtk_builder_get_object(builder, "advanced_tab_content_box"));
            GtkWidget* advanced_label = GTK_WIDGET(gtk_builder_get_object(builder, "advanced_tab_label"));
            if (advanced_page && advanced_label) {
                set_tab_icon_label(notebook, advanced_page, advanced_label, "/icons/settings-advanced.png");
            }
            GtkWidget* cms_page = GTK_WIDGET(gtk_builder_get_object(builder, "cms_tab_content_box"));
            GtkWidget* cms_label = GTK_WIDGET(gtk_builder_get_object(builder, "cms_tab_label"));
            if (cms_page && cms_label) {
                set_tab_icon_label(notebook, cms_page, cms_label, "/icons/settings-cms.png");
            }
            GtkWidget* mouse_page = GTK_WIDGET(gtk_builder_get_object(builder, "mouse_tab_content_box"));
            GtkWidget* mouse_label = GTK_WIDGET(gtk_builder_get_object(builder, "mouse_tab_label"));
            if (mouse_page && mouse_label) {
                set_tab_icon_label(notebook, mouse_page, mouse_label, "/icons/settings-mouse.png");
            }
        }
    }

    /* Canvas background color: plain button that opens custom color chooser */
    GtkWidget* canvas_bg_btn = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_bgcolor_button"));
    if (canvas_bg_btn) {
        gdouble r, g, b;
        settings_get_canvas_background(ctx->settings, &r, &g, &b);
        GdkRGBA rgba = {(float)r, (float)g, (float)b, 1.0f};
        update_color_button_appearance(canvas_bg_btn, &rgba);
        ui_utils_widget_set_hand_cursor(canvas_bg_btn);
        g_signal_connect(canvas_bg_btn, "clicked", G_CALLBACK(on_canvas_bgcolor_clicked), ctx);
    }

    /* Temp file directory (Advanced tab): load, wire real-time validation */
    {
        GtkEntry* temp_entry = GTK_ENTRY(gtk_builder_get_object(builder, "temp_location_entry"));
        GtkButton* temp_button = GTK_BUTTON(gtk_builder_get_object(builder, "temp_location_button"));
        GtkLabel* temp_msg_label = GTK_LABEL(gtk_builder_get_object(builder, "temp_location_msg_label"));
        if (temp_msg_label) {
            gtk_widget_set_no_show_all(GTK_WIDGET(temp_msg_label), TRUE);
        }
        if (temp_entry) {
            const gchar* temp_dir = settings_get_undo_temp_directory(ctx->settings);
            const gchar* display_dir = (temp_dir && temp_dir[0] != '\0') ? temp_dir : g_get_tmp_dir();
            gtk_entry_set_text(temp_entry, display_dir);
            g_signal_connect(temp_entry, "changed", G_CALLBACK(on_temp_location_changed), builder);
            temp_location_update_msg_label_visibility(builder, display_dir);
            if (temp_button) {
                GtkWidget* img = gtk_image_new_from_icon_name("folder-open", GTK_ICON_SIZE_BUTTON);
                if (img) {
                    gtk_button_set_image(temp_button, img);
                }
                g_signal_connect(temp_button, "clicked", G_CALLBACK(on_temp_location_browse_clicked), temp_entry);
            }
        }
    }

    /* Transparency (checkerboard): grid size combo - Small, Medium, Large */
    GtkWidget* grid_size_combo = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_grid_size_combo"));
    if (grid_size_combo) {
        GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
        const gchar* size_labels[] = {_("Small"), _("Medium"), _("Large"), NULL};
        for (int i = 0; size_labels[i] != NULL; i++) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, size_labels[i], -1);
        }
        gtk_combo_box_set_model(GTK_COMBO_BOX(grid_size_combo), GTK_TREE_MODEL(store));
        g_object_unref(store);
        GtkCellRenderer* cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_clear(GTK_CELL_LAYOUT(grid_size_combo));
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(grid_size_combo), cell, TRUE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(grid_size_combo), cell, "text", 0, NULL);
        gint size_idx = settings_get_alpha_check_size(ctx->settings);
        if (size_idx < 0 || size_idx > 2)
            size_idx = 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(grid_size_combo), size_idx);
    }

    /* Transparency: grid colors combo - Highlights, Midtones, Shadows */
    GtkWidget* grid_colors_combo = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_grid_colors_combo"));
    if (grid_colors_combo) {
        GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
        const gchar* color_labels[] = {_("Highlights"), _("Midtones"), _("Shadows"), NULL};
        for (int i = 0; color_labels[i] != NULL; i++) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, color_labels[i], -1);
        }
        gtk_combo_box_set_model(GTK_COMBO_BOX(grid_colors_combo), GTK_TREE_MODEL(store));
        g_object_unref(store);
        GtkCellRenderer* cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_clear(GTK_CELL_LAYOUT(grid_colors_combo));
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(grid_colors_combo), cell, TRUE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(grid_colors_combo), cell, "text", 0, NULL);
        gint preset_idx = alpha_check_preset_from_settings(ctx->settings);
        gtk_combo_box_set_active(GTK_COMBO_BOX(grid_colors_combo), preset_idx);
    }

    /* Transparency preview: draw checkerboard with current combo values */
    GtkWidget* transparency_preview = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_preview"));
    if (transparency_preview) {
        gtk_widget_set_size_request(transparency_preview, 48, 48);
        gtk_widget_set_halign(transparency_preview, GTK_ALIGN_START);
        gtk_widget_set_valign(transparency_preview, GTK_ALIGN_CENTER);
        g_signal_connect(transparency_preview, "draw", G_CALLBACK(on_transparency_preview_draw), NULL);
        if (grid_size_combo) {
            g_signal_connect(grid_size_combo, "changed", G_CALLBACK(on_transparency_combo_changed), transparency_preview);
        }
        if (grid_colors_combo) {
            g_signal_connect(grid_colors_combo, "changed", G_CALLBACK(on_transparency_combo_changed), transparency_preview);
        }
    }

    ui_utils_replace_builder_spin_with_vertical(builder, "undo_limit_spin");
    ui_utils_replace_builder_spin_with_vertical(builder, "undo_compression_level_spin");
    ui_utils_replace_builder_spin_with_vertical(builder, "threads_spin");
    ui_utils_replace_builder_spin_with_vertical(builder, "recent_files_max_spin");
    ui_utils_replace_builder_spin_with_vertical(builder, "file_recovery_interval_spin");
    ui_utils_replace_builder_spin_with_vertical(builder, "mouse_snap_distance_spin");

    /* Undo limit: settings use 1-100 */
    GtkAdjustment* undo_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "undo_limit_adjustment"));
    if (undo_adj) {
        g_object_set(undo_adj,
                     "lower", (gdouble)1.0,
                     "upper", (gdouble)100.0,
                     "step-increment", 1.0,
                     "page-increment", 10.0,
                     NULL);
        gint levels = settings_get_undo_levels(ctx->settings);
        gtk_adjustment_set_value(undo_adj, (gdouble)levels);
    }

    /* Undo compression level: ensure lower=1 (glade may only set upper=9) */
    GtkAdjustment* comp_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "undo_compression_level_adjustment"));
    if (comp_adj) {
        g_object_set(comp_adj, "lower", (gdouble)1.0, NULL);
        gint level = settings_get_undo_compression_level(ctx->settings);
        gtk_adjustment_set_value(comp_adj, (gdouble)level);
    }

    /* Worker threads: upper = CPU count */
    GtkAdjustment* threads_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "threads_adjustment"));
    if (threads_adj) {
        gint cpu = (gint)g_get_num_processors();
        if (cpu < 1) {
            cpu = 1;
        }
        g_object_set(threads_adj, "upper", (gdouble)cpu, NULL);
        gint threads = settings_get_worker_threads(ctx->settings);
        gtk_adjustment_set_value(threads_adj, (gdouble)threads);
    }

    /* Max recent files: 1-32 already in glade */
    GtkAdjustment* recent_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "recent_files_max_adjustment"));
    if (recent_adj) {
        guint max_rf = settings_get_max_recent_files(ctx->settings);
        gtk_adjustment_set_value(recent_adj, (gdouble)max_rf);
    }

    /* File recovery interval */
    GtkAdjustment* recovery_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "file_recovery_interval_adjustment"));
    if (recovery_adj) {
        gint sec = settings_get_file_recovery_interval_seconds(ctx->settings);
        gtk_adjustment_set_value(recovery_adj, (gdouble)sec);
    }

    /* Mouse snap distance: 1-255 (glade adjustment + spin) */
    GtkAdjustment* snap_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "snap_distance_adjustment"));
    if (snap_adj) {
        gint snap_px = settings_get_mouse_snap_distance(ctx->settings);
        gtk_adjustment_set_value(snap_adj, (gdouble)snap_px);
    }

    /* Renderer combobox: populate with available GPUs; disable if none */
    GtkWidget* renderer_combo = GTK_WIDGET(gtk_builder_get_object(builder, "renderer_combobox"));
    GtkWidget* gpu_checkbox = GTK_WIDGET(gtk_builder_get_object(builder, "hardware_acceleration_checkbox"));
    gint gpu_count = 0;
    GPUDeviceInfo* gpu_list = gpu_compositor_get_device_list(&gpu_count);
    if (gpu_count > 0 && gpu_list && renderer_combo) {
        GtkListStore* store = gtk_list_store_new(RENDERER_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
        for (gint i = 0; i < gpu_count; i++) {
            GtkTreeIter iter;
            gchar* label = gpu_list[i].is_default && gpu_list[i].name
                               ? g_strdup_printf("Default (%s)", gpu_list[i].name)
                               : g_strdup(gpu_list[i].name ? gpu_list[i].name : "Unknown");
            gchar* device_id = gpu_list[i].is_default ? g_strdup("") : g_strdup(gpu_list[i].name ? gpu_list[i].name : "");
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, RENDERER_COLUMN_LABEL, label, RENDERER_COLUMN_DEVICE_ID, device_id, -1);
            g_free(label);
            g_free(device_id);
        }
        gtk_combo_box_set_model(GTK_COMBO_BOX(renderer_combo), GTK_TREE_MODEL(store));
        g_object_unref(store);
        GtkCellRenderer* cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_clear(GTK_CELL_LAYOUT(renderer_combo));
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(renderer_combo), cell, TRUE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(renderer_combo), cell, "text", RENDERER_COLUMN_LABEL, NULL);
        /* Select row matching current gpu_device setting (empty/NULL = default = first row) */
        const gchar* current = settings_get_gpu_device_name(ctx->settings);
        gint select = 0;
        if (current && current[0] != '\0') {
            for (gint i = 0; i < gpu_count; i++) {
                if (!gpu_list[i].is_default && gpu_list[i].name && strcmp(current, gpu_list[i].name) == 0) {
                    select = i;
                    break;
                }
            }
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(renderer_combo), select);
        gpu_compositor_free_device_list(gpu_list, gpu_count);
    } else {
        if (renderer_combo) {
            gtk_widget_set_sensitive(renderer_combo, FALSE);
        }
        if (gpu_checkbox) {
            gtk_widget_set_sensitive(gpu_checkbox, FALSE);
        }
        if (gpu_list) {
            gpu_compositor_free_device_list(gpu_list, gpu_count);
        }
    }

    /* Hardware acceleration checkbox */
    if (gpu_checkbox) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gpu_checkbox), settings_get_gpu_acceleration_enabled(ctx->settings));
    }

    /* CMS tab: populate displays combo, rendering intent, wire mode/profile controls */
    {
        g_object_set_data(G_OBJECT(dialog), "settings_ctx", ctx);
        GtkComboBox* cms_display_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "cms_available_displays_combo"));
        GtkWidget* cms_custom_box = GTK_WIDGET(gtk_builder_get_object(builder, "cms_custom_profile_controls_box"));
        GtkRadioButton* cms_system_radio = GTK_RADIO_BUTTON(gtk_builder_get_object(builder, "cms_mode_system_radio"));
        GtkRadioButton* cms_custom_radio = GTK_RADIO_BUTTON(gtk_builder_get_object(builder, "cms_mode_custom_radio"));
        GtkEntry* cms_profile_entry = GTK_ENTRY(gtk_builder_get_object(builder, "cms_color_profile_text"));
        GtkButton* cms_profile_btn = GTK_BUTTON(gtk_builder_get_object(builder, "cms_color_profile_button"));

        /* Populate display combo */
        if (cms_display_combo) {
            GtkListStore* store = gtk_list_store_new(CMS_DISPLAY_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
            GdkDisplay* gdk_display = gdk_display_get_default();
            GdkMonitor* primary = gdk_display ? gdk_display_get_primary_monitor(gdk_display) : NULL;
            gint n = gdk_display ? (gint)gdk_display_get_n_monitors(gdk_display) : 0;
            for (gint i = 0; i < n; i++) {
                GdkMonitor* mon = gdk_display_get_monitor(gdk_display, i);
                if (!mon) {
                    continue;
                }
                GdkRectangle geom;
                gdk_monitor_get_geometry(mon, &geom);
                /* Use monitor index as stable display_id (gdk_monitor_get_connector is GDK4-only) */
                gchar* display_id = g_strdup_printf("monitor-%d", i);
                const gchar* model = gdk_monitor_get_model(mon);
                gchar* base_name = (model && model[0] != '\0') ? g_strdup(model) : g_strdup_printf("Monitor %d", i + 1);
                gchar* label = g_strdup_printf("%s%s (%d x %d)",
                                               (primary && mon == primary) ? "Primary display: " : "",
                                               base_name,
                                               geom.width,
                                               geom.height);
                g_free(base_name);
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter, CMS_DISPLAY_COLUMN_LABEL, label, CMS_DISPLAY_COLUMN_ID, display_id, -1);
                g_free(label);
                g_free(display_id);
            }
            gtk_combo_box_set_model(cms_display_combo, GTK_TREE_MODEL(store));
            g_object_unref(store);
            GtkCellRenderer* cell = gtk_cell_renderer_text_new();
            gtk_cell_layout_clear(GTK_CELL_LAYOUT(cms_display_combo));
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cms_display_combo), cell, TRUE);
            gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(cms_display_combo), cell, "text", CMS_DISPLAY_COLUMN_LABEL, NULL);
            gtk_combo_box_set_active(cms_display_combo, n > 0 ? 0 : -1);
            g_signal_connect(cms_display_combo, "changed", G_CALLBACK(on_cms_display_changed), ctx);
        }

        /* Rendering intent combo */
        GtkComboBox* cms_intent_combo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "cms_rendering_intent_combo"));
        if (cms_intent_combo) {
            GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
            const gchar* intents[] = {"Perceptual", "Relative colorimetric", "Saturation", "Absolute colorimetric", NULL};
            for (int k = 0; intents[k] != NULL; k++) {
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter, 0, intents[k], -1);
            }
            gtk_combo_box_set_model(cms_intent_combo, GTK_TREE_MODEL(store));
            g_object_unref(store);
            GtkCellRenderer* cell = gtk_cell_renderer_text_new();
            gtk_cell_layout_clear(GTK_CELL_LAYOUT(cms_intent_combo));
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cms_intent_combo), cell, TRUE);
            gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(cms_intent_combo), cell, "text", 0, NULL);
            gint intent = settings_get_cm_rendering_intent(ctx->settings);
            if (intent < 0 || intent > 3) {
                intent = 1;
            }
            gtk_combo_box_set_active(cms_intent_combo, intent);
        }

        /* Mode radios: load from settings (none=2, custom=1, system=0), enable/disable custom box */
        GtkRadioButton* cms_none_radio = GTK_RADIO_BUTTON(gtk_builder_get_object(builder, "cms_mode_none_radio"));
        if (cms_system_radio && cms_custom_radio && cms_none_radio) {
            gint mode = settings_get_cm_mode(ctx->settings);
            GtkToggleButton* active_radio = (mode == 2)   ? GTK_TOGGLE_BUTTON(cms_none_radio)
                                            : (mode == 1) ? GTK_TOGGLE_BUTTON(cms_custom_radio)
                                                          : GTK_TOGGLE_BUTTON(cms_system_radio);
            gtk_toggle_button_set_active(active_radio, TRUE);
        }
        if (cms_custom_box && cms_custom_radio) {
            gtk_widget_set_sensitive(cms_custom_box, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cms_custom_radio)));
            g_signal_connect(cms_custom_radio, "toggled", G_CALLBACK(on_cms_mode_toggled), cms_custom_box);
        }

        /* Black point and embedded ICC checkboxes */
        GtkCheckButton* cms_bpc = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "cms_use_black_point_checkbox"));
        if (cms_bpc) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cms_bpc), settings_get_cm_black_point_compensation(ctx->settings));
        }
        GtkCheckButton* cms_embedded = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "cms_use_embedded_icc_checkbox"));
        if (cms_embedded) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cms_embedded), settings_get_cm_use_embedded_icc(ctx->settings));
        }

        /* Profile text: load for selected display (triggered by combo changed which fires after set_active) */
        if (cms_display_combo && cms_profile_entry) {
            GtkTreeIter iter;
            if (gtk_combo_box_get_active_iter(cms_display_combo, &iter)) {
                GtkTreeModel* model = gtk_combo_box_get_model(cms_display_combo);
                gchar* display_id = NULL;
                gtk_tree_model_get(model, &iter, CMS_DISPLAY_COLUMN_ID, &display_id, -1);
                if (display_id) {
                    const gchar* path = settings_get_cm_display_profile(ctx->settings, display_id);
                    gtk_entry_set_text(cms_profile_entry, path ? path : "");
                    g_free(display_id);
                }
            }
        }
        if (cms_profile_btn && cms_profile_entry) {
            g_signal_connect(cms_profile_btn, "clicked", G_CALLBACK(on_cms_profile_browse_clicked), cms_profile_entry);
        }
    }

    GtkWidget* ok_btn = GTK_WIDGET(gtk_builder_get_object(builder, "settings_ok_button"));
    GtkWidget* cancel_btn = GTK_WIDGET(gtk_builder_get_object(builder, "settings_cancel_button"));

    if (ok_btn) {
        g_object_set_data(G_OBJECT(ok_btn), "settings_dialog", dialog);
        g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_settings_ok_clicked), ctx);
    }
    if (cancel_btn) {
        g_object_set_data(G_OBJECT(cancel_btn), "settings_dialog", dialog);
        g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_settings_cancel_clicked), NULL);
    }

    g_signal_connect(dialog, "destroy", G_CALLBACK(on_settings_dialog_destroy), NULL);

    gtk_widget_show_all(dialog);
}
