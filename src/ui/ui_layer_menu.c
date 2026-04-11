/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/ui_layer_menu.h"
#include "i18n.h"
#include "commands/command_layer.h"
#include "commands/command_text_layer.h"
#include "document.h"
#include "render/layer.h"
#include "render/text_layer.h"
#include "tool_manager.h"
#include "tools/tool_text.h"
#include "ui.h"
#include "ui/dialogs/new_layer_dialog.h"
#include "ui/layers_panel.h"
#include <glib.h>
#include "debug_logger.h"

static void on_layer_visibility_show_current_toggled(GtkCheckMenuItem* check_item, gpointer data);

/**
 * Layer selection changed callback - proper signal handler signature
 */
void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    (void)selection; /* Unused - we get it from the tree view */

    AppContext* ctx = (AppContext*)user_data;

    if (!ctx) {
        return;
    }

    /* Get the currently active document */
    ImageDocument* active_doc = ui_get_active_document(ctx);
    if (active_doc && ctx->layers_panel) {
        /* Set the selected layer in the document so tools use the right layer */
        ImageLayer* selected_layer = layers_panel_get_selected_layer(ctx->layers_panel);
        document_set_selected_layer(active_doc, selected_layer);

        /* Update opacity controls to reflect selected layer */
        layers_panel_update_opacity_controls(ctx->layers_panel);
    }

    /* Update menu and button states */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Create the Layer menu
 */
GtkWidget* create_layer_menu(AppContext* ctx) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* menu_item;

    /* Layer > New Layer */
    menu_item = gtk_menu_item_new_with_mnemonic(_("_New Layer"));
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_new), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_new = menu_item;

    /* Layer > Duplicate Layer */
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Duplicate Layer"));
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_duplicate = menu_item;

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Layer > Delete Layer */
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Delete Layer"));
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_delete), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_delete = menu_item;

    gtk_widget_show_all(menu);

    return menu;
}

/**
 * Setup Layer menu from Glade builder
 */
void ui_layer_menu_setup(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* layer_menu = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu"));
    GtkWidget* layer_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_item"));

    if (layer_menu && layer_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(layer_menu_item), layer_menu);
    }

    /* Get menu items that need to be updated programmatically */
    ctx->layer_menu_new = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_new"));
    ctx->layer_menu_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_duplicate"));
    ctx->layer_menu_delete = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_delete"));
    ctx->layer_menu_merge_up = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_merge_up"));
    ctx->layer_menu_merge_down = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_merge_down"));
    ctx->layer_menu_rasterize_text = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_rasterize_text"));

    /* Connect Layer menu signals */
    if (ctx->layer_menu_new) {
        g_signal_connect(ctx->layer_menu_new, "activate", G_CALLBACK(on_layer_new), ctx);
    }
    if (ctx->layer_menu_duplicate) {
        g_signal_connect(ctx->layer_menu_duplicate, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    }
    if (ctx->layer_menu_delete) {
        g_signal_connect(ctx->layer_menu_delete, "activate", G_CALLBACK(on_layer_delete), ctx);
    }
    if (ctx->layer_menu_merge_up) {
        g_signal_connect(ctx->layer_menu_merge_up, "activate", G_CALLBACK(on_layer_merge_up), ctx);
    }
    if (ctx->layer_menu_merge_down) {
        g_signal_connect(ctx->layer_menu_merge_down, "activate", G_CALLBACK(on_layer_merge_down), ctx);
    }
    if (ctx->layer_menu_rasterize_text) {
        g_signal_connect(ctx->layer_menu_rasterize_text, "activate",
                         G_CALLBACK(on_layer_rasterize_text), ctx);
    }

    /* Get and connect Layer > Order submenu items */
    ctx->layer_menu_order_select_top = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_select_top"));
    ctx->layer_menu_order_select_above = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_select_above"));
    ctx->layer_menu_order_select_below = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_select_below"));
    ctx->layer_menu_order_select_bottom = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_select_bottom"));
    ctx->layer_menu_order_move_top = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_move_top"));
    ctx->layer_menu_order_move_up = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_move_up"));
    ctx->layer_menu_order_move_down = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_move_down"));
    ctx->layer_menu_order_move_bottom = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order_move_bottom"));
    if (ctx->layer_menu_order_select_top)
        g_signal_connect(ctx->layer_menu_order_select_top, "activate", G_CALLBACK(on_layer_order_select_top), ctx);
    if (ctx->layer_menu_order_select_above)
        g_signal_connect(ctx->layer_menu_order_select_above, "activate", G_CALLBACK(on_layer_order_select_above), ctx);
    if (ctx->layer_menu_order_select_below)
        g_signal_connect(ctx->layer_menu_order_select_below, "activate", G_CALLBACK(on_layer_order_select_below), ctx);
    if (ctx->layer_menu_order_select_bottom)
        g_signal_connect(ctx->layer_menu_order_select_bottom, "activate", G_CALLBACK(on_layer_order_select_bottom), ctx);
    if (ctx->layer_menu_order_move_top)
        g_signal_connect(ctx->layer_menu_order_move_top, "activate", G_CALLBACK(on_layer_order_move_top), ctx);
    if (ctx->layer_menu_order_move_up)
        g_signal_connect(ctx->layer_menu_order_move_up, "activate", G_CALLBACK(on_layer_order_move_up), ctx);
    if (ctx->layer_menu_order_move_down)
        g_signal_connect(ctx->layer_menu_order_move_down, "activate", G_CALLBACK(on_layer_order_move_down), ctx);
    if (ctx->layer_menu_order_move_bottom)
        g_signal_connect(ctx->layer_menu_order_move_bottom, "activate", G_CALLBACK(on_layer_order_move_bottom), ctx);

    /* Get and connect Layer > Visibility submenu items */
    ctx->layer_menu_visibility_show_current = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_visibility_show_current"));
    ctx->layer_menu_visibility_show_only = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_visibility_show_only"));
    ctx->layer_menu_visibility_hide_only = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_visibility_hide_only"));
    ctx->layer_menu_visibility_show_all = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_visibility_show_all"));
    ctx->layer_menu_visibility_hide_all = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_visibility_hide_all"));
    if (ctx->layer_menu_visibility_show_current)
        g_signal_connect(ctx->layer_menu_visibility_show_current, "toggled", G_CALLBACK(on_layer_visibility_show_current_toggled), ctx);
    if (ctx->layer_menu_visibility_show_only)
        g_signal_connect(ctx->layer_menu_visibility_show_only, "activate", G_CALLBACK(on_layer_visibility_show_only), ctx);
    if (ctx->layer_menu_visibility_hide_only)
        g_signal_connect(ctx->layer_menu_visibility_hide_only, "activate", G_CALLBACK(on_layer_visibility_hide_only), ctx);
    if (ctx->layer_menu_visibility_show_all)
        g_signal_connect(ctx->layer_menu_visibility_show_all, "activate", G_CALLBACK(on_layer_visibility_show_all), ctx);
    if (ctx->layer_menu_visibility_hide_all)
        g_signal_connect(ctx->layer_menu_visibility_hide_all, "activate", G_CALLBACK(on_layer_visibility_hide_all), ctx);

    ctx->layer_menu_order = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_order"));
    ctx->layer_menu_visibility = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_visibility"));

    /* Layers panel context menu — same commands as Layer menu */
    ctx->layer_panel_context_menu = GTK_WIDGET(gtk_builder_get_object(builder, "layer_panel_context_menu"));
    ctx->layer_panel_context_visibility_show = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_show"));
    ctx->layer_panel_context_visibility_show_only = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_show1"));
    ctx->layer_panel_context_visibility_hide_only = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_hide"));
    ctx->layer_panel_context_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_duplicate"));
    ctx->layer_panel_context_delete = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_delete"));
    ctx->layer_panel_context_rasterize_text = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_rasterize"));
    ctx->layer_panel_context_merge_up = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_merge_up"));
    ctx->layer_panel_context_merge_down = GTK_WIDGET(gtk_builder_get_object(builder, "layer_context_menu_merge_down"));
    if (ctx->layer_panel_context_visibility_show) {
        g_signal_connect(ctx->layer_panel_context_visibility_show, "toggled",
                         G_CALLBACK(on_layer_visibility_show_current_toggled), ctx);
    }
    if (ctx->layer_panel_context_visibility_show_only) {
        g_signal_connect(ctx->layer_panel_context_visibility_show_only, "activate",
                         G_CALLBACK(on_layer_visibility_show_only), ctx);
    }
    if (ctx->layer_panel_context_visibility_hide_only) {
        g_signal_connect(ctx->layer_panel_context_visibility_hide_only, "activate",
                         G_CALLBACK(on_layer_visibility_hide_only), ctx);
    }
    if (ctx->layer_panel_context_duplicate) {
        g_signal_connect(ctx->layer_panel_context_duplicate, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    }
    if (ctx->layer_panel_context_delete) {
        g_signal_connect(ctx->layer_panel_context_delete, "activate", G_CALLBACK(on_layer_delete), ctx);
    }
    if (ctx->layer_panel_context_rasterize_text) {
        g_signal_connect(ctx->layer_panel_context_rasterize_text, "activate",
                         G_CALLBACK(on_layer_rasterize_text), ctx);
    }
    if (ctx->layer_panel_context_merge_up) {
        g_signal_connect(ctx->layer_panel_context_merge_up, "activate", G_CALLBACK(on_layer_merge_up), ctx);
    }
    if (ctx->layer_panel_context_merge_down) {
        g_signal_connect(ctx->layer_panel_context_merge_down, "activate", G_CALLBACK(on_layer_merge_down), ctx);
    }
}

static void sync_layer_panel_context_from_layer_menu(AppContext* ctx) {
#define SYNC_MENU_SENS(wmain, wctx) \
    do { \
        if ((wctx) && GTK_IS_WIDGET((wctx)) && (wmain) && GTK_IS_WIDGET((wmain))) \
            gtk_widget_set_sensitive((wctx), gtk_widget_get_sensitive((wmain))); \
    } while (0)

    SYNC_MENU_SENS(ctx->layer_menu_visibility_show_current, ctx->layer_panel_context_visibility_show);
    SYNC_MENU_SENS(ctx->layer_menu_visibility_show_only, ctx->layer_panel_context_visibility_show_only);
    SYNC_MENU_SENS(ctx->layer_menu_visibility_hide_only, ctx->layer_panel_context_visibility_hide_only);
    SYNC_MENU_SENS(ctx->layer_menu_duplicate, ctx->layer_panel_context_duplicate);
    SYNC_MENU_SENS(ctx->layer_menu_delete, ctx->layer_panel_context_delete);
    SYNC_MENU_SENS(ctx->layer_menu_rasterize_text, ctx->layer_panel_context_rasterize_text);
    SYNC_MENU_SENS(ctx->layer_menu_merge_up, ctx->layer_panel_context_merge_up);
    SYNC_MENU_SENS(ctx->layer_menu_merge_down, ctx->layer_panel_context_merge_down);
#undef SYNC_MENU_SENS
}

void ui_layer_menu_update_sensitivity(AppContext* ctx) {
    ImageDocument* doc;
    LayersPanel* layers_panel;
    gboolean has_document;
    gboolean has_selection;
    ImageLayer* selected_layer;
    guint layer_count;
    gboolean can_move_up;
    gboolean can_move_down;
    ImageLayer* top_layer;
    ImageLayer* bottom_layer;
    gboolean can_select_top;
    gboolean can_select_above;
    gboolean can_select_below;
    gboolean can_select_bottom;
    gboolean can_show_all;
    gboolean can_hide_all;
    gboolean visibility_has_selection;
    gboolean can_layer_submenus;

    if (!ctx || !ctx->window) {
        return;
    }

    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");

    has_selection = FALSE;
    selected_layer = NULL;
    if (layers_panel && doc && layers_panel->tree_view) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
        has_selection = (selected_layer != NULL);
    }

    if (ctx->layer_menu_new && GTK_IS_WIDGET(ctx->layer_menu_new)) {
        gtk_widget_set_sensitive(ctx->layer_menu_new, has_document);
    }
    if (ctx->layer_menu_delete && GTK_IS_WIDGET(ctx->layer_menu_delete)) {
        gtk_widget_set_sensitive(ctx->layer_menu_delete, has_document && has_selection);
    }
    if (ctx->layer_menu_duplicate && GTK_IS_WIDGET(ctx->layer_menu_duplicate)) {
        gtk_widget_set_sensitive(ctx->layer_menu_duplicate, has_document && has_selection);
    }
    if (ctx->layer_menu_merge_up && GTK_IS_WIDGET(ctx->layer_menu_merge_up)) {
        gboolean can_merge_up = has_document && has_selection && selected_layer &&
                                document_layer_can_move_up(doc, selected_layer);
        gtk_widget_set_sensitive(ctx->layer_menu_merge_up, can_merge_up);
    }
    if (ctx->layer_menu_merge_down && GTK_IS_WIDGET(ctx->layer_menu_merge_down)) {
        gboolean can_merge_down = has_document && has_selection && selected_layer &&
                                  document_layer_can_move_down(doc, selected_layer);
        gtk_widget_set_sensitive(ctx->layer_menu_merge_down, can_merge_down);
    }
    if (ctx->layer_menu_rasterize_text && GTK_IS_WIDGET(ctx->layer_menu_rasterize_text)) {
        gboolean can_rasterize = has_document && has_selection && selected_layer &&
                                 selected_layer->layer_type == LAYER_TYPE_TEXT;
        gtk_widget_set_sensitive(ctx->layer_menu_rasterize_text, can_rasterize);
    }

    layer_count = has_document && doc ? document_get_layer_count(doc) : 0;
    can_move_up = has_document && has_selection && selected_layer &&
                  document_layer_can_move_up(doc, selected_layer);
    can_move_down = has_document && has_selection && selected_layer &&
                    document_layer_can_move_down(doc, selected_layer);
    top_layer = (has_document && doc && layer_count > 0) ? document_get_layer(doc, layer_count - 1) : NULL;
    bottom_layer = (has_document && doc && layer_count > 0) ? document_get_layer(doc, 0) : NULL;
    can_select_top = has_document && layer_count > 0 && selected_layer &&
                     selected_layer != top_layer;
    can_select_above = can_move_up;
    can_select_below = can_move_down;
    can_select_bottom = has_document && layer_count > 0 && selected_layer &&
                        selected_layer != bottom_layer;

    if (ctx->layer_menu_order_select_top && GTK_IS_WIDGET(ctx->layer_menu_order_select_top)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_select_top, can_select_top);
    }
    if (ctx->layer_menu_order_select_above && GTK_IS_WIDGET(ctx->layer_menu_order_select_above)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_select_above, can_select_above);
    }
    if (ctx->layer_menu_order_select_below && GTK_IS_WIDGET(ctx->layer_menu_order_select_below)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_select_below, can_select_below);
    }
    if (ctx->layer_menu_order_select_bottom && GTK_IS_WIDGET(ctx->layer_menu_order_select_bottom)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_select_bottom, can_select_bottom);
    }
    if (ctx->layer_menu_order_move_top && GTK_IS_WIDGET(ctx->layer_menu_order_move_top)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_move_top, can_move_up);
    }
    if (ctx->layer_menu_order_move_up && GTK_IS_WIDGET(ctx->layer_menu_order_move_up)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_move_up, can_move_up);
    }
    if (ctx->layer_menu_order_move_down && GTK_IS_WIDGET(ctx->layer_menu_order_move_down)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_move_down, can_move_down);
    }
    if (ctx->layer_menu_order_move_bottom && GTK_IS_WIDGET(ctx->layer_menu_order_move_bottom)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order_move_bottom, can_move_down);
    }

    can_show_all = FALSE;
    can_hide_all = FALSE;
    if (has_document && doc && layer_count > 0) {
        gboolean any_hidden = FALSE;
        gboolean any_visible = FALSE;
        for (guint i = 0; i < layer_count; i++) {
            ImageLayer* l = document_get_layer(doc, i);
            if (l) {
                if (l->visible) {
                    any_visible = TRUE;
                } else {
                    any_hidden = TRUE;
                }
            }
        }
        can_show_all = any_hidden;
        can_hide_all = any_visible;
    }
    visibility_has_selection = has_document && has_selection;

    if (ctx->layer_menu_visibility_show_current && GTK_IS_WIDGET(ctx->layer_menu_visibility_show_current)) {
        gtk_widget_set_sensitive(ctx->layer_menu_visibility_show_current, visibility_has_selection);
    }
    if (ctx->layer_panel_context_visibility_show && GTK_IS_WIDGET(ctx->layer_panel_context_visibility_show)) {
        gtk_widget_set_sensitive(ctx->layer_panel_context_visibility_show, visibility_has_selection);
    }
    if (visibility_has_selection) {
        layer_visibility_update_check_state(ctx);
    }
    if (ctx->layer_menu_visibility_show_only && GTK_IS_WIDGET(ctx->layer_menu_visibility_show_only)) {
        gtk_widget_set_sensitive(ctx->layer_menu_visibility_show_only, visibility_has_selection);
    }
    if (ctx->layer_menu_visibility_hide_only && GTK_IS_WIDGET(ctx->layer_menu_visibility_hide_only)) {
        gtk_widget_set_sensitive(ctx->layer_menu_visibility_hide_only, visibility_has_selection);
    }
    if (ctx->layer_menu_visibility_show_all && GTK_IS_WIDGET(ctx->layer_menu_visibility_show_all)) {
        gtk_widget_set_sensitive(ctx->layer_menu_visibility_show_all, can_show_all);
    }
    if (ctx->layer_menu_visibility_hide_all && GTK_IS_WIDGET(ctx->layer_menu_visibility_hide_all)) {
        gtk_widget_set_sensitive(ctx->layer_menu_visibility_hide_all, can_hide_all);
    }

    can_layer_submenus = has_document && doc && layer_count > 0;
    if (ctx->layer_menu_order && GTK_IS_WIDGET(ctx->layer_menu_order)) {
        gtk_widget_set_sensitive(ctx->layer_menu_order, can_layer_submenus);
    }
    if (ctx->layer_menu_visibility && GTK_IS_WIDGET(ctx->layer_menu_visibility)) {
        gtk_widget_set_sensitive(ctx->layer_menu_visibility, can_layer_submenus);
    }

    sync_layer_panel_context_from_layer_menu(ctx);
}

/**
 * Layer > New Layer callback
 */
void on_layer_new(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;
    NewLayerDialog* dialog;
    NewLayerDialogResult* result;
    gint response;
    ImageLayer* new_layer;
    const gdouble* custom_color = NULL;

    if (!doc) {
        debug_log("WRN", "No document open");
        return;
    }

    /* Create and show new layer dialog */
    dialog = new_layer_dialog_new();
    if (!dialog) {
        debug_log("WRN", "Failed to create new layer dialog");
        return;
    }

    response = new_layer_dialog_run(dialog, GTK_WINDOW(ctx->window), &result);

    if (response == GTK_RESPONSE_OK && result) {
        /* Get custom color if needed */
        if (result->background == LAYER_BACKGROUND_CUSTOM) {
            custom_color = result->custom_color;
        }

        /* Create new layer with dialog parameters */
        new_layer = document_add_layer(doc, result->name,
                                       result->background,
                                       result->position,
                                       custom_color);

        if (new_layer) {
            /* Set as active layer if requested */
            if (result->set_active) {
                document_set_selected_layer(doc, new_layer);
            }

            /* Create undo command */
            cmd = command_create_layer_add(doc, new_layer);
            if (cmd) {
                document_push_undo_command(doc, cmd);
            }

            /* Update layers panel */
            if (layers_panel) {
                layers_panel_update(layers_panel, doc);

                /* Select the new layer if requested */
                if (result->set_active) {
                    layers_panel_select_layer(layers_panel, doc, new_layer);
                }
            }

            /* Update UI state */
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx, NULL);
            doc->modified = TRUE;
        }

        /* Free dialog result */
        new_layer_dialog_result_free(result);
    }

    /* Free dialog */
    new_layer_dialog_free(dialog);
}

/**
 * Layer > Delete Layer callback
 */
void on_layer_delete(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        debug_log("WRN", "No document or layers");
        return;
    }

    /* Delete the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        debug_log("WRN", "No layer selected");
        return;
    }

    /* Create undo command before deleting */
    cmd = command_create_layer_delete(doc, selected_layer);
    if (!cmd) {
        debug_log("WRN", "Failed to create delete layer command");
        return;
    }

    /* Execute the delete (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    document_push_undo_command(doc, cmd);

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Duplicate Layer callback
 */
void on_layer_duplicate(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        debug_log("WRN", "No document or layers");
        return;
    }

    /* Duplicate the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        debug_log("WRN", "No layer selected");
        return;
    }

    static int dup_count = 1;
    gchar* layer_name = g_strdup_printf("%s copy %d", selected_layer->name, dup_count++);

    ImageLayer* dup_layer = document_duplicate_layer(doc, selected_layer, layer_name);
    g_free(layer_name);

    if (dup_layer) {
        /* Create undo command */
        cmd = command_create_layer_duplicate(doc, selected_layer, dup_layer);
        if (cmd) {
            document_push_undo_command(doc, cmd);
        } else if (cmd) {
            command_free(cmd);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
            /* Select the duplicated layer */
            document_set_selected_layer(doc, dup_layer);
            layers_panel_select_layer(layers_panel, doc, dup_layer);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        doc->modified = TRUE;
    }
}

/**
 * Layer > Merge Up callback
 */
void on_layer_merge_up(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        debug_log("WRN", "No document or layers");
        return;
    }

    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        debug_log("WRN", "No layer selected");
        return;
    }

    cmd = command_create_layer_merge_up(doc, selected_layer);
    if (!cmd) {
        return; /* Can't merge up (no layer above) */
    }

    command_execute(cmd, doc);

    document_push_undo_command(doc, cmd);

    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
        layers_panel_select_layer(layers_panel, doc, doc->selected_layer);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Merge Down callback
 */
void on_layer_merge_down(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        debug_log("WRN", "No document or layers");
        return;
    }

    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        debug_log("WRN", "No layer selected");
        return;
    }

    cmd = command_create_layer_merge_down(doc, selected_layer);
    if (!cmd) {
        return; /* Can't merge down (no layer below) */
    }

    command_execute(cmd, doc);

    document_push_undo_command(doc, cmd);

    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
        layers_panel_select_layer(layers_panel, doc, doc->selected_layer);
    }

    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Move Up callback
 */
void on_layer_move_up(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        debug_log("WRN", "No document or layers");
        return;
    }

    /* Get the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        debug_log("WRN", "No layer selected");
        return;
    }

    /* Create undo command before moving */
    cmd = command_create_layer_move_up(doc, selected_layer);
    if (!cmd) {
        return; /* Can't move up */
    }

    /* Execute the move (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    document_push_undo_command(doc, cmd);

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Move Down callback
 */
void on_layer_move_down(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        debug_log("WRN", "No document or layers");
        return;
    }

    /* Get the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        debug_log("WRN", "No layer selected");
        return;
    }

    /* Create undo command before moving */
    cmd = command_create_layer_move_down(doc, selected_layer);
    if (!cmd) {
        return; /* Can't move down */
    }

    /* Execute the move (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    document_push_undo_command(doc, cmd);

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Order > Select top layer
 */
void on_layer_order_select_top(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (!doc || !layers_panel)
        return;
    guint count = document_get_layer_count(doc);
    if (count == 0)
        return;
    ImageLayer* top = document_get_layer(doc, count - 1);
    if (top) {
        document_set_selected_layer(doc, top);
        layers_panel_select_layer(layers_panel, doc, top);
        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Layer > Order > Select layer above
 */
void on_layer_order_select_above(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (!doc || !layers_panel)
        return;
    ImageLayer* selected = layers_panel_get_selected_layer(layers_panel);
    if (!selected)
        return;
    GList* iter = g_list_find(doc->layers, selected);
    if (!iter || !iter->next)
        return;
    ImageLayer* above = (ImageLayer*)iter->next->data;
    document_set_selected_layer(doc, above);
    layers_panel_select_layer(layers_panel, doc, above);
    ui_update_menu_and_button_states(ctx);
}

/**
 * Layer > Order > Select layer below
 */
void on_layer_order_select_below(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (!doc || !layers_panel)
        return;
    ImageLayer* selected = layers_panel_get_selected_layer(layers_panel);
    if (!selected)
        return;
    GList* iter = g_list_find(doc->layers, selected);
    if (!iter || !iter->prev)
        return;
    ImageLayer* below = (ImageLayer*)iter->prev->data;
    document_set_selected_layer(doc, below);
    layers_panel_select_layer(layers_panel, doc, below);
    ui_update_menu_and_button_states(ctx);
}

/**
 * Layer > Order > Select bottom layer
 */
void on_layer_order_select_bottom(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (!doc || !layers_panel)
        return;
    guint count = document_get_layer_count(doc);
    if (count == 0)
        return;
    ImageLayer* bottom = document_get_layer(doc, 0);
    if (bottom) {
        document_set_selected_layer(doc, bottom);
        layers_panel_select_layer(layers_panel, doc, bottom);
        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Layer > Order > Move layer to top
 */
void on_layer_order_move_top(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    Command* cmd;
    ImageLayer* selected;
    if (!doc || !doc->layers)
        return;
    selected = layers_panel ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!selected)
        return;
    cmd = command_create_layer_move_to_top(doc, selected);
    if (!cmd)
        return;
    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Order > Move layer up (delegates to on_layer_move_up)
 */
void on_layer_order_move_up(GtkWidget* widget, gpointer data) {
    on_layer_move_up(widget, data);
}

/**
 * Layer > Order > Move layer down (delegates to on_layer_move_down)
 */
void on_layer_order_move_down(GtkWidget* widget, gpointer data) {
    on_layer_move_down(widget, data);
}

/**
 * Layer > Order > Move layer to bottom
 */
void on_layer_order_move_bottom(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    Command* cmd;
    ImageLayer* selected;
    if (!doc || !doc->layers)
        return;
    selected = layers_panel ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!selected)
        return;
    cmd = command_create_layer_move_to_bottom(doc, selected);
    if (!cmd)
        return;
    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/* Flag to skip toggle when we programmatically update check state */
static gboolean visibility_check_updating = FALSE;

static void visibility_sync_one_check(GtkWidget* widget, AppContext* ctx, gboolean visible) {
    GtkCheckMenuItem* check_item;

    if (!widget || !GTK_IS_CHECK_MENU_ITEM(widget))
        return;
    check_item = GTK_CHECK_MENU_ITEM(widget);
    if (gtk_check_menu_item_get_active(check_item) == visible)
        return;

    visibility_check_updating = TRUE;
    g_signal_handlers_block_by_func(check_item, G_CALLBACK(on_layer_visibility_show_current_toggled), ctx);
    gtk_check_menu_item_set_active(check_item, visible);
    g_signal_handlers_unblock_by_func(check_item, G_CALLBACK(on_layer_visibility_show_current_toggled), ctx);
    visibility_check_updating = FALSE;
}

/**
 * Update "Show this layer" check state to match selected layer visibility.
 * Blocks toggled handler to avoid triggering toggle when we call set_active.
 */
void layer_visibility_update_check_state(AppContext* ctx) {
    ImageDocument* doc;
    LayersPanel* layers_panel;
    ImageLayer* selected;

    if (!ctx)
        return;
    if (!ctx->layer_menu_visibility_show_current && !ctx->layer_panel_context_visibility_show)
        return;

    doc = ui_get_active_document(ctx);
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    selected = (layers_panel && doc) ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!selected)
        return;

    visibility_sync_one_check(ctx->layer_menu_visibility_show_current, ctx, selected->visible);
    visibility_sync_one_check(ctx->layer_panel_context_visibility_show, ctx, selected->visible);
}

/**
 * Toggled when user changes the "Show this layer" check - toggle layer visibility
 */
static void on_layer_visibility_show_current_toggled(GtkCheckMenuItem* check_item, gpointer data) {
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    LayersPanel* layers_panel;
    ImageLayer* selected;

    if (visibility_check_updating)
        return; /* Programmatic update, ignore */

    doc = ui_get_active_document(ctx);
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    selected = layers_panel ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!doc || !selected)
        return;

    layer_visibility_toggle_execute(ctx, doc, selected);
}

/**
 * Toggle layer visibility - shared by layers panel and Layer menu
 */
void layer_visibility_toggle_execute(AppContext* ctx, ImageDocument* doc, ImageLayer* layer) {
    Command* cmd;
    LayersPanel* layers_panel;

    if (!ctx || !doc || !layer)
        return;

    cmd = command_create_layer_visibility_toggle(doc, layer);
    if (!cmd)
        return;

    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);

    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    doc->modified = TRUE;
}

/**
 * Layer > Visibility > Show this layer (toggle)
 */
void on_layer_visibility_show_current(GtkWidget* widget, gpointer data) {
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    ImageLayer* selected;

    (void)widget;
    if (!doc || !doc->layers)
        return;
    selected = layers_panel ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!selected)
        return;

    layer_visibility_toggle_execute(ctx, doc, selected);
}

/**
 * Layer > Visibility > Show only this layer
 */
void on_layer_visibility_show_only(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    Command* cmd;
    ImageLayer* selected;
    if (!doc || !doc->layers)
        return;
    selected = layers_panel ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!selected)
        return;
    cmd = command_create_layer_visibility_show_only(doc, selected);
    if (!cmd)
        return;
    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    doc->modified = TRUE;
}

/**
 * Layer > Visibility > Hide only this layer
 */
void on_layer_visibility_hide_only(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    Command* cmd;
    ImageLayer* selected;
    if (!doc || !doc->layers)
        return;
    selected = layers_panel ? layers_panel_get_selected_layer(layers_panel) : NULL;
    if (!selected)
        return;
    cmd = command_create_layer_visibility_hide_only(doc, selected);
    if (!cmd)
        return;
    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    doc->modified = TRUE;
}

/**
 * Layer > Visibility > Show all layers
 */
void on_layer_visibility_show_all(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    Command* cmd;
    if (!doc || !doc->layers)
        return;
    cmd = command_create_layer_visibility_show_all(doc);
    if (!cmd)
        return;
    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    doc->modified = TRUE;
}

/**
 * Layer > Visibility > Hide all layers
 */
void on_layer_visibility_hide_all(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");
    Command* cmd;
    if (!doc || !doc->layers)
        return;
    cmd = command_create_layer_visibility_hide_all(doc);
    if (!cmd)
        return;
    command_execute(cmd, doc);
    document_push_undo_command(doc, cmd);
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    doc->modified = TRUE;
}

void on_layer_rasterize_text(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel =
        (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window), "layers_panel");

    if (!doc)
        return;

    ImageLayer* layer = NULL;
    if (layers_panel)
        layer = layers_panel_get_selected_layer(layers_panel);
    if (!layer)
        layer = document_get_selected_layer(doc);

    if (!layer || layer->layer_type != LAYER_TYPE_TEXT) {
        debug_log("WRN", "Rasterize Text: no text layer selected");
        return;
    }

    /* Always confirm before rasterizing 
     *
     * Rasterizing permanently converts a vector text layer to pixels.
     * When the text tool is mid-edit the secondary message also mentions
     * that the current edits will be committed.
     * ----------------------------------------------------------------------- */
    Tool* active_tool = ctx->tool_registry
                            ? tool_manager_get_active(ctx->tool_registry)
                            : NULL;
    gboolean editing_this_layer = FALSE;
    if (active_tool && tool_text_is_editing(active_tool) && active_tool->user_data) {
        TextToolState* ts = (TextToolState*)active_tool->user_data;
        editing_this_layer = (ts->layer == layer);
    }

    {
        const gchar* secondary =
            editing_this_layer
                ? "This layer is currently being edited. "
                  "Rasterizing will commit the current text and "
                  "permanently convert it to pixels — it will no longer "
                  "be editable as a text layer.\n\n"
                  "This action can be undone."
                : "The text layer will be permanently converted to pixels "
                  "and will no longer be editable as a text layer.\n\n"
                  "This action can be undone.";

        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CANCEL,
            "Rasterize text layer?");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Rasterize", GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);

        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (response != GTK_RESPONSE_ACCEPT)
            return;
    }

    /* If mid-edit, commit the editing session before rasterizing so the
     * text edits land as a discrete "Edit Text" undo entry first. */
    if (editing_this_layer)
        tool_text_reset(active_tool);

    /* Rasterize
     *
     * command_create_text_layer_rasterize:
     *   1. Renders the text onto the layer's existing surface (preserving
     *      layer size, position via offset_x/y, and rotation baked in).
     *   2. Snapshots the result for redo.
     *   3. Frees text_data and changes layer_type to LAYER_TYPE_RASTER.
     *
     * The returned Command encapsulates undo (restore text_data) and
     * redo (re-apply raster snapshot).
     * ----------------------------------------------------------------------- */
    Command* cmd = command_create_text_layer_rasterize(doc, layer);
    if (!cmd)
        return;

    document_push_undo_command(doc, cmd);

    /* Clean up text-tool overlay state
     *
     * If the text tool still tracks this layer (selected but not editing),
     * clear its overlay so handles don't appear over a raster layer.
     * ----------------------------------------------------------------------- */
    if (active_tool && active_tool->type == TOOL_TEXT && active_tool->user_data) {
        TextToolState* ts = (TextToolState*)active_tool->user_data;
        if (ts->layer == layer) {
            ts->has_box = FALSE;
            ts->layer = NULL;
        }
    }

    doc->modified = TRUE;
    if (layers_panel)
        layers_panel_update(layers_panel, doc);
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    if (doc->drawing_area)
        gtk_widget_queue_draw(doc->drawing_area);
    if (doc->viewport)
        gtk_widget_queue_draw(doc->viewport);
}
