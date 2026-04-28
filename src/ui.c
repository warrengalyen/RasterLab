/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui.h"
#include "app/autosave.h"
#include "gradient.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "document.h"
#include "filters.h"
#include "ocular.h"
#include "panels.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "render/tile_worker.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "selection/selection_undo.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "tools/tool_crop.h"
#include "tools/tool_ellipse_select.h"
#include "tools/tool_magic_wand_select.h"
#include "tools/tool_polygon_select.h"
#include "tools/tool_rect_select.h"
#include "ui/dialogs/canvas_size_dialog.h"
#include "ui/dialogs/new_layer_dialog.h"
#include "ui/dialogs/recovery_dialog.h"
#include "ui/dialogs/selection_radius_dialog.h"
#include "ui/layers_panel.h"
#include "ui/ruler_units.h"
#include "ui/swatches.h"
#include "ui/tool_options_panel.h"
#include "ui/tools_panel.h"
#include "ui/ui_edit_menu.h"
#include "ui/ui_file_menu.h"
#include "ui/ui_filter.h"
#include "ui/ui_filter_adjust.h"
#include "ui/ui_filter_effects.h"
#include "ui/ui_image_menu.h"
#include "ui/ui_layer_menu.h"
#include "ui/ui_select_menu.h"
#include "ui/ui_help_menu.h"
#include "ui/ui_tools_menu.h"
#include "ui/ui_utils.h"
#include "ui/ui_view_menu.h"
#include "ui/workspace.h"
#include "undo/undo_disk.h"
#include "i18n.h"
#include <glib.h>
#include <limits.h>
#include <math.h>
#include <pango/pango.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "debug_logger.h"


/* Log handler to suppress harmless GTK Builder menu warnings */
static gboolean gtk_builder_menu_warning_handler(const gchar* log_domain,
                                                 GLogLevelFlags log_level,
                                                 const gchar* message,
                                                 gpointer user_data) {
    (void)log_domain;
    (void)log_level;
    (void)user_data;

    /* Suppress warnings about adding GtkMenu to GtkMenuItem */
    if (message && strstr(message, "Cannot add an object of type GtkMenu")) {
        return TRUE; /* Suppress this warning */
    }

    return FALSE; /* Let other warnings through */
}

/* Forward declarations */
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page,
                                    guint page_num, gpointer user_data);
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data);
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx);
static void on_statusbar_zoom_out(GtkWidget* widget, gpointer data);
static void on_statusbar_zoom_in(GtkWidget* widget, gpointer data);
static void on_statusbar_zoom_changed(GtkComboBox* combo, gpointer data);
static void on_statusbar_size_unit_changed(GtkComboBox* combo, gpointer data);
static gboolean combo_popup_fix_alignment_idle(gpointer user_data);
static GtkWidget* find_descendant_tree_view(GtkWidget* widget);
static GtkTreeModel* unwrap_combo_popup_model(GtkTreeModel* model);
static void zoom_combo_cell_data_func(GtkCellLayout* layout,
                                      GtkCellRenderer* cell,
                                      GtkTreeModel* model,
                                      GtkTreeIter* iter,
                                      gpointer data);

void ui_apply_list_combobox_style(GtkWidget* combo) {
    if (!combo) {
        return;
    }

    /* Apply once per process: GIMP forces list mode for zoom/unit combos.
     * This avoids the GTK menu-style popup behavior that can show “top whitespace”
     * when the popup flips to open upward (insufficient space below). */
    static gboolean css_installed = FALSE;
    if (!css_installed) {
        GtkCssProvider* provider = gtk_css_provider_new();
        const gchar* css =
            ".sb-combobox {"
            "  -GtkComboBox-appears-as-list: 1;"
            "}"
            /* Reduce padding to avoid odd vertical spacing */
            ".sb-combobox button {"
            "  padding-top: 0;"
            "  padding-bottom: 0;"
            "}"
            /* Darken row separators inside the popup list.
             * The popup is a separate toplevel; we tag it with sb-combobox-popup at runtime. */
            "window.sb-combobox-popup separator,"
            "window.sb-combobox-popup treeview.view separator {"
            "  background-color: rgba(0, 0, 0, 0.90);"
            "  min-height: 3px;"
            "  margin-top: 4px;"
            "  margin-bottom: 4px;"
            "}";
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        css_installed = TRUE;
    }

    GtkStyleContext* ctx = gtk_widget_get_style_context(combo);
    gtk_style_context_add_class(ctx, "sb-combobox");
}

/* Render “Separator” rows as a real, dark separator bar (theme-independent). */
static void zoom_combo_cell_data_func(GtkCellLayout* layout,
                                      GtkCellRenderer* cell,
                                      GtkTreeModel* model,
                                      GtkTreeIter* iter,
                                      gpointer data) {
    (void)layout;
    (void)data;

    gchar* text = NULL;
    gtk_tree_model_get(model, iter, 0, &text, -1);

    if (text && g_strcmp0(text, "Separator") == 0) {
        /* Draw a separator line using repeated “⎯”.
         * Let GTK clip it to the popup width. */
        static gchar* sep_text = NULL;
        if (!sep_text) {
            /* Build ~10 chars of U+23AF (UTF-8) */
            GString* s = g_string_sized_new(40);
            for (int i = 0; i < 10; i++) {
                g_string_append(s, "⎯");
            }
            sep_text = g_string_free(s, FALSE);
        }

        g_object_set(cell,
                     "markup", NULL,
                     "text", sep_text,
                     "foreground", "#2b2b2b",
                     "foreground-set", TRUE,
                     "cell-background-set", FALSE,
                     "ellipsize", PANGO_ELLIPSIZE_NONE,
                     "height", 10,
                     "xalign", 0.0,
                     "yalign", 0.5,
                     "xpad", 0,
                     "ypad", 0,
                     NULL);
    } else {
        /* Normal rows */
        g_object_set(cell,
                     "markup", NULL,
                     "text", text ? text : "",
                     "foreground-set", FALSE,
                     "cell-background-set", FALSE,
                     "height", -1,
                     "xalign", 0.0,
                     "yalign", 0.5,
                     "xpad", 0,
                     "ypad", 0,
                     NULL);
    }

    g_free(text);
}

/* Unwrap GtkTreeModelFilter/Sort layers to the base model */
static GtkTreeModel* unwrap_combo_popup_model(GtkTreeModel* model) {
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

/**
 * Find first GtkTreeView descendant of a widget.
 * Used to access the GtkComboBox popup's internal tree view.
 */
static GtkWidget* find_descendant_tree_view(GtkWidget* widget) {
    if (!widget) {
        return NULL;
    }

    if (GTK_IS_TREE_VIEW(widget)) {
        return widget;
    }

    if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* l = children; l; l = l->next) {
            GtkWidget* found = find_descendant_tree_view(GTK_WIDGET(l->data));
            if (found) {
                g_list_free(children);
                return found;
            }
        }
        g_list_free(children);
    }

    return NULL;
}

/**
 * GTK quirk workaround:
 * When a GtkComboBox popup opens above the widget (not enough space below),
 * GTK may try to align the active row to the button area, which can produce
 * visible whitespace above the first row. This forces the popup tree view to
 * scroll so the active row is aligned to the top, eliminating the whitespace.
 */
static gboolean combo_popup_fix_alignment_idle(gpointer user_data) {
    GtkComboBox* combo = GTK_COMBO_BOX(user_data);

    if (!GTK_IS_COMBO_BOX(combo)) {
        return G_SOURCE_REMOVE;
    }

    gboolean popup_shown = FALSE;
    g_object_get(G_OBJECT(combo), "popup-shown", &popup_shown, NULL);
    if (!popup_shown) {
        g_object_unref(combo);
        return G_SOURCE_REMOVE;
    }

    /* Find the actual popup window + tree view reliably via toplevels.
     * Note: GTK often wraps the combo model in filter/sort models for the popup,
     * so we compare against the unwrapped base model. */
    GtkTreeModel* combo_model = gtk_combo_box_get_model(combo);
    GtkTreeModel* combo_base_model = unwrap_combo_popup_model(combo_model);
    GtkWidget* popup_tree_view = NULL;
    GtkWidget* popup_window = NULL;

    GList* toplevels = gtk_window_list_toplevels();
    for (GList* l = toplevels; l; l = l->next) {
        GtkWidget* w = GTK_WIDGET(l->data);
        if (!GTK_IS_WINDOW(w) || !gtk_widget_get_visible(w)) {
            continue;
        }

        /* ComboBox popups usually use the COMBO type hint */
        GdkWindowTypeHint hint = gtk_window_get_type_hint(GTK_WINDOW(w));
        if (hint != GDK_WINDOW_TYPE_HINT_COMBO && hint != GDK_WINDOW_TYPE_HINT_POPUP_MENU) {
            continue;
        }

        GtkWidget* tree = find_descendant_tree_view(w);
        if (!tree || !GTK_IS_TREE_VIEW(tree)) {
            continue;
        }

        GtkTreeModel* tree_model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
        GtkTreeModel* tree_base_model = unwrap_combo_popup_model(tree_model);
        if (tree_base_model && tree_base_model == combo_base_model) {
            popup_tree_view = tree;
            popup_window = w;
            break;
        }
    }
    g_list_free(toplevels);

    /* Tag popup window for CSS separator styling */
    if (popup_window) {
        GtkStyleContext* sc = gtk_widget_get_style_context(popup_window);
        gtk_style_context_add_class(sc, "sb-combobox-popup");
    }

    if (popup_tree_view) {
        gint active = gtk_combo_box_get_active(combo);
        if (active >= 0) {
            /* Force active row to top: prevents GTK from inserting top whitespace when popup opens above */
            GtkTreePath* path = gtk_tree_path_new_from_indices(active, -1);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(popup_tree_view),
                                         path, NULL, TRUE, 0.0f, 0.0f);
            gtk_tree_path_free(path);
        } else if (GTK_IS_SCROLLABLE(popup_tree_view)) {
            GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(popup_tree_view));
            if (vadj) {
                gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
            }
        }
    }

    g_object_unref(combo);
    return G_SOURCE_REMOVE;
}

void ui_combo_popup_shown_fix(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;

    gboolean popup_shown = FALSE;
    g_object_get(obj, "popup-shown", &popup_shown, NULL);
    if (popup_shown) {
        /* Defer until popup widgets exist and GTK finishes its own scroll positioning.
         * Run twice: one low-priority idle + a tiny timeout. */
        g_idle_add_full(G_PRIORITY_LOW, combo_popup_fix_alignment_idle, g_object_ref(obj), NULL);
        g_timeout_add(1, combo_popup_fix_alignment_idle, g_object_ref(obj));
    }
}

/**
 * Create the main application UI
 */
AppContext* ui_create_main_window(Settings* initial_settings) {
    AppContext* ctx = (AppContext*)g_malloc(sizeof(AppContext));
    GtkBuilder* builder;
    GtkWidget* tools_panel;
    GtkWidget* workspace_widget;

    ctx->documents = NULL;
    ctx->layer_menu_new = NULL;
    ctx->layer_menu_delete = NULL;
    ctx->layer_menu_duplicate = NULL;
    ctx->layer_menu_merge_up = NULL;
    ctx->layer_menu_merge_down = NULL;
    ctx->layer_menu_rasterize_text = NULL;
    ctx->layer_menu_order_select_top = NULL;
    ctx->layer_menu_order_select_above = NULL;
    ctx->layer_menu_order_select_below = NULL;
    ctx->layer_menu_order_select_bottom = NULL;
    ctx->layer_menu_order_move_top = NULL;
    ctx->layer_menu_order_move_up = NULL;
    ctx->layer_menu_order_move_down = NULL;
    ctx->layer_menu_order_move_bottom = NULL;
    ctx->layer_menu_visibility_show_current = NULL;
    ctx->layer_menu_visibility_show_only = NULL;
    ctx->layer_menu_visibility_hide_only = NULL;
    ctx->layer_menu_visibility_show_all = NULL;
    ctx->layer_menu_visibility_hide_all = NULL;
    ctx->layer_menu_order = NULL;
    ctx->layer_menu_visibility = NULL;
    ctx->layer_panel_context_menu = NULL;
    ctx->layer_panel_context_visibility_show = NULL;
    ctx->layer_panel_context_visibility_show_only = NULL;
    ctx->layer_panel_context_visibility_hide_only = NULL;
    ctx->layer_panel_context_duplicate = NULL;
    ctx->layer_panel_context_delete = NULL;
    ctx->layer_panel_context_rasterize_text = NULL;
    ctx->layer_panel_context_merge_up = NULL;
    ctx->layer_panel_context_merge_down = NULL;
    ctx->layer_panel_context_merge_visible = NULL;
    ctx->layer_panel_context_flatten = NULL;
    ctx->edit_menu_undo = NULL;
    ctx->edit_menu_redo = NULL;
    ctx->edit_menu_copy = NULL;
    ctx->edit_menu_cut = NULL;
    ctx->edit_menu_paste = NULL;
    ctx->edit_menu_paste_new_image = NULL;
    ctx->edit_menu_copy_merged = NULL;
    ctx->edit_menu_cut_merged = NULL;
    ctx->edit_menu_clear = NULL;
    ctx->edit_menu_fill = NULL;
    ctx->image_menu_duplicate = NULL;
    ctx->image_menu_resize = NULL;
    ctx->image_menu_canvas_size = NULL;
    ctx->image_menu_fit_active_layer = NULL;
    ctx->image_menu_fit_all_layer = NULL;
    ctx->image_menu_crop_selection = NULL;
    ctx->image_menu_trim_borders = NULL;
    ctx->rotate_menu = NULL;
    ctx->rotate_menu_90_cw = NULL;
    ctx->rotate_menu_90_ccw = NULL;
    ctx->rotate_menu_180 = NULL;
    ctx->rotate_menu_arbitrary = NULL;
    ctx->image_menu_flip_horizontal = NULL;
    ctx->image_menu_flip_vertical = NULL;
    ctx->image_menu_transpose = NULL;
    ctx->image_menu_merge_visible = NULL;
    ctx->image_menu_flatten = NULL;
    ctx->file_menu_new = NULL;
    ctx->file_menu_open = NULL;
    ctx->file_menu_open_recent = NULL;
    ctx->file_menu_save = NULL;
    ctx->file_menu_save_as = NULL;
    ctx->file_menu_revert = NULL;
    ctx->export_menu_color_lookup = NULL;
    ctx->file_menu_close = NULL;
    ctx->file_menu_close_all = NULL;
    ctx->file_menu_exit = NULL;
    ctx->select_menu_all = NULL;
    ctx->select_menu_none = NULL;
    ctx->select_menu_invert = NULL;
    ctx->select_menu_grow = NULL;
    ctx->select_menu_shrink = NULL;
    ctx->select_menu_border = NULL;
    ctx->select_menu_feather = NULL;
    ctx->select_menu_sharpen = NULL;
    ctx->view_menu_zoom_fit = NULL;
    ctx->view_menu_zoom_reset = NULL;
    ctx->view_menu_zoom_in = NULL;
    ctx->view_menu_zoom_out = NULL;
    ctx->view_menu_zoom = NULL;
    ctx->view_menu_zoom_1600 = NULL;
    ctx->view_menu_zoom_800 = NULL;
    ctx->view_menu_zoom_400 = NULL;
    ctx->view_menu_zoom_200 = NULL;
    ctx->view_menu_zoom_100 = NULL;
    ctx->view_menu_zoom_50 = NULL;
    ctx->view_menu_zoom_25 = NULL;
    ctx->view_menu_zoom_12_5 = NULL;
    ctx->view_menu_zoom_6_25 = NULL;
    ctx->adjust_menu_item = NULL;
    ctx->effects_menu_item = NULL;
    ctx->workspace = NULL;           /* Initialize workspace early */
    ctx->layers_panel = NULL;        /* Will be set from workspace */
    ctx->active_gradient = NULL;
    ctx->active_gradient_set = NULL;
    ctx->settings = initial_settings;
    ctx->app_dir = NULL; /* Set in main.c after window creation */
    ctx->size_unit = g_strdup("px"); /* Default size unit is pixels */
    swatches_init(&ctx->swatches);   /* Initialize swatches data */

    /* Create and initialize tool manager */
    ctx->tool_registry = tool_manager_new();
    if (!tool_manager_init_defaults(ctx->tool_registry)) {
        debug_log("WRN", "Failed to initialize tool manager");
        g_free(ctx);
        return NULL;
    }

    /* Set AppContext reference on all tools */
    if (ctx->tool_registry) {
        for (int i = 0; i < TOOL_COUNT; i++) {
            Tool* tool = tool_manager_get(ctx->tool_registry, i);
            if (tool) {
                tool->app_context = (gpointer)ctx;
            }
        }
    }

    /* Suppress GTK Builder warnings about menus (these are harmless when using standalone menus) */
    static gboolean menu_warning_suppressed = FALSE;
    if (!menu_warning_suppressed) {
        g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING,
                          (GLogFunc)gtk_builder_menu_warning_handler, NULL);
        menu_warning_suppressed = TRUE;
    }

    /* Load main window from Glade (translation domain must be set before load for translatable strings) */
    {
        GError* glade_err = NULL;

        builder = gtk_builder_new();
        ui_utils_builder_set_translation_domain(builder);
        if (!gtk_builder_add_from_resource(builder, "/ui/main_window.glade", &glade_err)) {
            debug_log("WRN", "Failed to load main window from Glade: %s", glade_err ? glade_err->message : "unknown");
            g_clear_error(&glade_err);
            g_object_unref(builder);
            g_free(ctx);
            return NULL;
        }
    }

    /* Get main window */
    ctx->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!ctx->window) {
        debug_log("WRN", "Failed to get main_window from Glade");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }

    /* Store AppContext in window for global access */
    g_object_set_data(G_OBJECT(ctx->window), "app_context", ctx);

    /* Set application icon */
    {
        GError* error = NULL;
        GBytes* icon_bytes = g_resources_lookup_data("/app-icon", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
        if (icon_bytes) {
            GInputStream* icon_stream = g_memory_input_stream_new_from_data(
                g_bytes_get_data(icon_bytes, NULL),
                g_bytes_get_size(icon_bytes),
                NULL);
            GdkPixbuf* icon_pixbuf = gdk_pixbuf_new_from_stream(icon_stream, NULL, &error);
            g_object_unref(icon_stream);
            g_bytes_unref(icon_bytes);

            if (icon_pixbuf) {
                gtk_window_set_icon(GTK_WINDOW(ctx->window), icon_pixbuf);
                g_object_unref(icon_pixbuf);
            } else if (error) {
                debug_log("WRN", "Failed to create pixbuf from app-icon: %s", error->message);
                g_error_free(error);
            }
        } else if (error) {
            debug_log("WRN", "Failed to load app-icon resource: %s", error->message);
            g_error_free(error);
        }
    }

    /* Get main containers */
    GtkWidget* main_vbox = GTK_WIDGET(gtk_builder_get_object(builder, "main_vbox"));
    GtkWidget* tool_options_container = GTK_WIDGET(gtk_builder_get_object(builder, "tool_options_container"));
    GtkWidget* main_hbox = GTK_WIDGET(gtk_builder_get_object(builder, "main_hbox"));
    ctx->notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
    GtkWidget* right_panel_container = GTK_WIDGET(gtk_builder_get_object(builder, "right_panel_container"));
    ctx->status_bar = GTK_WIDGET(gtk_builder_get_object(builder, "status_bar"));

    if (!main_vbox || !tool_options_container || !main_hbox ||
        !ctx->notebook || !right_panel_container || !ctx->status_bar) {
        debug_log("WRN", "Failed to get required widgets from Glade");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }

    /* Store builder reference in window for later use (e.g., status bar labels) */
    g_object_ref(builder);
    g_object_set_data_full(G_OBJECT(ctx->window), "main_builder", builder, g_object_unref);

    /* Connect notebook signal */
    g_signal_connect(ctx->notebook, "switch-page",
                     G_CALLBACK(on_notebook_switch_page), ctx);

    ui_file_menu_setup_notebook_drag_drop(ctx->notebook, ctx);

    /* Get menu bar and menu items from Glade */
    ctx->menu_bar = GTK_WIDGET(gtk_builder_get_object(builder, "menu_bar"));

    /* Create accelerator group for keyboard shortcuts */
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(ctx->window), accel_group);

    /* Setup each menu separately */
    ui_file_menu_setup(builder, ctx, accel_group);
    ui_edit_menu_setup(builder, ctx, accel_group);
    ui_view_menu_setup(builder, ctx, accel_group);
    ui_image_menu_setup(builder, ctx);
    ui_layer_menu_setup(builder, ctx);
    ui_select_menu_setup(builder, ctx, accel_group);
    setup_adjust_menu(builder, ctx);
    setup_effects_menu(builder, ctx);
    ui_tools_menu_setup(builder, ctx);
    ui_help_menu_setup(builder, ctx);

    /* ==== TOP PANEL: Tool Options ==== */
    ctx->tool_options_panel = create_tool_options_panel();
    if (!ctx->tool_options_panel || !ctx->tool_options_panel->panel) {
        debug_log("WRN", "Failed to create tool options panel");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    /* Set tool registry in panel for cursor updates */
    tool_options_panel_set_tool_registry(ctx->tool_options_panel, ctx->tool_registry);
    /* Store panel reference in panel widget */
    g_object_set_data(G_OBJECT(ctx->tool_options_panel->panel), "tool_options_panel", ctx->tool_options_panel);
    gtk_container_add(GTK_CONTAINER(tool_options_container), ctx->tool_options_panel->panel);

    /* ==== LEFT PANEL: Tools (fixed width) ==== */
    /* Tool panel is now loaded from main window Glade file */
    tools_panel = tools_panel_initialize_from_builder(builder, ctx->tool_registry);

    /* Connect tools panel to tool options panel for title updates */
    tools_panel_set_options_panel(ctx->tool_options_panel);

    /* Set main window reference for color chooser dialogs */
    tools_panel_set_main_window(GTK_WINDOW(ctx->window));

    /* Connect window key press handler for tool hotkeys
     * This should intercept keys before GTK's default mnemonic handling */
    if (ctx->window) {
        g_signal_connect(ctx->window, "key-press-event",
                         G_CALLBACK(tools_panel_on_window_key_press), ctx);
    }

    /* ==== RIGHT PANEL: Workspace ==== */
    ctx->workspace = workspace_create(ctx);
    if (!ctx->workspace) {
        debug_log("WRN", "Failed to create workspace");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    workspace_widget = workspace_get_panel(ctx->workspace);
    if (!workspace_widget) {
        debug_log("WRN", "Failed to get workspace panel");
        workspace_free(ctx->workspace);
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    gtk_container_add(GTK_CONTAINER(right_panel_container), workspace_widget);

    /* Get layers panel from workspace for compatibility */
    ctx->layers_panel = workspace_get_layers_panel(ctx->workspace);

    /* Store layers panel reference for later updates (for backward compatibility) */
    g_object_set_data(G_OBJECT(ctx->window), "layers_panel", ctx->layers_panel);
    g_object_set_data(G_OBJECT(ctx->window), "workspace", ctx->workspace);

    /* Connect layers panel buttons to callbacks */
    if (ctx->layers_panel) {
        layers_panel_connect_buttons(ctx->layers_panel,
                                     G_CALLBACK(on_layer_new),
                                     G_CALLBACK(on_layer_delete),
                                     G_CALLBACK(on_layer_duplicate),
                                     ctx);

        /* Connect move layer buttons */
        if (ctx->layers_panel->btn_up) {
            g_signal_connect(ctx->layers_panel->btn_up, "clicked",
                             G_CALLBACK(on_layer_move_up), ctx);
        }
        if (ctx->layers_panel->btn_down) {
            g_signal_connect(ctx->layers_panel->btn_down, "clicked",
                             G_CALLBACK(on_layer_move_down), ctx);
        }

        /* Connect layer tree view selection changes to update UI state */
        GtkTreeSelection* layer_selection = gtk_tree_view_get_selection(
            GTK_TREE_VIEW(ctx->layers_panel->tree_view));
        g_signal_connect(layer_selection, "changed",
                         G_CALLBACK(on_layer_selection_changed), ctx);
    }

    /* Setup status bar zoom controls */
    GtkWidget* sb_zoom_out_button = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_out_button"));
    GtkWidget* sb_zoom_in_button = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_in_button"));
    GtkWidget* sb_zoom_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_combobox"));
    GtkWidget* sb_size_unit_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "sb_size_unit_combobox"));

    if (sb_zoom_out_button) {
        g_signal_connect(sb_zoom_out_button, "clicked",
                         G_CALLBACK(on_statusbar_zoom_out), ctx);
    }
    if (sb_zoom_in_button) {
        g_signal_connect(sb_zoom_in_button, "clicked",
                         G_CALLBACK(on_statusbar_zoom_in), ctx);
    }
    if (sb_zoom_combobox) {
        ui_apply_list_combobox_style(sb_zoom_combobox);

        /* Keep popup width stable  */
        gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(sb_zoom_combobox), TRUE);
        /* Fix popup whitespace when opening above */
        g_signal_connect(sb_zoom_combobox, "notify::popup-shown",
                         G_CALLBACK(ui_combo_popup_shown_fix), NULL);

        /* Render “Separator” rows ourselves (darker + clearer than theme separators) */
        {
            GtkCellLayout* layout = GTK_CELL_LAYOUT(sb_zoom_combobox);
            GList* cells = gtk_cell_layout_get_cells(layout);
            if (cells && cells->data) {
                gtk_cell_layout_set_cell_data_func(layout,
                                                   GTK_CELL_RENDERER(cells->data),
                                                   zoom_combo_cell_data_func,
                                                   NULL, NULL);
            }
            if (cells) {
                g_list_free(cells);
            }
        }

        /* Connect change signal */
        g_signal_connect(sb_zoom_combobox, "changed",
                         G_CALLBACK(on_statusbar_zoom_changed), ctx);
    }
    if (sb_size_unit_combobox) {
        ui_apply_list_combobox_style(sb_size_unit_combobox);

        /* Keep popup width stable */
        gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(sb_size_unit_combobox), TRUE);
        /* Fix popup whitespace when opening above (limited space below) */
        g_signal_connect(sb_size_unit_combobox, "notify::popup-shown",
                         G_CALLBACK(ui_combo_popup_shown_fix), NULL);

        /* Connect change signal */
        g_signal_connect(sb_size_unit_combobox, "changed",
                         G_CALLBACK(on_statusbar_size_unit_changed), ctx);
    }

    /* Connect window signals */
    g_signal_connect(ctx->window, "delete-event",
                     G_CALLBACK(on_window_delete), ctx);

    gtk_widget_show_all(ctx->window);

    /* Update status bar with initial information */
    ui_update_status_bar(ctx, NULL);

    /* Initialize menu and button states */
    ui_update_menu_and_button_states(ctx);

    /* Initialize recent files menu (must be before g_object_unref(builder)) */
    ui_update_recent_files_menu(ctx);

    /* Clean up builder - widgets are now owned by their containers */
    g_object_unref(builder);

    /* Check for recovery files and show dialog if found */
    recovery_dialog_show(ctx);

    return ctx;
}

/**
 * Create a document without adding it to the notebook
 * This allows loading the image first, then adding to notebook only if successful
 */
ImageDocument* ui_create_document_without_tab(AppContext* ctx, const gchar* filename) {
    ImageDocument* doc;

    /* Get undo levels and worker threads from settings */
    guint undo_levels = 10;   /* Default */
    guint worker_threads = 4; /* Default */
    if (ctx && ctx->settings) {
        undo_levels = (guint)settings_get_undo_levels(ctx->settings);
        worker_threads = (guint)settings_get_worker_threads(ctx->settings);
    }

    /* Create the document with worker pool for on-screen rendering */
    doc = document_new(filename, TRUE, undo_levels);

    /* Set worker thread count on tile worker pool if available */
    if (doc && doc->tile_worker_pool) {
        /* Recreate worker pool with configured thread count */
        tile_worker_pool_destroy(doc->tile_worker_pool);
        doc->tile_worker_pool = tile_worker_pool_create(worker_threads);
        if (!doc->tile_worker_pool) {
            debug_log("WRN", "Failed to create tile worker pool with %u threads", worker_threads);
        }
    }

    /* Create disk-backed undo journal if settings are available */
    if (ctx && ctx->settings && doc) {
        gint compression_level = settings_get_undo_compression_level(ctx->settings);
        const gchar* temp_dir = settings_get_undo_temp_directory(ctx->settings);
        doc->undo_journal = undo_journal_create((struct ImageDocument*)doc, temp_dir, compression_level);
        if (!doc->undo_journal) {
            debug_log("WRN", "Failed to create undo journal, falling back to in-memory undo");
        }
        /* Set max_entries on journal to match undo_levels */
        if (doc->undo_journal && undo_levels > 0) {
            doc->undo_journal->max_entries = undo_levels;
        }
    }

    /* Create the drawing area (scrolled window with drawing area) */
    document_create_drawing_area(doc);

    /* Store AppContext and tool registry in drawing area for tool event handlers */
    if (doc && doc->drawing_area) {
        g_object_set_data(G_OBJECT(doc->drawing_area), "app_context", ctx);
        if (ctx->tool_registry) {
            g_object_set_data(G_OBJECT(doc->drawing_area), "tool_registry", ctx->tool_registry);
        }
        /* Store layers panel reference for thumbnail updates */
        if (ctx->layers_panel) {
            g_object_set_data(G_OBJECT(doc->drawing_area), "layers_panel", ctx->layers_panel);
        }
    }

    /* Update viewport background color with current canvas background color using CSS */
    if (doc && doc->viewport) {
        gdouble r_val, g_val, b_val;
        ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
        guint r = (guint)(r_val * 255.0);
        guint g = (guint)(g_val * 255.0);
        guint b = (guint)(b_val * 255.0);

        /* Get or create CSS provider for this viewport */
        GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(G_OBJECT(doc->viewport), "canvas_bg_provider");
        if (!provider) {
            provider = gtk_css_provider_new();
            g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider", provider, g_object_unref);
            GtkStyleContext* style_context = gtk_widget_get_style_context(doc->viewport);
            gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        gchar* css = g_strdup_printf("#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        g_free(css);
        gtk_widget_queue_draw(doc->viewport);
    }

    if (doc) {
        ui_file_menu_setup_viewport_drag_drop(doc, ctx);
    }

    return doc;
}

/**
 * Add an existing document to the notebook tab
 */
void ui_add_document_to_notebook(AppContext* ctx, ImageDocument* doc) {
    GtkWidget* page_content;
    GtkWidget* tab_hbox;
    GtkWidget* tab_label;
    GtkWidget* close_button;
    gint page_num;

    if (!ctx || !doc || !doc->scrolled_window) {
        return;
    }

    /* Page content is the grid (rulers + scrolled window) when present, else the scrolled window */
    page_content = doc->canvas_container ? doc->canvas_container : doc->scrolled_window;

    /* Create tab label with close button */
    tab_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(tab_hbox), 0);

    tab_label = gtk_label_new(doc->filename ? doc->filename : _("Untitled"));
    gtk_box_pack_start(GTK_BOX(tab_hbox), tab_label, FALSE, FALSE, 0);

    close_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close_button, FALSE);
    gtk_widget_set_size_request(close_button, 20, 20);
    GtkWidget* close_image = gtk_image_new_from_icon_name("window-close-symbolic",
                                                          GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(close_button), close_image);
    g_signal_connect(close_button, "clicked",
                     G_CALLBACK(on_tab_close_button_clicked), doc);
    gtk_box_pack_start(GTK_BOX(tab_hbox), close_button, FALSE, FALSE, 0);

    gtk_widget_show_all(tab_hbox);
    gtk_widget_show_all(page_content);

    /* Add document to list BEFORE adding page to notebook
     * This ensures that when switch-page signal fires, the document is already in the list */
    ctx->documents = g_list_append(ctx->documents, doc);

    /* Add page to notebook */
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook),
                                        page_content, tab_hbox);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    /* Sync ruler unit from statusbar so new document's rulers show current unit */
    if (ctx->size_unit) {
        document_set_ruler_unit(doc, ruler_unit_from_string(ctx->size_unit));
    }

    /* Apply show rulers setting to new document */
    if (doc->ruler_h && doc->ruler_v && ctx->settings) {
        gboolean show_rulers = settings_get_show_rulers(ctx->settings);
        if (show_rulers) {
            gtk_widget_show(doc->ruler_h);
            gtk_widget_show(doc->ruler_v);
        } else {
            gtk_widget_hide(doc->ruler_h);
            gtk_widget_hide(doc->ruler_v);
        }
    }

    /* Store label in close button's data for later reference */
    g_object_set_data(G_OBJECT(close_button), "tab_label", tab_label);
    g_object_set_data(G_OBJECT(close_button), "app_context", ctx);
}

/**
 * Create and attach a new document tab
 * This is a convenience function that creates document and adds it to notebook
 */
ImageDocument* ui_create_document_tab(AppContext* ctx, const gchar* filename) {
    ImageDocument* doc = ui_create_document_without_tab(ctx, filename);
    if (doc) {
        ui_add_document_to_notebook(ctx, doc);

        /* Set the current document in tool registry so tools can access it */
        if (ctx->tool_registry) {
            ctx->tool_registry->current_doc = doc;
        }

        /* Register document for autosave */
        autosave_register_document(doc);
    }

    return doc;
}

/**
 * Close a document tab
 */
/**
 * Internal: Actually close the document without prompting
 */
static void ui_close_document_tab_internal(AppContext* ctx, ImageDocument* doc) {
    gint page_num;
    GtkWidget* scrolled_window;

    if (!doc || !ctx) {
        return;
    }

    /* Store page widget (grid or scrolled window) before we modify anything */
    scrolled_window = doc->canvas_container ? doc->canvas_container : doc->scrolled_window;

    /* Find the page containing this document */
    if (!scrolled_window) {
        /* Document already has no page widget, just free it */
        ctx->documents = g_list_remove(ctx->documents, doc);
        document_free(doc);
        return;
    }

    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), scrolled_window);

    if (page_num >= 0) {
        /* Disconnect all signals from drawing area to prevent callbacks
         * from accessing the document after it's freed */
        if (doc->drawing_area) {
            g_signal_handlers_disconnect_by_data(doc->drawing_area, doc);
        }

        /* NULL out widget pointers BEFORE removing page to prevent
         * access to destroyed widgets during cleanup and signal handlers */
        doc->canvas_container = NULL;
        doc->ruler_h = NULL;
        doc->ruler_v = NULL;
        doc->scrolled_window = NULL;
        doc->drawing_area = NULL;

        /* Remove from document list BEFORE removing page to prevent
         * switch-page callback from accessing the document */
        ctx->documents = g_list_remove(ctx->documents, doc);

        /* Unregister from autosave before freeing */
        autosave_unregister_document(doc);

        /* Remove the notebook page (this destroys the scrolled window
         * and may trigger switch-page signal, but doc is already removed) */
        gtk_notebook_remove_page(GTK_NOTEBOOK(ctx->notebook), page_num);

        /* Free the document */
        document_free(doc);
        doc = NULL; /* Ensure doc pointer is NULL after freeing */

        /* Update window title (handles empty notebook) */
        ui_update_window_title(ctx, NULL);

        /* Update status bar and menu/button states */
        ui_update_status_bar(ctx, NULL);

        /* Update layers panel - show active document if one exists, otherwise clear */
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                    "layers_panel");
        Workspace* workspace = (Workspace*)g_object_get_data(G_OBJECT(ctx->window), "workspace");
        if (layers_panel) {
            ImageDocument* active_doc = ui_get_active_document(ctx);
            layers_panel_update(layers_panel, active_doc);
        }
        if (workspace) {
            workspace_update_overview(workspace);
        }

        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Public: Close document with unsaved changes prompt
 */
void ui_close_document_tab(AppContext* ctx, ImageDocument* doc) {
    if (!doc || !ctx) {
        return;
    }

    /* Check if document has unsaved changes */
    if (document_is_dirty(doc)) {
        gint response;
        gchar* primary_text;
        const gchar* filename = document_get_filename(doc);

        primary_text = g_strdup_printf(_("Save changes to \"%s\" before closing?"),
                                       filename ? filename : _("Untitled"));

        response = ui_utils_message_dialog_run(
            GTK_WINDOW(ctx->window),
            GTK_MESSAGE_WARNING,
            primary_text,
            _("If you don't save, changes will be lost."),
            GTK_RESPONSE_ACCEPT,
            _("_Discard"), GTK_RESPONSE_REJECT,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Save"), GTK_RESPONSE_ACCEPT,
            NULL);

        g_free(primary_text);

        switch (response) {
            case GTK_RESPONSE_ACCEPT:
                ui_save_document_as(ctx);
                /* Note: We don't actually close here - let user complete save */
                /* In a full implementation, we'd detect when save completes */
                break;

            case GTK_RESPONSE_REJECT:
                ui_close_document_tab_internal(ctx, doc);
                break;

            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
                break;

            default:
                break;
        }
    } else {
        /* No unsaved changes, close directly */
        ui_close_document_tab_internal(ctx, doc);
    }
}

/**
 * Get the current active document
 */
ImageDocument* ui_get_active_document(AppContext* ctx) {
    gint page_num;
    GtkWidget* page;

    if (!ctx) {
        return NULL;
    }

    page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(ctx->notebook));

    if (page_num < 0) {
        return NULL;
    }

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    if (!page) {
        return NULL;
    }

    /* Find the document by scrolled window */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument* doc = (ImageDocument*)iter->data;
        if (doc && (doc->canvas_container ? doc->canvas_container : doc->scrolled_window) == page) {
            return doc;
        }
    }

    return NULL;
}

/**
 * Update the tab label for a document
 */
void ui_update_document_tab_label(AppContext* ctx, ImageDocument* doc) {
    GtkWidget* tab_label = NULL;
    GtkWidget* tab_hbox;
    gint page_num;

    if (!ctx || !doc || !doc->scrolled_window) {
        return;
    }

    /* Find the page number for this document */
    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook),
                                     doc->canvas_container ? doc->canvas_container : doc->scrolled_window);
    if (page_num < 0) {
        return;
    }

    /* Get the tab label widget (tab_hbox) for this page */
    tab_hbox = gtk_notebook_get_tab_label(GTK_NOTEBOOK(ctx->notebook),
                                          doc->canvas_container ? doc->canvas_container : doc->scrolled_window);
    if (!tab_hbox || !GTK_IS_CONTAINER(tab_hbox)) {
        return;
    }

    /* Find the close button (second child) to get the stored tab_label */
    GList* children = gtk_container_get_children(GTK_CONTAINER(tab_hbox));
    if (children) {
        if (children->next) {
            GtkWidget* close_button = GTK_WIDGET(children->next->data);
            tab_label = (GtkWidget*)g_object_get_data(G_OBJECT(close_button), "tab_label");
        }

        if (!tab_label && children) {
            tab_label = GTK_WIDGET(children->data);
        }

        if (tab_label && GTK_IS_LABEL(tab_label)) {
            /* Update the label text */
            const gchar* filename = doc->filename ? doc->filename : _("Untitled");
            gtk_label_set_text(GTK_LABEL(tab_label), filename);
        }

        g_list_free(children);
    }
}

/**
 * Update the window title based on active document
 */
void ui_update_window_title(AppContext* ctx, ImageDocument* doc) {
    ImageDocument* active_doc;
    gchar* title;

    if (!ctx) {
        return;
    }

    /* Use provided document, or query for active document if not provided */
    active_doc = doc ? doc : ui_get_active_document(ctx);

    if (active_doc) {
        const gchar* filename = document_get_filename(active_doc);
        const gchar* modified = active_doc->modified ? "*" : "";
        title = g_strdup_printf(_("RasterLab - %s%s"),
                                filename ? filename : _("Untitled"), modified);
    } else {
        title = g_strdup(_("RasterLab"));
    }

    gtk_window_set_title(GTK_WINDOW(ctx->window), title);
    g_free(title);
}

/**
 * Free the application context
 */
void ui_context_free(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    /* Free all documents */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        document_free((ImageDocument*)iter->data);
    }

    g_list_free(ctx->documents);

    /* Free tool manager */
    if (ctx->tool_registry) {
        tool_manager_free(ctx->tool_registry);
    }

    /* Free settings (will be saved in main.c before this is called) */
    if (ctx->settings) {
        settings_free(ctx->settings);
    }

    /* Free active gradient set (gradient tool selection) */
    if (ctx->active_gradient_set) {
        gradient_set_free((GradientSet*)ctx->active_gradient_set);
        ctx->active_gradient_set = NULL;
        ctx->active_gradient = NULL;
    }

    /* Free swatches data */
    swatches_free(&ctx->swatches);

    /* Free workspace (this will free layers panel, accordion, etc.) */
    if (ctx->workspace) {
        workspace_free(ctx->workspace);
    }

    /* Free app directory */
    if (ctx->app_dir) {
        g_free(ctx->app_dir);
    }

    /* Free size unit */
    if (ctx->size_unit) {
        g_free(ctx->size_unit);
    }

    g_free(ctx);
}

/**
 * Setup Adjustments menu from Glade builder
 */
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx) {
    ui_filter_adjust_setup_menu(builder, ctx);
}

/**
 * Setup Effects menu from Glade builder
 */
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx) {
    ui_filter_effects_setup_menu(builder, ctx);
}

/**
 * Helper function to extract pixels from a layer for copying
 * Returns a cairo surface with the pixels to copy (selection or entire layer)
 */
static cairo_surface_t* extract_pixels_for_copy(ImageDocument* doc, ImageLayer* layer) {
    if (!doc || !layer || !layer->surface) {
        return NULL;
    }

    gint width = layer->width;
    gint height = layer->height;

    /* Check if there's a selection */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (!has_selection) {
        /* No selection: copy entire layer */
        cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (!copy) {
            return NULL;
        }

        cairo_t* cr = cairo_create(copy);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);

        return copy;
    }

    /* Has selection: extract selected pixels */
    /* Calculate intersection of layer bounds and selection bounds in document coordinates */
    gint layer_x_min = layer->offset_x;
    gint layer_y_min = layer->offset_y;
    gint layer_x_max = layer->offset_x + layer->width;
    gint layer_y_max = layer->offset_y + layer->height;

    /* Clamp to document bounds */
    layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
    layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
    layer_x_max = (layer_x_max > (gint)doc->width) ? (gint)doc->width : layer_x_max;
    layer_y_max = (layer_y_max > (gint)doc->height) ? (gint)doc->height : layer_y_max;

    if (layer_x_max <= layer_x_min || layer_y_max <= layer_y_min) {
        return NULL; /* No intersection */
    }

    /* Create dirty rect for the layer region in document coordinates */
    DirtyRect layer_rect;
    dirty_rect_set(&layer_rect, layer_x_min, layer_y_min,
                   layer_x_max - layer_x_min, layer_y_max - layer_y_min);

    /* Get selection mask for this region */
    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data || dirty_rect_is_empty(&actual_region)) {
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels within the region */
    gint sel_x_min = actual_region.x + actual_region.width;
    gint sel_y_min = actual_region.y + actual_region.height;
    gint sel_x_max = actual_region.x;
    gint sel_y_max = actual_region.y;

    /* Scan mask to find actual bounds */
    for (gint y = 0; y < region_mask->height; y++) {
        for (gint x = 0; x < region_mask->width; x++) {
            uint8_t mask_alpha = region_mask->data[y * region_mask->stride + x];
            if (mask_alpha > 0) {
                gint doc_x = actual_region.x + x;
                gint doc_y = actual_region.y + y;
                if (doc_x < sel_x_min)
                    sel_x_min = doc_x;
                if (doc_y < sel_y_min)
                    sel_y_min = doc_y;
                if (doc_x > sel_x_max)
                    sel_x_max = doc_x;
                if (doc_y > sel_y_max)
                    sel_y_max = doc_y;
            }
        }
    }

    if (sel_x_max < sel_x_min || sel_y_max < sel_y_min) {
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create surface with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!copy) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy pixels from source layer to copy, masked by selection */
    cairo_surface_flush(layer->surface);
    cairo_surface_flush(copy);

    guchar* src_data = cairo_image_surface_get_data(layer->surface);
    gint src_stride = cairo_image_surface_get_stride(layer->surface);
    guchar* dst_data = cairo_image_surface_get_data(copy);
    gint dst_stride = cairo_image_surface_get_stride(copy);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint src_y = doc_y - layer->offset_y;
        gint mask_y = doc_y - actual_region.y;

        if (src_y < 0 || src_y >= (gint)layer->height) {
            continue; /* Outside source layer */
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue; /* Outside mask */
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint src_x = doc_x - layer->offset_x;
            gint mask_x = doc_x - actual_region.x;

            if (src_x < 0 || src_x >= (gint)layer->width) {
                continue; /* Outside source layer */
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue; /* Outside mask */
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from source to destination */
            guchar* src_pixel = src_data + src_y * src_stride + src_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
                dst_pixel[3] = src_pixel[3];
            } else {
                /* Partially selected (feathered): apply mask to alpha */
                uint8_t src_a = src_pixel[3];
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply, then re-premultiply with new alpha */
                    uint16_t r = (src_pixel[2] * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_pixel[1] * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_pixel[0] * 255 + src_a / 2) / src_a;

                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    dst_pixel[0] = (b * new_alpha + 127) / 255;
                    dst_pixel[1] = (g * new_alpha + 127) / 255;
                    dst_pixel[2] = (r * new_alpha + 127) / 255;
                    dst_pixel[3] = new_alpha;
                } else {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(copy);
    selection_mask_free(region_mask);

    return copy;
}

/**
 * Helper function to create a merged surface from all visible layers
 * Returns a cairo surface with all visible layers composited, or NULL on error
 */
static cairo_surface_t* create_merged_surface(ImageDocument* doc) {
    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Create surface for merged result */
    cairo_surface_t* merged = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    if (!merged) {
        return NULL;
    }

    /* Composite all visible layers */
    cairo_t* cr = cairo_create(merged);
    if (!cr) {
        cairo_surface_destroy(merged);
        return NULL;
    }

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Composite each visible layer */
    gboolean is_first_layer = TRUE;
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;

        if (!layer || !layer->surface) {
            continue;
        }

        /* Skip invisible layers */
        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw layer with offset */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op;
        if (is_first_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(merged);

    return merged;
}

/**
 * Helper function to extract pixels from merged layers for copying
 * Returns a cairo surface with the pixels to copy (selection or entire merged image)
 */
static cairo_surface_t* extract_merged_pixels_for_copy(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    /* Create merged surface from all visible layers */
    cairo_surface_t* merged = create_merged_surface(doc);
    if (!merged) {
        return NULL;
    }

    gint width = doc->width;
    gint height = doc->height;

    /* Check if there's a selection */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (!has_selection) {
        /* No selection: return merged surface as-is */
        return merged;
    }

    /* Has selection: extract selected pixels from merged surface */
    DirtyRect doc_rect;
    dirty_rect_set(&doc_rect, 0, 0, width, height);

    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &doc_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data || dirty_rect_is_empty(&actual_region)) {
        cairo_surface_destroy(merged);
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels */
    gint sel_x_min = actual_region.x + actual_region.width;
    gint sel_y_min = actual_region.y + actual_region.height;
    gint sel_x_max = actual_region.x;
    gint sel_y_max = actual_region.y;

    /* Scan mask to find actual bounds */
    for (gint y = 0; y < region_mask->height; y++) {
        for (gint x = 0; x < region_mask->width; x++) {
            uint8_t mask_alpha = region_mask->data[y * region_mask->stride + x];
            if (mask_alpha > 0) {
                gint doc_x = actual_region.x + x;
                gint doc_y = actual_region.y + y;
                if (doc_x < sel_x_min)
                    sel_x_min = doc_x;
                if (doc_y < sel_y_min)
                    sel_y_min = doc_y;
                if (doc_x > sel_x_max)
                    sel_x_max = doc_x;
                if (doc_y > sel_y_max)
                    sel_y_max = doc_y;
            }
        }
    }

    if (sel_x_max < sel_x_min || sel_y_max < sel_y_min) {
        cairo_surface_destroy(merged);
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create surface with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!copy) {
        cairo_surface_destroy(merged);
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy pixels from merged surface to copy, masked by selection */
    cairo_surface_flush(merged);
    cairo_surface_flush(copy);

    guchar* src_data = cairo_image_surface_get_data(merged);
    gint src_stride = cairo_image_surface_get_stride(merged);
    guchar* dst_data = cairo_image_surface_get_data(copy);
    gint dst_stride = cairo_image_surface_get_stride(copy);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint mask_y = doc_y - actual_region.y;

        if (doc_y < 0 || doc_y >= height) {
            continue;
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue;
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint mask_x = doc_x - actual_region.x;

            if (doc_x < 0 || doc_x >= width) {
                continue;
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue;
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from merged to destination */
            guchar* src_pixel = src_data + doc_y * src_stride + doc_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
                dst_pixel[3] = src_pixel[3];
            } else {
                /* Partially selected (feathered): apply mask to alpha */
                uint8_t src_a = src_pixel[3];
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply, then re-premultiply with new alpha */
                    uint16_t r = (src_pixel[2] * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_pixel[1] * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_pixel[0] * 255 + src_a / 2) / src_a;

                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    dst_pixel[0] = (b * new_alpha + 127) / 255;
                    dst_pixel[1] = (g * new_alpha + 127) / 255;
                    dst_pixel[2] = (r * new_alpha + 127) / 255;
                    dst_pixel[3] = new_alpha;
                } else {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(copy);
    cairo_surface_destroy(merged);
    selection_mask_free(region_mask);

    return copy;
}

/**
 * Notebook page switch callback
 */
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page,
                                    guint page_num, gpointer user_data) {
    (void)notebook; /* Unused */
    (void)page_num; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = NULL;
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");

    /* Find the document that matches the page widget (canvas container or scrolled window) */
    if (page) {
        for (GList* iter = ctx->documents; iter; iter = iter->next) {
            ImageDocument* d = (ImageDocument*)iter->data;
            if (d && (d->canvas_container ? d->canvas_container : d->scrolled_window) == page) {
                doc = d;
                break;
            }
        }
    }

    /* Fallback to ui_get_active_document if page matching fails */
    if (!doc) {
        doc = ui_get_active_document(ctx);
    }

    /* Reset any active selection-tool preview before switching documents.
     *
     * This fires in two scenarios:
     *   1. The user manually clicks a different document tab.
     *   2. A document is closed: gtk_notebook_remove_page() triggers this
     *      callback while the old document is still valid (document_free()
     *      is called *after* page removal), so the reset is safe.
     *
     * Without this, the tool state (is_editing, preview_cache, …) from the
     * previous document would be rendered on whatever document becomes active
     * next — the "ghost selection" bug. */
    if (ctx->tool_registry) {
        Tool* active_tool = tool_manager_get_active(ctx->tool_registry);
        if (active_tool) {
            if (active_tool->type == TOOL_RECT_SELECT) {
                tool_rect_select_reset(active_tool);
            } else if (active_tool->type == TOOL_ELLIPSE_SELECT) {
                tool_ellipse_select_reset(active_tool);
            } else if (active_tool->type == TOOL_POLYGON_SELECT) {
                tool_polygon_select_reset(active_tool);
            }
        }
    }

    /* Set the current document in the tool registry so tools can access it */
    if (ctx->tool_registry) {
        ctx->tool_registry->current_doc = doc;
    }

    /* Sync ruler unit from statusbar so rulers on this tab show the current unit */
    if (doc && ctx->size_unit) {
        document_set_ruler_unit(doc, ruler_unit_from_string(ctx->size_unit));
    }

    ui_update_window_title(ctx, doc);
    ui_update_status_bar(ctx, doc);

    /* Update layers panel with current document's layers */
    Workspace* workspace = (Workspace*)g_object_get_data(G_OBJECT(ctx->window), "workspace");
    if (layers_panel && doc) {
        layers_panel_update(layers_panel, doc);
    }
    if (workspace) {
        workspace_update_overview(workspace);
    }

    /* Update menu and button sensitivity */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Tab close button clicked callback
 */
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(button),
                                                     "app_context");

    if (ctx && doc) {
        ui_close_document_tab(ctx, doc);
    } else {
        printf("  ERROR: ctx=%p or doc=%p is NULL\n", ctx, doc);
    }
}

/**
 * Helper function to flip a layer using Ocular library
 */
static gboolean flip_layer(ImageLayer* layer, OcDirection direction) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    if (width == 0 || height == 0) {
        return FALSE;
    }

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        debug_log("WRN", "Flip layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        debug_log("WRN", "Flip layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply flip using Ocular library (4 channels for RGBA) */
    status = ocularFlipImage(rgba_input, rgba_output, width, height, 4, direction);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Flip layer: Ocular flip returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        debug_log("WRN", "Flip layer: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper function to transpose a layer using Ocular library
 */
static gboolean transpose_layer(ImageLayer* layer) {
    cairo_surface_t* old_surface;
    cairo_surface_t* new_surface;
    gint old_width, old_height;
    gint new_width, new_height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    old_surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(old_surface, &old_width, &old_height)) {
        return FALSE;
    }

    if (old_width == 0 || old_height == 0) {
        return FALSE;
    }

    /* Transpose swaps width and height */
    new_width = old_height;
    new_height = old_width;

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(old_width * old_height * 4);
    rgba_output = (guchar*)g_malloc(new_width * new_height * 4);

    if (!rgba_input || !rgba_output) {
        debug_log("WRN", "Transpose layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(old_surface, rgba_input)) {
        debug_log("WRN", "Transpose layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply transpose using Ocular library
       Stride is width * 4 for RGBA format */
    status = ocularTransposeImage(rgba_input, rgba_output, old_width, old_height, old_width * 4);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Transpose layer: Ocular transpose returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Create new surface with swapped dimensions */
    new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!new_surface) {
        debug_log("WRN", "Transpose layer: Failed to create new surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert RGBA output to new Cairo surface */
    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        debug_log("WRN", "Transpose layer: Failed to convert RGBA to new surface");
        cairo_surface_destroy(new_surface);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Replace old surface with new one */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;

    /* Update layer dimensions */
    layer->width = new_width;
    layer->height = new_height;

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Convert dimension from pixels to the specified unit
 * @param pixels Dimension in pixels
 * @param unit Unit string (%, px, in, cm, mm, pt, pc)
 * @param zoom_factor Current zoom factor (for % calculation)
 * @param dpi Resolution in DPI (default: 96)
 * @return Converted dimension value
 */
static gdouble convert_dimension(guint pixels, const gchar* unit, gdouble zoom_factor, gdouble dpi) {
    if (!unit) {
        return (gdouble)pixels;
    }

    if (g_strcmp0(unit, "px") == 0) {
        return (gdouble)pixels;
    } else if (g_strcmp0(unit, "%") == 0) {
        /* Percentage of image size (always 100% for actual dimensions) */
        return 100.0;
    } else if (g_strcmp0(unit, "in") == 0) {
        /* Inches */
        return (gdouble)pixels / dpi;
    } else if (g_strcmp0(unit, "cm") == 0) {
        /* Centimeters (1 inch = 2.54 cm) */
        return ((gdouble)pixels / dpi) * 2.54;
    } else if (g_strcmp0(unit, "mm") == 0) {
        /* Millimeters (1 inch = 25.4 mm) */
        return ((gdouble)pixels / dpi) * 25.4;
    } else if (g_strcmp0(unit, "pt") == 0) {
        /* Points (72 points = 1 inch) */
        return ((gdouble)pixels / dpi) * 72.0;
    } else if (g_strcmp0(unit, "pc") == 0) {
        /* Picas (6 picas = 1 inch) */
        return ((gdouble)pixels / dpi) * 6.0;
    }

    /* Default to pixels */
    return (gdouble)pixels;
}

/**
 * Format dimension value with appropriate precision
 * @param value The dimension value
 * @param unit Unit string (for determining integer vs decimal formatting)
 * @return Formatted string (must be freed)
 */
static gchar* format_dimension(gdouble value, const gchar* unit) {
    /* px and pt should always be displayed as integers (no decimals) */
    if (unit && (g_strcmp0(unit, "px") == 0 || g_strcmp0(unit, "pt") == 0)) {
        return g_strdup_printf("%d", (int)(value + 0.5)); /* Round to nearest integer */
    }

    /* Use appropriate precision based on magnitude for other units */
    if (value >= 100.0) {
        return g_strdup_printf("%.1f", value);
    } else if (value >= 10.0) {
        return g_strdup_printf("%.2f", value);
    } else {
        return g_strdup_printf("%.3f", value);
    }
}

/**
 * Callback for statusbar zoom out button
 */
static void on_statusbar_zoom_out(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_out(doc);
        ui_update_status_bar(ctx, doc);
    }
}

/**
 * Callback for statusbar zoom in button
 */
static void on_statusbar_zoom_in(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_in(doc);
        ui_update_status_bar(ctx, doc);
    }
}

/**
 * Callback for statusbar zoom combobox change
 */
static void on_statusbar_zoom_changed(GtkComboBox* combo, gpointer data) {
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    gchar* text;
    GtkTreeIter iter;

    if (!doc || !gtk_combo_box_get_active_iter(combo, &iter)) {
        return;
    }

    /* Get the text from the combo box */
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    gtk_tree_model_get(model, &iter, 0, &text, -1);

    if (!text) {
        return;
    }

    /* Skip separators */
    if (g_strcmp0(text, "Separator") == 0) {
        g_free(text);
        /* Restore previous selection (do nothing) */
        ui_update_status_bar(ctx, doc);
        return;
    }

    /* Handle special zoom modes */
    if (g_strcmp0(text, _("Fit image")) == 0) {
        document_zoom_fit(doc);
    } else if (g_strcmp0(text, _("Fit width")) == 0) {
        document_zoom_fit_width(doc);
    } else if (g_strcmp0(text, _("Fit height")) == 0) {
        document_zoom_fit_height(doc);
    } else {
        /* Parse zoom percentage */
        int zoom_percent = atoi(text);
        if (zoom_percent > 0) {
            doc->zoom_mode = 0; /* Manual zoom */
            document_set_zoom(doc, zoom_percent / 100.0);
        }
    }

    g_free(text);
    ui_update_status_bar(ctx, doc);
}

/**
 * Callback for statusbar size unit combobox change
 */
static void on_statusbar_size_unit_changed(GtkComboBox* combo, gpointer data) {
    AppContext* ctx = (AppContext*)data;
    gchar* text;
    GtkTreeIter iter;

    if (!gtk_combo_box_get_active_iter(combo, &iter)) {
        return;
    }

    /* Get the selected unit from the combo box */
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    gtk_tree_model_get(model, &iter, 0, &text, -1);

    if (!text) {
        return;
    }

    /* Update the context's size unit */
    if (ctx->size_unit) {
        g_free(ctx->size_unit);
    }
    ctx->size_unit = g_strdup(text);

    /* Sync ruler unit to active document so rulers redraw in the new unit */
    {
        ImageDocument* doc = ui_get_active_document(ctx);
        if (doc) {
            document_set_ruler_unit(doc, ruler_unit_from_string(text));
        }
    }
    g_free(text);

    /* Refresh status bar to show dimensions in new unit */
    ui_update_status_bar(ctx, NULL);
}

/**
 * Row separator function for zoom combobox
 */
/**
 * Update the status bar with document information
 */
void ui_update_status_bar(AppContext* ctx, ImageDocument* doc) {
    GtkWidget *size_label, *zoom_combobox, *zoom_box, *size_box, *position_box, *status_label;
    GtkBuilder* builder;
    gchar* size_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get active document if not provided */
    if (!doc) {
        doc = ui_get_active_document(ctx);
    }

    /* Get builder from window to retrieve status bar widgets */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get status bar widgets */
    size_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_size"));
    zoom_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_combobox"));
    zoom_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_box"));
    size_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_size_box"));
    position_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_position_box"));
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));

    if (!size_label || !zoom_combobox) {
        return;
    }

    if (!doc || doc->width == 0) {
        /* No document or empty document */
        gtk_combo_box_set_active(GTK_COMBO_BOX(zoom_combobox), -1);

        /* Hide statusbar widgets when no document */
        if (zoom_box) {
            gtk_widget_hide(zoom_box);
        }
        if (size_box) {
            gtk_widget_hide(size_box);
        }
        if (position_box) {
            gtk_widget_hide(position_box);
        }
        if (status_label) {
            gtk_widget_hide(status_label);
        }
        {
            GtkWidget* sb_select_size_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_select_size_box"));
            if (sb_select_size_box) {
                gtk_widget_hide(sb_select_size_box);
            }
        }

        size_text = g_strdup(_("—"));
    } else {
        /* Show statusbar widgets when document exists */
        if (zoom_box) {
            gtk_widget_show(zoom_box);
        }
        if (size_box) {
            gtk_widget_show(size_box);
        }
        if (position_box) {
            gtk_widget_show(position_box);
        }
        if (status_label) {
            gtk_widget_show(status_label);
        }

        /* Size label: WIDTH x HEIGHT in selected unit */
        gdouble dpi = 96.0; /* Standard screen DPI */
        gdouble width_converted = convert_dimension(doc->width, ctx->size_unit, doc->zoom_factor, dpi);
        gdouble height_converted = convert_dimension(doc->height, ctx->size_unit, doc->zoom_factor, dpi);

        /* Format dimensions with appropriate precision */
        gchar* width_str = format_dimension(width_converted, ctx->size_unit);
        gchar* height_str = format_dimension(height_converted, ctx->size_unit);
        size_text = g_strdup_printf(_("%s × %s"), width_str, height_str);
        g_free(width_str);
        g_free(height_str);

        /* Update zoom combobox based on zoom mode */
        GtkTreeModel* model = gtk_combo_box_get_model(GTK_COMBO_BOX(zoom_combobox));
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
        gboolean found = FALSE;
        gint found_index = -1;
        gint current_index = 0;
        gchar* search_text = NULL;

        /* Block the changed signal while we update */
        g_signal_handlers_block_by_func(zoom_combobox, on_statusbar_zoom_changed, ctx);

        /* Determine what to search for based on zoom mode */
        if (doc->zoom_mode == 1) {
            search_text = g_strdup(_("Fit image"));
        } else if (doc->zoom_mode == 2) {
            search_text = g_strdup(_("Fit width"));
        } else if (doc->zoom_mode == 3) {
            search_text = g_strdup(_("Fit height"));
        } else {
            /* Manual zoom - search for percentage */
            int zoom_percent = (int)(doc->zoom_factor * 100.0 + 0.5);
            search_text = g_strdup_printf("%d%%", zoom_percent);
        }

        /* Search for matching item in combobox */
        gint nearest_index = -1;
        gdouble nearest_diff = G_MAXDOUBLE;
        gdouble target_zoom = doc->zoom_factor * 100.0;

        valid = gtk_tree_model_get_iter_first(model, &iter);
        current_index = 0;

        while (valid) {
            gchar* item_text;
            gtk_tree_model_get(model, &iter, 0, &item_text, -1);

            if (item_text) {
                /* Check for exact match */
                if (g_strcmp0(item_text, search_text) == 0) {
                    found_index = current_index;
                    found = TRUE;
                }

                /* Also track nearest zoom level for manual zoom */
                if (doc->zoom_mode == 0 && g_str_has_suffix(item_text, "%")) {
                    /* Parse percentage value */
                    gdouble item_zoom = g_strtod(item_text, NULL);
                    gdouble diff = fabs(item_zoom - target_zoom);

                    if (diff < nearest_diff) {
                        nearest_diff = diff;
                        nearest_index = current_index;
                    }
                }
            }

            g_free(item_text);
            valid = gtk_tree_model_iter_next(model, &iter);
            current_index++;
        }

        /* Set active using index (not iter) to avoid rendering issues */
        if (found) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(zoom_combobox), found_index);
        } else if (nearest_index >= 0) {
            /* For manual zoom with no exact match, use nearest zoom level */
            gtk_combo_box_set_active(GTK_COMBO_BOX(zoom_combobox), nearest_index);
        } else {
            /* No match found at all (shouldn't happen) */
            gtk_combo_box_set_active(GTK_COMBO_BOX(zoom_combobox), -1);
        }

        /* Unblock the signal */
        g_signal_handlers_unblock_by_func(zoom_combobox, on_statusbar_zoom_changed, ctx);

        g_free(search_text);
    }

    /* Update label */
    gtk_label_set_text(GTK_LABEL(size_label), size_text);

    g_free(size_text);

    /* Update selection/crop size label and visibility of its container */
    ui_update_status_bar_select_size(ctx, doc);
}

/**
 * Update the status bar selection/crop size label (sb_label_select_size).
 * Shows sb_select_size_box only when dimensions > 0. Priority: crop preview or
 * selection preview first; if neither, total selection mask bounds when selections exist.
 */
void ui_update_status_bar_select_size(AppContext* ctx, ImageDocument* doc) {
    GtkWidget* select_size_box;
    GtkWidget* select_size_label;
    GtkBuilder* builder;
    gint show_w = 0, show_h = 0;
    gdouble dpi = 96.0;
    gchar* width_str = NULL;
    gchar* height_str = NULL;
    gchar* size_text = NULL;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    select_size_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_select_size_box"));
    select_size_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_select_size"));
    if (!select_size_box || !select_size_label) {
        return;
    }

    if (!doc || doc->width == 0) {
        gtk_widget_hide(select_size_box);
        return;
    }

    Tool* active_tool = ctx->tool_registry ? tool_manager_get_active(ctx->tool_registry) : NULL;

    /* 1) Crop preview when crop tool is selected and has an active rect */
    if (active_tool && active_tool->type == TOOL_CROP && active_tool->user_data) {
        CropToolState* crop_state = (CropToolState*)active_tool->user_data;
        if (crop_state->is_active && crop_state->rect_w > 0 && crop_state->rect_h > 0) {
            show_w = crop_state->rect_w;
            show_h = crop_state->rect_h;
        }
    }

    /* 2) Selection preview when rect/ellipse selection tool is selected and editing */
    if (show_w <= 0 && active_tool && active_tool->user_data) {
        if (active_tool->type == TOOL_RECT_SELECT) {
            RectSelectToolState* rs = (RectSelectToolState*)active_tool->user_data;
            if (rs->is_editing && rs->selection_w > 0 && rs->selection_h > 0) {
                show_w = rs->selection_w;
                show_h = rs->selection_h;
            }
        } else if (active_tool->type == TOOL_ELLIPSE_SELECT) {
            EllipseSelectToolState* es = (EllipseSelectToolState*)active_tool->user_data;
            if (es->is_editing && es->selection_w > 0 && es->selection_h > 0) {
                show_w = es->selection_w;
                show_h = es->selection_h;
            }
        }
    }

    /* 3) Total selection size from selection mask if no preview dimensions and selections exist */
    if (show_w <= 0 && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        GList* selections = selection_mask_get_selections(doc->selection_mask);
        int min_x = INT_MAX, min_y = INT_MAX;
        int max_x = INT_MIN, max_y = INT_MIN;

        for (GList* l = selections; l != NULL; l = l->next) {
            Selection* s = (Selection*)l->data;
            if (s->x < min_x) {
                min_x = s->x;
            }
            if (s->y < min_y) {
                min_y = s->y;
            }
            if (s->x + s->width > max_x) {
                max_x = s->x + s->width;
            }
            if (s->y + s->height > max_y) {
                max_y = s->y + s->height;
            }
        }
        if (min_x != INT_MAX && max_x >= min_x && max_y >= min_y) {
            show_w = max_x - min_x;
            show_h = max_y - min_y;
        }
    }

    if (show_w > 0 && show_h > 0) {
        gdouble zoom = doc->zoom_factor;
        gdouble w_converted = convert_dimension((guint)show_w, ctx->size_unit, zoom, dpi);
        gdouble h_converted = convert_dimension((guint)show_h, ctx->size_unit, zoom, dpi);
        width_str = format_dimension(w_converted, ctx->size_unit);
        height_str = format_dimension(h_converted, ctx->size_unit);
        size_text = g_strdup_printf(_("%s × %s"), width_str, height_str);
        gtk_label_set_text(GTK_LABEL(select_size_label), size_text);
        gtk_widget_show(select_size_box);
        g_free(width_str);
        g_free(height_str);
        g_free(size_text);
    } else {
        gtk_widget_hide(select_size_box);
    }
}

/**
 * Update the status bar time label with processing time
 * @param ctx The application context
 * @param time_seconds Processing time in seconds
 */
void ui_update_status_bar_time(AppContext* ctx, gdouble time_seconds) {
    GtkWidget* status_label;
    GtkBuilder* builder;
    gchar* time_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get builder from window to retrieve status bar labels */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get time label */
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));
    if (!status_label) {
        return;
    }

    /* Format time: show seconds with appropriate precision */
    if (time_seconds < 0.001) {
        time_text = g_strdup_printf(_("Time taken: %.3f ms"), time_seconds * 1000.0);
    } else if (time_seconds < 1.0) {
        time_text = g_strdup_printf(_("Time taken: %.3f s"), time_seconds);
    } else {
        time_text = g_strdup_printf(_("Time taken: %.2f s"), time_seconds);
    }

    /* Update label */
    gtk_label_set_text(GTK_LABEL(status_label), time_text);
    g_free(time_text);
}

/**
 * Update the status bar status label with a message
 */
void ui_update_status_bar_message(AppContext* ctx, const gchar* message) {
    GtkWidget* status_label;
    GtkBuilder* builder;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get builder from window to retrieve status bar labels */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get status label */
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));
    if (!status_label) {
        return;
    }

    /* Update label */
    gtk_label_set_text(GTK_LABEL(status_label), message ? message : "");
}

/**
 * Update the cursor position display in the status bar
 */
void ui_update_cursor_position(AppContext* ctx, ImageDocument* doc, gint image_x, gint image_y) {
    GtkWidget* position_label;
    GtkWidget* position_box;
    GtkBuilder* builder;
    gchar* position_text;
    gdouble x_value, y_value;
    gdouble dpi = 96.0;

    if (!ctx || !ctx->status_bar || !doc) {
        return;
    }

    /* Get builder from window to retrieve status bar widgets */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get position label and box */
    position_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_position"));
    position_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_position_box"));
    if (!position_label || !position_box) {
        return;
    }

    /* Convert image coordinates to selected unit */
    if (g_strcmp0(ctx->size_unit, "px") == 0) {
        /* Pixels - show as integers */
        position_text = g_strdup_printf(_("(%d, %d)"), image_x, image_y);
    } else if (g_strcmp0(ctx->size_unit, "%") == 0) {
        /* Percentage of image dimensions */
        x_value = ((gdouble)image_x / (gdouble)doc->width) * 100.0;
        y_value = ((gdouble)image_y / (gdouble)doc->height) * 100.0;
        position_text = g_strdup_printf(_("(%.1f, %.1f)"), x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "in") == 0) {
        /* Inches */
        x_value = (gdouble)image_x / dpi;
        y_value = (gdouble)image_y / dpi;
        position_text = g_strdup_printf(_("(%.3f, %.3f)"), x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "cm") == 0) {
        /* Centimeters */
        x_value = ((gdouble)image_x / dpi) * 2.54;
        y_value = ((gdouble)image_y / dpi) * 2.54;
        position_text = g_strdup_printf(_("(%.2f, %.2f)"), x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "mm") == 0) {
        /* Millimeters */
        x_value = ((gdouble)image_x / dpi) * 25.4;
        y_value = ((gdouble)image_y / dpi) * 25.4;
        position_text = g_strdup_printf(_("(%.1f, %.1f)"), x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "pt") == 0) {
        /* Points - show as integers */
        x_value = ((gdouble)image_x / dpi) * 72.0;
        y_value = ((gdouble)image_y / dpi) * 72.0;
        position_text = g_strdup_printf(_("(%d, %d)"), (gint)x_value, (gint)y_value);
    } else if (g_strcmp0(ctx->size_unit, "pc") == 0) {
        /* Picas */
        x_value = ((gdouble)image_x / dpi) * 6.0;
        y_value = ((gdouble)image_y / dpi) * 6.0;
        position_text = g_strdup_printf(_("(%.2f, %.2f)"), x_value, y_value);
    } else {
        /* Default to pixels */
        position_text = g_strdup_printf(_("(%d, %d)"), image_x, image_y);
    }

    gtk_label_set_text(GTK_LABEL(position_label), position_text);

    /* Show the position box */
    gtk_widget_show(position_box);

    g_free(position_text);
}

/**
 * Hide the cursor position display in the status bar
 */
void ui_hide_cursor_position(AppContext* ctx) {
    GtkWidget* position_box;
    GtkBuilder* builder;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get builder from window to retrieve status bar widgets */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get position box */
    position_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_position_box"));
    if (!position_box) {
        return;
    }

    /* Hide the position box */
    gtk_widget_hide(position_box);
}

static gboolean s_main_progress_bar_css_applied = FALSE;

static void ui_ensure_main_progress_bar_css(GtkWidget* progress_bar) {
    if (!progress_bar || s_main_progress_bar_css_applied) {
        return;
    }
    gtk_widget_set_name(progress_bar, "progress_bar");

    GtkCssProvider* css_provider = gtk_css_provider_new();
    const gchar* css =
        "progressbar#progress_bar {"
        "  padding: 0;"
        "  margin: 0;"
        "  border: none;"
        "}"
        "progressbar#progress_bar trough {"
        "  padding: 0;"
        "  margin: 0;"
        "  border: none;"
        "  min-height: 15px;"
        "  border-radius: 0;"
        "}"
        "progressbar#progress_bar progress {"
        "  padding: 0;"
        "  margin: 0;"
        "  border: none;"
        "  min-height: 15px;"
        "  border-radius: 0;"
        "}";
    gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
    GtkStyleContext* style_context = gtk_widget_get_style_context(progress_bar);
    gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);
    s_main_progress_bar_css_applied = TRUE;
}

/**
 * Show and start the progress bar with a message
 */
void ui_show_progress(AppContext* ctx, const gchar* message) {
    GtkWidget* progress_bar;
    GtkWidget* status_label;
    GtkBuilder* builder;

    if (!ctx || !ctx->window) {
        return;
    }

    /* Get builder from window to retrieve progress bar and status label */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get progress bar and status label */
    progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progress_bar"));
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));

    if (progress_bar) {
        ui_ensure_main_progress_bar_css(progress_bar);
        /* Show progress bar and start pulse animation */
        gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(progress_bar), 0.1);
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
        gtk_widget_show(progress_bar);
    }

    if (status_label && message) {
        /* Update status label with progress message */
        gtk_label_set_text(GTK_LABEL(status_label), message);
    }

    /* Process pending events to show the progress bar */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void ui_load_progress_show(AppContext* ctx, const gchar* message, gdouble fraction) {
    GtkWidget* progress_bar;
    GtkWidget* status_label;
    GtkBuilder* builder;

    if (!ctx || !ctx->window) {
        return;
    }

    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progress_bar"));
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));

    if (progress_bar) {
        ui_ensure_main_progress_bar_css(progress_bar);
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), FALSE);
        if (fraction < 0.0) {
            gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(progress_bar), 0.1);
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
        } else {
            gdouble c = (fraction < 0.0) ? 0.0 : (fraction > 1.0) ? 1.0 : fraction;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), c);
        }
        gtk_widget_show(progress_bar);
    }

    if (status_label && message) {
        gtk_label_set_text(GTK_LABEL(status_label), message);
    }

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void ui_load_progress_set(AppContext* ctx, gdouble fraction, const gchar* message) {
    GtkWidget* progress_bar;
    GtkWidget* status_label;
    GtkBuilder* builder;
    gdouble c;

    if (!ctx || !ctx->window) {
        return;
    }

    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progress_bar"));
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));
    c = (fraction < 0.0) ? 0.0 : (fraction > 1.0) ? 1.0 : fraction;

    if (progress_bar) {
        ui_ensure_main_progress_bar_css(progress_bar);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), c);
        gtk_widget_show(progress_bar);
    }

    if (status_label && message) {
        gtk_label_set_text(GTK_LABEL(status_label), message);
    }

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

/**
 * Update the progress bar (pulse animation)
 */
void ui_update_progress(AppContext* ctx) {
    GtkWidget* progress_bar;
    GtkBuilder* builder;

    if (!ctx || !ctx->window) {
        return;
    }

    /* Get builder from window to retrieve progress bar */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get progress bar */
    progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progress_bar"));

    if (progress_bar) {
        /* Pulse the progress bar */
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
    }

    /* Process pending events */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

/**
 * Hide the progress bar
 */
void ui_hide_progress(AppContext* ctx) {
    GtkWidget* progress_bar;
    GtkWidget* status_label;
    GtkBuilder* builder;

    if (!ctx || !ctx->window) {
        return;
    }

    /* Get builder from window to retrieve progress bar and status label */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get progress bar and status label */
    progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progress_bar"));
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));

    if (progress_bar) {
        /* Hide progress bar */
        gtk_widget_hide(progress_bar);
    }

    if (status_label) {
        /* Reset status label to default */
        gtk_label_set_text(GTK_LABEL(status_label), "");
    }
}

/**
 * Update menu and button sensitivity based on document and layer state
 */
void ui_update_menu_and_button_states(AppContext* ctx) {
    ImageDocument* doc;
    LayersPanel* layers_panel;
    gboolean has_document;
    gboolean has_selection;

    if (!ctx || !ctx->window) {
        return;
    }

    /* Get current document */
    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);

    /* Get layers panel and check for selection */
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                   "layers_panel");

    has_selection = FALSE;
    ImageLayer* selected_layer = NULL;
    if (layers_panel && doc && layers_panel->tree_view) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
        has_selection = (selected_layer != NULL);
    }

    /* Update layers panel button states */
    if (layers_panel) {
        layers_panel_update_button_sensitivity(layers_panel, has_document, has_selection, doc, selected_layer);
        layers_panel_update_opacity_controls(layers_panel);
    }

    ui_file_menu_update_sensitivity(ctx);
    ui_edit_menu_update_sensitivity(ctx);
    ui_image_menu_update_sensitivity(ctx);
    ui_select_menu_update_sensitivity(ctx);

    ui_layer_menu_update_sensitivity(ctx);
    ui_adjustments_and_effects_menu_update_sensitivity(ctx);
    ui_view_menu_update_zoom_sensitivity(ctx);
}

/**
 * Get canvas background color
 */
void ui_get_canvas_background_color(AppContext* ctx, gdouble* r, gdouble* g, gdouble* b) {
    if (!ctx) {
        /* Return default if context is NULL */
        if (r)
            *r = 160.0 / 255.0;
        if (g)
            *g = 160.0 / 255.0;
        if (b)
            *b = 160.0 / 255.0;
        return;
    }

    /* Get from settings if available, otherwise use defaults */
    if (ctx->settings) {
        settings_get_canvas_background(ctx->settings, r, g, b);
    } else {
        /* Return defaults if settings not loaded yet */
        if (r)
            *r = 160.0 / 255.0;
        if (g)
            *g = 160.0 / 255.0;
        if (b)
            *b = 160.0 / 255.0;
    }
}

/**
 * Set canvas background color
 */
void ui_set_canvas_background_color(AppContext* ctx, gdouble r, gdouble g, gdouble b) {
    if (!ctx) {
        return;
    }

    /* Clamp values to 0.0-1.0 */
    gdouble r_clamped = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    gdouble g_clamped = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    gdouble b_clamped = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);

    /* Update settings if available */
    if (ctx->settings) {
        settings_set_canvas_background(ctx->settings, r_clamped, g_clamped, b_clamped);
        /* Save settings immediately */
        if (ctx->app_dir) {
            settings_save(ctx->settings, ctx->app_dir);
        }
    }

    /* Update all open documents */
    ui_update_canvas_background_color(ctx);
}

/**
 * Update canvas background color for all open documents
 */
void ui_update_canvas_background_color(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    gdouble r_val, g_val, b_val;
    ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
    guint r = (guint)(r_val * 255.0);
    guint g = (guint)(g_val * 255.0);
    guint b = (guint)(b_val * 255.0);

    /* Update viewport background for all documents */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument* doc = (ImageDocument*)iter->data;
        if (doc && doc->viewport) {
            /* Get or create CSS provider for this viewport */
            GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(G_OBJECT(doc->viewport), "canvas_bg_provider");
            if (!provider) {
                provider = gtk_css_provider_new();
                g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider", provider, g_object_unref);
                GtkStyleContext* style_context = gtk_widget_get_style_context(doc->viewport);
                gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            }

            gchar* css = g_strdup_printf("#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
            gtk_css_provider_load_from_data(provider, css, -1, NULL);
            g_free(css);
            gtk_widget_queue_draw(doc->viewport);
        }
    }
}

/**
 * Save active document with file dialog
 */
void ui_save_document_as(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    on_file_save_as(NULL, ctx);
}

/* Forward declaration */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately);

/**
 * Get tool name from tool type (for settings storage)
 */
static const char* tool_type_to_name(ToolType tool_type) {
    switch (tool_type) {
        case TOOL_BRUSH:
            return "brush";
        case TOOL_PENCIL:
            return "pencil";
        case TOOL_ERASER:
            return "eraser";
        case TOOL_PAINT_BUCKET:
            return "paintbucket";
        case TOOL_HAND:
            return "hand";
        case TOOL_ZOOM:
            return "zoom";
        case TOOL_MOVE:
            return "move";
        case TOOL_RECT_SELECT:
            return "rect_select";
        case TOOL_ELLIPSE_SELECT:
            return "ellipse_select";
        case TOOL_CROP:
            return "crop";
        case TOOL_COLOR_PICKER:
            return "color_picker";
        case TOOL_MAGIC_WAND:
            return "magic_wand_select";
        default:
            return NULL;
    }
}

/**
 * Load tool options from settings
 */
void ui_load_tool_options_from_settings(AppContext* ctx) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    /* Load options for each tool type */
    for (int i = 0; i < TOOL_COUNT; i++) {
        ToolType tool_type = (ToolType)i;
        const char* tool_name = tool_type_to_name(tool_type);
        if (!tool_name) {
            continue;
        }

        ToolOptions* opts = tool_options_get_for_tool(tool_type);
        if (!opts) {
            continue;
        }

        /* Load size (use default if not found) */
        const char* size_str = settings_get_tool_option(ctx->settings, tool_name, "size");
        if (size_str) {
            gfloat size = (gfloat)g_ascii_strtod(size_str, NULL);
            if (size > 0.0f) {
                tool_options_set_size(opts, size);
            } else {
                tool_options_set_size(opts, settings_get_default_tool_size());
            }
        } else {
            tool_options_set_size(opts, settings_get_default_tool_size());
        }

        /* Load opacity (use default if not found) */
        const char* opacity_str = settings_get_tool_option(ctx->settings, tool_name, "opacity");
        if (opacity_str) {
            gfloat opacity = (gfloat)g_ascii_strtod(opacity_str, NULL);
            tool_options_set_opacity(opts, opacity);
        } else {
            tool_options_set_opacity(opts, settings_get_default_tool_opacity());
        }

        /* Load hardness (use default if not found) */
        const char* hardness_str = settings_get_tool_option(ctx->settings, tool_name, "hardness");
        if (hardness_str) {
            gfloat hardness = (gfloat)g_ascii_strtod(hardness_str, NULL);
            tool_options_set_hardness(opts, hardness);
        } else {
            tool_options_set_hardness(opts, settings_get_default_tool_hardness());
        }

        /* Load flow (use default if not found) */
        const char* flow_str = settings_get_tool_option(ctx->settings, tool_name, "flow");
        if (flow_str) {
            gfloat flow = (gfloat)g_ascii_strtod(flow_str, NULL);
            tool_options_set_flow(opts, flow);
        } else {
            tool_options_set_flow(opts, settings_get_default_tool_flow());
        }

        /* Load spacing (use default if not found) */
        const char* spacing_str = settings_get_tool_option(ctx->settings, tool_name, "spacing");
        if (spacing_str) {
            gfloat spacing = (gfloat)g_ascii_strtod(spacing_str, NULL);
            tool_options_set_spacing(opts, spacing);
        } else {
            tool_options_set_spacing(opts, settings_get_default_tool_spacing());
        }

        /* Load tolerance (use default if not found) */
        const char* tolerance_str = settings_get_tool_option(ctx->settings, tool_name, "tolerance");
        if (tolerance_str) {
            gfloat tolerance = (gfloat)g_ascii_strtod(tolerance_str, NULL);
            tool_options_set_tolerance(opts, tolerance);
        } else {
            tool_options_set_tolerance(opts, settings_get_default_tool_tolerance());
        }

        /* Load fill_contiguous (use default if not found) */
        const char* contiguous_str = settings_get_tool_option(ctx->settings, tool_name, "fill_contiguous");
        if (contiguous_str) {
            gboolean contiguous = (g_strcmp0(contiguous_str, "true") == 0 || g_strcmp0(contiguous_str, "1") == 0);
            tool_options_set_fill_contiguous(opts, contiguous);
        } else {
            tool_options_set_fill_contiguous(opts, settings_get_default_tool_fill_contiguous());
        }

        /* Load fill_antialiased (use default if not found) */
        const char* antialiased_str = settings_get_tool_option(ctx->settings, tool_name, "fill_antialiased");
        if (antialiased_str) {
            gboolean antialiased = (g_strcmp0(antialiased_str, "true") == 0 || g_strcmp0(antialiased_str, "1") == 0);
            tool_options_set_fill_antialiased(opts, antialiased);
        } else {
            tool_options_set_fill_antialiased(opts, settings_get_default_tool_fill_antialiased());
        }

        /* Load fill_compare_mode (use default COLOR if not found) */
        const char* compare_mode_str = settings_get_tool_option(ctx->settings, tool_name, "fill_compare_mode");
        if (compare_mode_str) {
            gint mode = (gint)g_ascii_strtoll(compare_mode_str, NULL, 10);
            if (mode >= 0 && mode <= (gint)FILL_COMPARE_ALPHA) {
                tool_options_set_fill_compare_mode(opts, (FillCompareMode)mode);
            } else {
                tool_options_set_fill_compare_mode(opts, FILL_COMPARE_COLOR);
            }
        } else {
            tool_options_set_fill_compare_mode(opts, FILL_COMPARE_COLOR);
        }

        /* Pencil tool specific options */
        if (tool_type == TOOL_PENCIL) {
            /* Load pencil antialias (default: false) */
            const char* pencil_antialias_str = settings_get_tool_option(ctx->settings, tool_name, "pencil_antialias");
            if (pencil_antialias_str) {
                gboolean antialias = (g_strcmp0(pencil_antialias_str, "true") == 0 || g_strcmp0(pencil_antialias_str, "1") == 0);
                opts->pencil_antialias = antialias;
            } else {
                opts->pencil_antialias = FALSE;
            }

            /* Load pencil align to pixel grid (default: true) */
            const char* pencil_align_pixel_grid_str = settings_get_tool_option(ctx->settings, tool_name, "pencil_align_pixel_grid");
            if (pencil_align_pixel_grid_str) {
                gboolean align = (g_strcmp0(pencil_align_pixel_grid_str, "true") == 0 || g_strcmp0(pencil_align_pixel_grid_str, "1") == 0);
                opts->pencil_align_pixel_grid = align;
            } else {
                opts->pencil_align_pixel_grid = TRUE;
            }
        }

        /* Color picker tool specific options */
        if (tool_type == TOOL_COLOR_PICKER) {
            const char* sample_radius_str = settings_get_tool_option(ctx->settings, tool_name, "sample_radius");
            if (sample_radius_str) {
                gint radius = (gint)g_ascii_strtoll(sample_radius_str, NULL, 10);
                tool_options_set_color_picker_sample_radius(opts, radius);
            } else {
                tool_options_set_color_picker_sample_radius(opts, 0);
            }
            const char* sample_from_str = settings_get_tool_option(ctx->settings, tool_name, "sample_from_layer");
            if (sample_from_str) {
                gboolean from_layer = (g_strcmp0(sample_from_str, "true") == 0 || g_strcmp0(sample_from_str, "1") == 0);
                tool_options_set_color_picker_sample_from_layer(opts, from_layer);
            } else {
                tool_options_set_color_picker_sample_from_layer(opts, TRUE);
            }
        }

        /* Rectangular select tool specific options */
        if (tool_type == TOOL_RECT_SELECT) {
            /* Load combine mode (0=NEW, 1=ADD, 2=SUBTRACT, 3=INTERSECT) */
            const char* combine_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_combine");
            if (combine_str) {
                gint combine_val = (gint)g_ascii_strtoll(combine_str, NULL, 10);
                if (combine_val >= 0 && combine_val < 4) {
                    tool_options_set_rect_select_combine(opts, (SelectionCombineMode)combine_val);
                } else {
                    tool_options_set_rect_select_combine(opts, SELECTION_COMBINE_NEW);
                }
            } else {
                tool_options_set_rect_select_combine(opts, SELECTION_COMBINE_NEW);
            }

            /* Load smoothing mode (0=NONE, 1=ANTIALIASED, 2=FEATHERED) */
            const char* smooth_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_smooth");
            if (smooth_str) {
                gint smooth_val = (gint)g_ascii_strtoll(smooth_str, NULL, 10);
                if (smooth_val >= 0 && smooth_val < 3) {
                    tool_options_set_rect_select_smooth(opts, (SelectionSmoothingMode)smooth_val);
                } else {
                    tool_options_set_rect_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
                }
            } else {
                tool_options_set_rect_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
            }

            /* Load feather radius */
            const char* feather_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_feather");
            if (feather_str) {
                gfloat feather_val = (gfloat)g_ascii_strtod(feather_str, NULL);
                if (feather_val >= 0.0f && feather_val <= 200.0f) {
                    tool_options_set_rect_select_feather(opts, feather_val);
                } else {
                    tool_options_set_rect_select_feather(opts, 0.0f);
                }
            } else {
                tool_options_set_rect_select_feather(opts, 0.0f);
            }

            /* Load animation toggle */
            const char* animate_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_animate");
            if (animate_str) {
                gboolean animate = (g_strcmp0(animate_str, "true") == 0 || g_strcmp0(animate_str, "1") == 0);
                tool_options_set_rect_select_animate(opts, animate);
            } else {
                tool_options_set_rect_select_animate(opts, TRUE);
            }
        }

        /* Elliptical select tool specific options */
        if (tool_type == TOOL_ELLIPSE_SELECT) {
            /* Load combine mode (0=NEW, 1=ADD, 2=SUBTRACT, 3=INTERSECT) */
            const char* combine_str = settings_get_tool_option(ctx->settings, tool_name, "ellipse_select_combine");
            if (combine_str) {
                gint combine_val = (gint)g_ascii_strtoll(combine_str, NULL, 10);
                if (combine_val >= 0 && combine_val < 4) {
                    tool_options_set_ellipse_select_combine(opts, (SelectionCombineMode)combine_val);
                } else {
                    tool_options_set_ellipse_select_combine(opts, SELECTION_COMBINE_NEW);
                }
            } else {
                tool_options_set_ellipse_select_combine(opts, SELECTION_COMBINE_NEW);
            }

            /* Load smoothing mode (0=NONE, 1=ANTIALIASED, 2=FEATHERED) */
            const char* smooth_str = settings_get_tool_option(ctx->settings, tool_name, "ellipse_select_smooth");
            if (smooth_str) {
                gint smooth_val = (gint)g_ascii_strtoll(smooth_str, NULL, 10);
                if (smooth_val >= 0 && smooth_val < 3) {
                    tool_options_set_ellipse_select_smooth(opts, (SelectionSmoothingMode)smooth_val);
                } else {
                    tool_options_set_ellipse_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
                }
            } else {
                tool_options_set_ellipse_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
            }

            /* Load feather radius */
            const char* feather_str = settings_get_tool_option(ctx->settings, tool_name, "ellipse_select_feather");
            if (feather_str) {
                gfloat feather_val = (gfloat)g_ascii_strtod(feather_str, NULL);
                if (feather_val >= 0.0f && feather_val <= 200.0f) {
                    tool_options_set_ellipse_select_feather(opts, feather_val);
                } else {
                    tool_options_set_ellipse_select_feather(opts, 0.0f);
                }
            } else {
                tool_options_set_ellipse_select_feather(opts, 0.0f);
            }

            /* Load animation toggle */
            const char* animate_str = settings_get_tool_option(ctx->settings, tool_name, "ellipse_select_animate");
            if (animate_str) {
                gboolean animate = (g_strcmp0(animate_str, "true") == 0 || g_strcmp0(animate_str, "1") == 0);
                tool_options_set_ellipse_select_animate(opts, animate);
            } else {
                tool_options_set_ellipse_select_animate(opts, TRUE);
            }
        }

        /* Magic wand select tool specific options */
        if (tool_type == TOOL_MAGIC_WAND) {
            const char* combine_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_combine");
            if (combine_str) {
                gint v = (gint)g_ascii_strtoll(combine_str, NULL, 10);
                tool_options_set_magicwand_combine(opts, (v >= 0 && v < 4) ? (SelectionCombineMode)v : SELECTION_COMBINE_NEW);
            }
            const char* smooth_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_smooth");
            if (smooth_str) {
                gint v = (gint)g_ascii_strtoll(smooth_str, NULL, 10);
                tool_options_set_magicwand_smooth(opts, (v >= 0 && v < 3) ? (SelectionSmoothingMode)v : SELECTION_SMOOTH_ANTIALIASED);
            }
            const char* feather_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_feather");
            if (feather_str) {
                gfloat v = (gfloat)g_ascii_strtod(feather_str, NULL);
                tool_options_set_magicwand_feather(opts, (v >= 0.0f && v <= 200.0f) ? v : 0.0f);
            }
            const char* animate_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_animate");
            if (animate_str)
                tool_options_set_magicwand_animate(opts, (g_strcmp0(animate_str, "true") == 0 || g_strcmp0(animate_str, "1") == 0));
            const char* tolerance_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_tolerance");
            if (tolerance_str) {
                gfloat v = (gfloat)g_ascii_strtod(tolerance_str, NULL);
                tool_options_set_magicwand_tolerance(opts, (v >= 0.0f && v <= 100.0f) ? v : 15.0f);
            }
            const char* compare_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_compare_mode");
            if (compare_str) {
                gint v = (gint)g_ascii_strtoll(compare_str, NULL, 10);
                tool_options_set_magicwand_compare_mode(opts, (v >= 0 && v <= (gint)FILL_COMPARE_ALPHA) ? (FillCompareMode)v : FILL_COMPARE_COLOR);
            }
            const char* contiguous_str = settings_get_tool_option(ctx->settings, tool_name, "magicwand_contiguous");
            if (contiguous_str)
                tool_options_set_magicwand_contiguous(opts, !(g_strcmp0(contiguous_str, "false") == 0 || g_strcmp0(contiguous_str, "0") == 0));
        }

        /* Crop tool specific options */
        if (tool_type == TOOL_CROP) {
            const char* overlay_str = settings_get_tool_option(ctx->settings, tool_name, "crop_overlay_mode");
            if (overlay_str) {
                gint overlay_val = (gint)g_ascii_strtoll(overlay_str, NULL, 10);
                if (overlay_val >= 0 && overlay_val <= 5) {
                    tool_options_set_crop_overlay_mode(opts, overlay_val);
                }
            }
            const char* delete_pixels_str = settings_get_tool_option(ctx->settings, tool_name, "crop_delete_pixels");
            if (delete_pixels_str) {
                gboolean val = (g_strcmp0(delete_pixels_str, "true") == 0 || g_strcmp0(delete_pixels_str, "1") == 0);
                tool_options_set_crop_delete_pixels(opts, val);
            }
            const char* grow_canvas_str = settings_get_tool_option(ctx->settings, tool_name, "crop_grow_canvas");
            if (grow_canvas_str) {
                gboolean val = (g_strcmp0(grow_canvas_str, "true") == 0 || g_strcmp0(grow_canvas_str, "1") == 0);
                tool_options_set_crop_grow_canvas(opts, val);
            }
            const char* darken_str = settings_get_tool_option(ctx->settings, tool_name, "crop_darken_outside");
            if (darken_str) {
                gboolean val = (g_strcmp0(darken_str, "true") == 0 || g_strcmp0(darken_str, "1") == 0);
                tool_options_set_crop_darken_outside(opts, val);
            }
            const char* darken_opacity_str = settings_get_tool_option(ctx->settings, tool_name, "crop_darken_opacity");
            if (darken_opacity_str) {
                gfloat val = (gfloat)g_ascii_strtod(darken_opacity_str, NULL);
                if (val >= 0.0f && val <= 100.0f) {
                    tool_options_set_crop_darken_opacity(opts, val);
                }
            }
        }
    }
}

/* Forward declaration */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately);

/**
 * Save tool options to settings (in-memory only, not to file)
 * Tool options are saved to file only on app shutdown
 */
void ui_save_tool_options_to_settings(AppContext* ctx, ToolType tool_type) {
    ui_save_tool_options_to_settings_internal(ctx, tool_type, FALSE);
}

/**
 * Internal function to save tool options to settings
 * @param save_immediately If TRUE, saves settings to file immediately. If FALSE, only updates in-memory settings.
 */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    const char* tool_name = tool_type_to_name(tool_type);
    if (!tool_name) {
        return;
    }

    /* Get the tool to check which options it supports */
    Tool* tool = tool_manager_get(ctx->tool_registry, tool_type);
    if (!tool) {
        return;
    }

    /* Read tool options flags immediately to avoid accessing freed memory */
    ToolOptionFlags tool_options_flags = tool->options;

    ToolOptions* opts = tool_options_get_for_tool(tool_type);
    if (!opts) {
        return;
    }

    /* Read all values from opts structure immediately to avoid accessing freed memory */
    /* Store values in local variables before any operations that might free memory */
    gfloat size_val = opts->size;
    gfloat opacity_val = opts->opacity;
    gfloat hardness_val = opts->hardness;
    gfloat flow_val = opts->flow;
    gfloat spacing_val = opts->spacing;
    gfloat tolerance_val = opts->tolerance;
    gboolean fill_contiguous_val = opts->fill_contiguous;
    gboolean fill_antialiased_val = opts->fill_antialiased;
    FillCompareMode fill_compare_mode_val = opts->fill_compare_mode;
    gboolean pencil_antialias_val = opts->pencil_antialias;
    gboolean pencil_align_pixel_grid_val = opts->pencil_align_pixel_grid;
    SelectionCombineMode rect_select_combine_val = opts->rect_select_combine;
    SelectionSmoothingMode rect_select_smooth_val = opts->rect_select_smooth;
    gfloat rect_select_feather_val = opts->rect_select_feather;
    gboolean rect_select_animate_val = opts->rect_select_animate;
    SelectionCombineMode ellipse_select_combine_val = opts->ellipse_select_combine;
    SelectionSmoothingMode ellipse_select_smooth_val = opts->ellipse_select_smooth;
    gfloat ellipse_select_feather_val = opts->ellipse_select_feather;
    gboolean ellipse_select_animate_val = opts->ellipse_select_animate;
    gint color_picker_sample_radius_val = opts->color_picker_sample_radius;
    gboolean color_picker_sample_from_layer_val = opts->color_picker_sample_from_layer;
    gint crop_overlay_mode_val = opts->crop_overlay_mode;
    gboolean crop_delete_pixels_val = opts->crop_delete_pixels;
    gboolean crop_grow_canvas_val = opts->crop_grow_canvas;
    gboolean crop_darken_outside_val = opts->crop_darken_outside;
    gfloat crop_darken_opacity_val = opts->crop_darken_opacity;

    /* Save only the options that this tool supports */
    /* Check tool_options_flags to determine which options to save */

    if (tool_options_flags & TOOL_OPT_SIZE) {
        gchar size_str[32];
        gfloat safe_size = (size_val >= 1.0f && size_val <= 10000.0f) ? size_val : 5.0f;
        g_snprintf(size_str, sizeof(size_str), "%.2f", safe_size);
        settings_set_tool_option(ctx->settings, tool_name, "size", size_str);
    }

    if (tool_options_flags & TOOL_OPT_OPACITY) {
        gchar opacity_str[32];
        gfloat safe_opacity = (opacity_val >= 0.0f && opacity_val <= 1.0f) ? opacity_val : 1.0f;
        g_snprintf(opacity_str, sizeof(opacity_str), "%.3f", safe_opacity);
        settings_set_tool_option(ctx->settings, tool_name, "opacity", opacity_str);
    }

    if (tool_options_flags & TOOL_OPT_HARDNESS) {
        gchar hardness_str[32];
        gfloat safe_hardness = (hardness_val >= 0.0f && hardness_val <= 1.0f) ? hardness_val : 1.0f;
        g_snprintf(hardness_str, sizeof(hardness_str), "%.3f", safe_hardness);
        settings_set_tool_option(ctx->settings, tool_name, "hardness", hardness_str);
    }

    if (tool_options_flags & TOOL_OPT_FLOW) {
        gchar flow_str[32];
        gfloat safe_flow = (flow_val >= 0.0f && flow_val <= 1.0f) ? flow_val : 1.0f;
        g_snprintf(flow_str, sizeof(flow_str), "%.3f", safe_flow);
        settings_set_tool_option(ctx->settings, tool_name, "flow", flow_str);
    }

    if (tool_options_flags & TOOL_OPT_SPACING) {
        gchar spacing_str[32];
        gfloat safe_spacing = (spacing_val >= 0.0f && spacing_val <= 1.0f) ? spacing_val : 0.25f;
        g_snprintf(spacing_str, sizeof(spacing_str), "%.3f", safe_spacing);
        settings_set_tool_option(ctx->settings, tool_name, "spacing", spacing_str);
    }

    /* Paint bucket tool uses tolerance, fill_contiguous, and fill_antialiased */
    /* These are not in ToolOptionFlags, so check tool type directly */
    if (tool_type == TOOL_PAINT_BUCKET) {
        gchar tolerance_str[32];
        gfloat safe_tolerance = (tolerance_val >= 0.0f && tolerance_val <= 100.0f) ? tolerance_val : 15.0f;
        g_snprintf(tolerance_str, sizeof(tolerance_str), "%.2f", safe_tolerance);
        settings_set_tool_option(ctx->settings, tool_name, "tolerance", tolerance_str);
        settings_set_tool_option(ctx->settings, tool_name, "fill_contiguous", fill_contiguous_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "fill_antialiased", fill_antialiased_val ? "true" : "false");
        gchar compare_mode_buf[16];
        g_snprintf(compare_mode_buf, sizeof(compare_mode_buf), "%d", (gint)fill_compare_mode_val);
        settings_set_tool_option(ctx->settings, tool_name, "fill_compare_mode", compare_mode_buf);
    }

    /* Color picker tool uses sample_radius and sample_from */
    if (tool_type == TOOL_COLOR_PICKER) {
        gchar radius_str[32];
        gint safe_radius = (color_picker_sample_radius_val >= 0 && color_picker_sample_radius_val <= 100)
                               ? color_picker_sample_radius_val
                               : 0;
        g_snprintf(radius_str, sizeof(radius_str), "%d", safe_radius);
        settings_set_tool_option(ctx->settings, tool_name, "sample_radius", radius_str);
        settings_set_tool_option(ctx->settings, tool_name, "sample_from_layer",
                                 color_picker_sample_from_layer_val ? "true" : "false");
    }

    /* Pencil tool uses antialias and align_pixel_grid options */
    if (tool_type == TOOL_PENCIL) {
        settings_set_tool_option(ctx->settings, tool_name, "pencil_antialias", pencil_antialias_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "pencil_align_pixel_grid", pencil_align_pixel_grid_val ? "true" : "false");
    }

    /* Rectangular select tool uses combine mode, smoothing, feather, and animate options */
    if (tool_type == TOOL_RECT_SELECT) {
        gchar combine_str[16];
        gint combine_val = (gint)rect_select_combine_val;
        g_snprintf(combine_str, sizeof(combine_str), "%d", combine_val);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_combine", combine_str);

        gchar smooth_str[16];
        gint smooth_val = (gint)rect_select_smooth_val;
        g_snprintf(smooth_str, sizeof(smooth_str), "%d", smooth_val);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_smooth", smooth_str);

        gchar feather_str[32];
        gfloat safe_feather = (rect_select_feather_val >= 0.0f && rect_select_feather_val <= 200.0f) ? rect_select_feather_val : 0.0f;
        g_snprintf(feather_str, sizeof(feather_str), "%.2f", safe_feather);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_feather", feather_str);

        settings_set_tool_option(ctx->settings, tool_name, "rect_select_animate", rect_select_animate_val ? "true" : "false");
    }

    /* Elliptical select tool uses combine mode, smoothing, feather, and animate options */
    if (tool_type == TOOL_ELLIPSE_SELECT) {
        gchar combine_str[16];
        gint combine_val = (gint)ellipse_select_combine_val;
        g_snprintf(combine_str, sizeof(combine_str), "%d", combine_val);
        settings_set_tool_option(ctx->settings, tool_name, "ellipse_select_combine", combine_str);

        gchar smooth_str[16];
        gint smooth_val = (gint)ellipse_select_smooth_val;
        g_snprintf(smooth_str, sizeof(smooth_str), "%d", smooth_val);
        settings_set_tool_option(ctx->settings, tool_name, "ellipse_select_smooth", smooth_str);

        gchar feather_str[32];
        gfloat safe_feather = (ellipse_select_feather_val >= 0.0f && ellipse_select_feather_val <= 200.0f) ? ellipse_select_feather_val : 0.0f;
        g_snprintf(feather_str, sizeof(feather_str), "%.2f", safe_feather);
        settings_set_tool_option(ctx->settings, tool_name, "ellipse_select_feather", feather_str);

        settings_set_tool_option(ctx->settings, tool_name, "ellipse_select_animate", ellipse_select_animate_val ? "true" : "false");
    }

    /* Magic wand select tool options */
    if (tool_type == TOOL_MAGIC_WAND) {
        ToolOptions* mw_opts = tool_options_get_for_tool(TOOL_MAGIC_WAND);
        if (mw_opts) {
            gchar v_str[32];
            g_snprintf(v_str, sizeof(v_str), "%d", (gint)mw_opts->magicwand_combine);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_combine", v_str);
            g_snprintf(v_str, sizeof(v_str), "%d", (gint)mw_opts->magicwand_smooth);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_smooth", v_str);
            g_snprintf(v_str, sizeof(v_str), "%.2f", mw_opts->magicwand_feather);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_feather", v_str);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_animate", mw_opts->magicwand_animate ? "true" : "false");
            g_snprintf(v_str, sizeof(v_str), "%.0f", (gdouble)mw_opts->magicwand_tolerance);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_tolerance", v_str);
            g_snprintf(v_str, sizeof(v_str), "%d", (gint)mw_opts->magicwand_compare_mode);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_compare_mode", v_str);
            settings_set_tool_option(ctx->settings, tool_name, "magicwand_contiguous", mw_opts->magicwand_contiguous ? "true" : "false");
        }
    }

    /* Crop tool options */
    if (tool_type == TOOL_CROP) {
        gchar overlay_str[16];
        g_snprintf(overlay_str, sizeof(overlay_str), "%d", crop_overlay_mode_val >= 0 && crop_overlay_mode_val <= 5 ? crop_overlay_mode_val : 0);
        settings_set_tool_option(ctx->settings, tool_name, "crop_overlay_mode", overlay_str);
        settings_set_tool_option(ctx->settings, tool_name, "crop_delete_pixels", crop_delete_pixels_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "crop_grow_canvas", crop_grow_canvas_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "crop_darken_outside", crop_darken_outside_val ? "true" : "false");
        gchar opacity_str[32];
        gfloat safe_opacity = (crop_darken_opacity_val >= 0.0f && crop_darken_opacity_val <= 100.0f) ? crop_darken_opacity_val : 60.0f;
        g_snprintf(opacity_str, sizeof(opacity_str), "%.1f", safe_opacity);
        settings_set_tool_option(ctx->settings, tool_name, "crop_darken_opacity", opacity_str);
    }

    /* Save settings immediately if requested */
    if (save_immediately && ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }
}

/**
 * Save all tool options to settings (saves all tools at once)
 */
void ui_save_all_tool_options_to_settings(AppContext* ctx) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    /* Save options for each tool type (without saving to file after each one) */
    /* settings_set_tool_option will create the hash table if needed */
    /* Tool options should always be saved on app close, even if unchanged */
    for (int i = 0; i < TOOL_COUNT; i++) {
        ToolType tool_type = (ToolType)i;
        ui_save_tool_options_to_settings_internal(ctx, tool_type, FALSE);
    }
}
