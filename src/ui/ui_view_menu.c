#include "ui/ui_view_menu.h"
#include "app/settings.h"
#include "document.h"
#include "tool_manager.h"
#include "tools/tool_text.h"
#include "ui.h"
#include <gtk/gtk.h>

/**
 * View > Zoom In callback
 */
void on_view_zoom_in(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_in(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Zoom Out callback
 */
void on_view_zoom_out(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_out(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Reset Zoom callback
 */
void on_view_zoom_reset(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_reset(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Zoom Fit callback
 */
void on_view_zoom_fit(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_fit(doc);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Show Layer Edges callback
 */
void on_view_show_layer_edges(GtkCheckMenuItem* check_menu_item, gpointer data) {
    AppContext* ctx = (AppContext*)data;

    if (!ctx || !ctx->settings) {
        return;
    }

    /* Get the new state from the menu item */
    gboolean active = gtk_check_menu_item_get_active(check_menu_item);

    /* Update the setting */
    settings_set_show_layer_edges(ctx->settings, active);

    /* Trigger redraw of all viewports to update outlines */
    if (ctx->documents) {
        for (GList* iter = ctx->documents; iter; iter = iter->next) {
            ImageDocument* doc = (ImageDocument*)iter->data;
            if (doc && doc->viewport) {
                gtk_widget_queue_draw(doc->viewport);
            }
        }
    }
}

/**
 * View > Show Statusbar callback
 */
void on_view_show_statusbar(GtkCheckMenuItem* check_menu_item, gpointer data) {
    AppContext* ctx = (AppContext*)data;

    if (!ctx || !ctx->settings) {
        return;
    }

    /* Get the new state from the menu item */
    gboolean active = gtk_check_menu_item_get_active(check_menu_item);

    /* Update the setting */
    settings_set_show_statusbar(ctx->settings, active);

    /* Show/hide status bar (it's part of the main window) */
    if (ctx->status_bar) {
        if (active) {
            gtk_widget_show(ctx->status_bar);
        } else {
            gtk_widget_hide(ctx->status_bar);
        }
    }
}

/**
 * View > Show Rulers callback
 */
void on_view_show_rulers(GtkCheckMenuItem* check_menu_item, gpointer data) {
    AppContext* ctx = (AppContext*)data;

    if (!ctx || !ctx->settings) {
        return;
    }

    gboolean active = gtk_check_menu_item_get_active(check_menu_item);
    settings_set_show_rulers(ctx->settings, active);

    /* Apply to all documents */
    if (ctx->documents) {
        for (GList* iter = ctx->documents; iter; iter = iter->next) {
            ImageDocument* doc = (ImageDocument*)iter->data;
            if (doc && doc->ruler_h && doc->ruler_v) {
                if (active) {
                    gtk_widget_show(doc->ruler_h);
                    gtk_widget_show(doc->ruler_v);
                } else {
                    gtk_widget_hide(doc->ruler_h);
                    gtk_widget_hide(doc->ruler_v);
                }
            }
        }
    }
}

/**
 * Helper function for zoom to specific level callbacks
 */
static void zoom_to_level(AppContext* ctx, gdouble zoom_percent) {
    ImageDocument* doc = ui_get_active_document(ctx);
    if (doc) {
        document_zoom_to(doc, zoom_percent);
        ui_update_status_bar(ctx, NULL);
    }
}

/**
 * View > Zoom to 1600% callback
 */
void on_view_zoom_1600(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 1600.0);
}

/**
 * View > Zoom to 800% callback
 */
void on_view_zoom_800(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 800.0);
}

/**
 * View > Zoom to 400% callback
 */
void on_view_zoom_400(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 400.0);
}

/**
 * View > Zoom to 200% callback
 */
void on_view_zoom_200(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 200.0);
}

/**
 * View > Zoom to 100% callback
 */
void on_view_zoom_100(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 100.0);
}

/**
 * View > Zoom to 50% callback
 */
void on_view_zoom_50(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 50.0);
}

/**
 * View > Zoom to 25% callback
 */
void on_view_zoom_25(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 25.0);
}

/**
 * View > Zoom to 12.5% callback
 */
void on_view_zoom_12_5(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 12.5);
}

/**
 * View > Zoom to 6.25% callback
 */
void on_view_zoom_6_25(GtkWidget* widget, gpointer data) {
    (void)widget;
    zoom_to_level((AppContext*)data, 6.25);
}

/**
 * Window key press handler for zoom accelerators
 * This is connected to catch number keys before the drawing area does
 */
static gboolean on_window_key_press_for_zoom(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    (void)widget;
    AppContext* ctx = (AppContext*)user_data;

    /* Don't steal number keys (1-5) when the text tool is in edit mode */
    if (ctx && ctx->tool_registry) {
        Tool* at = tool_manager_get_active(ctx->tool_registry);
        if (tool_text_is_editing(at))
            return FALSE;
    }

    /* Get the modifier state, ignoring lock keys (Caps Lock, Num Lock) */
    GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

    /* Check for Shift+Numpad zoom accelerators (for smaller zoom levels) */
    if (modifiers == GDK_SHIFT_MASK) {
        switch (event->keyval) {
            case GDK_KEY_KP_2:
                on_view_zoom_50(NULL, ctx);
                return TRUE; /* Stop event propagation */
            case GDK_KEY_KP_3:
                on_view_zoom_25(NULL, ctx);
                return TRUE;
            case GDK_KEY_KP_4:
                on_view_zoom_12_5(NULL, ctx);
                return TRUE;
            case GDK_KEY_KP_5:
            case GDK_KEY_KP_Begin:
                on_view_zoom_6_25(NULL, ctx);
                return TRUE;
        }
    }

    /* Only handle bare number keys (no modifiers at all) */
    if (modifiers != 0) {
        return FALSE; /* Let other handlers process modified keys */
    }

    /* Check for zoom accelerators (bare keys only) */
    switch (event->keyval) {
        /* Main keyboard: 1-5 for larger zoom levels */
        case GDK_KEY_1:
            on_view_zoom_100(NULL, ctx);
            return TRUE;
        case GDK_KEY_2:
            on_view_zoom_200(NULL, ctx);
            return TRUE;
        case GDK_KEY_3:
            on_view_zoom_400(NULL, ctx);
            return TRUE;
        case GDK_KEY_4:
            on_view_zoom_800(NULL, ctx);
            return TRUE;
        case GDK_KEY_5:
            on_view_zoom_1600(NULL, ctx);
            return TRUE;
    }

    return FALSE; /* Let other handlers process this key */
}

/**
 * Setup View menu from Glade builder
 */
void ui_view_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    GtkWidget* view_menu = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu"));
    GtkWidget* view_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_item"));

    if (view_menu && view_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_menu_item), view_menu);
    }

    /* Connect window key press handler for zoom shortcuts with normal priority
     * This should intercept keys before GTK's default mnemonic handling */
    if (ctx->window) {
        g_signal_connect(ctx->window, "key-press-event",
                         G_CALLBACK(on_window_key_press_for_zoom), ctx);
    }

    /* Connect View menu signals */
    GtkWidget* view_menu_zoom_in = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_in"));
    GtkWidget* view_menu_zoom_out = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_out"));
    GtkWidget* view_menu_zoom_reset = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_reset"));
    GtkWidget* view_menu_zoom_fit = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_fit"));
    GtkWidget* view_menu_show_layer_edges = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_show_layer_edges"));
    GtkWidget* view_menu_show_statusbar = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_show_statusbar"));
    GtkWidget* view_menu_show_rulers = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_show_rulers"));

    if (view_menu_zoom_in) {
        g_signal_connect(view_menu_zoom_in, "activate", G_CALLBACK(on_view_zoom_in), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_in, "activate", accel_group,
                                       GDK_KEY_plus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
            /* Also add equals key (since + requires Shift on most keyboards) */
            gtk_widget_add_accelerator(view_menu_zoom_in, "activate", accel_group,
                                       GDK_KEY_equal, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_out) {
        g_signal_connect(view_menu_zoom_out, "activate", G_CALLBACK(on_view_zoom_out), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_out, "activate", accel_group,
                                       GDK_KEY_minus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_reset) {
        g_signal_connect(view_menu_zoom_reset, "activate", G_CALLBACK(on_view_zoom_reset), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_reset, "activate", accel_group,
                                       GDK_KEY_0, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_fit) {
        g_signal_connect(view_menu_zoom_fit, "activate", G_CALLBACK(on_view_zoom_fit), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_fit, "activate", accel_group,
                                       GDK_KEY_1, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_show_layer_edges) {
        g_signal_connect(view_menu_show_layer_edges, "toggled", G_CALLBACK(on_view_show_layer_edges), ctx);

        /* Initialize menu item state from settings */
        if (ctx->settings) {
            gboolean show_edges = settings_get_show_layer_edges(ctx->settings);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(view_menu_show_layer_edges), show_edges);
        }
    }
    if (view_menu_show_statusbar) {
        g_signal_connect(view_menu_show_statusbar, "toggled", G_CALLBACK(on_view_show_statusbar), ctx);

        /* Initialize menu item state from settings and apply initial visibility */
        if (ctx->settings) {
            gboolean show_statusbar = settings_get_show_statusbar(ctx->settings);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(view_menu_show_statusbar), show_statusbar);

            /* Apply initial state to status bar */
            if (ctx->status_bar) {
                if (show_statusbar) {
                    gtk_widget_show(ctx->status_bar);
                } else {
                    gtk_widget_hide(ctx->status_bar);
                }
            }
        }
    }
    if (view_menu_show_rulers) {
        g_signal_connect(view_menu_show_rulers, "toggled", G_CALLBACK(on_view_show_rulers), ctx);

        /* Initialize menu item state from settings */
        if (ctx->settings) {
            gboolean show_rulers = settings_get_show_rulers(ctx->settings);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(view_menu_show_rulers), show_rulers);
        }
    }

    /* Connect zoom level menu items */
    GtkWidget* view_menu_zoom_1600 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_1600"));
    GtkWidget* view_menu_zoom_800 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_800"));
    GtkWidget* view_menu_zoom_400 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_400"));
    GtkWidget* view_menu_zoom_200 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_200"));
    GtkWidget* view_menu_zoom_100 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_100"));
    GtkWidget* view_menu_zoom_50 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_50"));
    GtkWidget* view_menu_zoom_25 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_25"));
    GtkWidget* view_menu_zoom_12_5 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_12_5"));
    GtkWidget* view_menu_zoom_6_25 = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_6_25"));

    if (view_menu_zoom_1600) {
        g_signal_connect(view_menu_zoom_1600, "activate", G_CALLBACK(on_view_zoom_1600), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_1600, "activate", accel_group,
                                       GDK_KEY_5, 0, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_800) {
        g_signal_connect(view_menu_zoom_800, "activate", G_CALLBACK(on_view_zoom_800), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_800, "activate", accel_group,
                                       GDK_KEY_4, 0, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_400) {
        g_signal_connect(view_menu_zoom_400, "activate", G_CALLBACK(on_view_zoom_400), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_400, "activate", accel_group,
                                       GDK_KEY_3, 0, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_200) {
        g_signal_connect(view_menu_zoom_200, "activate", G_CALLBACK(on_view_zoom_200), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_200, "activate", accel_group,
                                       GDK_KEY_2, 0, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_100) {
        g_signal_connect(view_menu_zoom_100, "activate", G_CALLBACK(on_view_zoom_100), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_100, "activate", accel_group,
                                       GDK_KEY_1, 0, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_50) {
        g_signal_connect(view_menu_zoom_50, "activate", G_CALLBACK(on_view_zoom_50), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_50, "activate", accel_group,
                                       GDK_KEY_KP_2, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_25) {
        g_signal_connect(view_menu_zoom_25, "activate", G_CALLBACK(on_view_zoom_25), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_25, "activate", accel_group,
                                       GDK_KEY_KP_3, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_12_5) {
        g_signal_connect(view_menu_zoom_12_5, "activate", G_CALLBACK(on_view_zoom_12_5), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_12_5, "activate", accel_group,
                                       GDK_KEY_KP_4, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
    if (view_menu_zoom_6_25) {
        g_signal_connect(view_menu_zoom_6_25, "activate", G_CALLBACK(on_view_zoom_6_25), ctx);
        if (accel_group) {
            gtk_widget_add_accelerator(view_menu_zoom_6_25, "activate", accel_group,
                                       GDK_KEY_KP_5, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        }
    }
}
