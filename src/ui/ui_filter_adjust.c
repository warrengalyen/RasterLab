#include "ui/ui_filter_adjust.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/dialogs/color_balance_dialog.h"
#include "ui/dialogs/curves_dialog.h"
#include "ui/dialogs/gamma_dialog.h"
#include "ui/dialogs/palettize_dialog.h"
#include "ui/dialogs/retinex_dialog.h"
#include "ui/filters/filter_auto_contrast.h"
#include "ui/filters/filter_auto_gamma.h"
#include "ui/filters/filter_auto_level.h"
#include "ui/filters/filter_auto_threshold.h"
#include "ui/filters/filter_auto_whitebalance.h"
#include "ui/filters/filter_backlight.h"
#include "ui/filters/filter_brightness_contrast.h"
#include "ui/filters/filter_chroma_key.h"
#include "ui/filters/filter_color_invert.h"
#include "ui/filters/filter_colorbalance.h"
#include "ui/filters/filter_curves.h"
#include "ui/filters/filter_dehaze.h"
#include "ui/filters/filter_equalize.h"
#include "ui/filters/filter_exposure.h"
#include "ui/filters/filter_gamma.h"
#include "ui/filters/filter_grayscale.h"
#include "ui/filters/filter_highlight_shadow_tint.h"
#include "ui/filters/filter_hsl.h"
#include "ui/filters/filter_luminance_threshold.h"
#include "ui/filters/filter_monochrome.h"
#include "ui/filters/filter_palettize.h"
#include "ui/filters/filter_posterize.h"
#include "ui/filters/filter_retinex.h"
#include "ui/filters/filter_sepia.h"
#include "ui/filters/filter_shadow_highlights.h"
#include "ui/filters/filter_stretch.h"
#include "ui/filters/filter_temperature.h"
#include "ui/filters/filter_vibrance.h"
#include "ui/filters/filter_whitebalance.h"
#include "ui/ui_filter.h"
#include "ui/ui_filter_utils.h"
#include "ui/widgets/filter_dialog.h"
#include <glib.h>

/* Filter wrapper functions moved to ui_filter_utils.c */

/**
 * Vibrance filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_vibrance_preview_update(FilterDialog* dialog,
                                           const gdouble* values,
                                           gint num_values,
                                           gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_vibrance_apply,
        .num_values = 1};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Exposure filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_exposure_preview_update(FilterDialog* dialog,
                                           const gdouble* values,
                                           gint num_values,
                                           gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_exposure_apply,
        .num_values = 1};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * HSL filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_hsl_preview_update(FilterDialog* dialog,
                                      const gdouble* values,
                                      gint num_values,
                                      gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_hsl_apply,
        .num_values = 3};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Brightness/Contrast filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_brightness_contrast_preview_update(FilterDialog* dialog,
                                                      const gdouble* values,
                                                      gint num_values,
                                                      gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_brightness_contrast_apply,
        .num_values = 2};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Shadow/Highlights filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_shadow_highlights_preview_update(FilterDialog* dialog,
                                                    const gdouble* values,
                                                    gint num_values,
                                                    gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_shadow_highlights_apply,
        .num_values = 3};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * White balance filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_whitebalance_preview_update(FilterDialog* dialog,
                                               const gdouble* values,
                                               gint num_values,
                                               gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_whitebalance_apply,
        .num_values = 2};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Color temperature filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_temperature_preview_update(FilterDialog* dialog,
                                              const gdouble* values,
                                              gint num_values,
                                              gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_temperature_apply,
        .num_values = 2};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Adjustments > Grayscale callback
 */
static void on_adjust_grayscale(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_grayscale_apply, "Grayscale");
}

/**
 * Adjustments > White balance callback
 */
static void on_adjust_whitebalance(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gfloat scaled_temperature;
    gfloat scaled_tint;

    if (!ctx) {
        return;
    }

    /* Define white balance control parameters */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Temperature (K):";
    controls[0].min_value = 4000.0;
    controls[0].max_value = 7500.0;
    controls[0].default_value = 5000.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 4000;
    controls[0].filter_max = 7500;

    controls[1].type = FILTER_CONTROL_DOUBLE;
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
        /* Scale UI values to filter range and apply filter */
        gfloat filter_values[2];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 2)) {
            ui_apply_layer_filter_with_value(ctx, filter_whitebalance_apply,
                                             "White Balance", filter_values, 2);
        }
    }
}

/**
 * Adjustments > Vibrance callback
 */
static void on_adjust_vibrance(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_vibrance;

    if (!ctx) {
        return;
    }

    /* Define vibrance control parameter */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "vibrance";
    controls[0].min_value = -100.0; /* UI range: -100 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0; /* Filter range: 0.0 to 1.0 */
    controls[0].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Vibrance", controls, 1,
                                     on_vibrance_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range and apply filter */
        gfloat filter_values[1];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 1)) {
            ui_apply_layer_filter_with_value(ctx, filter_vibrance_apply,
                                             "Vibrance", filter_values, 1);
        }
    }
}

/**
 * Histogram > Equalize callback
 */
static void on_histogram_equalize(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_equalize_apply, "Equalize");
}

/**
 * Histogram > Stretch callback
 */
static void on_histogram_stretch(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_stretch_apply, "Histogram Stretch");
}

/**
 * Sepia filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_sepia_preview_update(FilterDialog* dialog,
                                        const gdouble* values,
                                        gint num_values,
                                        gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_sepia_apply,
        .num_values = 1};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Adjustments > Sepia callback
 */
static void on_adjust_sepia(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_intensity;

    if (!ctx) {
        return;
    }

    /* Define sepia control parameter */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Intensity";
    controls[0].min_value = 0.0; /* UI range: 0 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 100.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0; /* Filter range: 0 to 100 */
    controls[0].filter_max = 100.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Sepia", controls, 1,
                                     on_sepia_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value to filter range and apply filter */
        gfloat filter_values[1];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 1)) {
            ui_apply_layer_filter_with_value(ctx, filter_sepia_apply,
                                             "Sepia", filter_values, 1);
        }
    }
}

/**
 * Adjustments > Backlight Repair callback
 */
static void on_adjust_backlight(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_backlight_apply, "Backlight Repair");
}

/**
 * Auto white balance filter callback
 */
static void on_adjust_auto_whitebalance(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_auto_whitebalance_apply, "Auto White Balance");
}

/**
 * Auto contrast filter callback
 */
static void on_adjust_auto_contrast(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_auto_contrast_apply, "Auto Contrast");
}

/**
 * Auto gamma filter callback
 */
static void on_adjust_auto_gamma(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_auto_gamma_apply, "Auto Gamma");
}

/**
 * Auto level filter callback
 */
static void on_adjust_auto_level(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_auto_level_apply, "Auto Level");
}

/**
 * Auto threshold filter callback
 */
static void on_adjust_auto_threshold(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_auto_threshold_apply, "Auto Threshold");
}

/**
 * Gamma filter preview update callback
 */
static gboolean on_gamma_preview_update(void* dialog_ptr,
                                        const gdouble* values,
                                        gint num_values,
                                        gpointer user_data) {
    ImageLayer* temp_layer = (ImageLayer*)user_data;
    ImageLayer* original_layer;
    GammaDialog* gamma_dialog;
    cairo_t* cr;
    gfloat gamma_values[3];
    GtkWindow* window;

    if (!values || num_values < 3 || !temp_layer) {
        return FALSE;
    }

    /* dialog_ptr is the GammaDialog* */
    gamma_dialog = (GammaDialog*)dialog_ptr;
    if (!gamma_dialog) {
        return FALSE;
    }

    window = gamma_dialog_get_window(gamma_dialog);
    if (!window) {
        return FALSE;
    }

    /* Get the original layer from the dialog's stored data */
    original_layer = (ImageLayer*)g_object_get_data(G_OBJECT(window), "original_layer");

    if (!original_layer) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    if (!ui_filter_utils_copy_layer_surface(temp_layer, original_layer)) {
        return FALSE;
    }

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
static void on_adjust_gamma(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    GammaDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
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
    temp_layer = ui_filter_utils_create_temp_layer(layer);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        gamma_dialog_free(dialog);
        return;
    }

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
                                         "Gamma Correction", gamma_values, 3);
    }

    /* Clean up */
    g_object_set_data(G_OBJECT(gamma_dialog_get_window(dialog)), "original_layer", NULL);
    g_object_set_data(G_OBJECT(gamma_dialog_get_window(dialog)), "gamma_dialog", NULL);
    gamma_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Curves filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_curves_preview_update(void* dialog_ptr,
                                         gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)dialog_ptr;
    ImageLayer* temp_layer = (ImageLayer*)user_data;
    ImageLayer* original_layer;
    CurvesWidget* curves;

    if (!dialog || !temp_layer) {
        return FALSE;
    }

    /* Get original layer from dialog window */
    original_layer = (ImageLayer*)g_object_get_data(G_OBJECT(curves_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    /* Get curves widget from dialog */
    curves = curves_dialog_get_curves_widget(dialog);
    if (!curves) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    if (!ui_filter_utils_copy_layer_surface(temp_layer, original_layer)) {
        return FALSE;
    }

    /* Apply curves filter to temp layer */
    if (!filter_curves_apply(temp_layer, curves)) {
        return FALSE;
    }

    /* Update preview */
    curves_dialog_update_after_layer(dialog, temp_layer);

    return TRUE;
}

/**
 * Adjustments > Curves callback
 */
static void on_adjust_curves(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    CurvesDialog* dialog;
    ImageLayer* temp_layer;
    gint response;

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

    /* Create curves dialog */
    dialog = curves_dialog_new("Curves");
    if (!dialog) {
        g_warning("Failed to create curves dialog");
        return;
    }

    /* Create a copy of the layer for preview */
    temp_layer = ui_filter_utils_create_temp_layer(layer);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        curves_dialog_free(dialog);
        return;
    }

    /* Set layers in dialog */
    curves_dialog_set_layers(dialog, layer, temp_layer);

    /* Store original layer reference for preview callback */
    g_object_set_data(G_OBJECT(curves_dialog_get_window(dialog)), "original_layer", layer);

    /* Set up live preview callback */
    curves_dialog_set_preview_callback(dialog, on_curves_preview_update, temp_layer);

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(curves_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = curves_dialog_run(dialog, GTK_WINDOW(ctx->window));

    if (response == GTK_RESPONSE_OK) {
        /* Apply curves filter */
        CurvesWidget* curves = curves_dialog_get_curves_widget(dialog);
        if (curves) {
            /* Create a draw command for undo/redo */
            Command* cmd = command_create_draw(layer, "Curves");
            if (cmd) {
                /* Apply filter */
                if (filter_curves_apply(layer, curves)) {
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
                    ui_update_window_title(ctx);
                    ui_update_menu_and_button_states(ctx);
                } else {
                    command_free(cmd);
                }
            }
        }
    }

    /* Clean up */
    g_object_set_data(G_OBJECT(curves_dialog_get_window(dialog)), "original_layer", NULL);
    curves_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Color balance filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_colorbalance_preview_update(void* dialog_ptr,
                                               const gint* values,
                                               gint num_values,
                                               OcToneBalanceMode mode,
                                               gboolean preserve_luminosity,
                                               gpointer user_data) {
    ColorBalanceDialog* dialog = (ColorBalanceDialog*)dialog_ptr;
    ImageLayer* temp_layer = (ImageLayer*)user_data;
    ImageLayer* original_layer;
    cairo_t* cr;

    if (!dialog || !values || num_values < 3 || !temp_layer) {
        return FALSE;
    }

    /* Get original layer from dialog window */
    original_layer = (ImageLayer*)g_object_get_data(G_OBJECT(color_balance_dialog_get_window(dialog)), "original_layer");
    if (!original_layer) {
        return FALSE;
    }

    /* Copy original layer to temp layer */
    if (!ui_filter_utils_copy_layer_surface(temp_layer, original_layer)) {
        return FALSE;
    }

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
static void on_adjust_colorbalance(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    ColorBalanceDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
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
    temp_layer = ui_filter_utils_create_temp_layer(layer);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        color_balance_dialog_free(dialog);
        return;
    }

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
                                         "Color Balance", filter_values, 5);
    }

    /* Clean up */
    g_object_set_data(G_OBJECT(color_balance_dialog_get_window(dialog)), "original_layer", NULL);
    color_balance_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Adjustments > Exposure callback
 */
static void on_adjust_exposure(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gdouble scaled_exposure;

    if (!ctx) {
        return;
    }

    /* Define exposure control parameter */
    controls[0].type = FILTER_CONTROL_DOUBLE;
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
        /* Scale UI value to filter range and apply filter */
        gfloat filter_values[1];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 1)) {
            ui_apply_layer_filter_with_value(ctx, filter_exposure_apply,
                                             "Exposure", filter_values, 1);
        }
    }
}

/**
 * Adjustments > HSL callback
 */
static void on_adjust_hsl(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[3];
    gdouble values[3];
    gint response;
    gdouble scaled_hue, scaled_saturation, scaled_lightness;

    if (!ctx) {
        return;
    }

    /* Define HSL control parameters */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "hue";
    controls[0].min_value = -180.0;
    controls[0].max_value = 180.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;
    controls[0].filter_max = 1.0;

    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "saturation";
    controls[1].min_value = -100.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 1.0;

    controls[2].type = FILTER_CONTROL_DOUBLE;
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
        /* Scale UI values to filter range and apply filter */
        gfloat filter_values[3];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 3)) {
            ui_apply_layer_filter_with_value(ctx, filter_hsl_apply,
                                             "HSL", filter_values, 3);
        }
    }
}

/**
 * Adjustments > Brightness/Contrast callback
 */
static void on_adjust_brightness_contrast(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gdouble scaled_brightness, scaled_contrast;

    if (!ctx) {
        return;
    }

    /* Define brightness/contrast control parameters */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "brightness";
    controls[0].min_value = -255.0; /* UI range: -255 to 255 */
    controls[0].max_value = 255.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = -1.0; /* Filter range: -1.0 to 1.0 */
    controls[0].filter_max = 1.0;

    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "contrast";
    controls[1].min_value = -100.0; /* UI range: -100 to 100 */
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = -1.0; /* Filter range: -1.0 to 1.0 */
    controls[1].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Brightness and Contrast", controls, 2,
                                     on_brightness_contrast_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range and apply filter */
        gfloat filter_values[2];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 2)) {
            ui_apply_layer_filter_with_value(ctx, filter_brightness_contrast_apply,
                                             "Brightness And Contrast", filter_values, 2);
        }
    }
}

/**
 * Adjustments > Shadows/Highlights callback
 */
static void on_adjust_shadow_highlights(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[3];
    gdouble values[3];
    gint response;
    gdouble scaled_shadows, scaled_highlights, scaled_midtone_contrast;

    if (!ctx) {
        return;
    }

    /* Define shadow/highlights control parameters */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "shadows";
    controls[0].min_value = -100.0; /* UI range: -100 to 100 */
    controls[0].max_value = 100.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = -1.0; /* Filter range: -1.0 to 1.0 */
    controls[0].filter_max = 1.0;

    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "midtone contrast";
    controls[1].min_value = -100.0; /* UI range: -100 to 100 */
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = -1.0; /* Filter range: -1.0 to 1.0 */
    controls[1].filter_max = 1.0;

    controls[2].type = FILTER_CONTROL_DOUBLE;
    controls[2].label = "highlights";
    controls[2].min_value = -100.0; /* UI range: -100 to 100 */
    controls[2].max_value = 100.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = -1.0; /* Filter range: -1.0 to 1.0 */
    controls[2].filter_max = 1.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Shadows and Highlights", controls, 3,
                                     on_shadow_highlights_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range and apply filter */
        gfloat filter_values[3];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 3)) {
            ui_apply_layer_filter_with_value(ctx, filter_shadow_highlights_apply,
                                             "Shadows And Highlights", filter_values, 3);
        }
    }
}

/**
 * Adjustments > Color Temperature callback
 */
static void on_adjust_temperature(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[2];
    gdouble values[2];
    gint response;
    gdouble scaled_temperature, scaled_strength;

    if (!ctx) {
        return;
    }

    /* Define color temperature control parameters */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "temperature (K)";
    controls[0].min_value = 1000.0; /* UI range: 2000 to 15000 K */
    controls[0].max_value = 15000.0;
    controls[0].default_value = 6500.0; /* Daylight */
    controls[0].step = 100.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1000.0;
    controls[0].filter_max = 15000.0;

    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "strength";
    controls[1].min_value = 1.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 50.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 1.0;
    controls[1].filter_max = 100.0;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Color Temperature", controls, 2,
                                     on_temperature_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range and apply filter */
        gfloat filter_values[2];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 2)) {
            ui_apply_layer_filter_with_value(ctx, filter_temperature_apply,
                                             "Color Temperature", filter_values, 2);
        }
    }
}

/**
 * Dehaze filter preview update callback
 * Called when control values change to update the preview
 */
static gboolean on_dehaze_preview_update(FilterDialog* dialog,
                                         const gdouble* values,
                                         gint num_values,
                                         gpointer user_data) {
    static FilterApplyFuncData func_data = {
        .filter_apply_func = (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_dehaze_apply,
        .num_values = 6};
    return ui_filter_utils_preview_update_scaled(dialog, values, num_values, &func_data);
}

/**
 * Adjustments > Dehaze callback
 */
static void on_adjust_dehaze(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[6];
    gdouble values[6];
    gint response;
    gdouble scaled_radius, scaled_guide_radius, scaled_max_atm, scaled_omega, scaled_epsilon, scaled_t0;

    if (!ctx) {
        return;
    }

    /* Define dehaze control parameters */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "radius";
    controls[0].min_value = 1.0;
    controls[0].max_value = 50.0;
    controls[0].default_value = 15.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 1.0;
    controls[0].filter_max = 50.0;

    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "edge preservation radius";
    controls[1].min_value = 10.0;
    controls[1].max_value = 120.0;
    controls[1].default_value = 60.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 1.0;
    controls[1].filter_max = 120.0;

    controls[2].type = FILTER_CONTROL_DOUBLE;
    controls[2].label = "sky brightness";
    controls[2].min_value = 0.1;
    controls[2].max_value = 1.0;
    controls[2].default_value = 0.75;
    controls[2].step = 0.01;
    controls[2].decimals = 2;
    controls[2].filter_min = 0.1;
    controls[2].filter_max = 1.0;

    controls[3].type = FILTER_CONTROL_DOUBLE;
    controls[3].label = "intensity";
    controls[3].min_value = 0.1;
    controls[3].max_value = 1.0;
    controls[3].default_value = 0.95;
    controls[3].step = 0.01;
    controls[3].decimals = 2;
    controls[3].filter_min = 0.0;
    controls[3].filter_max = 1.0;

    controls[4].type = FILTER_CONTROL_DOUBLE;
    controls[4].label = "edge sensitivity";
    controls[4].min_value = 0.0001;
    controls[4].max_value = 0.1;
    controls[4].default_value = 0.001;
    controls[4].step = 0.0001;
    controls[4].decimals = 4;
    controls[4].filter_min = 0.0001;
    controls[4].filter_max = 0.1;

    controls[5].type = FILTER_CONTROL_DOUBLE;
    controls[5].label = "minimum transmission";
    controls[5].min_value = 0.1;
    controls[5].max_value = 0.3;
    controls[5].default_value = 0.1;
    controls[5].step = 0.01;
    controls[5].decimals = 2;
    controls[5].filter_min = 0.1;
    controls[5].filter_max = 0.3;

    /* Show filter dialog */
    response = ui_show_filter_dialog(ctx, "Dehaze", controls, 6,
                                     on_dehaze_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI values to filter range and apply filter */
        gfloat filter_values[6];
        if (ui_filter_utils_scale_values(values, filter_values, controls, 6)) {
            ui_apply_layer_filter_with_value(ctx, filter_dehaze_apply,
                                             "Dehaze", filter_values, 6);
        }
    }
}

/**
 * Color Invert filter preview update callback
 */
static gboolean on_invert_preview_update(FilterDialog* dialog,
                                         const gdouble* values,
                                         gint num_values,
                                         gpointer user_data) {
    (void)dialog;
    (void)values;
    (void)num_values;
    (void)user_data;
    /* Invert filter has no parameters, so preview is handled directly */
    return TRUE;
}

/**
 * Adjustments > Invert callback
 */
static void on_adjust_invert(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ui_apply_layer_filter(ctx, filter_color_invert_apply, "Invert");
}

/**
 * Monochrome filter preview update callback
 */
static gboolean on_monochrome_preview_update(FilterDialog* dialog,
                                             const gdouble* values,
                                             gint num_values,
                                             gpointer user_data) {
    gfloat filter_values[4];

    if (!dialog || !values || num_values < 4) {
        return FALSE;
    }

    /* Values: [r, g, b, intensity] */
    filter_values[0] = (gfloat)values[0]; /* r (0.0-1.0) */
    filter_values[1] = (gfloat)values[1]; /* g (0.0-1.0) */
    filter_values[2] = (gfloat)values[2]; /* b (0.0-1.0) */
    filter_values[3] = (gfloat)values[3]; /* intensity (0-100) */

    /* Set up viewport-based filter */
    ui_filter_utils_setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_monochrome_apply,
                                          filter_values, 4);

    return TRUE;
}

/**
 * Adjustments > Monochrome callback
 */
static void on_adjust_monochrome(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[4];
    gdouble values[4];
    gint response;
    gfloat filter_values[4];

    if (!ctx) {
        return;
    }

    /* Control 0: Color (RGB) */
    controls[0].type = FILTER_CONTROL_RGB;
    controls[0].label = "Color";
    controls[0].default_r = 0.5; /* Default to gray */
    controls[0].default_g = 0.5;
    controls[0].default_b = 0.5;

    /* Control 1: Intensity (double) */
    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "Intensity";
    controls[1].min_value = 0.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 100.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 1.0;

    response = ui_show_filter_dialog(ctx, "Monochrome", controls, 2,
                                     on_monochrome_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Values: [r, g, b, intensity] = 4 values total */
        filter_values[0] = (gfloat)values[0]; /* r */
        filter_values[1] = (gfloat)values[1]; /* g */
        filter_values[2] = (gfloat)values[2]; /* b */
        filter_values[3] = (gfloat)values[3]; /* intensity */

        ui_apply_layer_filter_with_value(ctx, filter_monochrome_apply,
                                         "Monochrome", filter_values, 4);
    }
}

/**
 * Chroma key filter preview update callback
 */
static gboolean on_chroma_key_preview_update(FilterDialog* dialog,
                                             const gdouble* values,
                                             gint num_values,
                                             gpointer user_data) {
    gfloat filter_values[5];
    gdouble scaled_threshold, scaled_smoothing;

    if (!dialog || !values || num_values < 5) {
        return FALSE;
    }

    /* Scale threshold and smoothing from UI range (0-100) to filter range (0.0-1.0) */
    scaled_threshold = adjustments_scale_value(
        values[3], 0.0, 100.0, 0.0, 1.0);
    scaled_smoothing = adjustments_scale_value(
        values[4], 0.0, 100.0, 0.0, 1.0);

    /* Values: [r, g, b, threshold, smoothing] */
    filter_values[0] = (gfloat)values[0];        /* r (0.0-1.0) */
    filter_values[1] = (gfloat)values[1];        /* g (0.0-1.0) */
    filter_values[2] = (gfloat)values[2];        /* b (0.0-1.0) */
    filter_values[3] = (gfloat)scaled_threshold; /* threshold (0.0-1.0) */
    filter_values[4] = (gfloat)scaled_smoothing; /* smoothing (0.0-1.0) */

    /* Set up viewport-based filter */
    ui_filter_utils_setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_chroma_key_apply,
                                          filter_values, 5);

    return TRUE;
}

/**
 * Adjustments > Chroma Key callback
 */
static void on_adjust_chroma_key(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[3];
    gdouble values[5]; /* RGB (3) + threshold (1) + smoothing (1) = 5 total */
    gint response;
    gfloat filter_values[5];
    gdouble scaled_threshold, scaled_smoothing;

    if (!ctx) {
        return;
    }

    /* Control 0: Color to replace (RGB) */
    controls[0].type = FILTER_CONTROL_RGB;
    controls[0].label = "Color to Remove";
    controls[0].default_r = 0.0; /* Default to green */
    controls[0].default_g = 1.0;
    controls[0].default_b = 0.0;

    /* Control 1: Threshold (double) */
    controls[1].type = FILTER_CONTROL_DOUBLE;
    controls[1].label = "Threshold";
    controls[1].min_value = 0.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 15.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;
    controls[1].filter_min = 0.0;
    controls[1].filter_max = 1.0;

    /* Control 2: Smoothing (double) */
    controls[2].type = FILTER_CONTROL_DOUBLE;
    controls[2].label = "Smoothing";
    controls[2].min_value = 0.0;
    controls[2].max_value = 100.0;
    controls[2].default_value = 15.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = 0.0;
    controls[2].filter_max = 1.0;

    response = ui_show_filter_dialog(ctx, "Chroma Key", controls, 3,
                                     on_chroma_key_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale threshold and smoothing from UI range (0-100) to filter range (0.0-1.0) */
        scaled_threshold = adjustments_scale_value(
            values[3], controls[1].min_value, controls[1].max_value,
            controls[1].filter_min, controls[1].filter_max);
        scaled_smoothing = adjustments_scale_value(
            values[4], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);

        /* Values: [r, g, b, threshold, smoothing] = 5 values total */
        filter_values[0] = (gfloat)values[0];        /* r (0.0-1.0) */
        filter_values[1] = (gfloat)values[1];        /* g (0.0-1.0) */
        filter_values[2] = (gfloat)values[2];        /* b (0.0-1.0) */
        filter_values[3] = (gfloat)scaled_threshold; /* threshold (0.0-1.0) */
        filter_values[4] = (gfloat)scaled_smoothing; /* smoothing (0.0-1.0) */

        ui_apply_layer_filter_with_value(ctx, filter_chroma_key_apply,
                                         "Chroma Key", filter_values, 5);
    }
}

/**
 * Posterize filter preview update callback
 */
static gboolean on_posterize_preview_update(FilterDialog* dialog,
                                            const gdouble* values,
                                            gint num_values,
                                            gpointer user_data) {
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    filter_values[0] = (gfloat)values[0]; /* levels */

    /* Set up viewport-based filter */
    ui_filter_utils_setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_posterize_apply,
                                          filter_values, 1);

    return TRUE;
}

/**
 * Adjustments > Posterize callback
 */
static void on_adjust_posterize(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) {
        return;
    }

    /* Control 0: Levels (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Levels";
    controls[0].min_value = 2.0;
    controls[0].max_value = 255.0;
    controls[0].default_value = 8.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 2.0;
    controls[0].filter_max = 255.0;

    response = ui_show_filter_dialog(ctx, "Posterize", controls, 1,
                                     on_posterize_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        filter_values[0] = (gfloat)values[0]; /* levels */

        ui_apply_layer_filter_with_value(ctx, filter_posterize_apply,
                                         "Posterize", filter_values, 1);
    }
}

/**
 * Luminance Threshold filter preview update callback
 */
static gboolean on_threshold_preview_update(FilterDialog* dialog,
                                            const gdouble* values,
                                            gint num_values,
                                            gpointer user_data) {
    gfloat filter_values[1];

    if (!dialog || !values || num_values < 1) {
        return FALSE;
    }

    filter_values[0] = (gfloat)values[0]; /* threshold (0.0-1.0) */

    /* Set up viewport-based filter */
    ui_filter_utils_setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_luminance_threshold_apply,
                                          filter_values, 1);

    return TRUE;
}

/**
 * Adjustments > Threshold callback
 */
static void on_adjust_threshold(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[1];
    gdouble values[1];
    gint response;
    gfloat filter_values[1];

    if (!ctx) {
        return;
    }

    /* Control 0: Threshold (double) */
    controls[0].type = FILTER_CONTROL_DOUBLE;
    controls[0].label = "Threshold";
    controls[0].min_value = 0.0;
    controls[0].max_value = 255.0;
    controls[0].default_value = 50.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;
    controls[0].filter_min = 0.0;
    controls[0].filter_max = 255.0;

    response = ui_show_filter_dialog(ctx, "Threshold", controls, 1,
                                     on_threshold_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Scale UI value (0-100) to filter range (0.0-1.0) */
        gdouble scaled_threshold = adjustments_scale_value(
            values[0], controls[0].min_value, controls[0].max_value,
            controls[0].filter_min, controls[0].filter_max);

        filter_values[0] = (gfloat)scaled_threshold;

        ui_apply_layer_filter_with_value(ctx, filter_luminance_threshold_apply,
                                         "Threshold", filter_values, 1);
    }
}

/**
 * Highlight/Shadow Tint filter preview update callback
 */
static gboolean on_shadows_highlights_tint_preview_update(FilterDialog* dialog,
                                                          const gdouble* values,
                                                          gint num_values,
                                                          gpointer user_data) {
    gfloat filter_values[8];

    if (!dialog || !values || num_values < 8) {
        return FALSE;
    }

    /* Values: [shadowR, shadowG, shadowB, highlightR, highlightG, highlightB, shadowIntensity, highlightIntensity] */
    filter_values[0] = (gfloat)values[0]; /* shadowR */
    filter_values[1] = (gfloat)values[1]; /* shadowG */
    filter_values[2] = (gfloat)values[2]; /* shadowB */
    filter_values[3] = (gfloat)values[3]; /* highlightR */
    filter_values[4] = (gfloat)values[4]; /* highlightG */
    filter_values[5] = (gfloat)values[5]; /* highlightB */
    filter_values[6] = (gfloat)values[6]; /* shadowIntensity */
    filter_values[7] = (gfloat)values[7]; /* highlightIntensity */

    /* Set up viewport-based filter */
    ui_filter_utils_setup_viewport_filter(dialog, (gboolean(*)(ImageLayer*, const gfloat*, gint))filter_highlight_shadow_tint_apply,
                                          filter_values, 8);

    return TRUE;
}

/**
 * Adjustments > Shadows/Highlights Tint callback
 */
static void on_adjust_shadows_highlights_tint(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    FilterControlParam controls[8];
    gdouble values[8];
    gint response;
    gfloat filter_values[8];

    if (!ctx) {
        return;
    }

    /* Control 0: Shadow Tint Color (RGB) */
    controls[0].type = FILTER_CONTROL_RGB;
    controls[0].label = "Shadow Tint";
    controls[0].default_r = 0.0;
    controls[0].default_g = 0.0;
    controls[0].default_b = 0.0;

    /* Control 1: Highlight Tint Color (RGB) */
    controls[1].type = FILTER_CONTROL_RGB;
    controls[1].label = "Highlight Tint";
    controls[1].default_r = 1.0;
    controls[1].default_g = 1.0;
    controls[1].default_b = 1.0;

    /* Control 2: Shadow Intensity (double) */
    controls[2].type = FILTER_CONTROL_DOUBLE;
    controls[2].label = "Shadow Intensity";
    controls[2].min_value = 0.0;
    controls[2].max_value = 100.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;
    controls[2].filter_min = 0.0;
    controls[2].filter_max = 1.0;

    /* Control 3: Highlight Intensity (double) */
    controls[3].type = FILTER_CONTROL_DOUBLE;
    controls[3].label = "Highlight Intensity";
    controls[3].min_value = 0.0;
    controls[3].max_value = 100.0;
    controls[3].default_value = 0.0;
    controls[3].step = 1.0;
    controls[3].decimals = 0;
    controls[3].filter_min = 0.0;
    controls[3].filter_max = 1.0;

    response = ui_show_filter_dialog(ctx, "Shadows/Highlights Tint", controls, 4,
                                     on_shadows_highlights_tint_preview_update, values);

    if (response == GTK_RESPONSE_OK) {
        /* Values array will have: [shadowR, shadowG, shadowB, highlightR, highlightG, highlightB, shadowIntensity, highlightIntensity] */
        /* Total: 3 (shadow RGB) + 3 (highlight RGB) + 1 (shadow intensity) + 1 (highlight intensity) = 8 values */
        filter_values[0] = (gfloat)values[0]; /* shadowR */
        filter_values[1] = (gfloat)values[1]; /* shadowG */
        filter_values[2] = (gfloat)values[2]; /* shadowB */
        filter_values[3] = (gfloat)values[3]; /* highlightR */
        filter_values[4] = (gfloat)values[4]; /* highlightG */
        filter_values[5] = (gfloat)values[5]; /* highlightB */

        /* Scale intensity values from 0-100 to 0.0-1.0 */
        gdouble scaled_shadow_intensity = adjustments_scale_value(
            values[6], controls[2].min_value, controls[2].max_value,
            controls[2].filter_min, controls[2].filter_max);
        gdouble scaled_highlight_intensity = adjustments_scale_value(
            values[7], controls[3].min_value, controls[3].max_value,
            controls[3].filter_min, controls[3].filter_max);

        filter_values[6] = (gfloat)scaled_shadow_intensity;
        filter_values[7] = (gfloat)scaled_highlight_intensity;

        ui_apply_layer_filter_with_value(ctx, filter_highlight_shadow_tint_apply,
                                         "Shadows/Highlights Tint", filter_values, 8);
    }
}

/**
 * Adjustments > Palettize callback
 */
static void on_adjust_palettize(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    PalettizeDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
    gint response;
    PalettizeParams params;

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

    /* Create palettize dialog */
    dialog = palettize_dialog_new("Palettize");
    if (!dialog) {
        g_warning("Failed to create palettize dialog");
        return;
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        palettize_dialog_free(dialog);
        return;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Set layers in dialog */
    palettize_dialog_set_layers(dialog, layer, temp_layer);

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(palettize_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = palettize_dialog_run(dialog, GTK_WINDOW(ctx->window), &params);

    if (response == GTK_RESPONSE_OK) {
        /* Apply palettize filter directly */
        Command* cmd = command_create_draw(layer, "Palettize");
        if (cmd) {
            /* Start timing */
            gint64 start_time = g_get_monotonic_time();

            gboolean success = filter_palettize_apply(layer, &params);

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

        /* Free palette file path if allocated */
        if (params.palette_file) {
            g_free(params.palette_file);
        }
    }

    /* Clean up */
    palettize_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Adjustments > Retinex callback
 */
static void on_adjust_retinex(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc;
    ImageLayer* layer;
    RetinexDialog* dialog;
    ImageLayer* temp_layer;
    cairo_t* cr;
    gint response;
    OcRetinexMode mode;
    gint scale;
    gfloat num_scales;
    gfloat dynamic;

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

    /* Create retinex dialog */
    dialog = retinex_dialog_new("Retinex");
    if (!dialog) {
        g_warning("Failed to create retinex dialog");
        return;
    }

    /* Create a copy of the layer for preview */
    temp_layer = layer_new("Temp", layer->width, layer->height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
    if (!temp_layer) {
        g_warning("Failed to create temporary layer for preview");
        retinex_dialog_free(dialog);
        return;
    }

    /* Copy layer surface to temp layer */
    cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Set layers in dialog */
    retinex_dialog_set_layers(dialog, layer, temp_layer);

    /* Store original layer reference for preview callback */
    g_object_set_data(G_OBJECT(retinex_dialog_get_window(dialog)), "original_layer", layer);

    /* Set dialog as transient for main window */
    if (ctx->window) {
        gtk_window_set_transient_for(retinex_dialog_get_window(dialog), GTK_WINDOW(ctx->window));
    }

    /* Run dialog */
    response = retinex_dialog_run(dialog, GTK_WINDOW(ctx->window), &mode, &scale, &num_scales, &dynamic);

    if (response == GTK_RESPONSE_OK) {
        /* Apply retinex filter directly */
        Command* cmd = command_create_draw(layer, "Retinex");
        if (cmd) {
            /* Start timing */
            gint64 start_time = g_get_monotonic_time();

            gboolean success = filter_retinex_apply(layer, mode, scale, num_scales, dynamic);

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
    g_object_set_data(G_OBJECT(retinex_dialog_get_window(dialog)), "original_layer", NULL);
    g_object_set_data(G_OBJECT(retinex_dialog_get_window(dialog)), "retinex_params", NULL);
    retinex_dialog_free(dialog);
    layer_free(temp_layer);
}

/**
 * Setup Adjustments menu from Glade builder
 */
void ui_filter_adjust_setup_menu(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* adjust_menu = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu"));
    GtkWidget* adjust_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_item"));

    if (adjust_menu && adjust_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(adjust_menu_item), adjust_menu);
    }

    /* Connect Adjustments menu signals */
    GtkWidget* adjust_menu_grayscale = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_grayscale"));
    if (adjust_menu_grayscale) {
        g_signal_connect(adjust_menu_grayscale, "activate", G_CALLBACK(on_adjust_grayscale), ctx);
    }

    GtkWidget* adjust_menu_whitebalance = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_whitebalance"));
    if (adjust_menu_whitebalance) {
        g_signal_connect(adjust_menu_whitebalance, "activate", G_CALLBACK(on_adjust_whitebalance), ctx);
    }

    GtkWidget* adjust_menu_vibrance = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_vibrance"));
    if (adjust_menu_vibrance) {
        g_signal_connect(adjust_menu_vibrance, "activate", G_CALLBACK(on_adjust_vibrance), ctx);
    }

    /* Connect Histogram menu signals */
    GtkWidget* histogram_menu_equalize = GTK_WIDGET(gtk_builder_get_object(builder, "histogram_menu_equalize"));
    if (histogram_menu_equalize) {
        g_signal_connect(histogram_menu_equalize, "activate", G_CALLBACK(on_histogram_equalize), ctx);
    }

    GtkWidget* histogram_menu_stretch = GTK_WIDGET(gtk_builder_get_object(builder, "histogram_menu_stretch"));
    if (histogram_menu_stretch) {
        g_signal_connect(histogram_menu_stretch, "activate", G_CALLBACK(on_histogram_stretch), ctx);
    }

    /* Connect additional Adjustments menu signals */
    GtkWidget* adjust_menu_sepia = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_sepia"));
    if (adjust_menu_sepia) {
        g_signal_connect(adjust_menu_sepia, "activate", G_CALLBACK(on_adjust_sepia), ctx);
    }

    GtkWidget* adjust_menu_backlight = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_backlight"));
    if (adjust_menu_backlight) {
        g_signal_connect(adjust_menu_backlight, "activate", G_CALLBACK(on_adjust_backlight), ctx);
    }

    GtkWidget* adjust_menu_gamma = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_gamma"));
    if (adjust_menu_gamma) {
        g_signal_connect(adjust_menu_gamma, "activate", G_CALLBACK(on_adjust_gamma), ctx);
    }

    GtkWidget* adjust_menu_curves = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_curves"));
    if (adjust_menu_curves) {
        g_signal_connect(adjust_menu_curves, "activate", G_CALLBACK(on_adjust_curves), ctx);
    }

    GtkWidget* adjust_menu_colorbalance = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_colorbalance"));
    if (adjust_menu_colorbalance) {
        g_signal_connect(adjust_menu_colorbalance, "activate", G_CALLBACK(on_adjust_colorbalance), ctx);
    }

    GtkWidget* adjust_menu_exposure = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_exposure"));
    if (adjust_menu_exposure) {
        g_signal_connect(adjust_menu_exposure, "activate", G_CALLBACK(on_adjust_exposure), ctx);
    }

    GtkWidget* adjust_menu_hsl = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_hsl"));
    if (adjust_menu_hsl) {
        g_signal_connect(adjust_menu_hsl, "activate", G_CALLBACK(on_adjust_hsl), ctx);
    }

    GtkWidget* adjust_menu_brightness_contrast = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_brightness_contrast"));
    if (adjust_menu_brightness_contrast) {
        g_signal_connect(adjust_menu_brightness_contrast, "activate", G_CALLBACK(on_adjust_brightness_contrast), ctx);
    }

    GtkWidget* adjust_menu_shadows_highlights = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_shadows_highlights"));
    if (adjust_menu_shadows_highlights) {
        g_signal_connect(adjust_menu_shadows_highlights, "activate", G_CALLBACK(on_adjust_shadow_highlights), ctx);
    }

    GtkWidget* adjust_menu_temperature = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_temperature"));
    if (adjust_menu_temperature) {
        g_signal_connect(adjust_menu_temperature, "activate", G_CALLBACK(on_adjust_temperature), ctx);
    }

    /* Connect auto filter menu signals */
    GtkWidget* adjust_menu_auto_wb = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_auto_wb"));
    if (adjust_menu_auto_wb) {
        g_signal_connect(adjust_menu_auto_wb, "activate", G_CALLBACK(on_adjust_auto_whitebalance), ctx);
    }

    GtkWidget* adjust_menu_auto_contrast = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_auto_contrast"));
    if (adjust_menu_auto_contrast) {
        g_signal_connect(adjust_menu_auto_contrast, "activate", G_CALLBACK(on_adjust_auto_contrast), ctx);
    }

    GtkWidget* adjust_menu_auto_gamma = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_auto_gamma"));
    if (adjust_menu_auto_gamma) {
        g_signal_connect(adjust_menu_auto_gamma, "activate", G_CALLBACK(on_adjust_auto_gamma), ctx);
    }

    GtkWidget* adjust_menu_auto_levels = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_auto_levels"));
    if (adjust_menu_auto_levels) {
        g_signal_connect(adjust_menu_auto_levels, "activate", G_CALLBACK(on_adjust_auto_level), ctx);
    }

    GtkWidget* adjust_menu_auto_threshold = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_auto_threshold"));
    if (adjust_menu_auto_threshold) {
        g_signal_connect(adjust_menu_auto_threshold, "activate", G_CALLBACK(on_adjust_auto_threshold), ctx);
    }

    GtkWidget* adjust_menu_dehaze = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_dehaze"));
    if (adjust_menu_dehaze) {
        g_signal_connect(adjust_menu_dehaze, "activate", G_CALLBACK(on_adjust_dehaze), ctx);
    }

    GtkWidget* adjust_menu_invert = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_invert"));
    if (adjust_menu_invert) {
        g_signal_connect(adjust_menu_invert, "activate", G_CALLBACK(on_adjust_invert), ctx);
    }

    GtkWidget* adjust_menu_monochrome = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_monochrome"));
    if (adjust_menu_monochrome) {
        g_signal_connect(adjust_menu_monochrome, "activate", G_CALLBACK(on_adjust_monochrome), ctx);
    }

    GtkWidget* adjust_menu_posterize = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_posterize"));
    if (adjust_menu_posterize) {
        g_signal_connect(adjust_menu_posterize, "activate", G_CALLBACK(on_adjust_posterize), ctx);
    }

    GtkWidget* adjust_menu_threshold = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_threshold"));
    if (adjust_menu_threshold) {
        g_signal_connect(adjust_menu_threshold, "activate", G_CALLBACK(on_adjust_threshold), ctx);
    }

    GtkWidget* adjust_menu_shadows_highlights_tint = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_shadows_highlights_tint"));
    if (adjust_menu_shadows_highlights_tint) {
        g_signal_connect(adjust_menu_shadows_highlights_tint, "activate", G_CALLBACK(on_adjust_shadows_highlights_tint), ctx);
    }

    GtkWidget* adjust_menu_retinex = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_retinex"));
    if (adjust_menu_retinex) {
        g_signal_connect(adjust_menu_retinex, "activate", G_CALLBACK(on_adjust_retinex), ctx);
    }

    GtkWidget* adjust_menu_chroma_key = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_chroma_key"));
    if (adjust_menu_chroma_key) {
        g_signal_connect(adjust_menu_chroma_key, "activate", G_CALLBACK(on_adjust_chroma_key), ctx);
    }

    GtkWidget* adjust_menu_palettize = GTK_WIDGET(gtk_builder_get_object(builder, "adjust_menu_palettize"));
    if (adjust_menu_palettize) {
        g_signal_connect(adjust_menu_palettize, "activate", G_CALLBACK(on_adjust_palettize), ctx);
    }
}
