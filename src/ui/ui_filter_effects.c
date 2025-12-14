#include "ui/ui_filter_effects.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/dialogs/clouds_dialog.h"
#include "ui/filters/filter_average_blur.h"
#include "ui/filters/filter_box_blur.h"
#include "ui/filters/filter_canny_edge.h"
#include "ui/filters/filter_color_halftone.h"
#include "ui/filters/filter_crystallize.h"
#include "ui/filters/filter_exponential_blur.h"
#include "ui/filters/filter_film_grain.h"
#include "ui/filters/filter_fragment.h"
#include "ui/filters/filter_frosted_glass.h"
#include "ui/filters/filter_gaussian_blur.h"
#include "ui/filters/filter_gradient_edge.h"
#include "ui/filters/filter_laplacian_edge.h"
#include "ui/filters/filter_median_blur.h"
#include "ui/filters/filter_mosaic.h"
#include "ui/filters/filter_motion_blur.h"
#include "ui/filters/filter_oil_paint.h"
#include "ui/filters/filter_pointillize.h"
#include "ui/filters/filter_prewitt_edge.h"
#include "ui/filters/filter_radial_blur.h"
#include "ui/filters/filter_relief.h"
#include "ui/filters/filter_render_clouds.h"
#include "ui/filters/filter_roberts_edge.h"
#include "ui/filters/filter_sobel_edge.h"
#include "ui/filters/filter_surface_blur.h"
#include "ui/filters/filter_zoom_blur.h"
#include "ui/ui_filter.h"
#include "ui/widgets/filter_dialog.h"
#include <glib.h>

/**
 * Filter function wrapper data structure
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
 * Helper function to wrap layer-based filter functions for viewport system
 * Creates a temporary layer from the viewport surface, applies the filter, and returns the surface
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
    temp_layer = layer_new("TempViewport", width, height, TRUE);
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
 * Helper function to set up viewport-based filter for preview
 * This replaces the old approach of applying filters to full layers
 */
static void setup_viewport_filter(FilterDialog* dialog,
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
 * Average blur filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_average_blur_preview_update(FilterDialog* dialog,
                                               const gdouble* values,
                                               gint num_values,
                                               gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius;
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Scale UI value to filter range */
    scaled_radius = adjustments_scale_value(
        values[0],
        controls[0].min_value,
        controls[0].max_value,
        controls[0].filter_min,
        controls[0].filter_max);
    filter_values[0] = (gfloat)scaled_radius;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_average_blur_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Gaussian blur filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_gaussian_blur_preview_update(FilterDialog* dialog,
                                                const gdouble* values,
                                                gint num_values,
                                                gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_sigma;
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Scale UI value to filter range */
    scaled_sigma = adjustments_scale_value(
        values[0],
        controls[0].min_value,
        controls[0].max_value,
        controls[0].filter_min,
        controls[0].filter_max);
    filter_values[0] = (gfloat)scaled_sigma;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_gaussian_blur_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Effects > Blur > Average Blur callback
 */
static void on_effects_average_blur(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_radius;
    gfloat filter_values[1];

    if (!ctx) {
        return;
    }

    /* Define average blur control parameters */
    controls[0].label = "radius";
    controls[0].min_value = 1.0; /* UI range: 1 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 3.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0; /* Filter range: 1 to 100 */
    controls[0].filter_max = 100.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Average Blur", controls, 1,
                                     on_average_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range */
        scaled_radius = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max);

        /* Apply average blur filter */
        filter_values[0] = (gfloat)scaled_radius;
        ui_apply_layer_filter_with_value(ctx, filter_average_blur_apply,
                                         "Average Blur", filter_values, 1);
    }
}

/**
 * Effects > Blur > Gaussian Blur callback
 */
static void on_effects_gaussian_blur(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_sigma;
    gfloat filter_values[1];

    if (!ctx) {
        return;
    }

    /* Define Gaussian blur control parameters */
    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 1.0;
    controls[0].step = 0.1;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 100.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Gaussian Blur", controls, 1,
                                     on_gaussian_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range */
        scaled_sigma = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max);

        /* Apply Gaussian blur filter */
        filter_values[0] = (gfloat)scaled_sigma;
        ui_apply_layer_filter_with_value(ctx, filter_gaussian_blur_apply,
                                         "Gaussian Blur", filter_values, 1);
    }
}

/**
 * Exponential blur filter preview update callback
 */
static gboolean on_exponential_blur_preview_update(FilterDialog* dialog,
                                                   const gdouble* values,
                                                   gint num_values,
                                                   gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius;
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    filter_values[0] = (gfloat)scaled_radius;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_exponential_blur_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Effects > Blur > Exponential Blur callback
 */
static void on_effects_exponential_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx)
        return;

    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 5.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Exponential Blur", controls, 1,
                                     on_exponential_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        filter_values[0] = (gfloat)scaled_radius;
        ui_apply_layer_filter_with_value(ctx, filter_exponential_blur_apply,
                                         "Exponential Blur", filter_values, 1);
    }
}

/**
 * Box blur filter preview update callback
 */
static gboolean on_box_blur_preview_update(FilterDialog* dialog,
                                           const gdouble* values,
                                           gint num_values,
                                           gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius;
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    filter_values[0] = (gfloat)scaled_radius;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_box_blur_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Effects > Blur > Box Blur callback
 */
static void on_effects_box_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx)
        return;

    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 3.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Box Blur", controls, 1,
                                     on_box_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        filter_values[0] = (gfloat)scaled_radius;
        ui_apply_layer_filter_with_value(ctx, filter_box_blur_apply,
                                         "Box Blur", filter_values, 1);
    }
}

/**
 * Median blur filter preview update callback
 */
static gboolean on_median_blur_preview_update(FilterDialog* dialog,
                                              const gdouble* values,
                                              gint num_values,
                                              gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius;
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    filter_values[0] = (gfloat)scaled_radius;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_median_blur_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Effects > Blur > Median Blur callback
 */
static void on_effects_median_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx)
        return;

    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 3.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Median Blur", controls, 1,
                                     on_median_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        filter_values[0] = (gfloat)scaled_radius;
        ui_apply_layer_filter_with_value(ctx, filter_median_blur_apply,
                                         "Median Blur", filter_values, 1);
    }
}

/**
 * Motion blur filter preview update callback
 */
static gboolean on_motion_blur_preview_update(FilterDialog* dialog,
                                              const gdouble* values,
                                              gint num_values,
                                              gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_distance, scaled_angle;
    gfloat filter_values[2];

    if (!dialog || !values || num_values < 2) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_distance = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_angle = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);
    filter_values[0] = (gfloat)scaled_distance;
    filter_values[1] = (gfloat)scaled_angle;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_motion_blur_apply,
                          filter_values, 2);

    return TRUE;
}

/**
 * Effects > Blur > Motion Blur callback
 */
static void on_effects_motion_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx)
        return;

    controls[0].label = "distance";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 10.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 100.0;

    controls[1].label = "angle";
    controls[1].min_value = 0.0;
    controls[1].max_value = 360.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 360.0;

    response = ui_show_filter_dialog(ctx, "Motion Blur", controls, 2,
                                     on_motion_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_distance = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_angle = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        filter_values[0] = (gfloat)scaled_distance;
        filter_values[1] = (gfloat)scaled_angle;
        ui_apply_layer_filter_with_value(ctx, filter_motion_blur_apply,
                                         "Motion Blur", filter_values, 2);
    }
}

/**
 * Radial blur filter preview update callback
 */
static gboolean on_radial_blur_preview_update(FilterDialog* dialog,
                                              const gdouble* values,
                                              gint num_values,
                                              gpointer user_data) {
    FilterControlParam* controls;
    ImageLayer* original_layer;
    gint center_x, center_y;
    gdouble scaled_intensity;
    gfloat filter_values[3];

    if (!dialog || !values || num_values < 3) {
        return FALSE;
    }

    original_layer = (ImageLayer*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Convert normalized center coordinates (0.0-1.0) directly to pixel coordinates */
    center_x = (gint)(values[0] * original_layer->width);
    center_y = (gint)(values[1] * original_layer->height);

    scaled_intensity = adjustments_scale_value(
        values[2], controls[2].min_value, controls[2].max_value,
        controls[2].filter_min, controls[2].filter_max);
    filter_values[0] = (gfloat)center_x;
    filter_values[1] = (gfloat)center_y;
    filter_values[2] = (gfloat)scaled_intensity;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_radial_blur_apply,
                          filter_values, 3);

    return TRUE;
}

/**
 * Effects > Blur > Radial Blur callback
 */
static void on_effects_radial_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    FilterControlParam controls[3];
    gdouble values[3];
    gint response;
    gfloat filter_values[3];

    if (!ctx)
        return;

    doc = ui_get_active_document(ctx);
    layer = doc ? document_get_selected_layer(doc) : NULL;
    if (!layer)
        return;

    controls[0].label = "center X";
    controls[0].min_value = 0.0;
    controls[0].max_value = 1.0;
    controls[0].default_value = 0.5; /* Center of image */
    controls[0].step = 0.01;
    controls[0].decimals = 2;
    controls[0].filter_min = 0.0; /* Normalized range 0.0-1.0 */
    controls[0].filter_max = 1.0;

    controls[1].label = "center Y";
    controls[1].min_value = 0.0;
    controls[1].max_value = 1.0;
    controls[1].default_value = 0.5; /* Center of image */
    controls[1].step = 0.01;
    controls[1].decimals = 2;
    controls[1].filter_min = 0.0; /* Normalized range 0.0-1.0 */
    controls[1].filter_max = 1.0;

    controls[2].label = "intensity";
    controls[2].min_value = 1.0;
    controls[2].max_value = 100.0;
    controls[2].default_value = 10.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = 1.0;
    controls[2].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Radial Blur", controls, 3,
                                     on_radial_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Convert normalized center coordinates (0.0-1.0) directly to pixel coordinates */
        /* values[0] and values[1] are already in 0.0-1.0 range from the UI */
        gint center_x = (gint)(values[0] * layer->width);
        gint center_y = (gint)(values[1] * layer->height);

        gdouble scaled_intensity = adjustments_scale_value(
            values[2], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);
        filter_values[0] = (gfloat)center_x;
        filter_values[1] = (gfloat)center_y;
        filter_values[2] = (gfloat)scaled_intensity;
        ui_apply_layer_filter_with_value(ctx, filter_radial_blur_apply,
                                         "Radial Blur", filter_values, 3);
    }
}

/**
 * Surface blur filter preview update callback
 */
static gboolean on_surface_blur_preview_update(FilterDialog* dialog,
                                               const gdouble* values,
                                               gint num_values,
                                               gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius, scaled_threshold;
    gfloat filter_values[2];

    if (!dialog || !values || num_values < 2) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_threshold = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);
    filter_values[0] = (gfloat)scaled_radius;
    filter_values[1] = (gfloat)scaled_threshold;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_surface_blur_apply,
                          filter_values, 2);

    return TRUE;
}

/**
 * Effects > Blur > Surface Blur callback
 */
static void on_effects_surface_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx)
        return;

    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 127.0;
    controls[0].default_value = 20.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 127.0;

    controls[1].label = "threshold";
    controls[1].min_value = 2.0;
    controls[1].max_value = 255.0;
    controls[1].default_value = 20.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 2.0;
    controls[1].filter_max = 255.0;

    response = ui_show_filter_dialog(ctx, "Surface Blur", controls, 2,
                                     on_surface_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_threshold = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        filter_values[0] = (gfloat)scaled_radius;
        filter_values[1] = (gfloat)scaled_threshold;
        ui_apply_layer_filter_with_value(ctx, filter_surface_blur_apply,
                                         "Surface Blur", filter_values, 2);
    }
}

/**
 * Zoom blur filter preview update callback
 */
static gboolean on_zoom_blur_preview_update(FilterDialog* dialog,
                                            const gdouble* values,
                                            gint num_values,
                                            gpointer user_data) {
    FilterControlParam* controls;
    ImageLayer* original_layer;
    gdouble scaled_sample_radius, scaled_blur_amount;
    gint center_x, center_y;
    gfloat filter_values[4];

    if (!dialog || !values || num_values < 4) {
        return FALSE;
    }

    original_layer = (ImageLayer*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_sample_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_blur_amount = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);

    /* Convert normalized center coordinates (0.0-1.0) directly to pixel coordinates */
    center_x = (gint)(values[2] * original_layer->width);
    center_y = (gint)(values[3] * original_layer->height);

    filter_values[0] = (gfloat)scaled_sample_radius;
    filter_values[1] = (gfloat)scaled_blur_amount;
    filter_values[2] = (gfloat)center_x;
    filter_values[3] = (gfloat)center_y;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_zoom_blur_apply,
                          filter_values, 4);

    return TRUE;
}

/**
 * Effects > Blur > Zoom Blur callback
 */
static void on_effects_zoom_blur(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    FilterControlParam controls[4];
    gdouble values[4];
    gint response;
    gfloat filter_values[4];

    if (!ctx)
        return;

    doc = ui_get_active_document(ctx);
    layer = doc ? document_get_selected_layer(doc) : NULL;
    if (!layer)
        return;

    controls[0].label = "strength";
    controls[0].min_value = 10.0;
    controls[0].max_value = 200.0;
    controls[0].default_value = 20.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 10.0;
    controls[0].filter_max = 200.0;

    controls[1].label = "distance";
    controls[1].min_value = 1.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 20.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.1;
    controls[1].filter_max = 1.0;

    controls[2].label = "center X";
    controls[2].min_value = 0.0;
    controls[2].max_value = 1.0;
    controls[2].default_value = 0.5; /* Center of image */
    controls[2].step = 0.01;
    controls[2].decimals = 2;
    controls[2].filter_min = 0.0; /* Normalized range 0.0-1.0 */
    controls[2].filter_max = 1.0;

    controls[3].label = "center Y";
    controls[3].min_value = 0.0;
    controls[3].max_value = 1.0;
    controls[3].default_value = 0.5; /* Center of image */
    controls[3].step = 0.01;
    controls[3].decimals = 2;
    controls[3].filter_min = 0.0; /* Normalized range 0.0-1.0 */
    controls[3].filter_max = 1.0;

    response = ui_show_filter_dialog(ctx, "Zoom Blur", controls, 4,
                                     on_zoom_blur_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_sample_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_blur_amount = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);

        /* Convert normalized center coordinates (0.0-1.0) directly to pixel coordinates */
        /* values[2] and values[3] are already in 0.0-1.0 range from the UI */
        gint center_x = (gint)(values[2] * layer->width);
        gint center_y = (gint)(values[3] * layer->height);

        filter_values[0] = (gfloat)scaled_sample_radius;
        filter_values[1] = (gfloat)scaled_blur_amount;
        filter_values[2] = (gfloat)center_x;
        filter_values[3] = (gfloat)center_y;
        ui_apply_layer_filter_with_value(ctx, filter_zoom_blur_apply,
                                         "Zoom Blur", filter_values, 4);
    }
}

/**
 * Film grain filter preview update callback
 */
static gboolean on_film_grain_preview_update(FilterDialog* dialog,
                                             const gdouble* values,
                                             gint num_values,
                                             gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_strength, scaled_softness;
    gfloat filter_values[2];

    if (!dialog || !values || num_values < 2) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_strength = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_softness = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);
    filter_values[0] = (gfloat)scaled_strength;
    filter_values[1] = (gfloat)scaled_softness;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_film_grain_apply,
                          filter_values, 2);

    return TRUE;
}

/**
 * Effects > Artistic > Film Grain callback
 */
static void on_artistic_film_grain(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx)
        return;

    controls[0].label = "strength";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 10.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;
    controls[0].filter_max = 1.0;

    controls[1].label = "softness";
    controls[1].min_value = 0.0;
    controls[1].max_value = 25.0;
    controls[1].default_value = 5.0;
    controls[1].step = 0.1;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 25.0;

    response = ui_show_filter_dialog(ctx, "Film Grain", controls, 2,
                                     on_film_grain_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_strength = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_softness = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        filter_values[0] = (gfloat)scaled_strength;
        filter_values[1] = (gfloat)scaled_softness;
        ui_apply_layer_filter_with_value(ctx, filter_film_grain_apply,
                                         "Film Grain", filter_values, 2);
    }
}

/**
 * Frosted glass filter preview update callback
 */
static gboolean on_frosted_glass_preview_update(FilterDialog* dialog,
                                                const gdouble* values,
                                                gint num_values,
                                                gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius, scaled_range;
    gfloat filter_values[2];

    if (!dialog || !values || num_values < 2) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_range = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);
    filter_values[0] = (gfloat)scaled_radius;
    filter_values[1] = (gfloat)scaled_range;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_frosted_glass_apply,
                          filter_values, 2);

    return TRUE;
}

/**
 * Effects > Artistic > Frosted Glass callback
 */
static void on_artistic_frosted_glass(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx)
        return;

    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 2.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 100.0;

    controls[1].label = "range";
    controls[1].min_value = 1.0;
    controls[1].max_value = 20.0;
    controls[1].default_value = 3.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 1.0;
    controls[1].filter_max = 20.0;

    response = ui_show_filter_dialog(ctx, "Frosted Glass", controls, 2,
                                     on_frosted_glass_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_range = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        filter_values[0] = (gfloat)scaled_radius;
        filter_values[1] = (gfloat)scaled_range;
        ui_apply_layer_filter_with_value(ctx, filter_frosted_glass_apply,
                                         "Frosted Glass", filter_values, 2);
    }
}

/**
 * Oil paint filter preview update callback
 */
static gboolean on_oil_paint_preview_update(FilterDialog* dialog,
                                            const gdouble* values,
                                            gint num_values,
                                            gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius, scaled_intensity;
    gfloat filter_values[2];

    if (!dialog || !values || num_values < 2) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_intensity = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);
    filter_values[0] = (gfloat)scaled_radius;
    filter_values[1] = (gfloat)scaled_intensity;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_oil_paint_apply,
                          filter_values, 2);

    return TRUE;
}

/**
 * Relief filter preview update callback
 */
static gboolean on_relief_preview_update(FilterDialog* dialog,
                                         const gdouble* values,
                                         gint num_values,
                                         gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_angle;
    gfloat filter_values[2];

    if (!dialog || !values || num_values < 2) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_angle = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    filter_values[0] = (gfloat)scaled_angle;
    filter_values[1] = (gfloat)values[1]; /* offset doesn't need scaling */

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_relief_apply,
                          filter_values, 2);

    return TRUE;
}

/**
 * Effects > Artistic > Relief callback
 */
static void on_artistic_relief(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx) {
        return;
    }

    /* Control 0: Angle (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Angle";
    controls[0].min_value = 0.0;
    controls[0].max_value = 360.0;
    controls[0].default_value = 135.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;
    controls[0].filter_max = 360.0;

    /* Control 1: Offset (double) */
    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "Offset";
    controls[1].min_value = 0.0;
    controls[1].max_value = 255.0;
    controls[1].default_value = 127.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 255.0;

    response = ui_show_filter_dialog(ctx, "Relief", controls, 2,
                                     on_relief_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_angle = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        filter_values[0] = (gfloat)scaled_angle;
        filter_values[1] = (gfloat)values[1]; /* offset */

        ui_apply_layer_filter_with_value(ctx, filter_relief_apply,
                                         "Relief", filter_values, 2);
    }
}

/**
 * Effects > Artistic > Oil Paint callback
 */
static void on_artistic_oil_paint(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx)
        return;

    controls[0].label = "brush size";
    controls[0].min_value = 1.0;
    controls[0].max_value = 200.0;
    controls[0].default_value = 5.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 200.0;

    controls[1].label = "detail";
    controls[1].min_value = 1.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 15.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 1.0;
    controls[1].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Oil Painting", controls, 2,
                                     on_oil_paint_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_intensity = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        filter_values[0] = (gfloat)scaled_radius;
        filter_values[1] = (gfloat)scaled_intensity;
        ui_apply_layer_filter_with_value(ctx, filter_oil_paint_apply,
                                         "Oil Painting", filter_values, 2);
    }
}

/**
 * Edge Detect > Canny callback
 */
static void on_edge_canny(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_canny_edge_apply, "Canny Edge");
}

/**
 * Edge Detect > Gradient callback
 */
static void on_edge_gradient(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_gradient_edge_apply, "Gradient Edge");
}

/**
 * Edge Detect > Laplacian callback
 */
static void on_edge_laplacian(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_laplacian_edge_apply, "Laplacian Edge");
}

/**
 * Edge Detect > Prewitt callback
 */
static void on_edge_prewitt(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_prewitt_edge_apply, "Prewitt Edge");
}

/**
 * Edge Detect > Roberts callback
 */
static void on_edge_roberts(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_roberts_edge_apply, "Roberts Edge");
}

/**
 * Edge Detect > Sobel callback
 */
static void on_edge_sobel(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_sobel_edge_apply, "Sobel Edge");
}

/**
 * Pointillize filter preview update callback
 */
static gboolean on_pointillize_preview_update(FilterDialog* dialog,
                                              const gdouble* values,
                                              gint num_values,
                                              gpointer user_data) {
    gfloat filter_values[4];

    if (!dialog || !values || num_values < 4) {
        return FALSE;
    }

    /* Values: [cellSize, r, g, b]
       cellSize is already an integer value
       r, g, b are in 0.0-1.0 range, will be converted to 0-255 in filter */
    filter_values[0] = (gfloat)values[0]; /* cellSize */
    filter_values[1] = (gfloat)values[1]; /* r */
    filter_values[2] = (gfloat)values[2]; /* g */
    filter_values[3] = (gfloat)values[3]; /* b */

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_pointillize_apply,
                          filter_values, 4);

    return TRUE;
}

/**
 * Crystallize filter preview update callback
 */
static gboolean on_crystallize_preview_update(FilterDialog* dialog,
                                              const gdouble* values,
                                              gint num_values,
                                              gpointer user_data) {
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    filter_values[0] = (gfloat)values[0]; /* cellSize */

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_crystallize_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Mosaic filter preview update callback
 */
static gboolean on_mosaic_preview_update(FilterDialog* dialog,
                                         const gdouble* values,
                                         gint num_values,
                                         gpointer user_data) {
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    filter_values[0] = (gfloat)values[0]; /* blockSize */

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_mosaic_apply,
                          filter_values, 1);

    return TRUE;
}

/**
 * Effects > Pixelate > Mosaic callback
 */
static void on_pixelate_mosaic(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) {
        return;
    }

    /* Control 0: Block Size (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Block Size";
    controls[0].min_value = 3.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 10.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 3.0;
    controls[0].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Mosaic", controls, 1,
                                     on_mosaic_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        filter_values[0] = (gfloat)values[0]; /* blockSize */

        ui_apply_layer_filter_with_value(ctx, filter_mosaic_apply,
                                         "Mosaic", filter_values, 1);
    }
}

/**
 * Effects > Pixelate > Crystallize callback
 */
static void on_pixelate_crystallize(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) {
        return;
    }

    /* Control 0: Cell Size (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Cell Size";
    controls[0].min_value = 3.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 10.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 3.0;
    controls[0].filter_max = 100.0;

    response = ui_show_filter_dialog(ctx, "Crystallize", controls, 1,
                                     on_crystallize_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        filter_values[0] = (gfloat)values[0]; /* cellSize */

        ui_apply_layer_filter_with_value(ctx, filter_crystallize_apply,
                                         "Crystallize", filter_values, 1);
    }
}

/**
 * Effects > Pixelate > Fragment callback
 */
static void on_pixelate_fragment(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_fragment_apply, "Fragment");
}

/**
 * Color halftone filter preview update callback
 */
static gboolean on_color_halftone_preview_update(FilterDialog* dialog,
                                                 const gdouble* values,
                                                 gint num_values,
                                                 gpointer user_data) {
    FilterControlParam* controls;
    gdouble scaled_radius, scaled_dot_density;
    gdouble scaled_cyan_angle, scaled_magenta_angle, scaled_yellow_angle;
    gfloat filter_values[5];

    if (!dialog || !values || num_values < 5) {
        return FALSE;
    }

    controls = (FilterControlParam*)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    scaled_radius = adjustments_scale_value(
        values[0], controls[0].min_value, controls[0].max_value,
        controls[0].filter_min, controls[0].filter_max);
    scaled_dot_density = adjustments_scale_value(
        values[1], controls[1].min_value, controls[1].max_value,
        controls[1].filter_min, controls[1].filter_max);
    scaled_cyan_angle = adjustments_scale_value(
        values[2], controls[2].min_value, controls[2].max_value,
        controls[2].filter_min, controls[2].filter_max);
    scaled_magenta_angle = adjustments_scale_value(
        values[3], controls[3].min_value, controls[3].max_value,
        controls[3].filter_min, controls[3].filter_max);
    scaled_yellow_angle = adjustments_scale_value(
        values[4], controls[4].min_value, controls[4].max_value,
        controls[4].filter_min, controls[4].filter_max);

    filter_values[0] = (gfloat)scaled_radius;
    filter_values[1] = (gfloat)scaled_dot_density;
    filter_values[2] = (gfloat)scaled_cyan_angle;
    filter_values[3] = (gfloat)scaled_magenta_angle;
    filter_values[4] = (gfloat)scaled_yellow_angle;

    /* Set up viewport-based filter */
    setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_color_halftone_apply,
                          filter_values, 5);

    return TRUE;
}

/**
 * Effects > Pixelate > Color Halftone callback
 */
static void on_pixelate_halftone(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[5];
    gdouble values[5];
    gint response;
    gfloat filter_values[5];

    if (!ctx) {
        return;
    }

    /* Control 0: Radius (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Radius";
    controls[0].min_value = 4.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 5.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 4.0;
    controls[0].filter_max = 100.0;

    /* Control 1: Dot Density (double) */
    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "Dot Density";
    controls[1].min_value = 0.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 100.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 100.0;

    /* Control 2: Cyan Angle (double) */
    controls[2].type = FILTER_CONTROL_DOUBLE;
    controls[2].label = "Cyan Angle";
    controls[2].min_value = 0.0;
    controls[2].max_value = 360.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = 0.0;
    controls[2].filter_max = 360.0;

    /* Control 3: Magenta Angle (double) */
    controls[3].type = FILTER_CONTROL_DOUBLE;
    controls[3].label = "Magenta Angle";
    controls[3].min_value = 0.0;
    controls[3].max_value = 360.0;
    controls[3].default_value = 33.3;
    controls[3].step = 1.0;
    controls[3].decimals = 0;
    controls[3].filter_min = 0.0;
    controls[3].filter_max = 360.0;

    /* Control 4: Yellow Angle (double) */
    controls[4].type = FILTER_CONTROL_DOUBLE;
    controls[4].label = "Yellow Angle";
    controls[4].min_value = 0.0;
    controls[4].max_value = 360.0;
    controls[4].default_value = 66.7;
    controls[4].step = 1.0;
    controls[4].decimals = 0;
    controls[4].filter_min = 0.0;
    controls[4].filter_max = 360.0;

    response = ui_show_filter_dialog(ctx, "Color Halftone", controls, 5,
                                     on_color_halftone_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_dot_density = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gdouble scaled_cyan_angle = adjustments_scale_value(
            values[2], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);
        gdouble scaled_magenta_angle = adjustments_scale_value(
            values[3], controls[3].min_value, controls[3].max_value,
            controls[3].filter_min, controls[3].filter_max);
        gdouble scaled_yellow_angle = adjustments_scale_value(
            values[4], controls[4].min_value, controls[4].max_value,
            controls[4].filter_min, controls[4].filter_max);

        filter_values[0] = (gfloat)scaled_radius;
        filter_values[1] = (gfloat)scaled_dot_density;
        filter_values[2] = (gfloat)scaled_cyan_angle;
        filter_values[3] = (gfloat)scaled_magenta_angle;
        filter_values[4] = (gfloat)scaled_yellow_angle;

        ui_apply_layer_filter_with_value(ctx, filter_color_halftone_apply,
                                         "Color Halftone", filter_values, 5);
    }
}

/**
 * Effects > Pixelate > Pointillize callback
 */
static void on_pixelate_pointillize(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[4]; /* cellSize + RGB (3 values) = 4 total */
    gint response;
    gfloat filter_values[4];

    if (!ctx) {
        return;
    }

    /* Control 0: Cell Size (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Cell Size";
    controls[0].min_value = 3.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 10.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 3.0;
    controls[0].filter_max = 100.0;

    /* Control 1: Background Color (RGB) */
    controls[1].type = FILTER_CONTROL_RGB;
    controls[1].label = "Background Color";
    controls[1].default_r = 1.0; /* White by default */
    controls[1].default_g = 1.0;
    controls[1].default_b = 1.0;

    response = ui_show_filter_dialog(ctx, "Pointillize", controls, 2,
                                     on_pointillize_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Values: [cellSize, r, g, b] */
        filter_values[0] = (gfloat)values[0]; /* cellSize */
        filter_values[1] = (gfloat)values[1]; /* r */
        filter_values[2] = (gfloat)values[2]; /* g */
        filter_values[3] = (gfloat)values[3]; /* b */

        ui_apply_layer_filter_with_value(ctx, filter_pointillize_apply,
                                         "Pointillize", filter_values, 4);
    }
}

/**
 * Effects > Render > Clouds callback
 */
static void on_render_clouds(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    CloudsDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
    gint response;
    CloudParams params;

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return;
    }

    layer = document_get_selected_layer(doc);
    if (!layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create clouds dialog */
    dialog = clouds_dialog_new("Render Clouds");
    if (!dialog) {
        g_warning("Failed to create clouds dialog");
        return;
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        clouds_dialog_free(dialog);
        return;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Set layers in dialog */
    clouds_dialog_set_layers(dialog, layer, temp_layer);

    /* Store original layer reference for preview callback */
    g_object_set_data(G_OBJECT(clouds_dialog_get_window(dialog)), "original_layer", layer);

    /* Dialog handles preview internally via update_preview */

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(clouds_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = clouds_dialog_run(dialog, GTK_WINDOW(ctx->window), &params);

    if (response == GTK_RESPONSE_OK) {
        /* Apply clouds filter directly */
        Command* cmd = command_create_draw(layer, "Render Clouds");
        if (cmd) {
            /* Start timing */
            gint64 start_time = g_get_monotonic_time();

            gboolean success = filter_render_clouds_apply(layer, &params);

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
        }
    }

    /* Clean up */
    g_object_set_data(G_OBJECT(clouds_dialog_get_window(dialog)), "original_layer", NULL);
    g_object_set_data(G_OBJECT(clouds_dialog_get_window(dialog)), "clouds_params", NULL);
    clouds_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Setup Effects menu from Glade builder
 */
void ui_filter_effects_setup_menu(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* effects_menu = GTK_WIDGET(gtk_builder_get_object(builder, "effects_menu"));
    GtkWidget* effects_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "effects_menu_item"));

    if (effects_menu && effects_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(effects_menu_item), effects_menu);
    }

    /* Connect Blur submenu signals */
    GtkWidget* blur_menu_average_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_average_blur"));
    if (blur_menu_average_blur) {
        g_signal_connect(blur_menu_average_blur, "activate", G_CALLBACK(on_effects_average_blur), ctx);
    }

    GtkWidget* blur_menu_gaussian_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_gaussian_blur"));
    if (blur_menu_gaussian_blur) {
        g_signal_connect(blur_menu_gaussian_blur, "activate", G_CALLBACK(on_effects_gaussian_blur), ctx);
    }

    GtkWidget* blur_menu_box_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_box_blur"));
    if (blur_menu_box_blur) {
        g_signal_connect(blur_menu_box_blur, "activate", G_CALLBACK(on_effects_box_blur), ctx);
    }

    GtkWidget* blur_menu_exponential_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_exp_blur"));
    if (blur_menu_exponential_blur) {
        g_signal_connect(blur_menu_exponential_blur, "activate", G_CALLBACK(on_effects_exponential_blur), ctx);
    }

    GtkWidget* blur_menu_median_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_median_blur"));
    if (blur_menu_median_blur) {
        g_signal_connect(blur_menu_median_blur, "activate", G_CALLBACK(on_effects_median_blur), ctx);
    }

    GtkWidget* blur_menu_motion_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_motion_blur"));
    if (blur_menu_motion_blur) {
        g_signal_connect(blur_menu_motion_blur, "activate", G_CALLBACK(on_effects_motion_blur), ctx);
    }

    GtkWidget* blur_menu_radial_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_radial_blur"));
    if (blur_menu_radial_blur) {
        g_signal_connect(blur_menu_radial_blur, "activate", G_CALLBACK(on_effects_radial_blur), ctx);
    }

    GtkWidget* blur_menu_surface_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_surface_blur"));
    if (blur_menu_surface_blur) {
        g_signal_connect(blur_menu_surface_blur, "activate", G_CALLBACK(on_effects_surface_blur), ctx);
    }

    GtkWidget* blur_menu_zoom_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_zoom_blur"));
    if (blur_menu_zoom_blur) {
        g_signal_connect(blur_menu_zoom_blur, "activate", G_CALLBACK(on_effects_zoom_blur), ctx);
    }

    /* Connect Artistic submenu signals */
    GtkWidget* artistic_menu_film_grain = GTK_WIDGET(gtk_builder_get_object(builder, "artistic_menu_film_grain"));
    if (artistic_menu_film_grain) {
        g_signal_connect(artistic_menu_film_grain, "activate", G_CALLBACK(on_artistic_film_grain), ctx);
    }

    GtkWidget* artistic_menu_frosted_glass = GTK_WIDGET(gtk_builder_get_object(builder, "artistic_menu_frosted_glass"));
    if (artistic_menu_frosted_glass) {
        g_signal_connect(artistic_menu_frosted_glass, "activate", G_CALLBACK(on_artistic_frosted_glass), ctx);
    }

    GtkWidget* artistic_menu_oil_paint = GTK_WIDGET(gtk_builder_get_object(builder, "artistic_menu_oil_paint"));
    if (artistic_menu_oil_paint) {
        g_signal_connect(artistic_menu_oil_paint, "activate", G_CALLBACK(on_artistic_oil_paint), ctx);
    }

    GtkWidget* artistic_menu_relief = GTK_WIDGET(gtk_builder_get_object(builder, "artistic_menu_relief"));
    if (artistic_menu_relief) {
        g_signal_connect(artistic_menu_relief, "activate", G_CALLBACK(on_artistic_relief), ctx);
    }

    /* Connect Edge Detect submenu signals */
    GtkWidget* edge_menu_canny = GTK_WIDGET(gtk_builder_get_object(builder, "edge_menu_canny"));
    if (edge_menu_canny) {
        g_signal_connect(edge_menu_canny, "activate", G_CALLBACK(on_edge_canny), ctx);
    }

    GtkWidget* edge_menu_gradient = GTK_WIDGET(gtk_builder_get_object(builder, "edge_menu_gradient"));
    if (edge_menu_gradient) {
        g_signal_connect(edge_menu_gradient, "activate", G_CALLBACK(on_edge_gradient), ctx);
    }

    GtkWidget* edge_menu_laplacian = GTK_WIDGET(gtk_builder_get_object(builder, "edge_menu_laplacian"));
    if (edge_menu_laplacian) {
        g_signal_connect(edge_menu_laplacian, "activate", G_CALLBACK(on_edge_laplacian), ctx);
    }

    GtkWidget* edge_menu_prewiitt = GTK_WIDGET(gtk_builder_get_object(builder, "edge_menu_prewiitt"));
    if (edge_menu_prewiitt) {
        g_signal_connect(edge_menu_prewiitt, "activate", G_CALLBACK(on_edge_prewitt), ctx);
    }

    GtkWidget* edge_menu_roberts = GTK_WIDGET(gtk_builder_get_object(builder, "edge_menu_roberts"));
    if (edge_menu_roberts) {
        g_signal_connect(edge_menu_roberts, "activate", G_CALLBACK(on_edge_roberts), ctx);
    }

    GtkWidget* edge_menu_sobel = GTK_WIDGET(gtk_builder_get_object(builder, "edge_menu_sobel"));
    if (edge_menu_sobel) {
        g_signal_connect(edge_menu_sobel, "activate", G_CALLBACK(on_edge_sobel), ctx);
    }

    /* Connect Pixelate submenu signals */
    GtkWidget* pixelate_menu_crystallize = GTK_WIDGET(gtk_builder_get_object(builder, "pixelate_menu_crystallize"));
    if (pixelate_menu_crystallize) {
        g_signal_connect(pixelate_menu_crystallize, "activate", G_CALLBACK(on_pixelate_crystallize), ctx);
    }

    GtkWidget* pixelate_menu_fragment = GTK_WIDGET(gtk_builder_get_object(builder, "pixelate_menu_fragment"));
    if (pixelate_menu_fragment) {
        g_signal_connect(pixelate_menu_fragment, "activate", G_CALLBACK(on_pixelate_fragment), ctx);
    }

    GtkWidget* pixelate_menu_halftone = GTK_WIDGET(gtk_builder_get_object(builder, "pixelate_menu_halftone"));
    if (pixelate_menu_halftone) {
        g_signal_connect(pixelate_menu_halftone, "activate", G_CALLBACK(on_pixelate_halftone), ctx);
    }

    GtkWidget* pixelate_menu_mosaic = GTK_WIDGET(gtk_builder_get_object(builder, "pixelate_menu_mosaic"));
    if (pixelate_menu_mosaic) {
        g_signal_connect(pixelate_menu_mosaic, "activate", G_CALLBACK(on_pixelate_mosaic), ctx);
    }

    GtkWidget* pixelate_menu_pointillize = GTK_WIDGET(gtk_builder_get_object(builder, "pixelate_menu_pointillize"));
    if (pixelate_menu_pointillize) {
        g_signal_connect(pixelate_menu_pointillize, "activate", G_CALLBACK(on_pixelate_pointillize), ctx);
    }

    /* Connect Render submenu signals */
    GtkWidget* render_menu_clouds = GTK_WIDGET(gtk_builder_get_object(builder, "render_menu_clouds"));
    if (render_menu_clouds) {
        g_signal_connect(render_menu_clouds, "activate", G_CALLBACK(on_render_clouds), ctx);
    }
}
