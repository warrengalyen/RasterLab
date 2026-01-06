#include "ui/dialogs/clouds_dialog.h"
#include "document.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "ui/filters/filter_render_clouds.h"
#include "ui/filters/filter_utils.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * Clouds dialog structure
 */
struct _CloudsDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    GtkWidget* scale_scale;
    GtkWidget* scale_spin;
    GtkWidget* quality_scale;
    GtkWidget* quality_spin;
    GtkWidget* shadow_color_button;
    GtkWidget* highlight_color_button;
    GtkWidget* opacity_scale;
    GtkWidget* opacity_spin;
    GtkWidget* seed_scale;
    GtkWidget* seed_spin;
    GtkWidget* generator_combo;
    CloudParams params;
    CloudsDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/**
 * Wrapper structure for CloudParams with dialog pointer
 */
typedef struct {
    CloudParams params;
    CloudsDialog* dialog; /* Dialog pointer for accessing document/layer */
} CloudParamsWrapper;

/**
 * Helper function to wrap CloudParams filter for viewport system
 */
static cairo_surface_t* apply_clouds_filter_to_viewport_surface(cairo_surface_t* viewport_surface, gpointer params) {
    CloudParamsWrapper* wrapper = (CloudParamsWrapper*)params;
    CloudParams* cloud_params = wrapper ? &wrapper->params : NULL;
    ImageLayer* temp_layer;
    cairo_surface_t* result;
    cairo_surface_t* original_viewport = NULL;
    struct ImageDocument* doc = NULL;
    struct ImageLayer* layer = NULL;

    if (!viewport_surface || !cloud_params) {
        return NULL;
    }

    /* Get document and layer from dialog if available */
    if (wrapper && wrapper->dialog) {
        GtkWindow* window = clouds_dialog_get_window(wrapper->dialog);
        if (window) {
            doc = (struct ImageDocument*)g_object_get_data(G_OBJECT(window), "filter_doc");
            layer = (struct ImageLayer*)g_object_get_data(G_OBJECT(window), "original_layer");
        }
    }

    /* Get viewport dimensions */
    gint width = cairo_image_surface_get_width(viewport_surface);
    gint height = cairo_image_surface_get_height(viewport_surface);

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    /* Create a copy of the original viewport for selection masking */
    if (doc && layer) {
        original_viewport = cairo_image_surface_create(
            cairo_image_surface_get_format(viewport_surface), width, height);
        if (original_viewport) {
            cairo_t* cr = cairo_create(original_viewport);
            cairo_set_source_surface(cr, viewport_surface, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
    }

    /* Create a temporary layer with the viewport surface */
    temp_layer = layer_new("TempViewport", width, height, TRUE,
                           LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, NULL);
    if (!temp_layer) {
        if (original_viewport) {
            cairo_surface_destroy(original_viewport);
        }
        return NULL;
    }

    /* Copy viewport surface to layer */
    cairo_t* cr = cairo_create(temp_layer->surface);
    cairo_set_source_surface(cr, viewport_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Apply filter to the layer */
    if (!filter_render_clouds_apply(temp_layer, cloud_params)) {
        layer_free(temp_layer);
        if (original_viewport) {
            cairo_surface_destroy(original_viewport);
        }
        return NULL;
    }

    /* Apply selection masking if there's a selection */
    if (original_viewport && doc && layer) {
        /* Create a temporary layer structure for the viewport with correct offset */
        ImageLayer viewport_layer = *layer;
        viewport_layer.surface = temp_layer->surface;
        viewport_layer.width = width;
        viewport_layer.height = height;

        /* Check if preview was cropped (viewport smaller than layer) */
        /* If cropped, the viewport is at (0,0) relative to cropped surface, */
        /* which corresponds to (sel_x_min, sel_y_min) relative to original layer */
        gint layer_width = cairo_image_surface_get_width(layer->surface);
        gint layer_height = cairo_image_surface_get_height(layer->surface);

        if (width < layer_width || height < layer_height) {
            /* Preview was cropped - need to find selection bounding box to get crop offset */
            /* Build selection mask for a small region to find where selection starts */
            DirtyRect test_rect;
            dirty_rect_set(&test_rect, layer->offset_x, layer->offset_y, layer_width, layer_height);
            DirtyRect actual_region;
            SelectionMask* test_mask = selection_build_combined_mask(
                doc->selection_mask, &test_rect, FEATHER_QUALITY_NORMAL, &actual_region);

            if (test_mask && test_mask->data) {
                /* Find selection bounding box */
                gint sel_x_min_doc = actual_region.x + actual_region.width;
                gint sel_y_min_doc = actual_region.y + actual_region.height;
                gint sel_x_max_doc = actual_region.x - 1;
                gint sel_y_max_doc = actual_region.y - 1;

                for (gint y = 0; y < test_mask->height; y++) {
                    const uint8_t* mask_row = test_mask->data + y * test_mask->stride;
                    for (gint x = 0; x < test_mask->width; x++) {
                        if (mask_row[x] > 0) {
                            gint doc_x = actual_region.x + x;
                            gint doc_y = actual_region.y + y;
                            if (doc_x < sel_x_min_doc)
                                sel_x_min_doc = doc_x;
                            if (doc_y < sel_y_min_doc)
                                sel_y_min_doc = doc_y;
                            if (doc_x > sel_x_max_doc)
                                sel_x_max_doc = doc_x;
                            if (doc_y > sel_y_max_doc)
                                sel_y_max_doc = doc_y;
                        }
                    }
                }

                if (sel_x_max_doc >= sel_x_min_doc && sel_y_max_doc >= sel_y_min_doc) {
                    /* Convert to layer coordinates */
                    gint sel_x_min = sel_x_min_doc - layer->offset_x;
                    gint sel_y_min = sel_y_min_doc - layer->offset_y;

                    /* Viewport at (0,0) in cropped surface = (sel_x_min, sel_y_min) in layer */
                    viewport_layer.offset_x = layer->offset_x + sel_x_min;
                    viewport_layer.offset_y = layer->offset_y + sel_y_min;
                } else {
                    /* Fallback to original layer offset */
                    viewport_layer.offset_x = layer->offset_x;
                    viewport_layer.offset_y = layer->offset_y;
                }
            } else {
                /* Fallback to original layer offset */
                viewport_layer.offset_x = layer->offset_x;
                viewport_layer.offset_y = layer->offset_y;
            }

            if (test_mask) {
                selection_mask_free(test_mask);
            }
        } else {
            /* Preview not cropped - viewport is at (0,0) relative to layer */
            viewport_layer.offset_x = layer->offset_x;
            viewport_layer.offset_y = layer->offset_y;
        }

        filter_utils_apply_selection_mask(temp_layer->surface, original_viewport, doc, &viewport_layer);
        cairo_surface_destroy(original_viewport);
    }

    /* Return a reference to the filtered surface */
    result = cairo_surface_reference(temp_layer->surface);
    layer_free(temp_layer);

    return result;
}

/**
 * Update preview callback
 */
static void update_preview(CloudsDialog* dialog) {
    CloudParamsWrapper* stored_wrapper;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get current values from widgets */
    dialog->params.scale = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->scale_scale));
    dialog->params.quality = (gint)gtk_range_get_value(GTK_RANGE(dialog->quality_scale));
    dialog->params.opacity = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->opacity_scale));
    dialog->params.seed = (gint)gtk_range_get_value(GTK_RANGE(dialog->seed_scale));

    /* Get generator from combo */
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->generator_combo));
    dialog->params.generator = (active == 0) ? OC_NOISE_PERLIN : OC_NOISE_SIMPLEX;

    /* Get shadow color */
    GdkRGBA shadow_color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog->shadow_color_button), &shadow_color);
    dialog->params.shadowColorR = (guchar)(shadow_color.red * 255.0);
    dialog->params.shadowColorG = (guchar)(shadow_color.green * 255.0);
    dialog->params.shadowColorB = (guchar)(shadow_color.blue * 255.0);

    /* Get highlight color */
    GdkRGBA highlight_color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog->highlight_color_button), &highlight_color);
    dialog->params.highlightColorR = (guchar)(highlight_color.red * 255.0);
    dialog->params.highlightColorG = (guchar)(highlight_color.green * 255.0);
    dialog->params.highlightColorB = (guchar)(highlight_color.blue * 255.0);

    /* Get or create stored wrapper */
    stored_wrapper = (CloudParamsWrapper*)g_object_get_data(G_OBJECT(dialog->dialog), "clouds_params");

    if (!stored_wrapper) {
        stored_wrapper = g_malloc(sizeof(CloudParamsWrapper));
        g_object_set_data_full(G_OBJECT(dialog->dialog), "clouds_params",
                               stored_wrapper, g_free);
    }

    /* Copy params and dialog pointer */
    stored_wrapper->params = dialog->params;
    stored_wrapper->dialog = dialog;

    /* Set filter function on preview to use viewport-based filtering */
    filter_preview_set_filter_function(dialog->preview, apply_clouds_filter_to_viewport_surface, stored_wrapper);
    filter_preview_refresh(dialog->preview);

    /* Call user callback if provided */
    if (dialog->preview_callback) {
        dialog->preview_callback(dialog, &dialog->params, dialog->preview_user_data);
    }
}

/**
 * Scale value changed callback
 */
static void on_scale_changed(GtkRange* range, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->scale_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->scale_spin), value);
    update_preview(dialog);
}

/**
 * Scale spin value changed callback
 */
static void on_scale_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->scale_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->scale_scale), value);
}

/**
 * Quality value changed callback
 */
static void on_quality_changed(GtkRange* range, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->quality_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->quality_spin), value);
    update_preview(dialog);
}

/**
 * Quality spin value changed callback
 */
static void on_quality_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->quality_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->quality_scale), value);
}

/**
 * Shadow color changed callback
 */
static void on_shadow_color_set(GtkColorButton* button, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    (void)button;
    update_preview(dialog);
}

/**
 * Highlight color changed callback
 */
static void on_highlight_color_set(GtkColorButton* button, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    (void)button;
    update_preview(dialog);
}

/**
 * Opacity value changed callback
 */
static void on_opacity_changed(GtkRange* range, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->opacity_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->opacity_spin), value);
    update_preview(dialog);
}

/**
 * Opacity spin value changed callback
 */
static void on_opacity_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->opacity_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->opacity_scale), value);
}

/**
 * Seed value changed callback
 */
static void on_seed_changed(GtkRange* range, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->seed_spin) {
        return;
    }

    value = gtk_range_get_value(range);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->seed_spin), value);
    update_preview(dialog);
}

/**
 * Seed spin value changed callback
 */
static void on_seed_spin_changed(GtkSpinButton* spin, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    gdouble value;

    if (!dialog || !dialog->seed_scale) {
        return;
    }

    value = gtk_spin_button_get_value(spin);
    gtk_range_set_value(GTK_RANGE(dialog->seed_scale), value);
}

/**
 * Generator combo changed callback
 */
static void on_generator_changed(GtkComboBox* combo, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    (void)combo;
    update_preview(dialog);
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    CloudsDialog* dialog = (CloudsDialog*)user_data;
    (void)widget;
    clouds_dialog_reset(dialog);
}

/**
 * Create a new clouds dialog
 */
CloudsDialog* clouds_dialog_new(const gchar* title) {
    CloudsDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* control_vbox;
    GtkWidget* label;
    GtkWidget* scale_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adjustment;
    GtkWidget* color_vbox;
    GtkWidget* color_label;
    GtkWidget* color_button;
    GtkWidget* combo_vbox;
    GtkWidget* combo_label;
    GtkWidget* combo;
    GtkWidget* reset_button;
    GtkWidget* button_box;
    GdkRGBA color;

    if (!title) {
        return NULL;
    }

    dialog = (CloudsDialog*)g_malloc(sizeof(CloudsDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->scale_scale = NULL;
    dialog->scale_spin = NULL;
    dialog->quality_scale = NULL;
    dialog->quality_spin = NULL;
    dialog->shadow_color_button = NULL;
    dialog->highlight_color_button = NULL;
    dialog->opacity_scale = NULL;
    dialog->opacity_spin = NULL;
    dialog->seed_scale = NULL;
    dialog->seed_spin = NULL;
    dialog->generator_combo = NULL;
    dialog->preview_callback = NULL;
    dialog->preview_user_data = NULL;

    /* Set default parameters */
    dialog->params.scale = 15.0f;
    dialog->params.quality = 5;
    dialog->params.shadowColorR = 0;
    dialog->params.shadowColorG = 0;
    dialog->params.shadowColorB = 0;
    dialog->params.highlightColorR = 255;
    dialog->params.highlightColorG = 255;
    dialog->params.highlightColorB = 255;
    dialog->params.opacity = 100.0f;
    dialog->params.seed = 0;
    dialog->params.generator = OC_NOISE_SIMPLEX;

    /* Create dialog window */
    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK",
                                                 GTK_RESPONSE_OK,
                                                 "_Cancel",
                                                 GTK_RESPONSE_CANCEL,
                                                 NULL);

    gtk_window_set_resizable(GTK_WINDOW(dialog->dialog), TRUE);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

    /* Create main horizontal box */
    main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    /* Create filter preview widget (left side) */
    dialog->preview = FILTER_PREVIEW(filter_preview_new());
    gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), 375, 338);
    gtk_widget_set_hexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_box_pack_start(GTK_BOX(main_hbox), GTK_WIDGET(dialog->preview), FALSE, FALSE, 0);

    /* Create right side vertical box */
    right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right_vbox, 320, -1);
    gtk_widget_set_margin_start(right_vbox, 0);
    gtk_widget_set_margin_end(right_vbox, 0);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, FALSE, FALSE, 0);

    /* Create scale control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("scale");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(dialog->params.scale, 1.0, 100.0, 0.1, 1.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->scale_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 0.1, 1);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->scale_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_scale_spin_changed), dialog);

    /* Create quality control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("quality");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->params.quality, 1.0, 8.0, 1.0, 1.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->quality_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_quality_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->quality_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_quality_spin_changed), dialog);

    /* Create shadow color picker */
    color_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(color_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), color_vbox, FALSE, FALSE, 0);

    color_label = gtk_label_new("shadow color");
    gtk_widget_set_halign(color_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(color_label, 3);
    gtk_box_pack_start(GTK_BOX(color_vbox), color_label, FALSE, FALSE, 0);

    color = (GdkRGBA){dialog->params.shadowColorR / 255.0,
                      dialog->params.shadowColorG / 255.0,
                      dialog->params.shadowColorB / 255.0, 1.0};
    color_button = gtk_color_button_new_with_rgba(&color);
    gtk_widget_set_hexpand(color_button, TRUE);
    gtk_widget_set_size_request(color_button, -1, 35);
    gtk_box_pack_start(GTK_BOX(color_vbox), color_button, FALSE, FALSE, 0);
    dialog->shadow_color_button = color_button;
    g_signal_connect(color_button, "color-set", G_CALLBACK(on_shadow_color_set), dialog);

    /* Create highlight color picker */
    color_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(color_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), color_vbox, FALSE, FALSE, 0);

    color_label = gtk_label_new("highlight color");
    gtk_widget_set_halign(color_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(color_label, 3);
    gtk_box_pack_start(GTK_BOX(color_vbox), color_label, FALSE, FALSE, 0);

    color = (GdkRGBA){dialog->params.highlightColorR / 255.0,
                      dialog->params.highlightColorG / 255.0,
                      dialog->params.highlightColorB / 255.0, 1.0};
    color_button = gtk_color_button_new_with_rgba(&color);
    gtk_widget_set_hexpand(color_button, TRUE);
    gtk_widget_set_size_request(color_button, -1, 35);
    gtk_box_pack_start(GTK_BOX(color_vbox), color_button, FALSE, FALSE, 0);
    dialog->highlight_color_button = color_button;
    g_signal_connect(color_button, "color-set", G_CALLBACK(on_highlight_color_set), dialog);

    /* Create opacity control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("opacity");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new(dialog->params.opacity, 0.0, 100.0, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->opacity_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_opacity_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->opacity_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_opacity_spin_changed), dialog);

    /* Create seed control */
    control_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(control_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), control_vbox, FALSE, FALSE, 0);

    label = gtk_label_new("seed");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_box_pack_start(GTK_BOX(control_vbox), label, FALSE, FALSE, 0);

    scale_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(control_vbox), scale_hbox, TRUE, TRUE, 0);

    adjustment = gtk_adjustment_new((gdouble)dialog->params.seed, 0.0, 10000.0, 1.0, 100.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_pack_start(GTK_BOX(scale_hbox), scale, TRUE, TRUE, 0);
    dialog->seed_scale = scale;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_seed_changed), dialog);

    spin = gtk_spin_button_new(adjustment, 1.0, 0);
    gtk_widget_set_size_request(spin, 60, -1);
    gtk_box_pack_start(GTK_BOX(scale_hbox), spin, FALSE, FALSE, 0);
    dialog->seed_spin = spin;
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_seed_spin_changed), dialog);

    /* Create generator combo */
    combo_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_bottom(combo_vbox, 10);
    gtk_box_pack_start(GTK_BOX(right_vbox), combo_vbox, FALSE, FALSE, 0);

    combo_label = gtk_label_new("generator");
    gtk_widget_set_halign(combo_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(combo_label, 3);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo_label, FALSE, FALSE, 0);

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Perlin");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Simplex");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), (dialog->params.generator == OC_NOISE_PERLIN) ? 0 : 1);
    gtk_widget_set_hexpand(combo, TRUE);
    gtk_box_pack_start(GTK_BOX(combo_vbox), combo, FALSE, FALSE, 0);
    dialog->generator_combo = combo;
    g_signal_connect(combo, "changed", G_CALLBACK(on_generator_changed), dialog);

/* Get button box from dialog (for OK/Cancel) */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    button_box = gtk_dialog_get_action_area(GTK_DIALOG(dialog->dialog));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    if (button_box) {
        gtk_widget_set_margin_top(button_box, 5);
        gtk_widget_set_margin_bottom(button_box, 5);
        gtk_widget_set_margin_start(button_box, 5);
        gtk_widget_set_margin_end(button_box, 5);

        gtk_widget_set_hexpand(button_box, TRUE);

        /* Create reset button with reset.svg icon */
        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.svg");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(reset_button), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
        }
        gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(button_box), reset_button, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(button_box), reset_button, 0);
        g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset_clicked), dialog);
    }

    /* Show all widgets */
    gtk_widget_show_all(content_area);

    return dialog;
}

/**
 * Free clouds dialog
 */
void clouds_dialog_free(CloudsDialog* dialog) {
    if (!dialog) {
        return;
    }

    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }

    g_free(dialog);
}

/**
 * Get the dialog window
 */
GtkWindow* clouds_dialog_get_window(CloudsDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }
    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void clouds_dialog_set_layers(CloudsDialog* dialog, ImageLayer* original, ImageLayer* temp) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get composite surfaces from layers - pass full unmasked surfaces
       The preview widget will handle masking display based on selection */
    if (original && original->surface) {
        before_surface = cairo_surface_reference(original->surface);
    }

    if (temp && temp->surface) {
        after_surface = cairo_surface_reference(temp->surface);
    }

    filter_preview_set_before_surface(dialog->preview, before_surface);
    filter_preview_set_after_surface(dialog->preview, after_surface);

    /* Clean up references */
    if (before_surface) {
        cairo_surface_destroy(before_surface);
    }
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }

    /* Trigger initial preview update after layers are set */
    update_preview(dialog);
}

/**
 * Run the dialog and get cloud parameters
 */
gint clouds_dialog_run(CloudsDialog* dialog, GtkWindow* parent, CloudParams* params) {
    gint response;

    if (!dialog || !params) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    if (response == GTK_RESPONSE_OK) {
        /* Get final values from widgets */
        params->scale = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->scale_scale));
        params->quality = (gint)gtk_range_get_value(GTK_RANGE(dialog->quality_scale));
        params->opacity = (gfloat)gtk_range_get_value(GTK_RANGE(dialog->opacity_scale));
        params->seed = (gint)gtk_range_get_value(GTK_RANGE(dialog->seed_scale));

        /* Get generator */
        gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->generator_combo));
        params->generator = (active == 0) ? OC_NOISE_PERLIN : OC_NOISE_SIMPLEX;

        /* Get shadow color */
        GdkRGBA shadow_color;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog->shadow_color_button), &shadow_color);
        params->shadowColorR = (guchar)(shadow_color.red * 255.0);
        params->shadowColorG = (guchar)(shadow_color.green * 255.0);
        params->shadowColorB = (guchar)(shadow_color.blue * 255.0);

        /* Get highlight color */
        GdkRGBA highlight_color;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog->highlight_color_button), &highlight_color);
        params->highlightColorR = (guchar)(highlight_color.red * 255.0);
        params->highlightColorG = (guchar)(highlight_color.green * 255.0);
        params->highlightColorB = (guchar)(highlight_color.blue * 255.0);
    }

    return response;
}

/**
 * Update the after layer in preview
 */
void clouds_dialog_update_after_layer(CloudsDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;
    struct ImageDocument* doc = NULL;
    struct ImageLayer* original_layer = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get document and original layer from dialog if available */
    GtkWindow* window = clouds_dialog_get_window(dialog);
    if (window) {
        doc = (struct ImageDocument*)g_object_get_data(G_OBJECT(window), "filter_doc");
        original_layer = (struct ImageLayer*)g_object_get_data(G_OBJECT(window), "original_layer");
    }

    if (layer && layer->surface) {
        /* Pass the full unmasked surface - preview widget will handle masking display */
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);
    filter_preview_refresh(dialog->preview);

    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Set preview callback for live updates
 */
void clouds_dialog_set_preview_callback(CloudsDialog* dialog,
                                        CloudsDialogPreviewCallback callback,
                                        gpointer user_data) {
    if (!dialog) {
        return;
    }
    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

/**
 * Reset all controls to default values
 */
void clouds_dialog_reset(CloudsDialog* dialog) {
    if (!dialog) {
        return;
    }

    /* Reset to defaults */
    gtk_range_set_value(GTK_RANGE(dialog->scale_scale), 15.0);
    gtk_range_set_value(GTK_RANGE(dialog->quality_scale), 5.0);
    gtk_range_set_value(GTK_RANGE(dialog->opacity_scale), 100.0);
    gtk_range_set_value(GTK_RANGE(dialog->seed_scale), 0.0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->generator_combo), 1);

    /* Reset shadow color to black */
    GdkRGBA shadow_color = {0.0, 0.0, 0.0, 1.0};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog->shadow_color_button), &shadow_color);

    /* Reset highlight color to white */
    GdkRGBA highlight_color = {1.0, 1.0, 1.0, 1.0};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog->highlight_color_button), &highlight_color);

    update_preview(dialog);
}
