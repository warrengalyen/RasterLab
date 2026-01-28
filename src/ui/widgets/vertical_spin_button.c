#include "ui/widgets/vertical_spin_button.h"
#include <glib-object.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Vertical spin button structure
 */
struct _VerticalSpinButton {
    GtkBox parent;
    GtkEntry* entry;
    GtkButton* up_button;
    GtkButton* down_button;
    GtkAdjustment* adjustment;
    gdouble climb_rate;
    guint digits;
    guint timeout_id;
    gboolean is_spinning;
};

/**
 * Vertical spin button class structure
 */
struct _VerticalSpinButtonClass {
    GtkBoxClass parent_class;
};

G_DEFINE_TYPE(VerticalSpinButton, vertical_spin_button, GTK_TYPE_BOX)

/**
 * Update entry text from current value
 */
static void update_entry_text(VerticalSpinButton* spin) {
    gdouble value;
    gchar* text;

    if (!spin || !spin->adjustment || !spin->entry) {
        return;
    }

    value = gtk_adjustment_get_value(spin->adjustment);

    if (spin->digits > 0) {
        text = g_strdup_printf("%.*f", spin->digits, value);
    } else {
        text = g_strdup_printf("%.0f", value);
    }

    gtk_entry_set_text(spin->entry, text);
    g_free(text);
}

/**
 * Parse entry text and update adjustment
 */
static gboolean parse_and_update(VerticalSpinButton* spin) {
    const gchar* text;
    gdouble value;
    gchar* endptr;

    if (!spin || !spin->entry || !spin->adjustment) {
        return FALSE;
    }

    text = gtk_entry_get_text(spin->entry);
    value = g_strtod(text, &endptr);

    if (endptr == text || *endptr != '\0') {
        /* Invalid input, restore previous value */
        update_entry_text(spin);
        return FALSE;
    }

    /* Clamp to adjustment range */
    gdouble min = gtk_adjustment_get_lower(spin->adjustment);
    gdouble max = gtk_adjustment_get_upper(spin->adjustment);
    if (value < min) {
        value = min;
    } else if (value > max) {
        value = max;
    }

    gtk_adjustment_set_value(spin->adjustment, value);
    return TRUE;
}

/**
 * Spin button timeout callback (for continuous spinning)
 */
static gboolean spin_timeout(gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;
    gdouble value;
    gdouble step;

    if (!spin || !spin->is_spinning || !spin->adjustment) {
        spin->timeout_id = 0;
        return G_SOURCE_REMOVE;
    }

    value = gtk_adjustment_get_value(spin->adjustment);
    step = gtk_adjustment_get_step_increment(spin->adjustment);

    if (spin->up_button && gtk_widget_has_focus(GTK_WIDGET(spin->up_button))) {
        value += step;
    } else if (spin->down_button && gtk_widget_has_focus(GTK_WIDGET(spin->down_button))) {
        value -= step;
    } else {
        spin->timeout_id = 0;
        spin->is_spinning = FALSE;
        return G_SOURCE_REMOVE;
    }

    gtk_adjustment_set_value(spin->adjustment, value);
    return G_SOURCE_CONTINUE;
}

/**
 * Up button clicked callback
 */
static void on_up_clicked(GtkButton* button, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;
    gdouble value;
    gdouble step;

    if (!spin || !spin->adjustment) {
        return;
    }

    value = gtk_adjustment_get_value(spin->adjustment);
    step = gtk_adjustment_get_step_increment(spin->adjustment);
    gtk_adjustment_set_value(spin->adjustment, value + step);
}

/**
 * Down button clicked callback
 */
static void on_down_clicked(GtkButton* button, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;
    gdouble value;
    gdouble step;

    if (!spin || !spin->adjustment) {
        return;
    }

    value = gtk_adjustment_get_value(spin->adjustment);
    step = gtk_adjustment_get_step_increment(spin->adjustment);
    gtk_adjustment_set_value(spin->adjustment, value - step);
}

/**
 * Up button pressed callback (for continuous spinning)
 */
static gboolean on_up_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;

    if (event->button == 1 && !spin->is_spinning) {
        spin->is_spinning = TRUE;
        on_up_clicked(GTK_BUTTON(widget), user_data);
        spin->timeout_id = g_timeout_add(150, spin_timeout, spin);
        return TRUE;
    }
    return FALSE;
}

/**
 * Up button released callback
 */
static gboolean on_up_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;

    if (event->button == 1) {
        spin->is_spinning = FALSE;
        if (spin->timeout_id) {
            g_source_remove(spin->timeout_id);
            spin->timeout_id = 0;
        }
        return TRUE;
    }
    return FALSE;
}

/**
 * Down button pressed callback (for continuous spinning)
 */
static gboolean on_down_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;

    if (event->button == 1 && !spin->is_spinning) {
        spin->is_spinning = TRUE;
        on_down_clicked(GTK_BUTTON(widget), user_data);
        spin->timeout_id = g_timeout_add(150, spin_timeout, spin);
        return TRUE;
    }
    return FALSE;
}

/**
 * Down button released callback
 */
static gboolean on_down_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;

    if (event->button == 1) {
        spin->is_spinning = FALSE;
        if (spin->timeout_id) {
            g_source_remove(spin->timeout_id);
            spin->timeout_id = 0;
        }
        return TRUE;
    }
    return FALSE;
}

/**
 * Entry activate callback (Enter key pressed)
 */
static void on_entry_activate(GtkEntry* entry, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;
    parse_and_update(spin);
}

/**
 * Entry focus out callback
 */
static void on_entry_focus_out(GtkWidget* widget, GdkEventFocus* event, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;
    (void)widget;
    (void)event;
    parse_and_update(spin);
}

/**
 * Adjustment value changed callback
 */
static void on_adjustment_value_changed(GtkAdjustment* adjustment, gpointer user_data) {
    VerticalSpinButton* spin = (VerticalSpinButton*)user_data;
    (void)adjustment;
    update_entry_text(spin);
    g_signal_emit_by_name(spin, "value-changed");
}

/**
 * Initialize vertical spin button instance
 */
static void vertical_spin_button_init(VerticalSpinButton* spin) {
    GtkWidget* entry;
    GtkWidget* button_box;
    GtkWidget* up_button;
    GtkWidget* down_button;
    GtkWidget* up_arrow;
    GtkWidget* down_arrow;

    /* Initialize structure */
    spin->entry = NULL;
    spin->up_button = NULL;
    spin->down_button = NULL;
    spin->adjustment = NULL;
    spin->climb_rate = 0.0;
    spin->digits = 0;
    spin->timeout_id = 0;
    spin->is_spinning = FALSE;

    /* Set box orientation to horizontal */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(spin), GTK_ORIENTATION_HORIZONTAL);

    /* Create entry */
    entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_widget_set_vexpand(entry, FALSE);
    gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(spin), entry, TRUE, TRUE, 0);
    spin->entry = GTK_ENTRY(entry);

    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), spin);
    g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_entry_focus_out), spin);

    /* Create vertical button box */
    button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(button_box, FALSE);
    gtk_widget_set_valign(button_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(spin), button_box, FALSE, FALSE, 0);

    /* Create up button with arrow */
    up_button = gtk_button_new();

    /* Use small text label for arrow - more reliable sizing */
    up_arrow = gtk_label_new("▲");
    gtk_widget_set_name(up_arrow, "spin-up-arrow");
    {
        GtkCssProvider* font_css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(font_css, "* { font-size: 8pt; }", -1, NULL);
        GtkStyleContext* ctx = gtk_widget_get_style_context(up_arrow);
        gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(font_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(font_css);
    }

    gtk_button_set_image(GTK_BUTTON(up_button), up_arrow);
    gtk_button_set_relief(GTK_BUTTON(up_button), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(up_button, 20, 12);
    gtk_widget_set_vexpand(up_button, FALSE);
    gtk_widget_set_valign(up_button, GTK_ALIGN_START);

    /* Remove button padding using CSS */
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "button { padding: 0px; margin: 0px; min-height: 0px; }", -1, NULL);
    GtkStyleContext* context = gtk_widget_get_style_context(up_button);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_widget_set_margin_start(up_button, 0);
    gtk_widget_set_margin_end(up_button, 0);
    gtk_widget_set_margin_top(up_button, 0);
    gtk_widget_set_margin_bottom(up_button, 0);

    gtk_box_pack_start(GTK_BOX(button_box), up_button, FALSE, FALSE, 0);
    spin->up_button = GTK_BUTTON(up_button);

    g_signal_connect(up_button, "clicked", G_CALLBACK(on_up_clicked), spin);
    g_signal_connect(up_button, "button-press-event", G_CALLBACK(on_up_button_press), spin);
    g_signal_connect(up_button, "button-release-event", G_CALLBACK(on_up_button_release), spin);

    /* Create down button with arrow */
    down_button = gtk_button_new();

    /* Use small text label for arrow - more reliable sizing */
    down_arrow = gtk_label_new("▼");
    gtk_widget_set_name(down_arrow, "spin-down-arrow");
    {
        GtkCssProvider* font_css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(font_css, "* { font-size: 8pt; }", -1, NULL);
        GtkStyleContext* ctx = gtk_widget_get_style_context(down_arrow);
        gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(font_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(font_css);
    }

    gtk_button_set_image(GTK_BUTTON(down_button), down_arrow);
    gtk_button_set_relief(GTK_BUTTON(down_button), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(down_button, 20, 12);
    gtk_widget_set_vexpand(down_button, FALSE);
    gtk_widget_set_valign(down_button, GTK_ALIGN_END);

    /* Remove button padding using CSS */
    css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "button { padding: 0px; margin: 0px; min-height: 0px; }", -1, NULL);
    context = gtk_widget_get_style_context(down_button);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_widget_set_margin_start(down_button, 0);
    gtk_widget_set_margin_end(down_button, 0);
    gtk_widget_set_margin_top(down_button, 0);
    gtk_widget_set_margin_bottom(down_button, 0);

    gtk_box_pack_start(GTK_BOX(button_box), down_button, FALSE, FALSE, 0);
    spin->down_button = GTK_BUTTON(down_button);

    g_signal_connect(down_button, "clicked", G_CALLBACK(on_down_clicked), spin);
    g_signal_connect(down_button, "button-press-event", G_CALLBACK(on_down_button_press), spin);
    g_signal_connect(down_button, "button-release-event", G_CALLBACK(on_down_button_release), spin);
}

/**
 * Finalize vertical spin button
 */
static void vertical_spin_button_finalize(GObject* object) {
    VerticalSpinButton* spin = (VerticalSpinButton*)object;

    if (spin->timeout_id) {
        g_source_remove(spin->timeout_id);
        spin->timeout_id = 0;
    }

    if (spin->adjustment) {
        g_signal_handlers_disconnect_by_func(spin->adjustment, G_CALLBACK(on_adjustment_value_changed), spin);
        g_object_unref(spin->adjustment);
    }

    G_OBJECT_CLASS(vertical_spin_button_parent_class)->finalize(object);
}

/**
 * Vertical spin button class initialization
 */
static void vertical_spin_button_class_init(VerticalSpinButtonClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = vertical_spin_button_finalize;

    /* Register "value-changed" signal */
    g_signal_new("value-changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

/**
 * Create a new vertical spin button
 */
GtkWidget* vertical_spin_button_new(GtkAdjustment* adjustment, gdouble climb_rate, guint digits) {
    VerticalSpinButton* spin;
    GtkAdjustment* adj;

    spin = g_object_new(vertical_spin_button_get_type(), NULL);

    if (adjustment) {
        adj = adjustment;
        g_object_ref(adj);
    } else {
        adj = gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 1.0, 0.0);
    }

    spin->adjustment = adj;
    spin->climb_rate = climb_rate;
    spin->digits = digits;

    g_signal_connect(adj, "value-changed", G_CALLBACK(on_adjustment_value_changed), spin);

    update_entry_text(spin);

    return GTK_WIDGET(spin);
}

/**
 * Get the value from the spin button
 */
gdouble vertical_spin_button_get_value(VerticalSpinButton* spin) {
    if (!spin || !spin->adjustment) {
        return 0.0;
    }
    return gtk_adjustment_get_value(spin->adjustment);
}

/**
 * Set the value of the spin button
 */
void vertical_spin_button_set_value(VerticalSpinButton* spin, gdouble value) {
    if (!spin || !spin->adjustment) {
        return;
    }
    gtk_adjustment_set_value(spin->adjustment, value);
}

/**
 * Get the adjustment used by the spin button
 */
GtkAdjustment* vertical_spin_button_get_adjustment(VerticalSpinButton* spin) {
    if (!spin) {
        return NULL;
    }
    return spin->adjustment;
}

/**
 * Set the adjustment for the spin button
 */
void vertical_spin_button_set_adjustment(VerticalSpinButton* spin, GtkAdjustment* adjustment) {
    if (!spin) {
        return;
    }

    if (spin->adjustment) {
        g_signal_handlers_disconnect_by_func(spin->adjustment, G_CALLBACK(on_adjustment_value_changed), spin);
        g_object_unref(spin->adjustment);
    }

    if (adjustment) {
        spin->adjustment = g_object_ref(adjustment);
        g_signal_connect(adjustment, "value-changed", G_CALLBACK(on_adjustment_value_changed), spin);
        update_entry_text(spin);
    } else {
        spin->adjustment = NULL;
    }
}

/**
 * Get the entry widget
 */
GtkWidget* vertical_spin_button_get_entry(VerticalSpinButton* spin) {
    if (!spin) {
        return NULL;
    }
    return GTK_WIDGET(spin->entry);
}
