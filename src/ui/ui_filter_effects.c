#include "ui/ui_filter_effects.h"
#include "ui/ui_filter.h"
#include "ui/widgets/filter_dialog.h"
#include "ui/filters/filter_average_blur.h"
#include "ui/filters/filter_gaussian_blur.h"
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
                                        "average blur", filter_values, 1);
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
                                        "gaussian blur", filter_values, 1);
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
}

