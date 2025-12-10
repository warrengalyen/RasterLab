#include "ui/ui_filter_adjust.h"
#include "ui/ui_filter.h"
#include "ui/widgets/filter_dialog.h"
#include "ui/filters/filter_grayscale.h"
#include "ui/filters/filter_vibrance.h"
#include "ui/filters/filter_whitebalance.h"
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
}

