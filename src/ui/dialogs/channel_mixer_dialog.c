#include "ui/dialogs/channel_mixer_dialog.h"
#include "ui/filters/filter_utils.h"
#include "ui/ui_utils.h"
#include "ui/widgets/filter_preview.h"
#include "ui/widgets/vertical_spin_button.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#define MIXER_ROWS 4
#define MIXER_COLS 4
#define MIXER_SIZE (MIXER_ROWS * MIXER_COLS)
#define RGB_MIN (-200)
#define RGB_MAX 200
#define CONSTANT_MIN (-255)
#define CONSTANT_MAX 255
#define DEFAULT_PRIMARY 100
#define DEFAULT_OTHER 0

enum {
    OUTPUT_RED = 0,
    OUTPUT_GREEN = 1,
    OUTPUT_BLUE = 2,
    OUTPUT_GRAY = 3
};

struct _ChannelMixerDialog {
    GtkWidget* dialog;
    FilterPreview* preview;
    /* Output channel toggle group (red, green, blue) */
    GtkWidget* out_red_btn;
    GtkWidget* out_green_btn;
    GtkWidget* out_blue_btn;
    /* Input sliders: row index 0=Red, 1=Green, 2=Blue, 3=Constant */
    GtkWidget* scale[4];
    GtkWidget* spin[4];
    GtkWidget* monochrome_check;
    GtkWidget* preserve_luminance_check;
    gfloat mixer[MIXER_SIZE];
    int output_channel; /* 0=Red, 1=Green, 2=Blue (only when !monochrome) */
    gboolean monochrome;
    gboolean preserve_luminance;
    ChannelMixerDialogPreviewCallback preview_callback;
    gpointer preview_user_data;
};

static void on_scale_value_changed(GtkRange* range, gpointer user_data);
static void on_spin_value_changed(GtkWidget* spin, gpointer user_data);

static void mixer_row_to_sliders(ChannelMixerDialog* dialog, int row) {
    if (!dialog->scale[0]) {
        return;
    }
    g_signal_handlers_block_by_func(dialog->scale[0], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->scale[1], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->scale[2], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->scale[3], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->spin[0], G_CALLBACK(on_spin_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->spin[1], G_CALLBACK(on_spin_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->spin[2], G_CALLBACK(on_spin_value_changed), dialog);
    g_signal_handlers_block_by_func(dialog->spin[3], G_CALLBACK(on_spin_value_changed), dialog);

    for (int i = 0; i < 4; i++) {
        gfloat v = dialog->mixer[row * 4 + i];
        gtk_range_set_value(GTK_RANGE(dialog->scale[i]), (gdouble)v);
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->spin[i]), (gdouble)v);
    }

    g_signal_handlers_unblock_by_func(dialog->scale[0], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->scale[1], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->scale[2], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->scale[3], G_CALLBACK(on_scale_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->spin[0], G_CALLBACK(on_spin_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->spin[1], G_CALLBACK(on_spin_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->spin[2], G_CALLBACK(on_spin_value_changed), dialog);
    g_signal_handlers_unblock_by_func(dialog->spin[3], G_CALLBACK(on_spin_value_changed), dialog);
}

static void sliders_to_mixer_row(ChannelMixerDialog* dialog, int row) {
    for (int i = 0; i < 3; i++) {
        gdouble v = gtk_range_get_value(GTK_RANGE(dialog->scale[i]));
        gfloat vf = (gfloat)(v < RGB_MIN ? RGB_MIN : (v > RGB_MAX ? RGB_MAX : v));
        dialog->mixer[row * 4 + i] = vf;
    }
    {
        gdouble v = gtk_range_get_value(GTK_RANGE(dialog->scale[3]));
        gfloat vf = (gfloat)(v < CONSTANT_MIN ? CONSTANT_MIN : (v > CONSTANT_MAX ? CONSTANT_MAX : v));
        dialog->mixer[row * 4 + 3] = vf;
    }
}

static void fire_preview(ChannelMixerDialog* dialog) {
    if (dialog->preview_callback) {
        dialog->preview_callback((void*)dialog, dialog->mixer,
                                 dialog->monochrome, dialog->preserve_luminance,
                                 dialog->preview_user_data);
    }
}

static void on_output_button_clicked(GtkWidget* button, gpointer user_data) {
    ChannelMixerDialog* dialog = (ChannelMixerDialog*)user_data;
    if (!dialog) {
        return;
    }
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
        return;
    }
    if (button == dialog->out_red_btn) {
        dialog->output_channel = OUTPUT_RED;
    } else if (button == dialog->out_green_btn) {
        dialog->output_channel = OUTPUT_GREEN;
    } else if (button == dialog->out_blue_btn) {
        dialog->output_channel = OUTPUT_BLUE;
    } else {
        return;
    }
    g_signal_handlers_block_by_func(dialog->out_red_btn, G_CALLBACK(on_output_button_clicked), dialog);
    g_signal_handlers_block_by_func(dialog->out_green_btn, G_CALLBACK(on_output_button_clicked), dialog);
    g_signal_handlers_block_by_func(dialog->out_blue_btn, G_CALLBACK(on_output_button_clicked), dialog);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->out_red_btn), (dialog->output_channel == OUTPUT_RED));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->out_green_btn), (dialog->output_channel == OUTPUT_GREEN));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->out_blue_btn), (dialog->output_channel == OUTPUT_BLUE));
    g_signal_handlers_unblock_by_func(dialog->out_red_btn, G_CALLBACK(on_output_button_clicked), dialog);
    g_signal_handlers_unblock_by_func(dialog->out_green_btn, G_CALLBACK(on_output_button_clicked), dialog);
    g_signal_handlers_unblock_by_func(dialog->out_blue_btn, G_CALLBACK(on_output_button_clicked), dialog);
    mixer_row_to_sliders(dialog, dialog->output_channel);
    fire_preview(dialog);
}

static void on_scale_value_changed(GtkRange* range, gpointer user_data) {
    ChannelMixerDialog* dialog = (ChannelMixerDialog*)user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "channel_mixer_idx"));
    if (dialog->spin[idx]) {
        vertical_spin_button_set_value(VERTICAL_SPIN_BUTTON(dialog->spin[idx]), gtk_range_get_value(range));
    }
    int row = dialog->monochrome ? OUTPUT_GRAY : dialog->output_channel;
    sliders_to_mixer_row(dialog, row);
    fire_preview(dialog);
}

static void on_spin_value_changed(GtkWidget* spin, gpointer user_data) {
    ChannelMixerDialog* dialog = (ChannelMixerDialog*)user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "channel_mixer_idx"));
    if (dialog->scale[idx]) {
        gtk_range_set_value(GTK_RANGE(dialog->scale[idx]),
                            vertical_spin_button_get_value(VERTICAL_SPIN_BUTTON(spin)));
    }
    int row = dialog->monochrome ? OUTPUT_GRAY : dialog->output_channel;
    sliders_to_mixer_row(dialog, row);
    fire_preview(dialog);
}

static void on_monochrome_toggled(GtkToggleButton* btn, gpointer user_data) {
    ChannelMixerDialog* dialog = (ChannelMixerDialog*)user_data;
    dialog->monochrome = gtk_toggle_button_get_active(btn);
    gtk_widget_set_sensitive(dialog->out_red_btn, !dialog->monochrome);
    gtk_widget_set_sensitive(dialog->out_green_btn, !dialog->monochrome);
    gtk_widget_set_sensitive(dialog->out_blue_btn, !dialog->monochrome);
    if (dialog->monochrome) {
        gtk_widget_set_sensitive(dialog->preserve_luminance_check, FALSE);
        mixer_row_to_sliders(dialog, OUTPUT_GRAY);
    } else {
        gtk_widget_set_sensitive(dialog->preserve_luminance_check, TRUE);
        mixer_row_to_sliders(dialog, dialog->output_channel);
    }
    fire_preview(dialog);
}

static void on_preserve_luminance_toggled(GtkToggleButton* btn, gpointer user_data) {
    ChannelMixerDialog* dialog = (ChannelMixerDialog*)user_data;
    dialog->preserve_luminance = gtk_toggle_button_get_active(btn);
    fire_preview(dialog);
}

static void set_default_mixer(ChannelMixerDialog* dialog) {
    static const gfloat defaults[MIXER_SIZE] = {
        100.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 100.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 100.0f, 0.0f,
        21.0f, 72.0f, 7.0f, 0.0f};
    memcpy(dialog->mixer, defaults, sizeof(dialog->mixer));
}

static GtkWidget* create_output_channel_group(ChannelMixerDialog* dialog) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget* label = gtk_label_new("output channel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* btn;
    GtkWidget* icon;
    GtkWidget* lbl;
    GtkWidget* inner;
    GtkSizeGroup* size_group;

    size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    btn = gtk_toggle_button_new();
    icon = gtk_image_new_from_resource("/icons/channel-red.png");
    lbl = gtk_label_new("red");
    gtk_widget_set_margin_start(lbl, 5);
    inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(inner), icon);
    gtk_container_add(GTK_CONTAINER(inner), lbl);
    gtk_container_add(GTK_CONTAINER(btn), inner);
    gtk_widget_set_halign(inner, GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(size_group, btn);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_output_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), btn, TRUE, TRUE, 0);
    dialog->out_red_btn = btn;

    btn = gtk_toggle_button_new();
    icon = gtk_image_new_from_resource("/icons/channel-green.png");
    lbl = gtk_label_new("green");
    gtk_widget_set_margin_start(lbl, 5);
    inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(inner), icon);
    gtk_container_add(GTK_CONTAINER(inner), lbl);
    gtk_container_add(GTK_CONTAINER(btn), inner);
    gtk_widget_set_halign(inner, GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(size_group, btn);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_output_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), btn, TRUE, TRUE, 0);
    dialog->out_green_btn = btn;

    btn = gtk_toggle_button_new();
    icon = gtk_image_new_from_resource("/icons/channel-blue.png");
    lbl = gtk_label_new("blue");
    gtk_widget_set_margin_start(lbl, 5);
    inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(inner), icon);
    gtk_container_add(GTK_CONTAINER(inner), lbl);
    gtk_container_add(GTK_CONTAINER(btn), inner);
    gtk_widget_set_halign(inner, GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(size_group, btn);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_output_button_clicked), dialog);
    gtk_box_pack_start(GTK_BOX(hbox), btn, TRUE, TRUE, 0);
    dialog->out_blue_btn = btn;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);

    g_object_unref(size_group);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    return vbox;
}

static GtkWidget* create_input_slider_row(ChannelMixerDialog* dialog, int idx,
                                          const char* label_text,
                                          gboolean is_constant) {
    GtkWidget* row_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget* slider_hbox;
    GtkWidget* scale;
    GtkWidget* spin;
    GtkAdjustment* adj;
    gdouble min_val = is_constant ? (gdouble)CONSTANT_MIN : (gdouble)RGB_MIN;
    gdouble max_val = is_constant ? (gdouble)CONSTANT_MAX : (gdouble)RGB_MAX;

    GtkWidget* lbl = gtk_label_new(label_text);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(lbl, 2);
    gtk_box_pack_start(GTK_BOX(row_vbox), lbl, FALSE, FALSE, 0);

    slider_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_hexpand(slider_hbox, TRUE);
    gtk_widget_set_halign(slider_hbox, GTK_ALIGN_FILL);
    adj = gtk_adjustment_new(0.0, min_val, max_val, 1.0, 10.0, 0.0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_halign(scale, GTK_ALIGN_FILL);
    g_object_set_data(G_OBJECT(scale), "channel_mixer_idx", GINT_TO_POINTER(idx));
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_value_changed), dialog);
    gtk_box_pack_start(GTK_BOX(slider_hbox), scale, TRUE, TRUE, 0);
    dialog->scale[idx] = scale;

    spin = vertical_spin_button_new(adj, 1.0, 0);
    gtk_widget_set_size_request(spin, 55, -1);
    gtk_widget_set_hexpand(spin, FALSE);
    gtk_widget_set_halign(spin, GTK_ALIGN_END);
    g_object_set_data(G_OBJECT(spin), "channel_mixer_idx", GINT_TO_POINTER(idx));
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_spin_value_changed), dialog);
    gtk_box_pack_end(GTK_BOX(slider_hbox), spin, FALSE, FALSE, 0);
    dialog->spin[idx] = spin;

    gtk_box_pack_start(GTK_BOX(row_vbox), slider_hbox, TRUE, TRUE, 0);
    return row_vbox;
}

static GtkWidget* create_input_channels_section(ChannelMixerDialog* dialog) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* label = gtk_label_new("input channel(s)");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox),
                       create_input_slider_row(dialog, 0, "red", FALSE),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       create_input_slider_row(dialog, 1, "green", FALSE),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       create_input_slider_row(dialog, 2, "blue", FALSE),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       create_input_slider_row(dialog, 3, "constant", TRUE),
                       FALSE, FALSE, 0);

    return vbox;
}

static void on_reset_all_clicked(GtkWidget* widget, gpointer user_data) {
    ChannelMixerDialog* dialog = (ChannelMixerDialog*)user_data;
    (void)widget;
    channel_mixer_dialog_reset(dialog);
}

ChannelMixerDialog* channel_mixer_dialog_new(const gchar* title) {
    ChannelMixerDialog* dialog;
    GtkWidget* content_area;
    GtkWidget* main_hbox;
    GtkWidget* right_vbox;
    GtkWidget* options_vbox;
    GtkWidget* options_label;
    GtkWidget* button_box;
    GtkWidget* reset_btn;

    if (!title) {
        return NULL;
    }

    dialog = (ChannelMixerDialog*)g_malloc(sizeof(ChannelMixerDialog));
    if (!dialog) {
        return NULL;
    }

    memset(dialog, 0, sizeof(*dialog));
    set_default_mixer(dialog);
    dialog->output_channel = OUTPUT_RED;
    dialog->monochrome = FALSE;
    dialog->preserve_luminance = TRUE;

    dialog->dialog = gtk_dialog_new_with_buttons(title,
                                                 NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK", GTK_RESPONSE_OK,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 NULL);

    ui_utils_set_header_bar(GTK_WINDOW(dialog->dialog), title);
    gtk_window_set_resizable(GTK_WINDOW(dialog->dialog), TRUE);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);

    main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), main_hbox);

    dialog->preview = FILTER_PREVIEW(filter_preview_new());
    gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), 375, 338);
    gtk_box_pack_start(GTK_BOX(main_hbox), GTK_WIDGET(dialog->preview), FALSE, FALSE, 0);

    right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right_vbox, 320, -1);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, FALSE, FALSE, 0);
    gtk_widget_set_margin_start(right_vbox, 10);
    gtk_widget_set_margin_end(right_vbox, 10);
    gtk_widget_set_margin_top(right_vbox, 10);
    gtk_widget_set_margin_bottom(right_vbox, 10);

    gtk_box_pack_start(GTK_BOX(right_vbox), create_output_channel_group(dialog), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_vbox), create_input_channels_section(dialog), FALSE, FALSE, 0);

    options_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    options_label = gtk_label_new("options for all channels");
    gtk_widget_set_halign(options_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(options_vbox), options_label, FALSE, FALSE, 0);

    dialog->monochrome_check = gtk_check_button_new_with_label("monochrome");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->monochrome_check), FALSE);
    g_signal_connect(dialog->monochrome_check, "toggled", G_CALLBACK(on_monochrome_toggled), dialog);
    gtk_box_pack_start(GTK_BOX(options_vbox), dialog->monochrome_check, FALSE, FALSE, 0);

    dialog->preserve_luminance_check = gtk_check_button_new_with_label("preserve luminance");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->preserve_luminance_check), TRUE);
    g_signal_connect(dialog->preserve_luminance_check, "toggled", G_CALLBACK(on_preserve_luminance_toggled), dialog);
    gtk_box_pack_start(GTK_BOX(options_vbox), dialog->preserve_luminance_check, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(right_vbox), options_vbox, FALSE, FALSE, 0);

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
        reset_btn = gtk_button_new();
        GtkWidget* reset_icon = gtk_image_new_from_resource("/icons/reset.png");
        if (reset_icon) {
            gtk_button_set_image(GTK_BUTTON(reset_btn), reset_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(reset_btn), TRUE);
        }
        gtk_widget_set_halign(reset_btn, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(button_box), reset_btn, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(button_box), reset_btn, 0);
        g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_all_clicked), dialog);
    }

    gtk_widget_show_all(content_area);

    mixer_row_to_sliders(dialog, dialog->output_channel);
    return dialog;
}

void channel_mixer_dialog_free(ChannelMixerDialog* dialog) {
    if (!dialog) {
        return;
    }
    if (dialog->dialog) {
        gtk_widget_destroy(dialog->dialog);
    }
    g_free(dialog);
}

GtkWindow* channel_mixer_dialog_get_window(ChannelMixerDialog* dialog) {
    return dialog && dialog->dialog ? GTK_WINDOW(dialog->dialog) : NULL;
}

void channel_mixer_dialog_set_layers(ChannelMixerDialog* dialog, ImageLayer* original, ImageLayer* temp) {
    cairo_surface_t* before_surface = NULL;
    cairo_surface_t* after_surface = NULL;
    if (!dialog || !dialog->preview) {
        return;
    }
    if (original && original->surface) {
        before_surface = cairo_surface_reference(original->surface);
    }
    if (temp && temp->surface) {
        after_surface = cairo_surface_reference(temp->surface);
    }
    filter_preview_set_before_surface(dialog->preview, before_surface);
    filter_preview_set_after_surface(dialog->preview, after_surface);
    if (before_surface) {
        cairo_surface_destroy(before_surface);
    }
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

void channel_mixer_dialog_update_after_layer(ChannelMixerDialog* dialog, ImageLayer* layer) {
    cairo_surface_t* after_surface = NULL;
    if (!dialog || !dialog->preview) {
        return;
    }
    if (layer && layer->surface) {
        after_surface = cairo_surface_reference(layer->surface);
    }
    filter_preview_set_after_surface(dialog->preview, after_surface);
    if (after_surface) {
        cairo_surface_destroy(after_surface);
    }
}

void channel_mixer_dialog_set_preview_callback(ChannelMixerDialog* dialog,
                                               ChannelMixerDialogPreviewCallback callback,
                                               gpointer user_data) {
    if (!dialog) {
        return;
    }
    dialog->preview_callback = callback;
    dialog->preview_user_data = user_data;
}

void channel_mixer_dialog_reset(ChannelMixerDialog* dialog) {
    if (!dialog) {
        return;
    }
    set_default_mixer(dialog);
    dialog->output_channel = OUTPUT_BLUE;
    dialog->monochrome = FALSE;
    dialog->preserve_luminance = TRUE;

    if (dialog->out_red_btn) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->out_blue_btn), TRUE);
        gtk_widget_set_sensitive(dialog->out_red_btn, TRUE);
        gtk_widget_set_sensitive(dialog->out_green_btn, TRUE);
        gtk_widget_set_sensitive(dialog->out_blue_btn, TRUE);
    }
    if (dialog->monochrome_check) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->monochrome_check), FALSE);
    }
    if (dialog->preserve_luminance_check) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->preserve_luminance_check), TRUE);
    }

    mixer_row_to_sliders(dialog, dialog->output_channel);
    fire_preview(dialog);
}

gint channel_mixer_dialog_run(ChannelMixerDialog* dialog,
                              GtkWindow* parent,
                              gfloat* out_mixer,
                              gboolean* out_monochrome,
                              gboolean* out_preserve_luminance) {
    gint response;
    if (!dialog || !dialog->dialog) {
        return GTK_RESPONSE_CANCEL;
    }
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent);
    }
    response = gtk_dialog_run(GTK_DIALOG(dialog->dialog));
    if (response == GTK_RESPONSE_OK) {
        int row = dialog->monochrome ? OUTPUT_GRAY : dialog->output_channel;
        sliders_to_mixer_row(dialog, row);
        if (out_mixer) {
            memcpy(out_mixer, dialog->mixer, MIXER_SIZE * sizeof(gfloat));
        }
        if (out_monochrome) {
            *out_monochrome = dialog->monochrome;
        }
        if (out_preserve_luminance) {
            *out_preserve_luminance = dialog->preserve_luminance;
        }
    }
    return response;
}
