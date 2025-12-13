#include "ui/ui_filter.h"
#include "command.h"
#include "filters.h"
#include "render/layer.h"
#include "ui.h"
#include <glib.h>

/**
 * Start processing timer
 * @return Start time in microseconds
 */
static gint64 start_processing_timer(void) {
    return g_get_monotonic_time();
}

/**
 * Stop processing timer and get elapsed time in seconds
 * @param start_time Start time from start_processing_timer()
 * @return Elapsed time in seconds
 */
static gdouble stop_processing_timer(gint64 start_time) {
    gint64 current_time = g_get_monotonic_time();
    return (gdouble)(current_time - start_time) / 1000000.0;
}

/**
 * Apply a filter adjustment to the active layer with undo/redo support
 */
gboolean ui_apply_layer_filter(AppContext* ctx,
                               gboolean (*filter_func)(ImageLayer* layer),
                               const gchar* filter_name) {
    ImageDocument* doc;
    ImageLayer* layer;
    Command* cmd;
    gint64 start_time;
    gdouble processing_time;

    if (!ctx || !filter_func || !filter_name) {
        return FALSE;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return FALSE;
    }

    /* Get the currently selected layer */
    layer = document_get_selected_layer(doc);
    if (!layer) {
        g_warning("No layer selected");
        return FALSE;
    }

    /* Create a draw command for undo/redo (saves layer snapshot) */
    cmd = command_create_draw(layer, filter_name);
    if (!cmd) {
        g_warning("Failed to create undo command for %s filter", filter_name);
        return FALSE;
    }

    /* Start timing */
    start_time = start_processing_timer();

    /* Apply filter */
    if (!filter_func(layer)) {
        g_warning("Failed to apply %s filter", filter_name);
        command_free(cmd);
        return FALSE;
    }

    /* Finalize draw command by taking snapshot of state after filter */
    command_finalize_draw(cmd);

    /* Get processing time */
    processing_time = stop_processing_timer(start_time);

    /* Push command to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    /* Mark document as modified */
    doc->modified = TRUE;

    /* Invalidate document for redraw */
    document_invalidate_composite(doc);

    /* Update status bar with processing time */
    ui_update_status_bar_time(ctx, processing_time);

    /* Update window title and menu states */
    ui_update_window_title(ctx);
    ui_update_menu_and_button_states(ctx);

    return TRUE;
}

/**
 * Apply a filter with parameter values to the currently selected layer
 */
gboolean ui_apply_layer_filter_with_value(AppContext* ctx,
                                          gboolean (*filter_func)(ImageLayer* layer, const gfloat* values, gint num_values),
                                          const gchar* filter_name,
                                          const gfloat* values,
                                          gint num_values) {
    ImageDocument* doc;
    ImageLayer* layer;
    Command* cmd;
    gint64 start_time;
    gdouble processing_time;

    if (!ctx || !filter_func || !filter_name || !values || num_values <= 0) {
        return FALSE;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return FALSE;
    }

    /* Get the currently selected layer */
    layer = document_get_selected_layer(doc);
    if (!layer) {
        g_warning("No layer selected");
        return FALSE;
    }

    /* Create a draw command for undo/redo (saves layer snapshot) */
    cmd = command_create_draw(layer, filter_name);
    if (!cmd) {
        g_warning("Failed to create undo command for %s filter", filter_name);
        return FALSE;
    }

    /* Start timing */
    start_time = start_processing_timer();

    /* Apply filter */
    if (!filter_func(layer, values, num_values)) {
        g_warning("Failed to apply %s filter", filter_name);
        command_free(cmd);
        return FALSE;
    }

    /* Finalize draw command by taking snapshot of state after filter */
    command_finalize_draw(cmd);

    /* Get processing time */
    processing_time = stop_processing_timer(start_time);

    /* Push command to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    /* Mark document as modified */
    doc->modified = TRUE;

    /* Invalidate document for redraw */
    document_invalidate_composite(doc);

    /* Update status bar with processing time */
    ui_update_status_bar_time(ctx, processing_time);

    /* Update window title and menu states */
    ui_update_window_title(ctx);
    ui_update_menu_and_button_states(ctx);

    return TRUE;
}

/**
 * Show a filter dialog and get user values
 */
gint ui_show_filter_dialog(AppContext* ctx,
                           const gchar* title,
                           FilterControlParam* controls,
                           gint num_controls,
                           FilterDialogPreviewCallback preview_callback,
                           gdouble* values) {
    ImageDocument* doc;
    ImageLayer* layer;
    FilterDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
    gint response;

    if (!ctx || !controls || num_controls <= 0 || !values) {
        return GTK_RESPONSE_CANCEL;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return GTK_RESPONSE_CANCEL;
    }

    /* Get the currently selected layer */
    layer = document_get_selected_layer(doc);
    if (!layer) {
        g_warning("No layer selected");
        return GTK_RESPONSE_CANCEL;
    }

    /* Create filter dialog */
    dialog = filter_dialog_new(title, controls, num_controls);
    if (!dialog) {
        g_warning("Failed to create filter dialog");
        return GTK_RESPONSE_CANCEL;
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        filter_dialog_free(dialog);
        return GTK_RESPONSE_CANCEL;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Set layers in dialog */
    filter_dialog_set_layers(dialog, layer, temp_layer);

    /* Store original layer reference and control params for preview callback */
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer", layer);
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params", controls);

    /* Set up live preview callback if provided */
    if (preview_callback) {
        filter_dialog_set_preview_callback(dialog, preview_callback, temp_layer);

        /* Trigger initial preview update with default values */
        gdouble* default_values = (gdouble*)g_malloc(sizeof(gdouble) * num_controls);
        if (default_values) {
            gint i;
            for (i = 0; i < num_controls; i++) {
                default_values[i] = controls[i].default_value;
            }
            preview_callback(dialog, default_values, num_controls, temp_layer);
            g_free(default_values);
        }
    }

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(filter_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = filter_dialog_run(dialog, GTK_WINDOW(ctx->window), values, num_controls);

    /* Clean up */
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer", NULL);
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params", NULL);
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "filter_wrapper_data", NULL);
    filter_dialog_free(dialog);
    layer_free(temp_layer);

    return response;
}
