#include "ui/ui_filter_utils.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/widgets/filter_dialog.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <string.h>

/**
 * Filter function wrapper data structure (internal)
 */
typedef struct {
    gboolean (*filter_apply_func)(ImageLayer* layer, const gfloat* values, gint num_values);
    gfloat* filter_values;
    gint num_values;
} FilterWrapperData;

/* Forward declaration */
static cairo_surface_t* apply_filter_to_viewport_surface(cairo_surface_t* viewport_surface, gpointer params);

/**
 * Free function for filter wrapper data
 */
static void free_filter_wrapper_data(gpointer data) {
    FilterWrapperData* wrapper_data = (FilterWrapperData*)data;
    if (wrapper_data) {
        if (wrapper_data->filter_values) {
            g_free(wrapper_data->filter_values);
        }
        g_free(wrapper_data);
    }
}

/**
 * Set up viewport-based filter for preview
 */
void ui_filter_utils_setup_viewport_filter(FilterDialog* dialog,
                                           gboolean (*filter_func)(ImageLayer*, const gfloat*, gint),
                                           const gfloat* filter_values,
                                           gint num_values) {
    FilterWrapperData* wrapper_data;
    FilterPreview* preview;

    if (!dialog || !filter_func || !filter_values || num_values <= 0) {
        return;
    }

    preview = filter_dialog_get_preview(dialog);
    if (!preview) {
        return;
    }

    /* Get or create wrapper data */
    wrapper_data = (FilterWrapperData*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "filter_wrapper_data");

    if (!wrapper_data || wrapper_data->num_values != num_values) {
        /* Free old wrapper data if it exists */
        if (wrapper_data) {
            free_filter_wrapper_data(wrapper_data);
        }

        /* Create new wrapper data */
        wrapper_data = g_malloc(sizeof(FilterWrapperData));
        wrapper_data->filter_apply_func = filter_func;
        wrapper_data->filter_values = g_malloc(sizeof(gfloat) * num_values);
        wrapper_data->num_values = num_values;
        g_object_set_data_full(G_OBJECT(filter_dialog_get_window(dialog)), "filter_wrapper_data",
                               wrapper_data, (GDestroyNotify)free_filter_wrapper_data);
    }

    /* Update filter function and values */
    wrapper_data->filter_apply_func = filter_func;
    memcpy(wrapper_data->filter_values, filter_values, sizeof(gfloat) * num_values);

    /* Set filter function on preview to use viewport-based filtering */
    filter_preview_set_filter_function(preview, apply_filter_to_viewport_surface, wrapper_data);
}

/**
 * Helper function to wrap layer-based filter functions for viewport system
 */
static cairo_surface_t* apply_filter_to_viewport_surface(cairo_surface_t* viewport_surface, gpointer params) {
    FilterWrapperData* wrapper_data = (FilterWrapperData*)params;
    ImageLayer* temp_layer;
    cairo_surface_t* result;

    if (!viewport_surface || !wrapper_data || !wrapper_data->filter_apply_func) {
        return NULL;
    }

    /* Get viewport dimensions */
    gint width = cairo_image_surface_get_width(viewport_surface);
    gint height = cairo_image_surface_get_height(viewport_surface);

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    /* Create a temporary layer with the viewport surface */
    temp_layer = layer_new("TempViewport", width, height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
    if (!temp_layer) {
        return NULL;
    }

    /* Copy viewport surface to layer */
    cairo_t* cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, viewport_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Apply filter to the layer */
    if (!wrapper_data->filter_apply_func(temp_layer, wrapper_data->filter_values, wrapper_data->num_values)) {
        layer_free(temp_layer);
        return NULL;
    }

    /* Return a reference to the filtered surface */
    result = cairo_surface_reference(temp_layer->surface);
    layer_free(temp_layer);

    return result;
}

/**
 * Scale multiple UI values to filter range
 */
gboolean ui_filter_utils_scale_values(const gdouble* ui_values,
                                      gfloat* filter_values,
                                      const FilterControlParam* controls,
                                      gint num_values) {
    gint i;

    if (!ui_values || !filter_values || !controls || num_values <= 0) {
        return FALSE;
    }

    for (i = 0; i < num_values; i++) {
        gdouble scaled = adjustments_scale_value(
            ui_values[i],
            controls[i].min_value,
            controls[i].max_value,
            controls[i].filter_min,
            controls[i].filter_max);
        filter_values[i] = (gfloat)scaled;
    }

    return TRUE;
}

/**
 * Generic preview update callback for filters that need value scaling
 */
gboolean ui_filter_utils_preview_update_scaled(FilterDialog* dialog,
                                               const gdouble* values,
                                               gint num_values,
                                               gpointer user_data) {
    FilterControlParam* controls;
    FilterApplyFuncData* func_data;
    gfloat* filter_values;

    if (!dialog || !values || num_values <= 0 || !user_data) {
        return FALSE;
    }

    func_data = (FilterApplyFuncData*)user_data;
    if (!func_data->filter_apply_func || func_data->num_values != num_values) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Allocate array for scaled values */
    filter_values = (gfloat*)g_malloc(sizeof(gfloat) * num_values);
    if (!filter_values) {
        return FALSE;
    }

    /* Scale UI values to filter range */
    if (!ui_filter_utils_scale_values(values, filter_values, controls, num_values)) {
        g_free(filter_values);
        return FALSE;
    }

    /* Set up viewport-based filter */
    ui_filter_utils_setup_viewport_filter(dialog, func_data->filter_apply_func, filter_values, num_values);

    g_free(filter_values);
    return TRUE;
}

/**
 * Create a temporary layer copy for preview
 */
ImageLayer* ui_filter_utils_create_temp_layer(ImageLayer* source_layer) {
    ImageLayer* temp_layer;
    cairo_t* cr;

    if (!source_layer) {
        return NULL;
    }

    temp_layer = layer_new("Temp", source_layer->width, source_layer->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
    if (!temp_layer) {
        return NULL;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, source_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    return temp_layer;
}

/**
 * Copy layer surface to another layer
 */
gboolean ui_filter_utils_copy_layer_surface(ImageLayer* dest_layer, ImageLayer* source_layer) {
    cairo_t* cr;

    if (!dest_layer || !source_layer) {
        return FALSE;
    }

    if (dest_layer->width != source_layer->width || dest_layer->height != source_layer->height) {
        return FALSE;
    }

    cr = cairo_create(dest_layer->surface);
    cairo_set_source_surface(cr, source_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    return TRUE;
}

/**
 * Apply filter with command, timing, and document updates
 */
gboolean ui_filter_utils_apply_with_command(AppContext* ctx,
                                            ImageLayer* layer,
                                            gboolean (*filter_func)(ImageLayer* layer, const gfloat* values, gint num_values),
                                            const gchar* filter_name,
                                            const gfloat* values,
                                            gint num_values) {
    ImageDocument* doc;
    Command* cmd;
    gint64 start_time;
    gboolean success;

    if (!ctx || !layer || !filter_func || !filter_name) {
        return FALSE;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        return FALSE;
    }

    /* Create command */
    cmd = command_create_draw(layer, filter_name);
    if (!cmd) {
        return FALSE;
    }

    /* Start timing */
    start_time = g_get_monotonic_time();

    /* Apply filter */
    if (values && num_values > 0) {
        success = filter_func(layer, values, num_values);
    } else {
        /* For filters that don't take values, we need a different approach */
        /* This is a limitation - we'd need a different function signature */
        success = FALSE;
    }

    if (success) {
        /* Get processing time */
        gint64 current_time = g_get_monotonic_time();
        gdouble processing_time = (gdouble)(current_time - start_time) / 1000000.0;

        command_finalize_draw(cmd);
        if (doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }
        } else {
            command_free(cmd);
        }
        layer_invalidate_cache(layer);
        doc->modified = TRUE;
        document_invalidate_composite(doc);
        ui_update_status_bar_time(ctx, processing_time);
        ui_update_window_title(ctx);
        ui_update_menu_and_button_states(ctx);
    } else {
        command_free(cmd);
    }

    return success;
}
