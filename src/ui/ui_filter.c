#include "ui/ui_filter.h"
#include "command.h"
#include "filters.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/filters/filter_utils.h"
#include <glib.h>
#include <gtk/gtk.h>

/**
 * Progress dialog structure
 */
typedef struct {
    GtkWidget* dialog;
    GtkWidget* label;
    GtkWidget* progress_bar;
    guint pulse_timeout_id;
} FilterProgressDialog;

/* Forward declaration */
static gboolean pulse_progress_bar(gpointer user_data);

/**
 * Create and show a progress dialog for filter application
 */
static FilterProgressDialog* show_filter_progress_dialog(GtkWindow* parent, const gchar* filter_name) {
    FilterProgressDialog* progress;
    GtkWidget* content_area;
    GtkWidget* vbox;
    GtkWidget* label;
    GtkWidget* progress_bar;

    progress = (FilterProgressDialog*)g_malloc(sizeof(FilterProgressDialog));
    if (!progress) {
        return NULL;
    }

    /* Initialize */
    progress->pulse_timeout_id = 0;

    /* Create dialog */
    progress->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(progress->dialog), "Applying Filter");
    gtk_window_set_modal(GTK_WINDOW(progress->dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(progress->dialog), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(progress->dialog), FALSE);

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(progress->dialog), parent);
    }

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(progress->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);

    /* Create vertical box */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    /* Create label showing filter name */
    label = gtk_label_new(NULL);
    gchar* label_text = g_strdup_printf("Applying %s...", filter_name ? filter_name : "filter");
    gtk_label_set_text(GTK_LABEL(label), label_text);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    g_free(label_text);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    progress->label = label;

    /* Create progress bar in activity mode (indeterminate) */
    progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(progress_bar), 0.1);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), FALSE);
    gtk_widget_set_size_request(progress_bar, 300, -1);
    gtk_box_pack_start(GTK_BOX(vbox), progress_bar, FALSE, FALSE, 0);
    progress->progress_bar = progress_bar;

    /* Show all widgets */
    gtk_widget_show_all(progress->dialog);

    /* Start pulsing the progress bar periodically (every 50ms) */
    progress->pulse_timeout_id = g_timeout_add(50, pulse_progress_bar, progress);
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));

    /* Process pending events to show the dialog */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return progress;
}

/**
 * Timeout callback to pulse progress bar
 */
static gboolean pulse_progress_bar(gpointer user_data) {
    FilterProgressDialog* progress = (FilterProgressDialog*)user_data;
    if (progress && progress->progress_bar) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress->progress_bar));
    }
    return G_SOURCE_CONTINUE;
}

/**
 * Hide and destroy progress dialog
 */
static void hide_filter_progress_dialog(FilterProgressDialog* progress) {
    if (!progress) {
        return;
    }

    /* Remove timeout if it exists */
    if (progress->pulse_timeout_id > 0) {
        g_source_remove(progress->pulse_timeout_id);
        progress->pulse_timeout_id = 0;
    }

    if (progress->dialog) {
        gtk_widget_destroy(progress->dialog);
    }

    g_free(progress);
}

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

    /* Show progress dialog */
    FilterProgressDialog* progress = NULL;
    if (ctx->window) {
        progress = show_filter_progress_dialog(GTK_WINDOW(ctx->window), filter_name);
    }

    /* Start timing */
    start_time = start_processing_timer();

    /* Create a copy of the original surface for selection masking */
    cairo_surface_t* original_surface = NULL;
    if (layer->surface) {
        gint width = cairo_image_surface_get_width(layer->surface);
        gint height = cairo_image_surface_get_height(layer->surface);
        original_surface = cairo_image_surface_create(
            cairo_image_surface_get_format(layer->surface), width, height);
        if (original_surface) {
            cairo_t* cr = cairo_create(original_surface);
            cairo_set_source_surface(cr, layer->surface, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
    }

    /* Apply filter (this is blocking, but progress bar will pulse via timeout) */
    gboolean success = filter_func(layer);

    /* Apply selection masking if there's a selection */
    if (success && original_surface && layer->surface) {
        filter_utils_apply_selection_mask(layer->surface, original_surface, doc, layer);
    }

    /* Free original surface copy */
    if (original_surface) {
        cairo_surface_destroy(original_surface);
    }

    /* Process any pending events after filter completes */
    if (progress) {
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }
    }

    if (!success) {
        g_warning("Failed to apply %s filter", filter_name);
        if (progress) {
            hide_filter_progress_dialog(progress);
        }
        command_free(cmd);
        return FALSE;
    }

    /* Hide progress dialog */
    if (progress) {
        hide_filter_progress_dialog(progress);
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

    /* Show progress dialog */
    FilterProgressDialog* progress = NULL;
    if (ctx->window) {
        progress = show_filter_progress_dialog(GTK_WINDOW(ctx->window), filter_name);
    }

    /* Start timing */
    start_time = start_processing_timer();

    /* Create a copy of the original surface for selection masking */
    cairo_surface_t* original_surface = NULL;
    if (layer->surface) {
        gint width = cairo_image_surface_get_width(layer->surface);
        gint height = cairo_image_surface_get_height(layer->surface);
        original_surface = cairo_image_surface_create(
            cairo_image_surface_get_format(layer->surface), width, height);
        if (original_surface) {
            cairo_t* cr = cairo_create(original_surface);
            cairo_set_source_surface(cr, layer->surface, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
    }

    /* Apply filter (this is blocking, but progress bar will pulse via timeout) */
    gboolean success = filter_func(layer, values, num_values);

    /* Apply selection masking if there's a selection */
    if (success && original_surface && layer->surface) {
        filter_utils_apply_selection_mask(layer->surface, original_surface, doc, layer);
    }

    /* Free original surface copy */
    if (original_surface) {
        cairo_surface_destroy(original_surface);
    }

    /* Process any pending events after filter completes */
    if (progress) {
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }
    }

    if (!success) {
        g_warning("Failed to apply %s filter", filter_name);
        if (progress) {
            hide_filter_progress_dialog(progress);
        }
        command_free(cmd);
        return FALSE;
    }

    /* Hide progress dialog */
    if (progress) {
        hide_filter_progress_dialog(progress);
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
/* Forward declaration */
gint ui_show_filter_dialog_with_zoom_pan(AppContext* ctx,
                                         const gchar* title,
                                         FilterControlParam* controls,
                                         gint num_controls,
                                         FilterDialogPreviewCallback preview_callback,
                                         gdouble* values,
                                         gboolean allow_zoom_pan);

gint ui_show_filter_dialog(AppContext* ctx,
                           const gchar* title,
                           FilterControlParam* controls,
                           gint num_controls,
                           FilterDialogPreviewCallback preview_callback,
                           gdouble* values) {
    return ui_show_filter_dialog_with_zoom_pan(ctx, title, controls, num_controls,
                                               preview_callback, values, TRUE);
}

gint ui_show_filter_dialog_with_zoom_pan(AppContext* ctx,
                                         const gchar* title,
                                         FilterControlParam* controls,
                                         gint num_controls,
                                         FilterDialogPreviewCallback preview_callback,
                                         gdouble* values,
                                         gboolean allow_zoom_pan) {
    ImageDocument* doc;
    ImageLayer* layer;
    FilterDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
    gint response;
    gint total_values;

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

    /* Set allow_zoom_pan property on preview widget */
    FilterPreview* preview = filter_dialog_get_preview(dialog);
    if (preview) {
        filter_preview_set_allow_zoom_pan(preview, allow_zoom_pan);
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, NULL);
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
        gint total_values = filter_dialog_get_total_values_count(dialog);
        gdouble* default_values = (gdouble*)g_malloc(sizeof(gdouble) * total_values);
        if (default_values) {
            gint i;
            gint value_index = 0;
            for (i = 0; i < num_controls; i++) {
                if (controls[i].type == FILTER_CONTROL_DOUBLE) {
                    default_values[value_index++] = controls[i].default_value;
                } else if (controls[i].type == FILTER_CONTROL_BOOLEAN) {
                    default_values[value_index++] = controls[i].default_bool ? 1.0 : 0.0;
                } else if (controls[i].type == FILTER_CONTROL_RGB) {
                    default_values[value_index++] = controls[i].default_r;
                    default_values[value_index++] = controls[i].default_g;
                    default_values[value_index++] = controls[i].default_b;
                }
            }
            preview_callback(dialog, default_values, total_values, temp_layer);
            g_free(default_values);
        }
    }

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(filter_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog - get total values count for proper allocation */
    total_values = filter_dialog_get_total_values_count(dialog);
    gint actual_count = (total_values < num_controls) ? num_controls : total_values;
    response = filter_dialog_run(dialog, GTK_WINDOW(ctx->window), values, actual_count);

    /* Clean up */
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer", NULL);
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params", NULL);
    g_object_set_data(G_OBJECT(filter_dialog_get_window(dialog)), "filter_wrapper_data", NULL);
    filter_dialog_free(dialog);
    layer_free(temp_layer);

    return response;
}
