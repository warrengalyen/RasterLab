#include "ui/ui_filter_effects.h"
#include "ui/ui_filter.h"
#include "ui/widgets/filter_dialog.h"
#include "ui/filters/filter_average_blur.h"
#include "ui/filters/filter_gaussian_blur.h"
#include "ui/filters/filter_box_blur.h"
#include "ui/filters/filter_median_blur.h"
#include "ui/filters/filter_motion_blur.h"
#include "ui/filters/filter_radial_blur.h"
#include "ui/filters/filter_surface_blur.h"
#include "ui/filters/filter_zoom_blur.h"
#include "ui/filters/filter_exponential_blur.h"
#include "document.h"
#include "filters.h"
#include <glib.h>

/**
 * Average blur filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_average_blur_preview_update(FilterDialog *dialog,
                                               const gdouble *values,
                                               gint num_values,
                                               gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 1 || !temp_layer) {
        return FALSE;
    }

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    
    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI value to filter range and apply average blur filter to temp layer */
    {
        gdouble scaled_radius = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        gfloat filter_values[1] = { (gfloat)scaled_radius };
        if (!filter_average_blur_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Gaussian blur filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_gaussian_blur_preview_update(FilterDialog *dialog,
                                               const gdouble *values,
                                               gint num_values,
                                               gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 1 || !temp_layer) {
        return FALSE;
    }

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    
    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI value to filter range and apply Gaussian blur filter to temp layer */
    {
        gdouble scaled_sigma = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        gfloat filter_values[1] = { (gfloat)scaled_sigma };
        if (!filter_gaussian_blur_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Effects > Blur > Average Blur callback
 */
static void on_effects_average_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
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
    controls[0].min_value = 1.0;  /* UI range: 1 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 3.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;  /* Filter range: 1 to 100 */
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
            controls[0].filter_max
        );
        
        /* Apply average blur filter */
        filter_values[0] = (gfloat)scaled_radius;
        ui_apply_layer_filter_with_value(ctx, filter_average_blur_apply,
                                        "Average Blur", filter_values, 1);
    }
}

/**
 * Effects > Blur > Gaussian Blur callback
 */
static void on_effects_gaussian_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
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
            controls[0].filter_max
        );
        
        /* Apply Gaussian blur filter */
        filter_values[0] = (gfloat)scaled_sigma;
        ui_apply_layer_filter_with_value(ctx, filter_gaussian_blur_apply,
                                        "Gaussian Blur", filter_values, 1);
    }
}

/**
 * Exponential blur filter preview update callback
 */
static gboolean on_exponential_blur_preview_update(FilterDialog *dialog,
                                                   const gdouble *values,
                                                   gint num_values,
                                                   gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 1 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gfloat filter_values[1] = { (gfloat)scaled_radius };
        if (!filter_exponential_blur_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Exponential Blur callback
 */
static void on_effects_exponential_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) return;

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
static gboolean on_box_blur_preview_update(FilterDialog *dialog,
                                          const gdouble *values,
                                          gint num_values,
                                          gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 1 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gfloat filter_values[1] = { (gfloat)scaled_radius };
        if (!filter_box_blur_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Box Blur callback
 */
static void on_effects_box_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) return;

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
static gboolean on_median_blur_preview_update(FilterDialog *dialog,
                                             const gdouble *values,
                                             gint num_values,
                                             gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 1 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gfloat filter_values[1] = { (gfloat)scaled_radius };
        if (!filter_median_blur_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Median Blur callback
 */
static void on_effects_median_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) return;

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
static gboolean on_motion_blur_preview_update(FilterDialog *dialog,
                                             const gdouble *values,
                                             gint num_values,
                                             gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 2 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        gdouble scaled_distance = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_angle = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gfloat filter_values[2] = { (gfloat)scaled_distance, (gfloat)scaled_angle };
        if (!filter_motion_blur_apply(temp_layer, filter_values, 2)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Motion Blur callback
 */
static void on_effects_motion_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx) return;

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
static gboolean on_radial_blur_preview_update(FilterDialog *dialog,
                                             const gdouble *values,
                                             gint num_values,
                                             gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 3 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        /* Convert normalized center coordinates (0.0-1.0) directly to pixel coordinates */
        /* values[0] and values[1] are already in 0.0-1.0 range from the UI */
        gint center_x = (gint)(values[0] * original_layer->width);
        gint center_y = (gint)(values[1] * original_layer->height);
        
        gdouble scaled_intensity = adjustments_scale_value(
            values[2], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);
        gfloat filter_values[3] = { (gfloat)center_x, (gfloat)center_y, (gfloat)scaled_intensity };
        if (!filter_radial_blur_apply(temp_layer, filter_values, 3)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Radial Blur callback
 */
static void on_effects_radial_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc;
    ImageLayer *layer;
    FilterControlParam controls[3];
    gdouble values[3];
    gint response;
    gfloat filter_values[3];

    if (!ctx) return;

    doc = ui_get_active_document(ctx);
    layer = doc ? document_get_selected_layer(doc) : NULL;
    if (!layer) return;

    controls[0].label = "center X";
    controls[0].min_value = 0.0;
    controls[0].max_value = 1.0;
    controls[0].default_value = 0.5;  /* Center of image */
    controls[0].step = 0.01;
    controls[0].decimals = 2;
    controls[0].filter_min = 0.0;  /* Normalized range 0.0-1.0 */
    controls[0].filter_max = 1.0;

    controls[1].label = "center Y";
    controls[1].min_value = 0.0;
    controls[1].max_value = 1.0;
    controls[1].default_value = 0.5;  /* Center of image */
    controls[1].step = 0.01;
    controls[1].decimals = 2;
    controls[1].filter_min = 0.0;  /* Normalized range 0.0-1.0 */
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
static gboolean on_surface_blur_preview_update(FilterDialog *dialog,
                                              const gdouble *values,
                                              gint num_values,
                                              gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 2 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        gdouble scaled_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_threshold = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gfloat filter_values[2] = { (gfloat)scaled_radius, (gfloat)scaled_threshold };
        if (!filter_surface_blur_apply(temp_layer, filter_values, 2)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Surface Blur callback
 */
static void on_effects_surface_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat filter_values[2];

    if (!ctx) return;

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
static gboolean on_zoom_blur_preview_update(FilterDialog *dialog,
                                          const gdouble *values,
                                          gint num_values,
                                          gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 4 || !temp_layer) {
        return FALSE;
    }

    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    controls = (FilterControlParam *)g_object_get_data(G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    {
        gdouble scaled_sample_radius = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_blur_amount = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        
        /* Convert normalized center coordinates (0.0-1.0) directly to pixel coordinates */
        /* values[2] and values[3] are already in 0.0-1.0 range from the UI */
        gint center_x = (gint)(values[2] * original_layer->width);
        gint center_y = (gint)(values[3] * original_layer->height);
        
        gfloat filter_values[4] = {
            (gfloat)scaled_sample_radius,
            (gfloat)scaled_blur_amount,
            (gfloat)center_x,
            (gfloat)center_y
        };
        if (!filter_zoom_blur_apply(temp_layer, filter_values, 4)) {
            return FALSE;
        }
    }

    filter_dialog_update_after_layer(dialog, temp_layer);
    return TRUE;
}

/**
 * Effects > Blur > Zoom Blur callback
 */
static void on_effects_zoom_blur(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc;
    ImageLayer *layer;
    FilterControlParam controls[4];
    gdouble values[4];
    gint response;
    gfloat filter_values[4];

    if (!ctx) return;

    doc = ui_get_active_document(ctx);
    layer = doc ? document_get_selected_layer(doc) : NULL;
    if (!layer) return;

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
    controls[2].default_value = 0.5;  /* Center of image */
    controls[2].step = 0.01;
    controls[2].decimals = 2;
    controls[2].filter_min = 0.0;  /* Normalized range 0.0-1.0 */
    controls[2].filter_max = 1.0;

    controls[3].label = "center Y";
    controls[3].min_value = 0.0;
    controls[3].max_value = 1.0;
    controls[3].default_value = 0.5;  /* Center of image */
    controls[3].step = 0.01;
    controls[3].decimals = 2;
    controls[3].filter_min = 0.0;  /* Normalized range 0.0-1.0 */
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
 * Setup Effects menu from Glade builder
 */
void ui_filter_effects_setup_menu(GtkBuilder *builder, AppContext *ctx)
{
    GtkWidget *effects_menu = GTK_WIDGET(gtk_builder_get_object(builder, "effects_menu"));
    GtkWidget *effects_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "effects_menu_item"));
    
    if (effects_menu && effects_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(effects_menu_item), effects_menu);
    }
    
    /* Connect Blur submenu signals */
    GtkWidget *blur_menu_average_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_average_blur"));
    if (blur_menu_average_blur) {
        g_signal_connect(blur_menu_average_blur, "activate", G_CALLBACK(on_effects_average_blur), ctx);
    }

    GtkWidget *blur_menu_gaussian_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_gaussian_blur"));
    if (blur_menu_gaussian_blur) {
        g_signal_connect(blur_menu_gaussian_blur, "activate", G_CALLBACK(on_effects_gaussian_blur), ctx);
    }

    GtkWidget *blur_menu_box_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_box_blur"));
    if (blur_menu_box_blur) {
        g_signal_connect(blur_menu_box_blur, "activate", G_CALLBACK(on_effects_box_blur), ctx);
    }

    GtkWidget *blur_menu_exponential_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_exp_blur"));
    if (blur_menu_exponential_blur) {
        g_signal_connect(blur_menu_exponential_blur, "activate", G_CALLBACK(on_effects_exponential_blur), ctx);
    }

    GtkWidget *blur_menu_median_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_median_blur"));
    if (blur_menu_median_blur) {
        g_signal_connect(blur_menu_median_blur, "activate", G_CALLBACK(on_effects_median_blur), ctx);
    }

    GtkWidget *blur_menu_motion_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_motion_blur"));
    if (blur_menu_motion_blur) {
        g_signal_connect(blur_menu_motion_blur, "activate", G_CALLBACK(on_effects_motion_blur), ctx);
    }

    GtkWidget *blur_menu_radial_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_radial_blur"));
    if (blur_menu_radial_blur) {
        g_signal_connect(blur_menu_radial_blur, "activate", G_CALLBACK(on_effects_radial_blur), ctx);
    }

    GtkWidget *blur_menu_surface_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_surface_blur"));
    if (blur_menu_surface_blur) {
        g_signal_connect(blur_menu_surface_blur, "activate", G_CALLBACK(on_effects_surface_blur), ctx);
    }

    GtkWidget *blur_menu_zoom_blur = GTK_WIDGET(gtk_builder_get_object(builder, "blur_menu_zoom_blur"));
    if (blur_menu_zoom_blur) {
        g_signal_connect(blur_menu_zoom_blur, "activate", G_CALLBACK(on_effects_zoom_blur), ctx);
    }
}

