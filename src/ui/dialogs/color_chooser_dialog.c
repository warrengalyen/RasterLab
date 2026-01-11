#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/widgets/hsv_color_wheel.h"
#include "ui/widgets/hsv_scale.h"
#include "ui/widgets/rgb_scale.h"
#include "ui/widgets/vertical_spin_button.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    ColorWheel* wheel;
    HsvScale* hue_scale;
    HsvScale* saturation_scale;
    HsvScale* value_scale;
    RgbScale* red_scale;
    RgbScale* green_scale;
    RgbScale* blue_scale;
    GtkWidget* hue_spin;
    GtkWidget* saturation_spin;
    GtkWidget* value_spin;
    GtkWidget* red_spin;
    GtkWidget* green_spin;
    GtkWidget* blue_spin;
    GtkWidget* hex_entry;
    GtkWidget* color_preview;
    gboolean updating;         // Prevent recursive updates
    gboolean realtime_updates; // If FALSE, only call callback on dialog close
    void (*callback)(double r, double g, double b, gpointer user_data);
    gpointer callback_data;
} ColorChooserData;

// Timer for syncing scales from color wheel changes
static guint color_wheel_sync_timer = 0;

static void update_hex_and_preview(ColorChooserData* data) {
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

    // Call the update callback (only if realtime updates are enabled)
    if (data->realtime_updates && data->callback) {
        data->callback(r, g, b, data->callback_data);
    }
}

static gboolean sync_scales_from_wheel(gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
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

        // Update spin buttons
        if (data->hue_spin)
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(data->hue_spin), h);
        if (data->saturation_spin)
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(data->saturation_spin), s * 100.0);
        if (data->value_spin)
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(data->value_spin), v * 100.0);
        if (data->red_spin)
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(data->red_spin), r * 255.0);
        if (data->green_spin)
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(data->green_spin), g * 255.0);
        if (data->blue_spin)
            vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(data->blue_spin), b * 255.0);

        update_hex_and_preview(data);

        last_h = h;
        last_s = s;
        last_v = v;

        data->updating = FALSE;
    }

    return G_SOURCE_CONTINUE;
}

static void on_hue_scale_changed(GtkRange* range, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
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
    ColorChooserData* data = (ColorChooserData*)user_data;
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
    ColorChooserData* data = (ColorChooserData*)user_data;
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
    ColorChooserData* data = (ColorChooserData*)user_data;
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
    ColorChooserData* data = (ColorChooserData*)user_data;
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
    ColorChooserData* data = (ColorChooserData*)user_data;
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

// Spin button change callbacks
static void on_hue_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double h = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin));
    double s, v;
    color_wheel_get_hsv(data->wheel, NULL, &s, &v);

    color_wheel_set_hsv(data->wheel, h, s, v);

    // Update HSV scales
    if (data->hue_scale) {
        hsv_scale_set_value(data->hue_scale, h);
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    }
    if (data->saturation_scale)
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    if (data->value_scale)
        hsv_scale_set_hsv(data->value_scale, h, s, v);

    data->updating = FALSE;
}

static void on_saturation_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double s = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)) / 100.0;
    double h, v;
    color_wheel_get_hsv(data->wheel, &h, NULL, &v);

    color_wheel_set_hsv(data->wheel, h, s, v);

    // Update HSV scales
    if (data->saturation_scale) {
        hsv_scale_set_value(data->saturation_scale, s);
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);
    }
    if (data->hue_scale)
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    if (data->value_scale)
        hsv_scale_set_hsv(data->value_scale, h, s, v);

    data->updating = FALSE;
}

static void on_value_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double v = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)) / 100.0;
    double h, s;
    color_wheel_get_hsv(data->wheel, &h, &s, NULL);

    color_wheel_set_hsv(data->wheel, h, s, v);

    // Update HSV scales
    if (data->value_scale) {
        hsv_scale_set_value(data->value_scale, v);
        hsv_scale_set_hsv(data->value_scale, h, s, v);
    }
    if (data->hue_scale)
        hsv_scale_set_hsv(data->hue_scale, h, s, v);
    if (data->saturation_scale)
        hsv_scale_set_hsv(data->saturation_scale, h, s, v);

    data->updating = FALSE;
}

static void on_red_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double r = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)) / 255.0;
    double current_r, g, b;
    color_wheel_get_rgb(data->wheel, &current_r, &g, &b);

    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update RGB scales
    if (data->red_scale) {
        rgb_scale_set_value(data->red_scale, r);
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    }
    if (data->green_scale)
        rgb_scale_set_rgb(data->green_scale, r, g, b);
    if (data->blue_scale)
        rgb_scale_set_rgb(data->blue_scale, r, g, b);

    update_hex_and_preview(data);

    data->updating = FALSE;
}

static void on_green_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double g = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)) / 255.0;
    double r, current_g, b;
    color_wheel_get_rgb(data->wheel, &r, &current_g, &b);

    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update RGB scales
    if (data->green_scale) {
        rgb_scale_set_value(data->green_scale, g);
        rgb_scale_set_rgb(data->green_scale, r, g, b);
    }
    if (data->red_scale)
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    if (data->blue_scale)
        rgb_scale_set_rgb(data->blue_scale, r, g, b);

    update_hex_and_preview(data);

    data->updating = FALSE;
}

static void on_blue_spin_changed(GtkWidget* spin, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
    if (!data || !data->wheel || data->updating)
        return;

    data->updating = TRUE;

    double b = vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)) / 255.0;
    double r, g, current_b;
    color_wheel_get_rgb(data->wheel, &r, &g, &current_b);

    color_wheel_set_rgb(data->wheel, r, g, b);

    // Update RGB scales
    if (data->blue_scale) {
        rgb_scale_set_value(data->blue_scale, b);
        rgb_scale_set_rgb(data->blue_scale, r, g, b);
    }
    if (data->red_scale)
        rgb_scale_set_rgb(data->red_scale, r, g, b);
    if (data->green_scale)
        rgb_scale_set_rgb(data->green_scale, r, g, b);

    update_hex_and_preview(data);

    data->updating = FALSE;
}

static gboolean on_color_preview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ColorChooserData* data = (ColorChooserData*)user_data;
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
    ColorChooserData* data = (ColorChooserData*)user_data;
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

    // Call the update callback (only if realtime updates are enabled)
    if (data->realtime_updates && data->callback) {
        data->callback(r, g, b, data->callback_data);
    }

    data->updating = FALSE;
}

static void on_dialog_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    ColorChooserData* data = (ColorChooserData*)user_data;

    // Stop sync timer
    if (color_wheel_sync_timer != 0) {
        g_source_remove(color_wheel_sync_timer);
        color_wheel_sync_timer = 0;
    }

    // Clean up
    if (data) {
        if (data->wheel)
            color_wheel_free(data->wheel);
        g_free(data);
    }
}

GtkWidget* color_chooser_dialog_new(GtkWindow* parent,
                                    const char* title,
                                    GdkRGBA* initial_color,
                                    void (*callback)(double r, double g, double b, gpointer user_data),
                                    gpointer callback_data,
                                    gboolean realtime_updates) {
    ColorChooserData* data = g_new0(ColorChooserData, 1);
    data->callback = callback;
    data->callback_data = callback_data;
    data->realtime_updates = realtime_updates;

    ColorWheel* wheel = color_wheel_new();
    data->wheel = wheel;

    // Set initial color if provided
    if (initial_color) {
        color_wheel_set_rgb(wheel, initial_color->red, initial_color->green, initial_color->blue);
    }

    GtkWidget* dialog = gtk_dialog_new_with_buttons(title,
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close",
                                                    GTK_RESPONSE_CLOSE,
                                                    NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 450);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    // Store data in dialog for retrieval
    g_object_set_data(G_OBJECT(dialog), "color_chooser_data", data);

    // Get content area
    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);

    // Create main horizontal layout
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);

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

    // Add spin button for hue (0-360)
    GtkAdjustment* hue_adj = gtk_adjustment_new(h, 0.0, 360.0, 1.0, 10.0, 0.0);
    data->hue_spin = vertical_spin_button_new(hue_adj, 1.0, 0);
    gtk_widget_set_size_request(data->hue_spin, 45, 30);
    g_signal_connect(data->hue_spin, "value-changed", G_CALLBACK(on_hue_spin_changed), data);
    gtk_box_pack_start(GTK_BOX(hue_hbox), data->hue_spin, FALSE, FALSE, 0);

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

    // Add spin button for saturation (0-100%)
    GtkAdjustment* sat_adj = gtk_adjustment_new(s * 100.0, 0.0, 100.0, 1.0, 10.0, 0.0);
    data->saturation_spin = vertical_spin_button_new(sat_adj, 1.0, 0);
    gtk_widget_set_size_request(data->saturation_spin, 45, 30);
    g_signal_connect(data->saturation_spin, "value-changed", G_CALLBACK(on_saturation_spin_changed), data);
    gtk_box_pack_start(GTK_BOX(saturation_hbox), data->saturation_spin, FALSE, FALSE, 0);

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

    // Add spin button for value (0-100%)
    GtkAdjustment* val_adj = gtk_adjustment_new(v * 100.0, 0.0, 100.0, 1.0, 10.0, 0.0);
    data->value_spin = vertical_spin_button_new(val_adj, 1.0, 0);
    gtk_widget_set_size_request(data->value_spin, 45, 30);
    g_signal_connect(data->value_spin, "value-changed", G_CALLBACK(on_value_spin_changed), data);
    gtk_box_pack_start(GTK_BOX(value_hbox), data->value_spin, FALSE, FALSE, 0);

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

    // Add spin button for red (0-255)
    GtkAdjustment* red_adj = gtk_adjustment_new(r_val * 255.0, 0.0, 255.0, 1.0, 10.0, 0.0);
    data->red_spin = vertical_spin_button_new(red_adj, 1.0, 0);
    gtk_widget_set_size_request(data->red_spin, 45, 30);
    g_signal_connect(data->red_spin, "value-changed", G_CALLBACK(on_red_spin_changed), data);
    gtk_box_pack_start(GTK_BOX(red_hbox), data->red_spin, FALSE, FALSE, 0);

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

    // Add spin button for green (0-255)
    GtkAdjustment* green_adj = gtk_adjustment_new(g_val * 255.0, 0.0, 255.0, 1.0, 10.0, 0.0);
    data->green_spin = vertical_spin_button_new(green_adj, 1.0, 0);
    gtk_widget_set_size_request(data->green_spin, 45, 30);
    g_signal_connect(data->green_spin, "value-changed", G_CALLBACK(on_green_spin_changed), data);
    gtk_box_pack_start(GTK_BOX(green_hbox), data->green_spin, FALSE, FALSE, 0);

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

    // Add spin button for blue (0-255)
    GtkAdjustment* blue_adj = gtk_adjustment_new(b_val * 255.0, 0.0, 255.0, 1.0, 10.0, 0.0);
    data->blue_spin = vertical_spin_button_new(blue_adj, 1.0, 0);
    gtk_widget_set_size_request(data->blue_spin, 45, 30);
    g_signal_connect(data->blue_spin, "value-changed", G_CALLBACK(on_blue_spin_changed), data);
    gtk_box_pack_start(GTK_BOX(blue_hbox), data->blue_spin, FALSE, FALSE, 0);

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
    gtk_entry_set_width_chars(GTK_ENTRY(data->hex_entry), 7);
    gtk_entry_set_max_width_chars(GTK_ENTRY(data->hex_entry), 7);
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

    // Connect destroy signal to clean up
    g_signal_connect(dialog, "destroy", G_CALLBACK(on_dialog_destroy), data);

    gtk_widget_show_all(dialog);

    return dialog;
}

void color_chooser_dialog_get_color(GtkWidget* dialog, double* r, double* g, double* b) {
    ColorChooserData* data = (ColorChooserData*)g_object_get_data(G_OBJECT(dialog), "color_chooser_data");
    if (data && data->wheel) {
        color_wheel_get_rgb(data->wheel, r, g, b);
    }
}
