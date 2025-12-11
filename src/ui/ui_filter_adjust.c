#include "ui/ui_filter_adjust.h"
#include "ui/ui_filter.h"
#include "ui/widgets/filter_dialog.h"
#include "ui/filters/filter_grayscale.h"
#include "ui/filters/filter_vibrance.h"
#include "ui/filters/filter_whitebalance.h"
#include "ui/filters/filter_equalize.h"
#include "ui/filters/filter_stretch.h"
#include "ui/filters/filter_backlight.h"
#include "ui/filters/filter_sepia.h"
#include "ui/filters/filter_gamma.h"
#include "ui/widgets/gamma_dialog.h"
#include "ui/filters/filter_colorbalance.h"
#include "ui/widgets/color_balance_dialog.h"
#include "ui/filters/filter_exposure.h"
#include "ui/filters/filter_hsl.h"
#include "ui/filters/filter_brightness_contrast.h"
#include "ui/filters/filter_shadow_highlights.h"
#include "filters.h"
#include <glib.h>

/**
 * Vibrance filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_vibrance_preview_update(FilterDialog *dialog,
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

    /* Scale UI value to filter range and apply vibrance filter to temp layer */
    {
        gdouble scaled_vibrance = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        gfloat filter_values[1] = { (gfloat)scaled_vibrance };
        if (!filter_vibrance_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Exposure filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_exposure_preview_update(FilterDialog *dialog,
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
    original_layer = (ImageLayer *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");

    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI value to filter range and apply exposure filter to temp layer */
    {
        gdouble scaled_exposure = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gfloat filter_values[1] = { (gfloat)scaled_exposure };
        if (!filter_exposure_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * HSL filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_hsl_preview_update(FilterDialog *dialog,
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

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");

    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI values to filter range and apply HSL filter to temp layer */
    {
        gdouble scaled_hue = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_saturation = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gdouble scaled_lightness = adjustments_scale_value(
            values[2], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);
        gfloat filter_values[3] = { 
            (gfloat)scaled_hue, 
            (gfloat)scaled_saturation, 
            (gfloat)scaled_lightness 
        };
        if (!filter_hsl_apply(temp_layer, filter_values, 3)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Brightness/Contrast filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_brightness_contrast_preview_update(FilterDialog *dialog,
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

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");

    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI values to filter range and apply brightness/contrast filter to temp layer */
    {
        gdouble scaled_brightness = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_contrast = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gfloat filter_values[2] = { 
            (gfloat)scaled_brightness, 
            (gfloat)scaled_contrast 
        };
        if (!filter_brightness_contrast_apply(temp_layer, filter_values, 2)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Shadow/Highlights filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_shadow_highlights_preview_update(FilterDialog *dialog,
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

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");

    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI values to filter range and apply shadow/highlights filter to temp layer */
    {
        gdouble scaled_shadows = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_midtone_contrast = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gdouble scaled_highlights = adjustments_scale_value(
            values[2], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);
        gfloat filter_values[3] = { 
            (gfloat)scaled_shadows, 
            (gfloat)scaled_midtone_contrast,
            (gfloat)scaled_highlights
        };
        if (!filter_shadow_highlights_apply(temp_layer, filter_values, 3)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * White balance filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_whitebalance_preview_update(FilterDialog *dialog,
                                           const gdouble *values,
                                           gint num_values,
                                           gpointer user_data) {
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    FilterControlParam *controls;
    cairo_t *cr;

    if (!dialog || !values || num_values < 2 || !temp_layer) {
        return FALSE;
    }

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "original_layer");

    if (!original_layer) {
        return FALSE;
    }

    /* Get control parameters from dialog's stored data */
    controls = (FilterControlParam *)g_object_get_data(
        G_OBJECT(filter_dialog_get_window(dialog)), "control_params");
    if (!controls) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Scale UI value to filter range and apply white balance filter to temp layer */
    {
        gdouble scaled_temperature = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);
        gdouble scaled_tint = adjustments_scale_value(
            values[1], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        gfloat filter_values[2] = { (gfloat)scaled_temperature, (gfloat)scaled_tint };
        if (!filter_whitebalance_apply(temp_layer, filter_values, 2)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Adjustments > Grayscale callback
 */
static void on_adjust_grayscale(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ui_apply_layer_filter(ctx, filter_grayscale_apply, "grayscale");
}

/**
 * Adjustments > White balance callback
 */
static void on_adjust_whitebalance(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat scaled_temperature;
    gfloat scaled_tint;

    if (!ctx) {
        return;
    }

    /* Define white balance control parameters */
    controls[0].label = "Temperature (K):";
    controls[0].min_value = 4000.0;
    controls[0].max_value = 7500.0;
    controls[0].default_value = 5000.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 4000;
    controls[0].filter_max = 7500;

    controls[1].label = "Tint";
    controls[1].min_value = -200.0;
    controls[1].max_value = 200.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = -200.0;
    controls[1].filter_max = 200.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "White Balance", controls, 2,
                                     on_whitebalance_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
      /* Scale UI value to filter range */
      scaled_temperature = adjustments_scale_value(
          values[0], controls[0].min_value, controls[0].max_value,
          controls[0].filter_min, controls[0].filter_max);
      scaled_tint = adjustments_scale_value(
          values[1], controls[1].min_value, controls[1].max_value,
          controls[1].filter_min, controls[1].filter_max);

      /* Apply white balance filter */
      gfloat filter_values[2] = { (gfloat)scaled_temperature, (gfloat)scaled_tint };
      ui_apply_layer_filter_with_value(ctx, filter_whitebalance_apply,
                                       "whitebalance", filter_values, 2);
    }
}

/**
 * Adjustments > Vibrance callback
 */
static void on_adjust_vibrance(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_vibrance;

    if (!ctx) {
        return;
    }

    /* Define vibrance control parameter */
    controls[0].label = "vibrance";
    controls[0].min_value = -100.0;      /* UI range: -100 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;       /* Filter range: 0.0 to 1.0 */
    controls[0].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Vibrance", controls, 1, 
                                     on_vibrance_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range */
        scaled_vibrance = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );

        /* Apply vibrance filter */
        gfloat filter_values[1] = { (gfloat)scaled_vibrance };
        ui_apply_layer_filter_with_value(ctx, filter_vibrance_apply, 
                                        "vibrance", filter_values, 1);
    }
}

/**
 * Histogram > Equalize callback
 */
static void on_histogram_equalize(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ui_apply_layer_filter(ctx, filter_equalize_apply, "equalize");
}

/**
 * Histogram > Stretch callback
 */
static void on_histogram_stretch(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ui_apply_layer_filter(ctx, filter_stretch_apply, "stretch");
}

/**
 * Sepia filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_sepia_preview_update(FilterDialog *dialog,
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

    /* Scale UI value to filter range and apply sepia filter to temp layer */
    {
        gdouble scaled_intensity = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        gfloat filter_values[1] = { (gfloat)scaled_intensity };
        if (!filter_sepia_apply(temp_layer, filter_values, 1)) {
            return FALSE;
        }
    }

    /* Update preview */
    filter_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Adjustments > Sepia callback
 */
static void on_adjust_sepia(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_intensity;

    if (!ctx) {
        return;
    }

    /* Define sepia control parameter */
    controls[0].label = "Intensity";
    controls[0].min_value = 0.0;      /* UI range: 0 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 100.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;     /* Filter range: 0 to 100 */
    controls[0].filter_max = 100.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Sepia", controls, 1, 
                                     on_sepia_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range */
        scaled_intensity = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );

        /* Apply sepia filter */
        gfloat filter_values[1] = { (gfloat)scaled_intensity };
        ui_apply_layer_filter_with_value(ctx, filter_sepia_apply, 
                                        "sepia", filter_values, 1);
    }
}

/**
 * Adjustments > Backlight Repair callback
 */
static void on_adjust_backlight(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ui_apply_layer_filter(ctx, filter_backlight_apply, "backlight repair");
}

/**
 * Gamma filter preview update callback
 */
static gboolean on_gamma_preview_update(void *dialog_ptr,
                                       const gdouble *values,
                                       gint num_values,
                                       gpointer user_data)
{
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    GammaDialog *gamma_dialog;
    cairo_t *cr;
    gfloat gamma_values[3];
    GtkWindow *window;

    if (!values || num_values < 3 || !temp_layer) {
        return FALSE;
    }

    /* dialog_ptr is the GammaDialog* */
    gamma_dialog = (GammaDialog *)dialog_ptr;
    if (!gamma_dialog) {
        return FALSE;
    }

    window = gamma_dialog_get_window(gamma_dialog);
    if (!window) {
        return FALSE;
    }

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(window), "original_layer");
    
    if (!original_layer) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Apply gamma filter to temp layer */
    gamma_values[0] = (gfloat)values[0];
    gamma_values[1] = (gfloat)values[1];
    gamma_values[2] = (gfloat)values[2];
    if (!filter_gamma_apply(temp_layer, gamma_values, 3)) {
        return FALSE;
    }

    /* Update preview */
    gamma_dialog_update_after_layer(gamma_dialog, temp_layer);

    return TRUE;
}

/**
 * Adjustments > Gamma Correction callback
 */
static void on_adjust_gamma(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc;
    ImageLayer *layer;
    GammaDialog *dialog;
    ImageLayer *temp_layer;
    cairo_t *cr;
    gint response;
    gfloat gamma_values[3];

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Get the currently selected layer */
    layer = document_get_selected_layer(doc);
    if (!layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create gamma dialog */
    dialog = gamma_dialog_new("Gamma Correction");
    if (!dialog) {
        g_warning("Failed to create gamma dialog");
        return;
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        gamma_dialog_free(dialog);
        return;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Set layers in dialog */
    gamma_dialog_set_layers(dialog, layer, temp_layer);

    /* Store original layer reference and dialog for preview callback */
    g_object_set_data(G_OBJECT(gamma_dialog_get_window(dialog)), "original_layer", layer);
    g_object_set_data(G_OBJECT(gamma_dialog_get_window(dialog)), "gamma_dialog", dialog);

    /* Set up live preview callback */
    gamma_dialog_set_preview_callback(dialog, on_gamma_preview_update, temp_layer);

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(gamma_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = gamma_dialog_run(dialog, GTK_WINDOW(ctx->window), gamma_values);

    if (response == GTK_RESPONSE_OK) {
        /* Apply gamma filter */
        ui_apply_layer_filter_with_value(ctx, filter_gamma_apply, 
                                        "gamma correction", gamma_values, 3);
    }

    /* Clean up */
    g_object_set_data(G_OBJECT(gamma_dialog_get_window(dialog)), "original_layer", NULL);
    g_object_set_data(G_OBJECT(gamma_dialog_get_window(dialog)), "gamma_dialog", NULL);
    gamma_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Color balance filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_colorbalance_preview_update(void *dialog_ptr,
                                                const gint *values,
                                                gint num_values,
                                                OcToneBalanceMode mode,
                                                gboolean preserve_luminosity,
                                                gpointer user_data)
{
    ColorBalanceDialog *dialog = (ColorBalanceDialog *)dialog_ptr;
    ImageLayer *temp_layer = (ImageLayer *)user_data;
    ImageLayer *original_layer;
    cairo_t *cr;

    if (!dialog || !values || num_values < 3 || !temp_layer) {
        return FALSE;
    }

    /* Get original layer from dialog window */
    original_layer = (ImageLayer *)g_object_get_data(G_OBJECT(color_balance_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, original_layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Apply color balance filter to temp layer */
    if (!filter_colorbalance_apply(temp_layer, values[0], values[1], values[2], mode, preserve_luminosity)) {
        return FALSE;
    }

    /* Update preview */
    color_balance_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Adjustments > Color Balance callback
 */
static void on_adjust_colorbalance(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    ImageDocument *doc;
    ImageLayer *layer;
    ColorBalanceDialog *dialog;
    ImageLayer *temp_layer;
    cairo_t *cr;
    gint response;
    gint red_balance, green_balance, blue_balance;
    OcToneBalanceMode mode;
    gboolean preserve_luminosity;
    gfloat filter_values[5];

    if (!ctx) {
        return;
    }

    doc = ui_get_active_document(ctx);
    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Get the currently selected layer */
    layer = document_get_selected_layer(doc);
    if (!layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create color balance dialog */
    dialog = color_balance_dialog_new("Color Balance");
    if (!dialog) {
        g_warning("Failed to create color balance dialog");
        return;
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        color_balance_dialog_free(dialog);
        return;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Set layers in dialog */
    color_balance_dialog_set_layers(dialog, layer, temp_layer);

    /* Store original layer reference for preview callback */
    g_object_set_data(G_OBJECT(color_balance_dialog_get_window(dialog)), "original_layer", layer);

    /* Set up live preview callback */
    color_balance_dialog_set_preview_callback(dialog, on_colorbalance_preview_update, temp_layer);

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(color_balance_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = color_balance_dialog_run(dialog, GTK_WINDOW(ctx->window), 
                                         &red_balance, &green_balance, &blue_balance,
                                         &mode, &preserve_luminosity);

    if (response == GTK_RESPONSE_OK) {
        /* Prepare filter values array */
        filter_values[0] = (gfloat)red_balance;
        filter_values[1] = (gfloat)green_balance;
        filter_values[2] = (gfloat)blue_balance;
        filter_values[3] = (gfloat)mode;
        filter_values[4] = (gfloat)preserve_luminosity;

        /* Apply color balance filter */
        ui_apply_layer_filter_with_value(ctx, filter_colorbalance_apply_values, 
                                        "color balance", filter_values, 5);
    }

    /* Clean up */
    g_object_set_data(G_OBJECT(color_balance_dialog_get_window(dialog)), "original_layer", NULL);
    color_balance_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Adjustments > Exposure callback
 */
static void on_adjust_exposure(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_exposure;

    if (!ctx) {
        return;
    }

    /* Define exposure control parameter */
    controls[0].label = "exposure";
    controls[0].min_value = -5.0;  
    controls[0].max_value = 5.0;
    controls[0].default_value = 0.0;
    controls[0].step = 0.01;
    controls[0].decimals = 2;
    controls[0].filter_min = -5.0;   
    controls[0].filter_max = 5.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Exposure", controls, 1, 
                                     on_exposure_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range */
        scaled_exposure = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );

        /* Apply exposure filter */
        gfloat filter_values[1] = { (gfloat)scaled_exposure };
        ui_apply_layer_filter_with_value(ctx, filter_exposure_apply, 
                                        "exposure", filter_values, 1);
    }
}

/**
 * Adjustments > HSL callback
 */
static void on_adjust_hsl(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[3];
    gdouble values[3];
    gint response;
    gdouble scaled_hue, scaled_saturation, scaled_lightness;

    if (!ctx) {
        return;
    }

    /* Define HSL control parameters */
    controls[0].label = "hue";
    controls[0].min_value = -180.0; 
    controls[0].max_value = 180.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;
    controls[0].filter_max = 1.0;

    controls[1].label = "saturation";
    controls[1].min_value = -100.0; 
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 1.0;

    controls[2].label = "lightness";
    controls[2].min_value = -100.0; 
    controls[2].max_value = 100.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = 0.0; 
    controls[2].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "HSL", controls, 3, 
                                    on_hsl_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range */
        scaled_hue = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        scaled_saturation = adjustments_scale_value(
            values[1],
            controls[1].min_value,
            controls[1].max_value,
            controls[1].filter_min,
            controls[1].filter_max
        );
        scaled_lightness = adjustments_scale_value(
            values[2],
            controls[2].min_value,
            controls[2].max_value,
            controls[2].filter_min,
            controls[2].filter_max
        );

        /* Apply HSL filter */
        gfloat filter_values[3] = { 
            (gfloat)scaled_hue, 
            (gfloat)scaled_saturation, 
            (gfloat)scaled_lightness 
        };
        ui_apply_layer_filter_with_value(ctx, filter_hsl_apply, 
                                        "HSL", filter_values, 3);
    }
}

/**
 * Adjustments > Brightness/Contrast callback
 */
static void on_adjust_brightness_contrast(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gdouble scaled_brightness, scaled_contrast;

    if (!ctx) {
        return;
    }

    /* Define brightness/contrast control parameters */
    controls[0].label = "brightness";
    controls[0].min_value = -255.0;  /* UI range: -255 to 255 */
    controls[0].max_value = 255.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = -1.0;  /* Filter range: -1.0 to 1.0 */
    controls[0].filter_max = 1.0;

    controls[1].label = "contrast";
    controls[1].min_value = -100.0;  /* UI range: -100 to 100 */
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = -1.0;  /* Filter range: -1.0 to 1.0 */
    controls[1].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Brightness and Contrast", controls, 2, 
                                    on_brightness_contrast_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range */
        scaled_brightness = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        scaled_contrast = adjustments_scale_value(
            values[1],
            controls[1].min_value,
            controls[1].max_value,
            controls[1].filter_min,
            controls[1].filter_max
        );

        /* Apply brightness/contrast filter */
        gfloat filter_values[2] = { 
            (gfloat)scaled_brightness, 
            (gfloat)scaled_contrast 
        };
        ui_apply_layer_filter_with_value(ctx, filter_brightness_contrast_apply, 
                                        "brightness and contrast", filter_values, 2);
    }
}

/**
 * Adjustments > Shadows/Highlights callback
 */
static void on_adjust_shadow_highlights(GtkWidget *widget, gpointer data)
{
    (void)widget;  /* Unused */

    AppContext *ctx = (AppContext *)data;
    FilterControlParam controls[3];
    gdouble values[3];
    gint response;
    gdouble scaled_shadows, scaled_highlights, scaled_midtone_contrast;

    if (!ctx) {
        return;
    }

    /* Define shadow/highlights control parameters */
    controls[0].label = "shadows";
    controls[0].min_value = -100.0;  /* UI range: -100 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = -1.0;  /* Filter range: -1.0 to 1.0 */
    controls[0].filter_max = 1.0;

    controls[1].label = "midtone contrast";
    controls[1].min_value = -100.0;  /* UI range: -100 to 100 */
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = -1.0;  /* Filter range: -1.0 to 1.0 */
    controls[1].filter_max = 1.0;

    controls[2].label = "highlights";
    controls[2].min_value = -100.0;  /* UI range: -100 to 100 */
    controls[2].max_value = 100.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = -1.0;  /* Filter range: -1.0 to 1.0 */
    controls[2].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Shadows and Highlights", controls, 3, 
                                    on_shadow_highlights_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range */
        /* UI order: shadows (values[0]), midtone contrast (values[1]), highlights (values[2]) */
        scaled_shadows = adjustments_scale_value(
            values[0],
            controls[0].min_value,
            controls[0].max_value,
            controls[0].filter_min,
            controls[0].filter_max
        );
        scaled_midtone_contrast = adjustments_scale_value(
            values[1],
            controls[1].min_value,
            controls[1].max_value,
            controls[1].filter_min,
            controls[1].filter_max
        );
        scaled_highlights = adjustments_scale_value(
            values[2],
            controls[2].min_value,
            controls[2].max_value,
            controls[2].filter_min,
            controls[2].filter_max
        );

        /* Apply shadow/highlights filter */
        /* Filter order: shadows, midtone contrast, highlights */
        gfloat filter_values[3] = { 
            (gfloat)scaled_shadows, 
            (gfloat)scaled_midtone_contrast,
            (gfloat)scaled_highlights
        };
        ui_apply_layer_filter_with_value(ctx, filter_shadow_highlights_apply, 
                                        "shadows and highlights", filter_values, 3);
    }
}

/**
 * Setup Adjustments menu from Glade builder
 */
void ui_filter_adjust_setup_menu(GtkBuilder *builder, AppContext *ctx)
{
    GtkWidget *adjust_menu = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu"));
    GtkWidget *adjust_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_item"));
    
    if (adjust_menu && adjust_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(adjust_menu_item), adjust_menu);
    }
    
    /* Connect Adjustments menu signals */
    GtkWidget *adjust_menu_grayscale = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_grayscale"));
    if (adjust_menu_grayscale) {
        g_signal_connect(adjust_menu_grayscale, "activate", G_CALLBACK(on_adjust_grayscale), ctx);
    }

    GtkWidget *adjust_menu_whitebalance = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_whitebalance"));
    if (adjust_menu_whitebalance) {
        g_signal_connect(adjust_menu_whitebalance, "activate", G_CALLBACK(on_adjust_whitebalance), ctx);
    }
    
    GtkWidget *adjust_menu_vibrance = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_vibrance"));
    if (adjust_menu_vibrance) {
        g_signal_connect(adjust_menu_vibrance, "activate", G_CALLBACK(on_adjust_vibrance), ctx);
    }
    
    /* Connect Histogram menu signals */
    GtkWidget *histogram_menu_equalize = GTK_WIDGET(gtk_builder_get_object(builder, "histogram_menu_equalize"));
    if (histogram_menu_equalize) {
        g_signal_connect(histogram_menu_equalize, "activate", G_CALLBACK(on_histogram_equalize), ctx);
    }
    
    GtkWidget *histogram_menu_stretch = GTK_WIDGET(gtk_builder_get_object(builder, "histogram_menu_stretch"));
    if (histogram_menu_stretch) {
        g_signal_connect(histogram_menu_stretch, "activate", G_CALLBACK(on_histogram_stretch), ctx);
    }
    
    /* Connect additional Adjustments menu signals */
    GtkWidget *adjust_menu_sepia = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_sepia"));
    if (adjust_menu_sepia) {
        g_signal_connect(adjust_menu_sepia, "activate", G_CALLBACK(on_adjust_sepia), ctx);
    }
    
    GtkWidget *adjust_menu_backlight = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_backlight"));
    if (adjust_menu_backlight) {
        g_signal_connect(adjust_menu_backlight, "activate", G_CALLBACK(on_adjust_backlight), ctx);
    }
    
    GtkWidget *adjust_menu_gamma = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_gamma"));
    if (adjust_menu_gamma) {
        g_signal_connect(adjust_menu_gamma, "activate", G_CALLBACK(on_adjust_gamma), ctx);
    }

    GtkWidget *adjust_menu_colorbalance = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_colorbalance"));
    if (adjust_menu_colorbalance) {
        g_signal_connect(adjust_menu_colorbalance, "activate", G_CALLBACK(on_adjust_colorbalance), ctx);
    }

    GtkWidget *adjust_menu_exposure = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_exposure"));
    if (adjust_menu_exposure) {
        g_signal_connect(adjust_menu_exposure, "activate", G_CALLBACK(on_adjust_exposure), ctx);
    }

    GtkWidget *adjust_menu_hsl = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_hsl"));
    if (adjust_menu_hsl) {
        g_signal_connect(adjust_menu_hsl, "activate", G_CALLBACK(on_adjust_hsl), ctx);
    }

    GtkWidget *adjust_menu_brightness_contrast = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_brightness_contrast"));
    if (adjust_menu_brightness_contrast) {
        g_signal_connect(adjust_menu_brightness_contrast, "activate", G_CALLBACK(on_adjust_brightness_contrast), ctx);
    }

    GtkWidget *adjust_menu_shadows_highlights = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_shadows_highlights"));
    if (adjust_menu_shadows_highlights) {
        g_signal_connect(adjust_menu_shadows_highlights, "activate", G_CALLBACK(on_adjust_shadow_highlights), ctx);
    }
}

