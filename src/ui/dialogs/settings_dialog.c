/*
 * Settings dialog 
 * Uses layout from resources/ui/settings_dialog.glade
 */
#include "ui/dialogs/settings_dialog.h"
#include "app/autosave.h"
#include "app/settings.h"
#include "render/gpu_compositor.h"
#include "ui.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

enum {
    RENDERER_COLUMN_LABEL,
    RENDERER_COLUMN_DEVICE_ID,
    RENDERER_N_COLUMNS
};

/* Apply dialog values to settings and save; then update UI (e.g. canvas bg) */
static void settings_dialog_apply_and_save(GtkDialog* dialog, AppContext* ctx) {
    GtkBuilder* builder = (GtkBuilder*)g_object_get_data(G_OBJECT(dialog), "settings_builder");
    if (!builder || !ctx || !ctx->settings) {
        return;
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

    if (ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Refresh canvas background on all documents */
    ui_update_canvas_background_color(ctx);
}

static void on_settings_ok_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    AppContext* ctx = (AppContext*)user_data;
    GtkWidget* dialog = (GtkWidget*)g_object_get_data(G_OBJECT(button), "settings_dialog");
    if (dialog && ctx) {
        settings_dialog_apply_and_save(GTK_DIALOG(dialog), ctx);
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

static void on_settings_dialog_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
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
        "Canvas Background Color",
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
    if (!gtk_builder_add_from_resource(builder, "/ui/settings_dialog.glade", &error)) {
        g_warning("Failed to load settings_dialog.glade: %s", error ? error->message : "Unknown error");
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

    /* Canvas background color: plain button that opens custom color chooser */
    GtkWidget* canvas_bg_btn = GTK_WIDGET(gtk_builder_get_object(builder, "canvas_bgcolor_button"));
    if (canvas_bg_btn) {
        gdouble r, g, b;
        settings_get_canvas_background(ctx->settings, &r, &g, &b);
        GdkRGBA rgba = {(float)r, (float)g, (float)b, 1.0f};
        update_color_button_appearance(canvas_bg_btn, &rgba);
        g_signal_connect(canvas_bg_btn, "clicked", G_CALLBACK(on_canvas_bgcolor_clicked), ctx);
    }

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
    /* Fix glade typo: undo_limitl_spin */
    GtkSpinButton* undo_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "undo_limitl_spin"));
    if (undo_spin && undo_adj) {
        gtk_spin_button_set_adjustment(undo_spin, undo_adj);
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
