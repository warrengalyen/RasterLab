#include "ui/ui_utils.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include <stdarg.h>

void update_color_button_appearance(GtkWidget* button, GdkRGBA* color) {
    if (!button || !color)
        return;

    // Create CSS to set button background color
    // Need to override all button states and remove default styling
    char css_str[512];
    g_snprintf(css_str, sizeof(css_str),
               "button { "
               "background-color: rgb(%d, %d, %d); "
               "background-image: none; "
               "border: 1px solid #000; "
               "border-radius: 0; "
               "box-shadow: none; "
               "padding: 0; "
               "margin: 0; "
               "min-width: 20px; "
               "min-height: 20px; "
               "outline: none; "
               "} "
               "button:hover { "
               "background-color: rgb(%d, %d, %d); "
               "background-image: none; "
               "box-shadow: none; "
               "} "
               "button:active, button:checked { "
               "background-color: rgb(%d, %d, %d); "
               "background-image: none; "
               "box-shadow: none; "
               "}",
               (int)(color->red * 255),
               (int)(color->green * 255),
               (int)(color->blue * 255),
               (int)(color->red * 255),
               (int)(color->green * 255),
               (int)(color->blue * 255),
               (int)(color->red * 255),
               (int)(color->green * 255),
               (int)(color->blue * 255));

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, css_str, -1, NULL);
    GtkStyleContext* context = gtk_widget_get_style_context(button);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

/**
 * Color update callback for real-time color updates from dialog
 */
static void on_color_update_internal(double r, double g, double b, gpointer user_data) {
    ColorButtonData* data = (ColorButtonData*)user_data;
    if (!data)
        return;

    data->color.red = r;
    data->color.green = g;
    data->color.blue = b;
    data->color.alpha = 1.0;

    update_color_button_appearance(data->button, &data->color);

    // Call user callback if set
    if (data->callback) {
        data->callback(data->button, data->callback_data);
    }
}

/**
 * Button clicked callback - opens color chooser dialog
 */
static void on_color_button_clicked(GtkButton* button, gpointer user_data) {
    ColorButtonData* data = (ColorButtonData*)user_data;
    if (!data || !data->parent_window)
        return;

    // Create and show color chooser dialog without real-time updates
    // This prevents excessive filter preview updates
    GtkWidget* dialog = color_chooser_dialog_new(
        data->parent_window,
        "Choose Color",
        &data->color,
        on_color_update_internal,
        data,
        FALSE); // Disable real-time updates for filter dialogs

    // Run dialog
    gtk_dialog_run(GTK_DIALOG(dialog));

    // Get final color
    double r, g, b;
    color_chooser_dialog_get_color(dialog, &r, &g, &b);

    data->color.red = r;
    data->color.green = g;
    data->color.blue = b;
    data->color.alpha = 1.0;

    update_color_button_appearance(data->button, &data->color);

    // Call user callback if set
    if (data->callback) {
        data->callback(data->button, data->callback_data);
    }

    gtk_widget_destroy(dialog);
}

GtkWidget* create_custom_color_button(GtkWindow* parent_window,
                                      GdkRGBA* initial_color,
                                      void (*callback)(GtkWidget* button, gpointer user_data),
                                      gpointer user_data) {
    GtkWidget* button = gtk_button_new();

    // Create data structure
    ColorButtonData* data = g_new0(ColorButtonData, 1);
    data->button = button;
    data->color = initial_color ? *initial_color : (GdkRGBA){0.0, 0.0, 0.0, 1.0};
    data->parent_window = parent_window;
    data->callback = callback;
    data->callback_data = user_data;

    // Store data in button
    g_object_set_data_full(G_OBJECT(button), "color_button_data", data, g_free);

    // Set initial appearance
    update_color_button_appearance(button, &data->color);

    // Connect clicked signal
    g_signal_connect(button, "clicked", G_CALLBACK(on_color_button_clicked), data);

    return button;
}

gboolean get_custom_color_button_color(GtkWidget* button, GdkRGBA* color) {
    if (!button || !color)
        return FALSE;

    ColorButtonData* data = (ColorButtonData*)g_object_get_data(G_OBJECT(button), "color_button_data");
    if (!data)
        return FALSE;

    *color = data->color;
    return TRUE;
}

void set_custom_color_button_color(GtkWidget* button, GdkRGBA* color) {
    if (!button || !color)
        return;

    ColorButtonData* data = (ColorButtonData*)g_object_get_data(G_OBJECT(button), "color_button_data");
    if (!data)
        return;

    data->color = *color;
    update_color_button_appearance(button, &data->color);
}

/**
 * Create and run a message dialog with custom buttons and spacing
 */
gint ui_utils_message_dialog_run(GtkWindow* parent,
                                 GtkMessageType message_type,
                                 const gchar* primary_text,
                                 const gchar* secondary_text,
                                 gint default_response,
                                 ...) {
    GtkWidget* dialog;
    GtkWidget* button_box;
    gint response;
    va_list args;

    dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL,
        message_type,
        GTK_BUTTONS_NONE,
        "%s",
        primary_text ? primary_text : "");

    if (secondary_text && secondary_text[0] != '\0') {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "%s", secondary_text);
    }

    va_start(args, default_response);
    while (1) {
        const gchar* button_text = va_arg(args, const gchar*);
        if (!button_text)
            break;
        gtk_dialog_add_button(GTK_DIALOG(dialog), button_text,
                              va_arg(args, gint));
    }
    va_end(args);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    button_box = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    if (button_box) {
        gtk_widget_set_margin_top(button_box, 5);
        gtk_widget_set_margin_bottom(button_box, 5);
        gtk_widget_set_margin_start(button_box, 5);
        gtk_widget_set_margin_end(button_box, 5);
    }

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), default_response);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return response;
}

/**
 * Replace default titlebar with header bar for a dialog window
 */
void ui_utils_set_header_bar(GtkWindow* window, const gchar* title) {
    if (!window || !GTK_IS_WINDOW(window)) {
        return;
    }

    GtkWidget* hb = gtk_header_bar_new();
    if (!hb) {
        return;
    }

    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), title ? title : "");

    /* must be done before window is realized */
    gtk_window_set_titlebar(window, hb);

    gtk_widget_show(hb);
}
