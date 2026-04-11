/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/ui_select_menu.h"
#include "i18n.h"
#include "command.h"
#include "document.h"
#include "selection/selection_mask.h"
#include "selection/selection_undo.h"
#include "ui.h"
#include "ui/dialogs/selection_radius_dialog.h"
#include <glib.h>
#include <stdint.h>
#include "debug_logger.h"

/* Forward declarations */
static gboolean pulse_selection_progress_bar(gpointer user_data);
static gboolean selection_operation_progress_callback(gint current, gint total, gpointer user_data);
static void execute_select_radius_operation(AppContext* ctx,
                                            const gchar* operation_name,
                                            gboolean (*operation_func)(SelectionMask*, gint,
                                                                       SelectionOperationProgressCallback,
                                                                       gpointer));

/**
 * Callback for Select All menu item
 */
void on_select_all(GtkMenuItem* menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = ui_get_active_document(ctx);

    (void)menu_item; /* Unused parameter */

    if (!doc || !doc->selection_mask) {
        return;
    }

    /* Begin transaction, perform operation, commit */
    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, command_get_name_string(CMD_NAME_SELECT_ALL));
    if (!transaction) {
        return;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0,
                                               doc->selection_mask->width,
                                               doc->selection_mask->height);

    /* Perform select all operation - fill entire mask using direct_modify flag */
    selection_mask_fill_rect(doc->selection_mask, 0, 0,
                             doc->selection_mask->width,
                             doc->selection_mask->height,
                             SELECTION_COMBINE_NEW,
                             SELECTION_SMOOTH_NONE,
                             0.0f,
                             TRUE); /* TRUE = direct modify (no Selection objects, no tool commands) */

    /* Ensure mask->data is set correctly after the operation */
    if (doc->selection_mask) {
        if (!doc->selection_mask->data && doc->selection_mask->base_mask) {
            doc->selection_mask->data = doc->selection_mask->base_mask;
        }
    }

    /* Commit transaction and get command */
    Command* cmd = selection_undo_transaction_commit(transaction);
    if (cmd) {
        document_push_undo_command(doc, cmd);
        command_execute(cmd, doc);
        document_invalidate_composite(doc);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        /* Update menu to show the new undo command */
        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Callback for Deselect All (Select None) menu item
 */
void on_select_none(GtkMenuItem* menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = ui_get_active_document(ctx);

    (void)menu_item; /* Unused parameter */

    if (!doc || !doc->selection_mask) {
        return;
    }

    /* Begin transaction, perform operation, commit */
    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, command_get_name_string(CMD_NAME_DESELECT_ALL));
    if (!transaction) {
        return;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0,
                                               doc->selection_mask->width,
                                               doc->selection_mask->height);

    /* Perform deselect all operation - directly clear base_mask */
    int stride = doc->selection_mask->stride;
    int width = doc->selection_mask->width;
    int height = doc->selection_mask->height;
    uint8_t* base_mask = doc->selection_mask->base_mask;

    /* Clear entire base_mask */
    for (int y = 0; y < height; y++) {
        uint8_t* row = base_mask + y * stride;
        for (int x = 0; x < width; x++) {
            row[x] = 0;
        }
    }

    /* Clear selections list since we're modifying base_mask directly */
    if (doc->selection_mask->selections) {
        GList* iter;
        for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(doc->selection_mask->selections);
        doc->selection_mask->selections = NULL;
    }

    /* Set data pointer to base_mask (no feathering) */
    doc->selection_mask->data = doc->selection_mask->base_mask;

    /* Mark mask as dirty */
    selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
    doc->selection_mask->feather_dirty = TRUE;

    /* Commit transaction and get command */
    Command* cmd = selection_undo_transaction_commit(transaction);
    if (cmd) {
        document_push_undo_command(doc, cmd);
        command_execute(cmd, doc);
        document_invalidate_composite(doc);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        /* Update menu to show the new undo command */
        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Callback for Invert Selection menu item
 */
void on_select_invert(GtkMenuItem* menu_item, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = ui_get_active_document(ctx);

    (void)menu_item; /* Unused parameter */

    if (!doc || !doc->selection_mask || !doc->selection_mask->base_mask) {
        return;
    }

    /* Begin transaction, perform operation, commit */
    SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
        doc->selection_mask, doc, command_get_name_string(CMD_NAME_INVERT_SELECTION));
    if (!transaction) {
        return;
    }

    /* Register entire mask as modified */
    selection_undo_transaction_register_region(transaction, 0, 0,
                                               doc->selection_mask->width,
                                               doc->selection_mask->height);

    /* Check if there's any feathering in existing selections before clearing them */
    gboolean has_feathering = FALSE;
    SelectionSmoothingMode preserved_feather_mode = SELECTION_SMOOTH_NONE;
    float preserved_feather_radius = 0.0f;
    if (doc->selection_mask->selections) {
        GList* iter;
        for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel && sel->feather_mode == SELECTION_SMOOTH_FEATHERED && sel->feather_radius > 0.0f) {
                has_feathering = TRUE;
                preserved_feather_mode = sel->feather_mode;
                preserved_feather_radius = sel->feather_radius;
                break; /* Use first feathered selection's parameters */
            }
        }
    }

    int stride = doc->selection_mask->stride;
    int width = doc->selection_mask->width;
    int height = doc->selection_mask->height;

    /* For Select Invert with feathered selections, we need to invert the base_mask first,
     * then regenerate feathering from the inverted base_mask. This ensures the feathering
     * gradient is calculated from the correct direction (outside edge inward, not inside edge outward). */

    /* Invert the base_mask */
    uint8_t* inverted_base_mask = g_malloc0(stride * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            inverted_base_mask[y * stride + x] = 255 - doc->selection_mask->base_mask[y * stride + x];
        }
    }

    /* Clear existing selections list */
    if (doc->selection_mask->selections) {
        GList* iter;
        for (iter = doc->selection_mask->selections; iter != NULL; iter = iter->next) {
            Selection* sel = (Selection*)iter->data;
            if (sel) {
                selection_unref(sel);
            }
        }
        g_list_free(doc->selection_mask->selections);
        doc->selection_mask->selections = NULL;
    }

    /* Create a Selection object to represent the inverted selection */
    /* Preserve feathering parameters if they existed */
    Selection* sel = selection_new(0, 0, width, height,
                                   SELECTION_COMBINE_NEW,
                                   preserved_feather_mode,
                                   preserved_feather_radius);
    if (sel) {
        /* Allocate mask for this selection (use same stride as main mask) */
        sel->mask = g_malloc0(stride * height);

        /* Copy the inverted base_mask to the selection's mask */
        /* Note: base_mask will be rebuilt by selection_mask_add_selection -> selection_mask_rebuild_from_selections */
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                sel->mask[y * stride + x] = inverted_base_mask[y * stride + x];
            }
        }

        /* If feathering is active, mark the selection as dirty so feathering will be
         * regenerated from the inverted base_mask. This ensures the feathering gradient
         * is calculated from the correct direction (outside edge inward for the inverted selection). */
        if (has_feathering) {
            /* Clear any existing feathered_preview to force regeneration */
            if (sel->feathered_preview) {
                g_free(sel->feathered_preview);
                sel->feathered_preview = NULL;
            }
            sel->feather_dirty = TRUE; /* Force regeneration from inverted base_mask */
        }

        g_free(inverted_base_mask);
        inverted_base_mask = NULL;

        /* Add selection to list */
        selection_mask_add_selection(doc->selection_mask, sel);
        selection_unref(sel); /* Release our reference (list now owns it) */
    } else {
        /* Failed to create selection, free the inverted base mask */
        if (inverted_base_mask) {
            g_free(inverted_base_mask);
        }
    }

    /* Regenerate combined feathered preview if feathering is active */
    /* For inverted selections with feathering, this will regenerate feathering from the
     * inverted base_mask, ensuring the gradient is calculated from the correct direction
     * (outside edge inward for the inverted selection). */
    if (has_feathering) {
        selection_mask_regenerate_combined_feather_preview(doc->selection_mask);
    } else {
        doc->selection_mask->data = doc->selection_mask->base_mask;
    }

    /* Mark mask as dirty */
    selection_mask_mark_dirty(doc->selection_mask, 0, 0, width, height);
    doc->selection_mask->feather_dirty = TRUE;

    /* Commit transaction and get command */
    Command* cmd = selection_undo_transaction_commit(transaction);
    if (cmd) {
        document_push_undo_command(doc, cmd);
        command_execute(cmd, doc);
        document_invalidate_composite(doc);
        if (doc->drawing_area) {
            gtk_widget_queue_draw(doc->drawing_area);
        }
        /* Update menu to show the new undo command */
        ui_update_menu_and_button_states(ctx);
    }
}

void ui_select_menu_update_sensitivity(AppContext* ctx) {
    ImageDocument* doc;
    gboolean has_mask;
    gboolean has_nonempty;
    gboolean can_invert;

    if (!ctx || !ctx->window) {
        return;
    }

    doc = ui_get_active_document(ctx);
    has_mask = (doc != NULL && doc->selection_mask != NULL);
    has_nonempty = has_mask && !selection_mask_is_empty(doc->selection_mask);
    can_invert = has_mask && doc->selection_mask->base_mask != NULL;

    if (ctx->select_menu_all && GTK_IS_WIDGET(ctx->select_menu_all)) {
        gtk_widget_set_sensitive(ctx->select_menu_all, has_mask);
    }
    if (ctx->select_menu_none && GTK_IS_WIDGET(ctx->select_menu_none)) {
        gtk_widget_set_sensitive(ctx->select_menu_none, has_nonempty);
    }
    if (ctx->select_menu_invert && GTK_IS_WIDGET(ctx->select_menu_invert)) {
        gtk_widget_set_sensitive(ctx->select_menu_invert, can_invert);
    }
    if (ctx->select_menu_grow && GTK_IS_WIDGET(ctx->select_menu_grow)) {
        gtk_widget_set_sensitive(ctx->select_menu_grow, has_nonempty);
    }
    if (ctx->select_menu_shrink && GTK_IS_WIDGET(ctx->select_menu_shrink)) {
        gtk_widget_set_sensitive(ctx->select_menu_shrink, has_nonempty);
    }
    if (ctx->select_menu_border && GTK_IS_WIDGET(ctx->select_menu_border)) {
        gtk_widget_set_sensitive(ctx->select_menu_border, has_nonempty);
    }
    if (ctx->select_menu_feather && GTK_IS_WIDGET(ctx->select_menu_feather)) {
        gtk_widget_set_sensitive(ctx->select_menu_feather, has_nonempty);
    }
    if (ctx->select_menu_sharpen && GTK_IS_WIDGET(ctx->select_menu_sharpen)) {
        gtk_widget_set_sensitive(ctx->select_menu_sharpen, has_nonempty);
    }
}

/**
 * Timeout callback to pulse progress bar
 */
static gboolean pulse_selection_progress_bar(gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (ctx) {
        ui_update_progress(ctx);
    }
    return G_SOURCE_CONTINUE;
}

/**
 * Progress callback for selection operations
 */
static gboolean selection_operation_progress_callback(gint current, gint total, gpointer user_data) {
    AppContext* ctx = (AppContext*)user_data;

    if (!ctx) {
        return TRUE; /* Continue */
    }

    /* Update progress bar */
    ui_update_progress(ctx);

    return TRUE; /* Continue */
}

/**
 * Execute a selection radius operation (grow, shrink, border, feather, sharpen)
 */
static void execute_select_radius_operation(AppContext* ctx,
                                            const gchar* operation_name,
                                            gboolean (*operation_func)(SelectionMask*, gint,
                                                                       SelectionOperationProgressCallback,
                                                                       gpointer)) {
    ImageDocument* doc = ui_get_active_document(ctx);
    SelectionRadiusDialog* dialog;
    gint radius;
    gint response;

    if (!doc || !doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        debug_log("WRN", "No selection to modify");
        return;
    }

    /* Create dialog */
    dialog = selection_radius_dialog_new(operation_name);
    if (!dialog) {
        debug_log("WRN", "Failed to create %s dialog", operation_name);
        return;
    }

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(selection_radius_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = selection_radius_dialog_run(dialog, GTK_WINDOW(ctx->window), &radius);

    if (response == GTK_RESPONSE_OK) {
        /* Begin transaction for undo/redo */
        SelectionUndoTransaction* transaction = selection_undo_transaction_begin(
            doc->selection_mask, doc, operation_name);
        if (transaction) {
            /* Register entire mask as modified */
            selection_undo_transaction_register_region(transaction, 0, 0,
                                                       doc->selection_mask->width,
                                                       doc->selection_mask->height);

            /* Show progress bar with message */
            gchar* progress_message = g_strdup_printf(_("Processing %s..."),
                                                      operation_name ? operation_name : _("selection"));
            ui_show_progress(ctx, progress_message);
            g_free(progress_message);

            /* Start pulsing the progress bar periodically (every 50ms) */
            guint pulse_timeout_id = g_timeout_add(50, pulse_selection_progress_bar, ctx);

            /* Apply operation with progress callback */
            gboolean success = operation_func(doc->selection_mask, radius,
                                              selection_operation_progress_callback,
                                              ctx);

            /* Remove pulse timeout */
            if (pulse_timeout_id > 0) {
                g_source_remove(pulse_timeout_id);
            }

            /* Hide progress bar */
            ui_hide_progress(ctx);

            if (success) {
                /* Commit transaction and get command */
                Command* cmd = selection_undo_transaction_commit(transaction);
                if (cmd) {
                    document_push_undo_command(doc, cmd);
                }

                /* Mark document as modified */
                doc->modified = TRUE;

                /* Invalidate document for redraw */
                document_invalidate_composite(doc);

                /* Update window title and menu states */
                ui_update_window_title(ctx, NULL);
                ui_update_menu_and_button_states(ctx);
            } else {
                /* Operation failed, cancel transaction */
                selection_undo_transaction_cancel(transaction);
            }
        }
    }

    /* Clean up */
    selection_radius_dialog_free(dialog);
}

/**
 * Callback for Grow Selection menu item
 */
void on_select_grow(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, _("Grow Selection"), selection_mask_grow);
}

/**
 * Callback for Shrink Selection menu item
 */
void on_select_shrink(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, _("Shrink Selection"), selection_mask_shrink);
}

/**
 * Callback for Border Selection menu item
 */
void on_select_border(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, _("Border Selection"), selection_mask_border);
}

/**
 * Callback for Feather Selection menu item
 */
void on_select_feather(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, _("Feather Selection"), selection_mask_feather);
}

/**
 * Callback for Sharpen Selection menu item
 */
void on_select_sharpen(GtkMenuItem* menu_item, gpointer user_data) {
    (void)menu_item; /* Unused parameter */
    execute_select_radius_operation((AppContext*)user_data, _("Sharpen Selection"), selection_mask_sharpen);
}

/**
 * Setup Select menu from Glade builder
 */
void ui_select_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* select_menu = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu"));
    GtkWidget* select_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_item"));

    if (select_menu && select_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(select_menu_item), select_menu);
    }

    ctx->select_menu_all = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_all"));
    ctx->select_menu_none = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_none"));
    ctx->select_menu_invert = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_invert"));
    ctx->select_menu_grow = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_grow"));
    ctx->select_menu_shrink = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_shrink"));
    ctx->select_menu_border = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_border"));
    ctx->select_menu_feather = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_feather"));
    ctx->select_menu_sharpen = GTK_WIDGET(gtk_builder_get_object(builder, "select_menu_sharpen"));

    /* Connect Select menu signals */
    if (ctx->select_menu_all) {
        g_signal_connect(ctx->select_menu_all, "activate", G_CALLBACK(on_select_all), ctx);
        /* Add accelerator (Ctrl+A) */
        gtk_widget_add_accelerator(ctx->select_menu_all, "activate", accel_group,
                                   GDK_KEY_a, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    if (ctx->select_menu_none) {
        g_signal_connect(ctx->select_menu_none, "activate", G_CALLBACK(on_select_none), ctx);
        /* Add accelerator (Ctrl+D) */
        gtk_widget_add_accelerator(ctx->select_menu_none, "activate", accel_group,
                                   GDK_KEY_d, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    if (ctx->select_menu_invert) {
        g_signal_connect(ctx->select_menu_invert, "activate", G_CALLBACK(on_select_invert), ctx);
        /* Add accelerator (Ctrl+I) if not already set in Glade */
        gtk_widget_add_accelerator(ctx->select_menu_invert, "activate", accel_group,
                                   GDK_KEY_i, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    }
    if (ctx->select_menu_grow) {
        g_signal_connect(ctx->select_menu_grow, "activate", G_CALLBACK(on_select_grow), ctx);
    }
    if (ctx->select_menu_shrink) {
        g_signal_connect(ctx->select_menu_shrink, "activate", G_CALLBACK(on_select_shrink), ctx);
    }
    if (ctx->select_menu_border) {
        g_signal_connect(ctx->select_menu_border, "activate", G_CALLBACK(on_select_border), ctx);
    }
    if (ctx->select_menu_feather) {
        g_signal_connect(ctx->select_menu_feather, "activate", G_CALLBACK(on_select_feather), ctx);
    }
    if (ctx->select_menu_sharpen) {
        g_signal_connect(ctx->select_menu_sharpen, "activate", G_CALLBACK(on_select_sharpen), ctx);
    }
}
