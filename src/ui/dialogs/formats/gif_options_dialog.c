/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/dialogs/formats/gif_options_dialog.h"
#include "i18n.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include "debug_logger.h"

/* -------------------------------------------------------------------------
 * Re-declare GIF save-option types locally
 * ---------------------------------------------------------------------- */

typedef enum {
    GIF_COLOR_MODEL_AUTO      = 0,
    GIF_COLOR_MODEL_COLOR     = 1,
    GIF_COLOR_MODEL_GRAYSCALE = 2
} GIFColorModel;

typedef enum {
    GIF_TRANSPARENCY_AUTO     = 0,
    GIF_TRANSPARENCY_NONE     = 1,
    GIF_TRANSPARENCY_BY_CUTOFF = 2,
    GIF_TRANSPARENCY_BY_COLOR  = 3
} GIFTransparency;

typedef struct {
    GIFColorModel   color_model;
    int             palette_size;
    uint8_t         bg_color_r, bg_color_g, bg_color_b;
    GIFTransparency transparency;
    uint8_t         alpha_cutoff;
    uint8_t         transparent_color_r, transparent_color_g, transparent_color_b;
    uint32_t        reserved[4];
} GIFSaveOptions;

/* -------------------------------------------------------------------------
 * Colour-button helper data
 * ---------------------------------------------------------------------- */

typedef struct {
    GdkRGBA*  color;
    GtkWidget* button;
} GIFColorButtonData;

static void update_color_btn(GtkWidget* btn, GdkRGBA* c) {
    update_color_button_appearance(btn, c);
}

/* -------------------------------------------------------------------------
 * Dialog state – all mutable per-dialog context lives here so we avoid
 * global variables and support re-entrancy.
 * ---------------------------------------------------------------------- */

typedef struct {
    /* Toggle buttons – color model */
    GtkWidget* cm_auto;
    GtkWidget* cm_color;
    GtkWidget* cm_gray;

    /* Toggle buttons – transparency */
    GtkWidget* tr_auto;
    GtkWidget* tr_none;
    GtkWidget* tr_cutoff;
    GtkWidget* tr_color;

    /* Visibility-controlled boxes */
    GtkWidget* palette_size_box;
    GtkWidget* alpha_cutoff_box;
    GtkWidget* transparent_color_box;

    /* Palette size adjustment */
    GtkAdjustment* palette_size_adj;

    /* Alpha cut-off adjustment */
    GtkAdjustment* alpha_cutoff_adj;

    /* Background colour */
    GdkRGBA         bg_color;
    GtkWidget*      bg_color_btn;
    GIFColorButtonData bg_color_data;

    /* Transparent colour */
    GdkRGBA         trans_color;
    GtkWidget*      trans_color_btn;
    GIFColorButtonData trans_color_data;

    /* Current selection state */
    GIFColorModel   color_model;
    GIFTransparency transparency;

    /* Signals blocked flag to prevent re-entrance during init */
    bool initialising;
} GIFDialogState;

/* -------------------------------------------------------------------------
 * Visibility update
 * ---------------------------------------------------------------------- */

static void update_visibility(GIFDialogState* s) {
    bool palette_visible = (s->color_model == GIF_COLOR_MODEL_COLOR ||
                            s->color_model == GIF_COLOR_MODEL_GRAYSCALE);
    bool cutoff_visible  = (s->transparency == GIF_TRANSPARENCY_BY_CUTOFF);
    bool color_visible   = (s->transparency == GIF_TRANSPARENCY_BY_COLOR);

    if (s->palette_size_box)
        gtk_widget_set_visible(s->palette_size_box,  palette_visible);
    if (s->alpha_cutoff_box)
        gtk_widget_set_visible(s->alpha_cutoff_box,  cutoff_visible);
    if (s->transparent_color_box)
        gtk_widget_set_visible(s->transparent_color_box, color_visible);
}

/* -------------------------------------------------------------------------
 * Toggle-button group helpers
 * Sets one button active and deactivates the others without re-entering.
 * ---------------------------------------------------------------------- */

static void set_color_model(GIFDialogState* s, GIFColorModel model) {
    s->color_model = model;

    GtkWidget* btns[3] = { s->cm_auto, s->cm_color, s->cm_gray };
    bool       active[3] = {
        model == GIF_COLOR_MODEL_AUTO,
        model == GIF_COLOR_MODEL_COLOR,
        model == GIF_COLOR_MODEL_GRAYSCALE
    };
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        g_signal_handlers_block_matched(btns[i], G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btns[i]), active[i]);
        g_signal_handlers_unblock_matched(btns[i], G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
    if (!s->initialising) update_visibility(s);
}

static void set_transparency(GIFDialogState* s, GIFTransparency t) {
    s->transparency = t;

    GtkWidget* btns[4] = { s->tr_auto, s->tr_none, s->tr_cutoff, s->tr_color };
    bool       active[4] = {
        t == GIF_TRANSPARENCY_AUTO,
        t == GIF_TRANSPARENCY_NONE,
        t == GIF_TRANSPARENCY_BY_CUTOFF,
        t == GIF_TRANSPARENCY_BY_COLOR
    };
    for (int i = 0; i < 4; i++) {
        if (!btns[i]) continue;
        g_signal_handlers_block_matched(btns[i], G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btns[i]), active[i]);
        g_signal_handlers_unblock_matched(btns[i], G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
    if (!s->initialising) update_visibility(s);
}

/* -------------------------------------------------------------------------
 * Toggle-button signal handlers
 * ---------------------------------------------------------------------- */

static void on_cm_auto_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_color_model(s, GIF_COLOR_MODEL_AUTO);
    else { /* keep at least one active */
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}
static void on_cm_color_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_color_model(s, GIF_COLOR_MODEL_COLOR);
    else {
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}
static void on_cm_gray_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_color_model(s, GIF_COLOR_MODEL_GRAYSCALE);
    else {
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}

static void on_tr_auto_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_transparency(s, GIF_TRANSPARENCY_AUTO);
    else {
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}
static void on_tr_none_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_transparency(s, GIF_TRANSPARENCY_NONE);
    else {
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}
static void on_tr_cutoff_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_transparency(s, GIF_TRANSPARENCY_BY_CUTOFF);
    else {
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}
static void on_tr_color_toggled(GtkToggleButton* btn, gpointer ud) {
    GIFDialogState* s = (GIFDialogState*)ud;
    if (s->initialising) return;
    if (gtk_toggle_button_get_active(btn)) set_transparency(s, GIF_TRANSPARENCY_BY_COLOR);
    else {
        g_signal_handlers_block_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
        gtk_toggle_button_set_active(btn, TRUE);
        g_signal_handlers_unblock_matched(GTK_WIDGET(btn), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
    }
}

/* -------------------------------------------------------------------------
 * Color-chooser button handlers
 * ---------------------------------------------------------------------- */

static void on_bg_color_update(double r, double g, double b, gpointer ud) {
    GIFColorButtonData* d = (GIFColorButtonData*)ud;
    if (!d) return;
    d->color->red = r; d->color->green = g; d->color->blue = b; d->color->alpha = 1.0;
    update_color_btn(d->button, d->color);
}

static void on_trans_color_update(double r, double g, double b, gpointer ud) {
    GIFColorButtonData* d = (GIFColorButtonData*)ud;
    if (!d) return;
    d->color->red = r; d->color->green = g; d->color->blue = b; d->color->alpha = 1.0;
    update_color_btn(d->button, d->color);
}

static void show_color_chooser(GtkButton* btn, GdkRGBA* color,
                                const gchar* title,
                                void (*update_cb)(double, double, double, gpointer),
                                gpointer ud) {
    GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn)));
    GtkWidget* dlg = color_chooser_dialog_new(parent, title, color, update_cb, ud, FALSE);
    gtk_dialog_run(GTK_DIALOG(dlg));
    double r, g, b;
    color_chooser_dialog_get_color(dlg, &r, &g, &b);
    color->red = r; color->green = g; color->blue = b; color->alpha = 1.0;
    update_color_btn(GTK_WIDGET(btn), color);
    gtk_widget_destroy(dlg);
}

static void on_bg_color_clicked(GtkButton* btn, gpointer ud) {
    GIFColorButtonData* d = (GIFColorButtonData*)ud;
    show_color_chooser(btn, d->color, _("Choose Background Color"),
                       on_bg_color_update, d);
}

static void on_trans_color_clicked(GtkButton* btn, gpointer ud) {
    GIFColorButtonData* d = (GIFColorButtonData*)ud;
    show_color_chooser(btn, d->color, _("Choose Transparent Color"),
                       on_trans_color_update, d);
}

/* -------------------------------------------------------------------------
 * OK / Cancel response helper
 * ---------------------------------------------------------------------- */

static void on_ok_cancel_clicked(GtkButton* btn, gpointer ud) {
    GtkDialog* dlg = GTK_DIALOG(ud);
    gint resp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "response-id"));
    gtk_dialog_response(dlg, resp);
}

/* =========================================================================
 * Public entry point
 * ====================================================================== */

gboolean gif_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc) {
    (void)doc;

    if (!opts) return FALSE;

    GIFSaveOptions* gif_opts = opts->plugin_data ? (GIFSaveOptions*)opts->plugin_data : NULL;

    /* Load Glade resource */
    GError*     error   = NULL;
    GtkBuilder* builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    if (!gtk_builder_add_from_resource(builder, "/ui/gif_options_dialog.glade", &error)) {
        debug_log("WRN", "Failed to load gif_options_dialog.glade: %s",
                  error ? error->message : "unknown");
        if (error) g_error_free(error);
        g_object_unref(builder);
        return FALSE;
    }

    GtkWidget* dialog = GTK_WIDGET(gtk_builder_get_object(builder, "gif_options_dialog"));
    if (!dialog) {
        debug_log("WRN", "gif_options_dialog widget not found");
        g_object_unref(builder);
        return FALSE;
    }

    if (parent) gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);

    /* Header bar */
    if (GTK_IS_WINDOW(dialog)) {
        const gchar* title = gtk_window_get_title(GTK_WINDOW(dialog));
        ui_utils_set_header_bar(GTK_WINDOW(dialog), title ? title : _("GIF Options"));
    }

    /* ---- Collect widgets ---- */
    GIFDialogState state;
    memset(&state, 0, sizeof(state));
    state.initialising = true;

    state.cm_auto  = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_auto_button"));
    state.cm_color = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_color__button"));
    state.cm_gray  = GTK_WIDGET(gtk_builder_get_object(builder, "color_model_grayscale_button"));

    state.palette_size_box = GTK_WIDGET(gtk_builder_get_object(builder, "palette_size_box"));
    state.palette_size_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "palette_size_adjustment"));

    state.bg_color_btn = GTK_WIDGET(gtk_builder_get_object(builder, "bg_color_button"));

    state.tr_auto   = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_auto_button"));
    state.tr_none   = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_none_button"));
    state.tr_cutoff = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_cutoff_button"));
    state.tr_color  = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_color_button"));

    state.alpha_cutoff_box = GTK_WIDGET(gtk_builder_get_object(builder, "transparency_alpha_cutoff_box"));
    state.alpha_cutoff_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "alpha_cutoff_adjustment"));

    state.transparent_color_box = GTK_WIDGET(gtk_builder_get_object(builder, "transparent_color_box"));
    state.trans_color_btn       = GTK_WIDGET(gtk_builder_get_object(builder, "transparent_color_button"));

    /* ---- Reconfigure adjustments with correct ranges ---- */
    if (state.palette_size_adj)
        gtk_adjustment_configure(state.palette_size_adj, 256.0, 2.0, 256.0, 1.0, 10.0, 0.0);
    if (state.alpha_cutoff_adj)
        gtk_adjustment_configure(state.alpha_cutoff_adj, 64.0, 0.0, 254.0, 1.0, 10.0, 0.0);

    /* ---- Connect toggle-button signals BEFORE setting initial values ---- */
    if (state.cm_auto)   g_signal_connect(state.cm_auto,   "toggled", G_CALLBACK(on_cm_auto_toggled),   &state);
    if (state.cm_color)  g_signal_connect(state.cm_color,  "toggled", G_CALLBACK(on_cm_color_toggled),  &state);
    if (state.cm_gray)   g_signal_connect(state.cm_gray,   "toggled", G_CALLBACK(on_cm_gray_toggled),   &state);
    if (state.tr_auto)   g_signal_connect(state.tr_auto,   "toggled", G_CALLBACK(on_tr_auto_toggled),   &state);
    if (state.tr_none)   g_signal_connect(state.tr_none,   "toggled", G_CALLBACK(on_tr_none_toggled),   &state);
    if (state.tr_cutoff) g_signal_connect(state.tr_cutoff, "toggled", G_CALLBACK(on_tr_cutoff_toggled), &state);
    if (state.tr_color)  g_signal_connect(state.tr_color,  "toggled", G_CALLBACK(on_tr_color_toggled),  &state);

    /* ---- Initialise from current options ---- */
    GIFColorModel   init_cm = gif_opts ? gif_opts->color_model   : GIF_COLOR_MODEL_AUTO;
    GIFTransparency init_tr = gif_opts ? gif_opts->transparency  : GIF_TRANSPARENCY_AUTO;
    int             init_ps = gif_opts ? gif_opts->palette_size  : 256;
    uint8_t         init_ac = gif_opts ? gif_opts->alpha_cutoff  : 64;

    /* Background colour */
    state.bg_color.red   = gif_opts ? (double)gif_opts->bg_color_r / 255.0 : 1.0;
    state.bg_color.green = gif_opts ? (double)gif_opts->bg_color_g / 255.0 : 1.0;
    state.bg_color.blue  = gif_opts ? (double)gif_opts->bg_color_b / 255.0 : 1.0;
    state.bg_color.alpha = 1.0;
    state.bg_color_data.color  = &state.bg_color;
    state.bg_color_data.button = state.bg_color_btn;

    /* Transparent colour */
    state.trans_color.red   = gif_opts ? (double)gif_opts->transparent_color_r / 255.0 : 1.0;
    state.trans_color.green = gif_opts ? (double)gif_opts->transparent_color_g / 255.0 : 0.0;
    state.trans_color.blue  = gif_opts ? (double)gif_opts->transparent_color_b / 255.0 : 1.0;
    state.trans_color.alpha = 1.0;
    state.trans_color_data.color  = &state.trans_color;
    state.trans_color_data.button = state.trans_color_btn;

    /* Set initial toggle states (signals are connected but initialising flag blocks handlers) */
    state.color_model  = init_cm;
    state.transparency = init_tr;

    set_color_model(&state, init_cm);
    set_transparency(&state, init_tr);

    /* Palette size */
    if (state.palette_size_adj)
        gtk_adjustment_set_value(state.palette_size_adj, (gdouble)init_ps);

    /* Alpha cut-off */
    if (state.alpha_cutoff_adj)
        gtk_adjustment_set_value(state.alpha_cutoff_adj, (gdouble)init_ac);

    /* Colour buttons */
    if (state.bg_color_btn) {
        update_color_btn(state.bg_color_btn, &state.bg_color);
        ui_utils_widget_set_hand_cursor(state.bg_color_btn);
        g_signal_connect(state.bg_color_btn, "clicked", G_CALLBACK(on_bg_color_clicked), &state.bg_color_data);
    }
    if (state.trans_color_btn) {
        update_color_btn(state.trans_color_btn, &state.trans_color);
        ui_utils_widget_set_hand_cursor(state.trans_color_btn);
        g_signal_connect(state.trans_color_btn, "clicked", G_CALLBACK(on_trans_color_clicked), &state.trans_color_data);
    }

    /* OK / Cancel buttons */
    GtkWidget* ok_btn     = GTK_WIDGET(gtk_builder_get_object(builder, "gif_options_ok_button"));
    GtkWidget* cancel_btn = GTK_WIDGET(gtk_builder_get_object(builder, "gif_options_cancel_button"));
    if (ok_btn) {
        g_object_set_data(G_OBJECT(ok_btn), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
        g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_ok_cancel_clicked), dialog);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    if (cancel_btn) {
        g_object_set_data(G_OBJECT(cancel_btn), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
        g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_ok_cancel_clicked), dialog);
    }

    /* Unblock and show */
    state.initialising = false;

    gtk_widget_show_all(dialog);
    update_visibility(&state); /* enforce correct initial visibility after show_all */

    gint     response = gtk_dialog_run(GTK_DIALOG(dialog));
    gboolean ok       = (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_ACCEPT);

    if (ok && gif_opts) {
        gif_opts->color_model  = state.color_model;
        gif_opts->transparency = state.transparency;

        if (state.palette_size_adj)
            gif_opts->palette_size = (int)gtk_adjustment_get_value(state.palette_size_adj);

        if (state.alpha_cutoff_adj)
            gif_opts->alpha_cutoff = (uint8_t)gtk_adjustment_get_value(state.alpha_cutoff_adj);

        gif_opts->bg_color_r = (uint8_t)(state.bg_color.red   * 255.0 + 0.5);
        gif_opts->bg_color_g = (uint8_t)(state.bg_color.green * 255.0 + 0.5);
        gif_opts->bg_color_b = (uint8_t)(state.bg_color.blue  * 255.0 + 0.5);

        gif_opts->transparent_color_r = (uint8_t)(state.trans_color.red   * 255.0 + 0.5);
        gif_opts->transparent_color_g = (uint8_t)(state.trans_color.green * 255.0 + 0.5);
        gif_opts->transparent_color_b = (uint8_t)(state.trans_color.blue  * 255.0 + 0.5);
    }

    gtk_widget_destroy(dialog);
    g_object_unref(builder);
    return ok;
}
