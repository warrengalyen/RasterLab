#include "ui/dialogs/curves_dialog.h"
#include "../lib/ocular.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "ui/filters/filter_curves.h"
#include "ui/filters/filter_utils.h"
#include "ui/widgets/curves_widget.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Curves dialog structure
 */
struct _CurvesDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    CurvesWidget* curves_widget;
    GtkWidget* channel_red_button;
    GtkWidget* channel_green_button;
    GtkWidget* channel_blue_button;
    GtkWidget* channel_rgb_button;
    GtkWidget* notebook; /* For tool/options tabs */
    GtkWidget* tool_page;
    GtkWidget* options_page;
    GtkWidget* histogram_toggle;
    GtkWidget* grid_toggle;
    GtkWidget* diagonal_toggle;
    CurvesDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

/**
 * Compute histogram from image layer
 */
static void compute_layer_histogram(ImageLayer* layer, double* hist_r, double* hist_g, double* hist_b) {
    gint width, height;
    gint x, y;
    guchar* data;
    gint stride;
    guchar r, g, b;

    if (!layer || !layer->surface) {
        return;
    }

    width = cairo_image_surface_get_width(layer->surface);
    height = cairo_image_surface_get_height(layer->surface);
    data = cairo_image_surface_get_data(layer->surface);
    stride = cairo_image_surface_get_stride(layer->surface);

    /* Initialize histograms */
    for (int i = 0; i < 256; i++) {
        hist_r[i] = 0.0;
        hist_g[i] = 0.0;
        hist_b[i] = 0.0;
    }

    /* Compute histogram from ARGB32 surface */
    for (y = 0; y < height; y++) {
        guchar* row = data + y * stride;
        for (x = 0; x < width; x++) {
            guint32* pixel = (guint32*)(row + x * 4);
            guint32 argb = *pixel;

            /* Extract ARGB components */
            r = (argb >> 16) & 0xFF;
            g = (argb >> 8) & 0xFF;
            b = argb & 0xFF;

            hist_r[r]++;
            hist_g[g]++;
            hist_b[b]++;
        }
    }
}

/* Use filter_curves_apply from filter_curves.c */

/**
 * Channel button clicked callback
 */
static void on_channel_button_clicked(GtkWidget* button, gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)user_data;
    Channel channel;

    if (!dialog || !dialog->curves_widget) {
        return;
    }

    /* Only proceed if the button is being activated (not deactivated) */
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
        /* If button is being deactivated, reactivate it (only one can be active) */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
        return;
    }

    /* Determine which channel was clicked */
    if (button == dialog->channel_red_button) {
        channel = CHANNEL_RED;
    } else if (button == dialog->channel_green_button) {
        channel = CHANNEL_GREEN;
    } else if (button == dialog->channel_blue_button) {
        channel = CHANNEL_BLUE;
    } else if (button == dialog->channel_rgb_button) {
        channel = CHANNEL_RGB;
    } else {
        return;
    }

    /* Block signal handlers to prevent infinite loop when updating button states */
    if (dialog->channel_red_button) {
        g_signal_handlers_block_by_func(dialog->channel_red_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }
    if (dialog->channel_green_button) {
        g_signal_handlers_block_by_func(dialog->channel_green_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }
    if (dialog->channel_blue_button) {
        g_signal_handlers_block_by_func(dialog->channel_blue_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }
    if (dialog->channel_rgb_button) {
        g_signal_handlers_block_by_func(dialog->channel_rgb_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }

    /* Update button states - make clicked button active, others inactive */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_red_button), (channel == CHANNEL_RED));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_green_button), (channel == CHANNEL_GREEN));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_blue_button), (channel == CHANNEL_BLUE));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_rgb_button), (channel == CHANNEL_RGB));

    /* Unblock signal handlers */
    if (dialog->channel_red_button) {
        g_signal_handlers_unblock_by_func(dialog->channel_red_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }
    if (dialog->channel_green_button) {
        g_signal_handlers_unblock_by_func(dialog->channel_green_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }
    if (dialog->channel_blue_button) {
        g_signal_handlers_unblock_by_func(dialog->channel_blue_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }
    if (dialog->channel_rgb_button) {
        g_signal_handlers_unblock_by_func(dialog->channel_rgb_button, G_CALLBACK(on_channel_button_clicked), dialog);
    }

    /* Update curves widget channel */
    curves_widget_set_channel(dialog->curves_widget, channel);

    /* Trigger preview update */
    if (dialog->preview_callback) {
        dialog->preview_callback(dialog, dialog->preview_user_data);
    }
}

/**
 * Histogram toggle callback
 */
static void on_histogram_toggled(GtkToggleButton* button, gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)user_data;
    if (dialog && dialog->curves_widget) {
        curves_widget_set_histogram_visible(dialog->curves_widget,
                                            gtk_toggle_button_get_active(button));
    }
}

/**
 * Grid toggle callback
 */
static void on_grid_toggled(GtkToggleButton* button, gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)user_data;
    if (dialog && dialog->curves_widget) {
        curves_widget_set_grid_visible(dialog->curves_widget,
                                       gtk_toggle_button_get_active(button));
    }
}

/**
 * Diagonal toggle callback
 */
static void on_diagonal_toggled(GtkToggleButton* button, gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)user_data;
    if (dialog && dialog->curves_widget) {
        curves_widget_set_diagonal_visible(dialog->curves_widget,
                                           gtk_toggle_button_get_active(button));
    }
}

/**
 * Curves changed callback
 */
static void on_curves_changed(GtkWidget* widget, gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)user_data;
    (void)widget;

    /* Trigger preview update */
    if (dialog && dialog->preview_callback) {
        dialog->preview_callback(dialog, dialog->preview_user_data);
    }
}

/**
 * Reset button clicked callback
 */
static void on_reset_clicked(GtkWidget* widget, gpointer user_data) {
    CurvesDialog* dialog = (CurvesDialog*)user_data;
    (void)widget;
    curves_dialog_reset(dialog);
}

/**
 * Create channel selection buttons
 */
static GtkWidget* create_channel_selector(CurvesDialog* dialog) {
    GtkWidget* hbox;
    GtkWidget* label;
    GtkWidget* button;

    /* Create horizontal box for channel selector */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_bottom(hbox, 10);

    /* Add label */
    label = gtk_label_new("channel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_end(label, 10);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    /* Create RGB button */
    button = gtk_toggle_button_new();
    GtkWidget* rgb_icon = gtk_image_new_from_resource("/icons/channel-rgb.png");
    GtkWidget* rgb_label = gtk_label_new("RGB");
    gtk_widget_set_margin_start(rgb_label, 5);
    GtkWidget* rgb_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(rgb_hbox), rgb_icon);
    gtk_container_add(GTK_CONTAINER(rgb_hbox), rgb_label);
    gtk_container_add(GTK_CONTAINER(button), rgb_hbox);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    g_signal_connect(button, "clicked", G_CALLBACK(on_channel_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    dialog->channel_rgb_button = button;

    /* Create Red button */
    button = gtk_toggle_button_new();
    GtkWidget* red_icon = gtk_image_new_from_resource("/icons/channel-red.png");
    GtkWidget* red_label = gtk_label_new("red");
    gtk_widget_set_margin_start(red_label, 5);
    GtkWidget* red_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(red_hbox), red_icon);
    gtk_container_add(GTK_CONTAINER(red_hbox), red_label);
    gtk_container_add(GTK_CONTAINER(button), red_hbox);
    g_signal_connect(button, "clicked", G_CALLBACK(on_channel_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    dialog->channel_red_button = button;

    /* Create Green button */
    button = gtk_toggle_button_new();
    GtkWidget* green_icon = gtk_image_new_from_resource("/icons/channel-green.png");
    GtkWidget* green_label = gtk_label_new("green");
    gtk_widget_set_margin_start(green_label, 5);
    GtkWidget* green_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(green_hbox), green_icon);
    gtk_container_add(GTK_CONTAINER(green_hbox), green_label);
    gtk_container_add(GTK_CONTAINER(button), green_hbox);
    g_signal_connect(button, "clicked", G_CALLBACK(on_channel_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    dialog->channel_green_button = button;

    /* Create Blue button */
    button = gtk_toggle_button_new();
    GtkWidget* blue_icon = gtk_image_new_from_resource("/icons/channel-blue.png");
    GtkWidget* blue_label = gtk_label_new("blue");
    gtk_widget_set_margin_start(blue_label, 5);
    GtkWidget* blue_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(blue_hbox), blue_icon);
    gtk_container_add(GTK_CONTAINER(blue_hbox), blue_label);
    gtk_container_add(GTK_CONTAINER(button), blue_hbox);
    g_signal_connect(button, "clicked", G_CALLBACK(on_channel_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    dialog->channel_blue_button = button;

    return hbox;
}

/**
 * Create display options page
 */
static GtkWidget* create_options_page(CurvesDialog* dialog) {
    GtkWidget* vbox;
    GtkWidget* hbox;
    GtkWidget* label;
    GtkWidget* toggle;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    /* Histogram overlay toggle */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    label = gtk_label_new("histogram overlay");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    toggle = gtk_toggle_button_new();
    GtkWidget* enable_label = gtk_label_new("show");
    gtk_container_add(GTK_CONTAINER(toggle), enable_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_histogram_toggled), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), toggle, FALSE, FALSE, 0);
    dialog->histogram_toggle = toggle;
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Grid toggle */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    label = gtk_label_new("grid");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    toggle = gtk_toggle_button_new();
    enable_label = gtk_label_new("show");
    gtk_container_add(GTK_CONTAINER(toggle), enable_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_grid_toggled), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), toggle, FALSE, FALSE, 0);
    dialog->grid_toggle = toggle;
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Original curve (diagonal line) toggle */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    label = gtk_label_new("original curve (diagonal line)");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    toggle = gtk_toggle_button_new();
    enable_label = gtk_label_new("show");
    gtk_container_add(GTK_CONTAINER(toggle), enable_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_diagonal_toggled), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), toggle, FALSE, FALSE, 0);
    dialog->diagonal_toggle = toggle;
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    return vbox;
}

/**
 * Create a new curves adjustment dialog
 */
CurvesDialog* curves_dialog_new(const gchar* title) {
    CurvesDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* channel_selector;
    GtkWidget* curves_container;
    GtkWidget* display_label;
    GtkWidget* reset_button;
    GtkWidget* button_box;

    if (!title) {
        return NULL;
    }

    dialog = (CurvesDialog*)g_malloc(sizeof(CurvesDialog));
    if (!dialog) {
        return NULL;
    }

    /* Initialize */
    dialog->curves_widget = NULL;
    dialog->channel_red_button = NULL;
    dialog->channel_green_button = NULL;
    dialog->channel_blue_button = NULL;
    dialog->channel_rgb_button = NULL;
    dialog->notebook = NULL;
    dialog->tool_page = NULL;
    dialog->options_page = NULL;
    dialog->histogram_toggle = NULL;
    dialog->grid_toggle = NULL;
    dialog->diagonal_toggle = NULL;
    dialog->preview_callback = NULL;
    dialog->preview_user_data = NULL;

    /* Create dialog window */
    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK",
                                                 GTK_RESPONSE_OK,
                                                 "_Cancel",
                                                 GTK_RESPONSE_CANCEL,
                                                 NULL);

    /* Don't set a fixed default size - let dialog size to content */
    gtk_window_set_resizable(GTK_WINDOW(dialog->dialog), TRUE);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);
    gtk_widget_set_margin_start(content_area, 10);
    gtk_widget_set_margin_end(content_area, 10);

    /* Create main horizontal box */
    main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    /* Create left side vertical box for preview and instructions */
    GtkWidget* left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(left_vbox, FALSE);
    gtk_widget_set_vexpand(left_vbox, FALSE);
    gtk_box_pack_start(GTK_BOX(main_hbox), left_vbox, FALSE, FALSE, 0);

    /* Create filter preview widget (left side) */
    dialog->preview = FILTER_PREVIEW(filter_preview_new());
    gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), -1, 338);
    gtk_widget_set_hexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(dialog->preview), FALSE);
    gtk_box_pack_start(GTK_BOX(left_vbox), GTK_WIDGET(dialog->preview), FALSE, FALSE, 0);

    /* Create instructions label */
    GtkWidget* instructions_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(instructions_label),
                         "<span size='medium'>instructions:\n"
                         "+ left-click to add new nodes or drag existing nodes\n"
                         "+ right-click to remove nodes</span>");
    gtk_label_set_xalign(GTK_LABEL(instructions_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(instructions_label), 0.0);
    gtk_widget_set_margin_top(instructions_label, 5);
    gtk_widget_set_margin_start(instructions_label, 0);
    gtk_widget_set_margin_end(instructions_label, 0);
    gtk_widget_set_halign(instructions_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(left_vbox), instructions_label, FALSE, FALSE, 0);

    /* Create right side vertical box */
    right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right_vbox, 320, -1);
    gtk_widget_set_margin_start(right_vbox, 0);
    gtk_widget_set_margin_end(right_vbox, 10);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, FALSE, FALSE, 0);

    /* Create notebook for tabs */
    dialog->notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(dialog->notebook), GTK_POS_BOTTOM);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(dialog->notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(dialog->notebook), FALSE);

    /* Style notebook and tabs to match dialog background */
    /* Get the background color from the content area's style context */
    GtkStyleContext* content_style = gtk_widget_get_style_context(content_area);
    GdkRGBA bg_color;
    if (gtk_style_context_lookup_color(content_style, "theme_bg_color", &bg_color)) {
        /* Use theme background color */
    } else {
        /* Fallback to default dialog background */
        bg_color.red = 0.96;
        bg_color.green = 0.96;
        bg_color.blue = 0.96;
        bg_color.alpha = 1.0;
    }

    GtkCssProvider* css_provider = gtk_css_provider_new();
    guint r = (guint)(bg_color.red * 255);
    guint g = (guint)(bg_color.green * 255);
    guint b = (guint)(bg_color.blue * 255);
    gchar* css = g_strdup_printf(
        "notebook {"
        "  background-color: rgb(%u, %u, %u);"
        "}"
        "notebook > stack {"
        "  background-color: rgb(%u, %u, %u);"
        "}"
        "notebook > stack > box {"
        "  background-color: rgb(%u, %u, %u);"
        "}"
        "notebook header {"
        "  background-color: rgb(%u, %u, %u);"
        "}"
        "notebook header tabs {"
        "  background-color: rgb(%u, %u, %u);"
        "}"
        "notebook header tabs tab {"
        "  background-color: rgb(%u, %u, %u);"
        "  border: none;"
        "}"
        "notebook header tabs tab:checked {"
        "  background-color: rgb(%u, %u, %u);"
        "}",
        r, g, b,  /* notebook */
        r, g, b,  /* notebook > stack */
        r, g, b,  /* notebook > stack > box */
        r, g, b,  /* notebook header */
        r, g, b,  /* notebook header tabs */
        r, g, b,  /* notebook header tabs tab */
        r, g, b); /* notebook header tabs tab:checked */
    gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
    g_free(css);
    GtkStyleContext* notebook_style = gtk_widget_get_style_context(dialog->notebook);
    gtk_style_context_add_provider(notebook_style, GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    gtk_box_pack_start(GTK_BOX(right_vbox), dialog->notebook, TRUE, TRUE, 0);

    /* Create tool page with curves widget and channel selector */
    dialog->tool_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(dialog->tool_page), 5);
    /* Style tool page background */
    GtkCssProvider* tool_page_css = gtk_css_provider_new();
    gchar* tool_page_css_str = g_strdup_printf(
        "#tool-page { background-color: rgb(%u, %u, %u); }",
        (guint)(bg_color.red * 255), (guint)(bg_color.green * 255), (guint)(bg_color.blue * 255));
    gtk_css_provider_load_from_data(tool_page_css, tool_page_css_str, -1, NULL);
    g_free(tool_page_css_str);
    gtk_widget_set_name(dialog->tool_page, "tool-page");
    /* Use ID selector for more specific targeting */
    GtkStyleContext* tool_page_style = gtk_widget_get_style_context(dialog->tool_page);
    gtk_style_context_add_provider(tool_page_style, GTK_STYLE_PROVIDER(tool_page_css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(tool_page_css);

    /* Create curves widget */
    dialog->curves_widget = CURVES_WIDGET(curves_widget_new());
    if (!dialog->curves_widget) {
        g_warning("Failed to create curves widget");
        g_free(dialog);
        return NULL;
    }
    gtk_widget_set_size_request(GTK_WIDGET(dialog->curves_widget), 464, 344);
    gtk_widget_set_margin_bottom(GTK_WIDGET(dialog->curves_widget), 10);
    gtk_box_pack_start(GTK_BOX(dialog->tool_page), GTK_WIDGET(dialog->curves_widget), FALSE, FALSE, 0);

    /* Connect curves changed signal */
    g_signal_connect(dialog->curves_widget, "curve-changed", G_CALLBACK(on_curves_changed), dialog);

    /* Create channel selector */
    channel_selector = create_channel_selector(dialog);
    gtk_box_pack_start(GTK_BOX(dialog->tool_page), channel_selector, FALSE, FALSE, 0);

    /* Add tool page to notebook with expanding tab */
    GtkWidget* tool_tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* tool_tab_label = gtk_label_new("tool");
    gtk_label_set_xalign(GTK_LABEL(tool_tab_label), 0.5);                     /* Center horizontally */
    gtk_box_pack_start(GTK_BOX(tool_tab_box), tool_tab_label, TRUE, TRUE, 0); /* Expand to fill */
    gtk_widget_set_hexpand(tool_tab_box, TRUE);
    gtk_widget_show_all(tool_tab_box);
    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), dialog->tool_page, tool_tab_box);

    /* Create display options page */
    dialog->options_page = create_options_page(dialog);
    /* Style options page background */
    GtkCssProvider* options_page_css = gtk_css_provider_new();
    gchar* options_page_css_str = g_strdup_printf(
        "#options-page { background-color: rgb(%u, %u, %u); }",
        (guint)(bg_color.red * 255), (guint)(bg_color.green * 255), (guint)(bg_color.blue * 255));
    gtk_css_provider_load_from_data(options_page_css, options_page_css_str, -1, NULL);
    g_free(options_page_css_str);
    gtk_widget_set_name(dialog->options_page, "options-page");
    GtkStyleContext* options_page_style = gtk_widget_get_style_context(dialog->options_page);
    gtk_style_context_add_provider(options_page_style, GTK_STYLE_PROVIDER(options_page_css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(options_page_css);

    GtkWidget* options_tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* options_tab_label = gtk_label_new("options");
    gtk_label_set_xalign(GTK_LABEL(options_tab_label), 0.5);                        /* Center horizontally */
    gtk_box_pack_start(GTK_BOX(options_tab_box), options_tab_label, TRUE, TRUE, 0); /* Expand to fill */
    gtk_widget_set_hexpand(options_tab_box, TRUE);
    gtk_widget_show_all(options_tab_box);
    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), dialog->options_page, options_tab_box);

    /* Set tool page as default */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(dialog->notebook), 0);

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

        /* Make action area expand horizontally to fill width */
        gtk_widget_set_hexpand(button_box, TRUE);

        /* Create reset button with reset.svg icon and add it to the left side of action area */
        reset_button = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.svg");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(reset_button), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_button), TRUE);
        }
        gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(button_box), reset_button, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(button_box), reset_button, 0); /* Move to start */
        g_signal_connect(reset_button, "clicked",
                         G_CALLBACK(on_reset_clicked), dialog);
    }

    /* Show all widgets */
    gtk_widget_show_all(content_area);

    return dialog;
}

/**
 * Free curves dialog
 */
void curves_dialog_free(CurvesDialog* dialog) {
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
GtkWindow* curves_dialog_get_window(CurvesDialog* dialog) {
    if (!dialog || !dialog->dialog) {
        return NULL;
    }

    return GTK_WINDOW(dialog->dialog);
}

/**
 * Set the layers for preview
 */
void curves_dialog_set_layers(CurvesDialog* dialog, ImageLayer* original, ImageLayer* temp) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;
    double hist_r[256], hist_g[256], hist_b[256];

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get composite surfaces from layers - pass full unmasked surfaces
       The preview widget will handle masking display based on selection */
    if (original && original->surface) {
        before_surface = cairo_surface_reference(original->surface);

        /* Compute histogram from original layer */
        compute_layer_histogram(original, hist_r, hist_g, hist_b);

        /* Set histogram data on curves widget */
        if (dialog->curves_widget) {
            curves_widget_set_histogram_data(dialog->curves_widget, CHANNEL_RED, hist_r, 256);
            curves_widget_set_histogram_data(dialog->curves_widget, CHANNEL_GREEN, hist_g, 256);
            curves_widget_set_histogram_data(dialog->curves_widget, CHANNEL_BLUE, hist_b, 256);
            /* Use red channel for RGB histogram */
            curves_widget_set_histogram_data(dialog->curves_widget, CHANNEL_RGB, hist_r, 256);
        }
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
}

/**
 * Run the dialog
 */
gint curves_dialog_run(CurvesDialog* dialog, GtkWindow* parent) {
    gint response;

    if (!dialog) {
        return GTK_RESPONSE_CANCEL;
    }

    if (parent) {
        gtk_window_set_transient_for(curves_dialog_get_window(dialog), parent);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));

    return response;
}

/**
 * Update the after layer in preview
 */
void curves_dialog_update_after_layer(CurvesDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;
    struct ImageDocument* doc = NULL;
    struct ImageLayer* original_layer = NULL;

    if (!dialog || !dialog->preview) {
        return;
    }

    /* Get document and layer from dialog if available */
    GtkWindow* window = curves_dialog_get_window(dialog);
    if (window) {
        doc = (struct ImageDocument*)g_object_get_data(G_OBJECT(window), "filter_doc");
        original_layer = (struct ImageLayer*)g_object_get_data(G_OBJECT(window), "original_layer");
    }

    if (layer && layer->surface) {
        /* Pass the full unmasked surface - preview widget will handle masking display */
        after_surface = cairo_surface_reference(layer->surface);
    }

    filter_preview_set_after_surface(dialog->preview, after_surface);

    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

/**
 * Set preview callback for live updates
 */
void curves_dialog_set_preview_callback(CurvesDialog* dialog,
                                        CurvesDialogPreviewCallback callback,
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
void curves_dialog_reset(CurvesDialog* dialog) {
    if (!dialog || !dialog->curves_widget) {
        return;
    }

    /* Reset curves widget */
    curves_widget_reset_curve(dialog->curves_widget);

    /* Reset channel to RGB */
    curves_widget_set_channel(dialog->curves_widget, CHANNEL_RGB);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_rgb_button), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_red_button), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_green_button), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->channel_blue_button), FALSE);

    /* Trigger preview update if callback is set */
    if (dialog->preview_callback) {
        dialog->preview_callback(dialog, dialog->preview_user_data);
    }
}

/**
 * Get the curves widget from the dialog
 */
CurvesWidget* curves_dialog_get_curves_widget(CurvesDialog* dialog) {
    if (!dialog) {
        return NULL;
    }

    return dialog->curves_widget;
}
