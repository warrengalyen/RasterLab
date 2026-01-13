#include "ui/ui_view_menu.h"
#include "app/settings.h"
#include "document.h"
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
 * Setup View menu from Glade builder
 */
void ui_view_menu_setup(GtkBuilder* builder, AppContext* ctx, GtkAccelGroup* accel_group) {
    (void)accel_group; /* Not used for View menu yet */

    GtkWidget* view_menu = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu"));
    GtkWidget* view_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_item"));

    if (view_menu && view_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_menu_item), view_menu);
    }

    /* Connect View menu signals */
    GtkWidget* view_menu_zoom_in = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_in"));
    GtkWidget* view_menu_zoom_out = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_out"));
    GtkWidget* view_menu_zoom_reset = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_reset"));
    GtkWidget* view_menu_zoom_fit = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_zoom_fit"));
    GtkWidget* view_menu_show_layer_edges = GTK_WIDGET(gtk_builder_get_object(builder, "view_menu_show_layer_edges"));

    if (view_menu_zoom_in) {
        g_signal_connect(view_menu_zoom_in, "activate", G_CALLBACK(on_view_zoom_in), ctx);
    }
    if (view_menu_zoom_out) {
        g_signal_connect(view_menu_zoom_out, "activate", G_CALLBACK(on_view_zoom_out), ctx);
    }
    if (view_menu_zoom_reset) {
        g_signal_connect(view_menu_zoom_reset, "activate", G_CALLBACK(on_view_zoom_reset), ctx);
    }
    if (view_menu_zoom_fit) {
        g_signal_connect(view_menu_zoom_fit, "activate", G_CALLBACK(on_view_zoom_fit), ctx);
    }
    if (view_menu_show_layer_edges) {
        g_signal_connect(view_menu_show_layer_edges, "toggled", G_CALLBACK(on_view_show_layer_edges), ctx);

        /* Initialize menu item state from settings */
        if (ctx->settings) {
            gboolean show_edges = settings_get_show_layer_edges(ctx->settings);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(view_menu_show_layer_edges), show_edges);
        }
    }
}
