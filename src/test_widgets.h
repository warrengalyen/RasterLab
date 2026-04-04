#include "../lib/ocular.h"
#include "ui/widgets/anchor_position_widget.h"
#include "ui/widgets/curves_widget.h"
#include "ui/widgets/filter_dialog.h"
#include "ui/widgets/filter_preview.h"
#include "ui/widgets/hsv_color_wheel.h"
#include "ui/widgets/hsv_scale.h"
#include "ui/widgets/rgb_scale.h"
#include <math.h>
#include "debug_logger.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    CurvesWidget* curves;
    FilterPreview* preview;
    unsigned char* test_image;
    int image_width;
    int image_height;
} CurvesTestData;

// Compute histogram from image data
static void compute_image_histogram(unsigned char* image_data, int width, int height,
                                    double* hist_r, double* hist_g, double* hist_b) {
    // Initialize histograms
    for (int i = 0; i < 256; i++) {
        hist_r[i] = 0.0;
        hist_g[i] = 0.0;
        hist_b[i] = 0.0;
    }

    // Compute histogram from image (BGRA format: B, G, R, A)
    // Count pixels at each intensity level
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            unsigned char b_val = image_data[idx + 0];
            unsigned char g_val = image_data[idx + 1];
            unsigned char r_val = image_data[idx + 2];

            // Validate values are in range
            if (b_val >= 256 || g_val >= 256 || r_val >= 256) {
                debug_log("WRN", "Invalid pixel value at (%d,%d): b=%u, g=%u, r=%u", x, y, b_val, g_val, r_val);
                continue;
            }

            hist_b[b_val]++;
            hist_g[g_val]++;
            hist_r[r_val]++;
        }
    }
}

// Convert curves widget to OcCurve format and apply filter
static void apply_curves_filter(CurvesTestData* data) {
    if (!data || !data->test_image || !data->preview || !data->curves) {
        debug_log("WRN", "apply_curves_filter: missing data (%p, img=%p, prev=%p, curves=%p)",
                  data, data ? data->test_image : NULL, data ? data->preview : NULL, data ? data->curves : NULL);
        return;
    }

    debug_log("DBG", "apply_curves_filter called: curves=%p, refcount=%d, is_widget=%d",
              data->curves, G_OBJECT(data->curves)->ref_count, CURVES_IS_WIDGET(data->curves));

    // Create fresh curves for this operation
    OcCurve* curveR = createCurve();
    OcCurve* curveG = createCurve();
    OcCurve* curveB = createCurve();
    OcCurve* curveL = createCurve();

    if (!curveR || !curveG || !curveB || !curveL) {
        debug_log("WRN", "Failed to create curves");
        if (curveR)
            destroyCurve(curveR);
        if (curveG)
            destroyCurve(curveG);
        if (curveB)
            destroyCurve(curveB);
        if (curveL)
            destroyCurve(curveL);
        return;
    }

    // Save current active channel so we can restore it later
    Channel saved_channel = data->curves->active_channel;

    // Get lookup table for RGB curve (luminance curve)
    uint8_t rgb_lut[256];
    data->curves->active_channel = CHANNEL_RGB;
    curves_widget_get_lut(data->curves, rgb_lut);

    // Populate luminance curve
    for (int i = 0; i < 256; i++) {
        curveAddPoint(curveL, (uint8_t)i, rgb_lut[i]);
    }
    curveBuild(curveL);

    // Get lookup tables from each channel and populate curves
    for (int c = 0; c < 3; c++) {
        OcCurve* curve = (c == 0) ? curveR : (c == 1) ? curveG
                                                      : curveB;

        uint8_t channel_lut[256];

        // If RGB curve is active, use it for all channels (luminance adjustment)
        if (saved_channel == CHANNEL_RGB) {
            memcpy(channel_lut, rgb_lut, 256);
        } else {
            // Otherwise, get individual channel LUT
            data->curves->active_channel = (Channel)(c + 1); // R, G, B channels
            curves_widget_get_lut(data->curves, channel_lut);
        }

        // Populate curve from lookup table
        for (int i = 0; i < 256; i++) {
            curveAddPoint(curve, (uint8_t)i, channel_lut[i]);
        }
        curveBuild(curve);
    }

    // Restore the original active channel
    data->curves->active_channel = saved_channel;

    // Apply filter to a copy of the test image
    unsigned char* output_image = g_malloc(data->image_width * data->image_height * 4);
    OC_STATUS status = ocularCurvesFilter(
        data->test_image, output_image,
        data->image_width, data->image_height,
        data->image_width * 4,
        curveR, curveG, curveB, curveL);

    if (status == OC_STATUS_OK) {
        // Create cairo surface from output image
        cairo_surface_t* after_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, data->image_width, data->image_height);
        unsigned char* surface_data = cairo_image_surface_get_data(after_surface);
        memcpy(surface_data, output_image, data->image_width * data->image_height * 4);
        cairo_surface_mark_dirty(after_surface);

        // Update preview
        filter_preview_set_after_surface(data->preview, after_surface);
        gtk_widget_queue_draw(GTK_WIDGET(data->preview));
        cairo_surface_destroy(after_surface);

        debug_log("DBG", "ocularCurvesFilter succeeded with status %d", status);
    } else {
        debug_log("WRN", "ocularCurvesFilter failed with status %d", status);
    }

    g_free(output_image);

    // Clean up temporary curves
    destroyCurve(curveR);
    destroyCurve(curveG);
    destroyCurve(curveB);
    destroyCurve(curveL);
}

// Timer ID for debouncing curve updates
static guint curve_update_timer = 0;

static gboolean on_curves_update_timeout(gpointer user_data) {
    CurvesTestData* data = (CurvesTestData*)user_data;

    // Verify widget is still valid before applying filter
    if (data && data->curves && CURVES_IS_WIDGET(data->curves)) {
        apply_curves_filter(data);
    } else {
        debug_log("WRN", "Widget invalid in curve update timeout");
    }

    curve_update_timer = 0;
    return FALSE;
}

static void on_curves_changed(GtkWidget* widget, gpointer user_data) {
    CurvesTestData* data = (CurvesTestData*)user_data;

    // Debounce: only apply filter after updates stop for 50ms
    if (curve_update_timer != 0) {
        g_source_remove(curve_update_timer);
    }

    curve_update_timer = g_timeout_add(50, on_curves_update_timeout, data);
}

static void on_histogram_toggled(GtkToggleButton* button, gpointer user_data) {
    CurvesWidget* curves = (CurvesWidget*)user_data;
    curves_widget_set_histogram_visible(curves,
                                        gtk_toggle_button_get_active(button));
}

static void on_grid_toggled(GtkToggleButton* button, gpointer user_data) {
    CurvesWidget* curves = (CurvesWidget*)user_data;
    curves_widget_set_grid_visible(curves,
                                   gtk_toggle_button_get_active(button));
}

static void on_diagonal_toggled(GtkToggleButton* button, gpointer user_data) {
    CurvesWidget* curves = (CurvesWidget*)user_data;
    curves_widget_set_diagonal_visible(curves,
                                       gtk_toggle_button_get_active(button));
}

static void on_channel_changed(GtkComboBox* combo, gpointer user_data) {
    CurvesWidget* curves = (CurvesWidget*)user_data;
    int active = gtk_combo_box_get_active(combo);
    curves_widget_set_channel(curves, (Channel)active);
}

static void test_curves_widget(void) {
    GtkWidget* dialog;
    GtkWidget* content_area;
    GtkWidget* hbox;
    GtkWidget* vbox;
    GtkWidget* left_vbox;
    GtkWidget* curves;
    GtkWidget* controls;
    GtkWidget* histogram_check;
    GtkWidget* grid_check;
    GtkWidget* diagonal_check;
    GtkWidget* channel_combo;
    GtkWidget* preview;
    CurvesTestData* data;
    gint width = 400;
    gint height = 300;
    gint x, y;
    cairo_surface_t* before_surface;
    cairo_t* cr;

    /* Create test data structure */
    data = g_new0(CurvesTestData, 1);

    /* Try to load an actual image file */
    GdkPixbuf* pixbuf = NULL;
    GError* error = NULL;

    /* Try to load a sample image - look for common locations */
    const char* sample_paths[] = {
        "C:\\Users\\wgaly\\Downloads\\confident-young-brunette-caucasian-girl-crosses-fingers-gesturing-no.jpg",
        NULL};

    for (int i = 0; sample_paths[i] != NULL && !pixbuf; i++) {
        pixbuf = gdk_pixbuf_new_from_file(sample_paths[i], &error);
        if (pixbuf) {
            debug_log("DBG", "Loaded image from: %s", sample_paths[i]);
            break;
        }
        if (error) {
            g_clear_error(&error);
        }
    }

    /* If no sample image found, create a test pattern */
    if (!pixbuf) {
        debug_log("DBG", "No sample image found, creating test pattern...");
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
    }

    /* Get image dimensions and convert to ARGB32 format */
    data->image_width = gdk_pixbuf_get_width(pixbuf);
    data->image_height = gdk_pixbuf_get_height(pixbuf);
    data->test_image = g_malloc(data->image_width * data->image_height * 4);

    /* Copy pixbuf data to our ARGB32 buffer */
    guchar* pixbuf_data = gdk_pixbuf_get_pixels(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    debug_log("DBG", "Loaded image: %dx%d, channels=%d, has_alpha=%d",
              data->image_width, data->image_height, n_channels, has_alpha);

    /* Sample first pixel to debug channel order */
    if (data->image_height > 0 && data->image_width > 0) {
        guchar* sample = pixbuf_data;
        if (n_channels >= 3) {
            debug_log("DBG", "First pixel values from pixbuf: [0]=%u, [1]=%u, [2]=%u",
                      sample[0], sample[1], sample[2]);
            if (n_channels >= 4) {
                debug_log("DBG", "  [3]=%u", sample[3]);
            }
        }
    }

    for (y = 0; y < data->image_height; y++) {
        guchar* row = pixbuf_data + y * rowstride;
        for (x = 0; x < data->image_width; x++) {
            guchar* pixel = row + x * n_channels;
            int idx = (y * data->image_width + x) * 4;

            if (n_channels >= 3) {
                // Store as BGRA: idx+0=B, idx+1=G, idx+2=R, idx+3=A
                // GdkPixbuf is RGB order (pixel[0]=R, pixel[1]=G, pixel[2]=B)
                data->test_image[idx + 0] = pixel[2];                           // B (from pixbuf B)
                data->test_image[idx + 1] = pixel[1];                           // G
                data->test_image[idx + 2] = pixel[0];                           // R (from pixbuf R)
                data->test_image[idx + 3] = (n_channels >= 4) ? pixel[3] : 255; // A
            } else if (n_channels == 1) {
                // Grayscale
                data->test_image[idx + 0] = pixel[0];
                data->test_image[idx + 1] = pixel[0];
                data->test_image[idx + 2] = pixel[0];
                data->test_image[idx + 3] = 255;
            }
        }
    }

    g_object_unref(pixbuf);

    // Create before surface for preview (use actual image dimensions, not default 400x300)
    before_surface = cairo_image_surface_create_for_data(
        data->test_image, CAIRO_FORMAT_ARGB32, data->image_width, data->image_height, data->image_width * 4);

    /* Create dialog window */
    dialog = gtk_dialog_new_with_buttons("Curves Editor Test",
                                         NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close",
                                         GTK_RESPONSE_CLOSE,
                                         NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 1000, 600);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Create main horizontal box for curves and preview */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);

    /* Left side - curves editor */
    left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(hbox), left_vbox, TRUE, TRUE, 0);

    /* Create curves widget */
    curves = curves_widget_new();
    debug_log("DBG", "curves_widget_new() returned: %p", curves);

    if (!curves) {
        debug_log("WRN", "Failed to create curves widget");
        g_free(data->test_image);
        g_free(data);
        cairo_surface_destroy(before_surface);
        gtk_widget_destroy(dialog);
        return;
    }

    gtk_widget_set_size_request(curves, 464, 344);
    gtk_widget_set_hexpand(curves, TRUE);
    gtk_box_pack_start(GTK_BOX(left_vbox), curves, TRUE, TRUE, 0);

    /* Cast to CurvesWidget - get strong reference and sink the floating reference */
    GObject* obj = G_OBJECT(curves);
    g_object_ref_sink(obj); /* Convert floating ref to normal ref */
    data->curves = (CurvesWidget*)curves;
    debug_log("DBG", "curves_widget_new() returned: %p, is_widget=%d, refcount=%d",
              curves, CURVES_IS_WIDGET(data->curves), obj->ref_count);

    /* Note: Connect curves change signal AFTER all setup (moved below) */

    /* Control panel */
    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(left_vbox), controls, FALSE, FALSE, 0);

    /* Checkboxes */
    histogram_check = gtk_check_button_new_with_label("Histogram");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(histogram_check), TRUE);
    g_signal_connect(histogram_check, "toggled",
                     G_CALLBACK(on_histogram_toggled), curves);
    gtk_box_pack_start(GTK_BOX(controls), histogram_check, FALSE, FALSE, 0);

    grid_check = gtk_check_button_new_with_label("Grid");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grid_check), TRUE);
    g_signal_connect(grid_check, "toggled",
                     G_CALLBACK(on_grid_toggled), curves);
    gtk_box_pack_start(GTK_BOX(controls), grid_check, FALSE, FALSE, 0);

    diagonal_check = gtk_check_button_new_with_label("Diagonal");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(diagonal_check), TRUE);
    g_signal_connect(diagonal_check, "toggled",
                     G_CALLBACK(on_diagonal_toggled), curves);
    gtk_box_pack_start(GTK_BOX(controls), diagonal_check, FALSE, FALSE, 0);

    /* Channel selector */
    channel_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(channel_combo), "RGB");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(channel_combo), "Red");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(channel_combo), "Green");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(channel_combo), "Blue");
    gtk_combo_box_set_active(GTK_COMBO_BOX(channel_combo), 0);
    g_signal_connect(channel_combo, "changed",
                     G_CALLBACK(on_channel_changed), curves);
    gtk_box_pack_start(GTK_BOX(controls), channel_combo, FALSE, FALSE, 0);

    /* Right side - preview */
    preview = GTK_WIDGET(filter_preview_new());
    gtk_widget_set_size_request(preview, 400, 400);
    gtk_box_pack_start(GTK_BOX(hbox), preview, TRUE, TRUE, 0);
    data->preview = FILTER_PREVIEW(preview);

    /* Set initial preview surfaces */
    filter_preview_set_before_surface(data->preview, before_surface);

    /* Connect response handler */
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);

    /* Show dialog */
    gtk_widget_show_all(dialog);

    /* Process pending events to ensure widget is fully realized */
    while (gtk_events_pending())
        gtk_main_iteration();

    /* Compute and set histogram from image (AFTER widget is realized) */
    debug_log("DBG", "Setting histogram data on curves widget...");
    double hist_r[256], hist_g[256], hist_b[256];
    compute_image_histogram(data->test_image, data->image_width, data->image_height, hist_r, hist_g, hist_b);

    /* Find max values to see distribution */
    double max_r = 0, max_g = 0, max_b = 0;
    for (int i = 0; i < 256; i++) {
        if (hist_r[i] > max_r)
            max_r = hist_r[i];
        if (hist_g[i] > max_g)
            max_g = hist_g[i];
        if (hist_b[i] > max_b)
            max_b = hist_b[i];
    }

    debug_log("DBG", "Computed histogram from %dx%d image:", data->image_width, data->image_height);
    debug_log("DBG", "  R: max=%f, [0]=%f, [128]=%f, [255]=%f", max_r, hist_r[0], hist_r[128], hist_r[255]);
    debug_log("DBG", "  G: max=%f, [0]=%f, [128]=%f, [255]=%f", max_g, hist_g[0], hist_g[128], hist_g[255]);
    debug_log("DBG", "  B: max=%f, [0]=%f, [128]=%f, [255]=%f", max_b, hist_b[0], hist_b[128], hist_b[255]);

    /* Ensure histogram display is enabled */
    curves_widget_set_histogram_visible(data->curves, TRUE);
    debug_log("DBG", "Histogram visibility set to TRUE");

    /* Set histograms for individual channels */
    debug_log("DBG", "Setting CHANNEL_RED histogram...");
    curves_widget_set_histogram_data(data->curves, CHANNEL_RED, hist_r, 256);
    debug_log("DBG", "Setting CHANNEL_GREEN histogram...");
    curves_widget_set_histogram_data(data->curves, CHANNEL_GREEN, hist_g, 256);
    debug_log("DBG", "Setting CHANNEL_BLUE histogram...");
    curves_widget_set_histogram_data(data->curves, CHANNEL_BLUE, hist_b, 256);

    /* Also set RGB histogram (for RGB channel mode) - use the red channel as base */
    debug_log("DBG", "Setting CHANNEL_RGB histogram...");
    curves_widget_set_histogram_data(data->curves, CHANNEL_RGB, hist_r, 256);

    debug_log("DBG", "Histogram data set, requesting widget redraw...");
    gtk_widget_queue_draw(GTK_WIDGET(data->curves));

    /* Process events again to flush the redraw */
    while (gtk_events_pending())
        gtk_main_iteration();

    /* Apply initial curves (after widget is realized) */
    apply_curves_filter(data);

    /* Connect signal for live curve updates */
    g_signal_connect(curves, "curve-changed", G_CALLBACK(on_curves_changed), data);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dialog));

    /* Clean up */
    // Remove any pending curve update timer
    if (curve_update_timer != 0) {
        g_source_remove(curve_update_timer);
        curve_update_timer = 0;
    }

    if (data->curves) {
        g_object_unref(data->curves);
    }
    g_free(data->test_image);
    g_free(data);
    cairo_surface_destroy(before_surface);
    gtk_widget_destroy(dialog);
}

/**
 * Create a test dialog for filter preview widget
 */
static void test_filter_preview_dialog(void) {
    GtkWidget* dialog;
    GtkWidget* content_area;
    FilterPreview* preview;
    cairo_surface_t* before_surface;
    cairo_surface_t* after_surface;
    cairo_t* cr;
    gint width = 800;
    gint height = 600;
    gint x, y;

    /* Create dialog window */
    dialog = gtk_dialog_new_with_buttons("Filter Preview Test",
                                         NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close",
                                         GTK_RESPONSE_CLOSE,
                                         NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 900, 700);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Create filter preview widget */
    preview = FILTER_PREVIEW(filter_preview_new());
    gtk_container_add(GTK_CONTAINER(content_area), GTK_WIDGET(preview));

    /* Create "before" test image - colorful gradient pattern */
    before_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(before_surface);

    /* Draw a colorful gradient pattern */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble r = (gdouble)x / (gdouble)width;
            gdouble g = (gdouble)y / (gdouble)height;
            gdouble b = 0.5;
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }

    /* Add some shapes for visual interest */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(before_surface);

    /* Create "after" test image - grayscale version */
    after_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(after_surface);

    /* Draw grayscale gradient pattern */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble gray = ((gdouble)x / (gdouble)width + (gdouble)y / (gdouble)height) / 2.0;
            cairo_set_source_rgb(cr, gray, gray, gray);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }

    /* Add same shapes but in grayscale */
    cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(after_surface);

    /* Set surfaces in preview widget */
    filter_preview_set_before_surface(preview, before_surface);
    filter_preview_set_after_surface(preview, after_surface);

    /* Connect response handler to clean up surfaces */
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);

    /* Show dialog */
    gtk_widget_show_all(dialog);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dialog));

    /* Clean up surfaces */
    if (before_surface) {
        cairo_surface_destroy(before_surface);
    }
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }

    gtk_widget_destroy(dialog);
}

/**
 * Create a test dialog for filter dialog widget
 */
static void test_filter_dialog(void) {
    FilterDialog* dialog;
    FilterControlParam controls[3];
    ImageLayer* before_layer;
    ImageLayer* after_layer;
    cairo_t* cr;
    gint width = 800;
    gint height = 600;
    gint x, y;
    gdouble values[3];
    gint response;
    gint i;

    /* Define control parameters */
    controls[0].label = "vibrance";
    controls[0].min_value = -100.0;
    controls[0].max_value = 100.0;
    controls[0].default_value = 0.0;
    controls[0].step = 1.0;
    controls[0].decimals = 0;

    controls[1].label = "saturation";
    controls[1].min_value = -100.0;
    controls[1].max_value = 100.0;
    controls[1].default_value = 0.0;
    controls[1].step = 1.0;
    controls[1].decimals = 0;

    controls[2].label = "brightness";
    controls[2].min_value = -100.0;
    controls[2].max_value = 100.0;
    controls[2].default_value = 0.0;
    controls[2].step = 1.0;
    controls[2].decimals = 0;

    /* Create filter dialog */
    dialog = filter_dialog_new("Filter Test", controls, 3);
    if (!dialog) {
        debug_log("WRN", "Failed to create filter dialog");
        return;
    }

    /* Create test layers */
    before_layer = layer_new("Before", width, height, TRUE,
                             LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, NULL);
    after_layer = layer_new("After", width, height, TRUE,
                            LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, NULL);

    if (!before_layer || !after_layer) {
        debug_log("WRN", "Failed to create test layers");
        filter_dialog_free(dialog);
        if (before_layer)
            layer_free(before_layer);
        if (after_layer)
            layer_free(after_layer);
        return;
    }

    /* Create "before" test image - colorful gradient pattern */
    cr = cairo_create(before_layer->surface);

    /* Draw a colorful gradient pattern */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble r = (gdouble)x / (gdouble)width;
            gdouble g = (gdouble)y / (gdouble)height;
            gdouble b = 0.5;
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }

    /* Add some shapes for visual interest */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(before_layer->surface);

    /* Create "after" test image - slightly modified version */
    cr = cairo_create(after_layer->surface);

    /* Draw similar pattern but with slight modification */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            gdouble r = (gdouble)x / (gdouble)width;
            gdouble g = (gdouble)y / (gdouble)height;
            gdouble b = 0.6; /* Slightly different */
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }

    /* Add same shapes */
    cairo_set_source_rgba(cr, 0.95, 0.95, 0.95, 0.8);
    cairo_arc(cr, width * 0.3, height * 0.3, 100, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.8);
    cairo_rectangle(cr, width * 0.6, height * 0.5, 150, 150);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(after_layer->surface);

    /* Set layers in dialog */
    filter_dialog_set_layers(dialog, before_layer, after_layer);

    /* Run dialog */
    response = filter_dialog_run(dialog, NULL, values, 3);

    if (response == GTK_RESPONSE_OK) {
        printf("Filter dialog values:\n");
        for (i = 0; i < 3; i++) {
            printf("  %s: %.0f\n", controls[i].label, values[i]);
        }
    } else {
        printf("Filter dialog cancelled\n");
    }

    /* Clean up */
    filter_dialog_free(dialog);
    layer_free(before_layer);
    layer_free(after_layer);
}

/**
 * Timeout callback to update anchor info label
 */
static gboolean update_anchor_info_timeout(gpointer data) {
    GtkWidget* label = (GtkWidget*)data;
    AnchorPositionWidget* widget = (AnchorPositionWidget*)g_object_get_data(G_OBJECT(label), "anchor_widget");
    if (widget) {
        CanvasAnchorPosition pos = anchor_position_widget_get_position(widget);
        const gchar* pos_names[] = {
            "Top-Left", "Top-Center", "Top-Right",
            "Middle-Left", "Center", "Middle-Right",
            "Bottom-Left", "Bottom-Center", "Bottom-Right"};
        gchar* text = g_strdup_printf("Position: %s", pos >= 0 && pos < 9 ? pos_names[pos] : "None");
        gtk_label_set_text(GTK_LABEL(label), text);
        g_free(text);
    }
    return G_SOURCE_CONTINUE;
}

/**
 * Test anchor position widget
 */
static void test_anchor_position_widget(void) {
    GtkWidget* dialog;
    GtkWidget* content_area;
    GtkWidget* vbox;
    GtkWidget* label;
    GtkWidget* hbox;
    GtkWidget* reset_button;
    GtkWidget* info_label;
    AnchorPositionWidget* anchor_widget;
    CanvasAnchorPosition position;

    /* Create dialog */
    dialog = gtk_dialog_new_with_buttons("Anchor Position Widget Test",
                                         NULL,
                                         GTK_DIALOG_MODAL,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 300);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    /* Create vertical box */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_box_pack_start(GTK_BOX(content_area), vbox, TRUE, TRUE, 0);

    /* Add label */
    label = gtk_label_new("Select anchor position:");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    /* Create anchor position widget */
    anchor_widget = anchor_position_widget_new();
    if (!anchor_widget) {
        debug_log("WRN", "Failed to create anchor position widget");
        gtk_widget_destroy(dialog);
        return;
    }

    gtk_box_pack_start(GTK_BOX(vbox), anchor_position_widget_get_widget(anchor_widget), FALSE, FALSE, 0);

    /* Create horizontal box for buttons */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Add reset button */
    reset_button = gtk_button_new_with_label("Reset to Center");
    g_signal_connect_swapped(reset_button, "clicked", G_CALLBACK(anchor_position_widget_reset), anchor_widget);
    gtk_box_pack_start(GTK_BOX(hbox), reset_button, FALSE, FALSE, 0);

    /* Add info label */
    info_label = gtk_label_new("Position: Center");
    gtk_label_set_xalign(GTK_LABEL(info_label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), info_label, FALSE, FALSE, 0);

    /* Store widget reference in label for update function */
    g_object_set_data(G_OBJECT(info_label), "anchor_widget", anchor_widget);

    /* Use a timeout to periodically update the label */
    g_timeout_add(100, update_anchor_info_timeout, info_label);

    /* Show all widgets */
    gtk_widget_show_all(dialog);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dialog));

    /* Get final position */
    position = anchor_position_widget_get_position(anchor_widget);
    debug_log("DBG", "Final anchor position: %d", position);

    /* Clean up */
    anchor_position_widget_free(anchor_widget);
    gtk_widget_destroy(dialog);
}

typedef struct {
    ColorWheel* wheel;
    HsvScale* hue_scale;
    HsvScale* saturation_scale;
    HsvScale* value_scale;
    RgbScale* red_scale;
    RgbScale* green_scale;
    RgbScale* blue_scale;
    GtkWidget* hex_entry;
    GtkWidget* color_preview;
    gboolean updating; // Prevent recursive updates
} ColorWheelTestData;

// Timer to sync scales from color wheel changes
static guint color_wheel_sync_timer = 0;
static ColorWheelTestData* g_test_data = NULL;

static void update_hex_and_preview(ColorWheelTestData* data) {
    if (!data || !data->wheel)
        return;

    double r, g, b;
    color_wheel_get_rgb(data->wheel, &r, &g, &b);

    // Convert to 0-255 range
    int ri = (int)(r * 255 + 0.5);
    int gi = (int)(g * 255 + 0.5);
    int bi = (int)(b * 255 + 0.5);

    // Update hex entry
    if (data->hex_entry) {
        char hex_str[8];
        g_snprintf(hex_str, sizeof(hex_str), "%02X%02X%02X", ri, gi, bi);
        gtk_entry_set_text(GTK_ENTRY(data->hex_entry), hex_str);
    }

    // Trigger redraw of color preview
    if (data->color_preview) {
        gtk_widget_queue_draw(data->color_preview);
    }
}

static gboolean sync_scales_from_wheel(gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return G_SOURCE_CONTINUE;

    static double last_h = -1, last_s = -1, last_v = -1;
    static gboolean initialized = FALSE;
    double h, s, v;
    color_wheel_get_hsv(data->wheel, &h, &s, &v);

    // Initialize on first run
    if (!initialized) {
        last_h = h;
        last_s = s;
        last_v = v;
        initialized = TRUE;
        return G_SOURCE_CONTINUE;
    }

    // Check if values changed
    if (fabs(h - last_h) > 0.01 || fabs(s - last_s) > 0.001 || fabs(v - last_v) > 0.001) {
        data->updating = TRUE;

        if (data->hue_scale) {
            hsv_scale_set_value(data->hue_scale, h);
            hsv_scale_set_hsv(data->hue_scale, h, s, v);
        }

        if (data->saturation_scale) {
            hsv_scale_set_value(data->saturation_scale, s);
            hsv_scale_set_hsv(data->saturation_scale, h, s, v);
        }

        if (data->value_scale) {
            hsv_scale_set_value(data->value_scale, v);
            hsv_scale_set_hsv(data->value_scale, h, s, v);
        }

        // Also update RGB scales
        double r, g, b;
        color_wheel_get_rgb(data->wheel, &r, &g, &b);

        if (data->red_scale) {
            rgb_scale_set_value(data->red_scale, r);
            rgb_scale_set_rgb(data->red_scale, r, g, b);
        }

        if (data->green_scale) {
            rgb_scale_set_value(data->green_scale, g);
            rgb_scale_set_rgb(data->green_scale, r, g, b);
        }

        if (data->blue_scale) {
            rgb_scale_set_value(data->blue_scale, b);
            rgb_scale_set_rgb(data->blue_scale, r, g, b);
        }

        update_hex_and_preview(data);

        last_h = h;
        last_s = s;
        last_v = v;

        data->updating = FALSE;
    }

    return G_SOURCE_CONTINUE;
}

static void on_hue_scale_changed(GtkRange* range, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double h = gtk_range_get_value(range);
    double s, v;
    color_wheel_get_hsv(data->wheel, NULL, &s, &v);

    color_wheel_set_hsv(data->wheel, h, s, v);

    // Update other scales
    if (data->hue_scale)
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    if (data->saturation_scale)
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    if (data->value_scale)
        hsv_scale_set_hsv(data->value_scale, h, s, v);

    data->updating = FALSE;
}

static void on_saturation_scale_changed(GtkRange* range, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double s = gtk_range_get_value(range);
    double h, v;
    color_wheel_get_hsv(data->wheel, &h, NULL, &v);

    color_wheel_set_hsv(data->wheel, h, s, v);

    // Update other scales
    if (data->hue_scale)
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    if (data->saturation_scale)
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    if (data->value_scale)
        hsv_scale_set_hsv(data->value_scale, h, s, v);

    data->updating = FALSE;
}

static void on_lightness_scale_changed(GtkRange* range, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double v = gtk_range_get_value(range);
    double h, s;
    color_wheel_get_hsv(data->wheel, &h, &s, NULL);

    color_wheel_set_hsv(data->wheel, h, s, v);

    // Update other scales
    if (data->hue_scale)
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    if (data->saturation_scale)
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    if (data->value_scale)
        hsv_scale_set_hsv(data->value_scale, h, s, v);

    data->updating = FALSE;
}

static void on_red_scale_changed(GtkRange* range, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double r = gtk_range_get_value(range);
    double current_r, g, b;
    color_wheel_get_rgb(data->wheel, &current_r, &g, &b);

    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update RGB scales
    if (data->red_scale)
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    if (data->green_scale)
        rgb_scale_set_rgb(data->green_scale, r, g, b);
    if (data->blue_scale)
        rgb_scale_set_rgb(data->blue_scale, r, g, b);

    update_hex_and_preview(data);

    data->updating = FALSE;
}

static void on_green_scale_changed(GtkRange* range, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double g = gtk_range_get_value(range);
    double r, current_g, b;
    color_wheel_get_rgb(data->wheel, &r, &current_g, &b);

    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update RGB scales
    if (data->red_scale)
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    if (data->green_scale)
        rgb_scale_set_rgb(data->green_scale, r, g, b);
    if (data->blue_scale)
        rgb_scale_set_rgb(data->blue_scale, r, g, b);

    update_hex_and_preview(data);

    data->updating = FALSE;
}

static void on_blue_scale_changed(GtkRange* range, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double b = gtk_range_get_value(range);
    double r, g, current_b;
    color_wheel_get_rgb(data->wheel, &r, &g, &current_b);

    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update RGB scales
    if (data->red_scale)
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    if (data->green_scale)
        rgb_scale_set_rgb(data->green_scale, r, g, b);
    if (data->blue_scale)
        rgb_scale_set_rgb(data->blue_scale, r, g, b);

    update_hex_and_preview(data);

    data->updating = FALSE;
}

static gboolean on_color_preview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel)
        return FALSE;

    double r, g, b;
    color_wheel_get_rgb(data->wheel, &r, &g, &b);

    // Fill with current color
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);

    // Draw border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    cairo_rectangle(cr, 0.5, 0.5, width - 1, height - 1);
    cairo_stroke(cr);

    return FALSE;
}

static void on_hex_entry_changed(GtkEditable* editable, gpointer user_data) {
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    const char* hex_text = gtk_entry_get_text(GTK_ENTRY(editable));
    if (!hex_text || strlen(hex_text) == 0)
        return;

    // Skip if not 6 characters yet (allow user to type)
    if (strlen(hex_text) != 6)
        return;

    // Parse hex color, default to 000000 if invalid
    unsigned int hex_value = 0;
    if (sscanf(hex_text, "%x", &hex_value) != 1) {
        // Invalid hex, default to black
        hex_value = 0x000000;
    }

    // Extract RGB components
    double r = ((hex_value >> 16) & 0xFF) / 255.0;
    double g = ((hex_value >> 8) & 0xFF) / 255.0;
    double b = (hex_value & 0xFF) / 255.0;

    data->updating = TRUE;
    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update scales
    double h, s, v;
    color_wheel_get_hsv(data->wheel, &h, &s, &v);

    if (data->hue_scale) {
        hsv_scale_set_value(data->hue_scale, h);
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    }

    if (data->saturation_scale) {
        hsv_scale_set_value(data->saturation_scale, s);
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    }

    if (data->value_scale) {
        hsv_scale_set_value(data->value_scale, v);
        hsv_scale_set_hsv(data->value_scale, h, s, v);
    }

    if (data->red_scale)
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    if (data->green_scale)
        rgb_scale_set_rgb(data->green_scale, r, g, b);
    if (data->blue_scale)
        rgb_scale_set_rgb(data->blue_scale, r, g, b);

    gtk_widget_queue_draw(data->color_preview);

    data->updating = FALSE;
}

static void on_window_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    ColorWheelTestData* data = (ColorWheelTestData*)user_data;

    // Stop sync timer
    if (color_wheel_sync_timer != 0) {
        g_source_remove(color_wheel_sync_timer);
        color_wheel_sync_timer = 0;
    }
    g_test_data = NULL;

    // Clean up
    if (data) {
        if (data->wheel)
            color_wheel_free(data->wheel);
        g_free(data);
    }

    gtk_main_quit();
}

static void test_color_chooser(void) {
    ColorWheelTestData* data = g_new0(ColorWheelTestData, 1);
    g_test_data = data;

    ColorWheel* wheel = color_wheel_new();
    data->wheel = wheel;

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "HSV Color Wheel & Scales");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 450);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), data);

    // Create main horizontal layout
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 15);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    // Left side: Color wheel
    GtkWidget* wheel_widget = color_wheel_get_widget(wheel);
    gtk_widget_set_size_request(wheel_widget, 400, 400);
    gtk_box_pack_start(GTK_BOX(hbox), wheel_widget, FALSE, FALSE, 0);

    // Right side: HSV scales (vertically stacked)
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    // Color preview box at top
    GtkWidget* preview_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    data->color_preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->color_preview, 60, 40);
    gtk_box_pack_start(GTK_BOX(preview_hbox), data->color_preview, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), preview_hbox, FALSE, FALSE, 0);

    // Connect draw event for color preview
    g_signal_connect(data->color_preview, "draw", G_CALLBACK(on_color_preview_draw), data);

    // Get initial HSV values
    double h, s, v;
    color_wheel_get_hsv(wheel, &h, &s, &v);

    // Hue scale (0-360)
    GtkWidget* hue_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* hue_label = gtk_label_new("Hue");
    gtk_widget_set_size_request(hue_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(hue_label), 0.0);
    gtk_box_pack_start(GTK_BOX(hue_hbox), hue_label, FALSE, FALSE, 0);

    data->hue_scale = HSV_SCALE(hsv_scale_new(HSV_SCALE_HUE, 0.0, 360.0));
    hsv_scale_set_value(data->hue_scale, h);
    hsv_scale_set_hsv(data->hue_scale, h, s, v);
    gtk_widget_set_size_request(GTK_WIDGET(data->hue_scale), 200, 30);
    g_signal_connect(data->hue_scale, "value-changed", G_CALLBACK(on_hue_scale_changed), data);
    gtk_box_pack_start(GTK_BOX(hue_hbox), GTK_WIDGET(data->hue_scale), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hue_hbox, FALSE, FALSE, 0);

    // Saturation scale (0-1)
    GtkWidget* saturation_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* saturation_label = gtk_label_new("Saturation");
    gtk_widget_set_size_request(saturation_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(saturation_label), 0.0);
    gtk_box_pack_start(GTK_BOX(saturation_hbox), saturation_label, FALSE, FALSE, 0);

    data->saturation_scale = HSV_SCALE(hsv_scale_new(HSV_SCALE_SATURATION, 0.0, 1.0));
    hsv_scale_set_value(data->saturation_scale, s);
    hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    gtk_widget_set_size_request(GTK_WIDGET(data->saturation_scale), 200, 30);
    g_signal_connect(data->saturation_scale, "value-changed", G_CALLBACK(on_saturation_scale_changed), data);
    gtk_box_pack_start(GTK_BOX(saturation_hbox), GTK_WIDGET(data->saturation_scale), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), saturation_hbox, FALSE, FALSE, 0);

    // Value scale (0-1)
    GtkWidget* value_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* value_label = gtk_label_new("Value");
    gtk_widget_set_size_request(value_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(value_label), 0.0);
    gtk_box_pack_start(GTK_BOX(value_hbox), value_label, FALSE, FALSE, 0);

    data->value_scale = HSV_SCALE(hsv_scale_new(HSV_SCALE_VALUE, 0.0, 1.0));
    hsv_scale_set_value(data->value_scale, v);
    hsv_scale_set_hsv(data->value_scale, h, s, v);
    gtk_widget_set_size_request(GTK_WIDGET(data->value_scale), 200, 30);
    g_signal_connect(data->value_scale, "value-changed", G_CALLBACK(on_lightness_scale_changed), data);
    gtk_box_pack_start(GTK_BOX(value_hbox), GTK_WIDGET(data->value_scale), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), value_hbox, FALSE, FALSE, 0);

    // RGB scales
    double r_val = 0.0, g_val = 0.0, b_val = 0.0;
    color_wheel_get_rgb(wheel, &r_val, &g_val, &b_val);

    // Red scale
    GtkWidget* red_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* red_label = gtk_label_new("Red");
    gtk_widget_set_size_request(red_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(red_label), 0.0);
    gtk_box_pack_start(GTK_BOX(red_hbox), red_label, FALSE, FALSE, 0);

    data->red_scale = RGB_SCALE(rgb_scale_new(RGB_SCALE_RED));
    rgb_scale_set_value(data->red_scale, r_val);
    rgb_scale_set_rgb(data->red_scale, r_val, g_val, b_val);
    gtk_widget_set_size_request(GTK_WIDGET(data->red_scale), 200, 30);
    g_signal_connect(data->red_scale, "value-changed", G_CALLBACK(on_red_scale_changed), data);
    gtk_box_pack_start(GTK_BOX(red_hbox), GTK_WIDGET(data->red_scale), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), red_hbox, FALSE, FALSE, 0);

    // Green scale
    GtkWidget* green_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* green_label = gtk_label_new("Green");
    gtk_widget_set_size_request(green_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(green_label), 0.0);
    gtk_box_pack_start(GTK_BOX(green_hbox), green_label, FALSE, FALSE, 0);

    data->green_scale = RGB_SCALE(rgb_scale_new(RGB_SCALE_GREEN));
    rgb_scale_set_value(data->green_scale, g_val);
    rgb_scale_set_rgb(data->green_scale, r_val, g_val, b_val);
    gtk_widget_set_size_request(GTK_WIDGET(data->green_scale), 200, 30);
    g_signal_connect(data->green_scale, "value-changed", G_CALLBACK(on_green_scale_changed), data);
    gtk_box_pack_start(GTK_BOX(green_hbox), GTK_WIDGET(data->green_scale), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), green_hbox, FALSE, FALSE, 0);

    // Blue scale
    GtkWidget* blue_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* blue_label = gtk_label_new("Blue");
    gtk_widget_set_size_request(blue_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(blue_label), 0.0);
    gtk_box_pack_start(GTK_BOX(blue_hbox), blue_label, FALSE, FALSE, 0);

    data->blue_scale = RGB_SCALE(rgb_scale_new(RGB_SCALE_BLUE));
    rgb_scale_set_value(data->blue_scale, b_val);
    rgb_scale_set_rgb(data->blue_scale, r_val, g_val, b_val);
    gtk_widget_set_size_request(GTK_WIDGET(data->blue_scale), 200, 30);
    g_signal_connect(data->blue_scale, "value-changed", G_CALLBACK(on_blue_scale_changed), data);
    gtk_box_pack_start(GTK_BOX(blue_hbox), GTK_WIDGET(data->blue_scale), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), blue_hbox, FALSE, FALSE, 0);

    // HTML/Hex input
    GtkWidget* html_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_hexpand(html_hbox, FALSE);
    gtk_widget_set_halign(html_hbox, GTK_ALIGN_START);
    GtkWidget* html_label = gtk_label_new("HTML");
    gtk_widget_set_size_request(html_label, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(html_label), 0.0);
    gtk_widget_set_hexpand(html_label, FALSE);
    gtk_widget_set_halign(html_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(html_hbox), html_label, FALSE, FALSE, 0);

    data->hex_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(data->hex_entry), 6);
    gtk_entry_set_width_chars(GTK_ENTRY(data->hex_entry), 6);
    gtk_entry_set_max_width_chars(GTK_ENTRY(data->hex_entry), 6);
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->hex_entry), "RRGGBB");
    gtk_widget_set_size_request(data->hex_entry, 50, -1);
    gtk_widget_set_hexpand(data->hex_entry, FALSE);
    gtk_widget_set_halign(data->hex_entry, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(html_hbox), data->hex_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), html_hbox, FALSE, FALSE, 0);

    // Connect changed event for hex entry
    g_signal_connect(data->hex_entry, "changed", G_CALLBACK(on_hex_entry_changed), data);

    // Initialize hex entry and color preview with initial values
    update_hex_and_preview(data);

    // Start timer to sync scales from color wheel changes
    color_wheel_sync_timer = g_timeout_add(50, sync_scales_from_wheel, data);

    gtk_widget_show_all(window);

    gtk_main();
}