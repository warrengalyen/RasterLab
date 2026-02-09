/*
 * Tools menu: Settings...
 */
#include "ui/dialogs/settings_dialog.h"
#include "ui/ui_tools_menu.h"
#include "ui.h"
#include <gtk/gtk.h>

static void on_tools_menu_settings_activate(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    AppContext* ctx = (AppContext*)user_data;
    if (ctx) {
        settings_dialog_show(ctx);
    }
}

void ui_tools_menu_setup(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* tools_menu = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu"));
    GtkWidget* tools_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_item"));

    if (tools_menu && tools_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_menu_item), tools_menu);
    }

    GtkWidget* tools_menu_settings = GTK_WIDGET(gtk_builder_get_object(builder, "tools_menu_settings"));
    if (tools_menu_settings) {
        g_signal_connect(tools_menu_settings, "activate", G_CALLBACK(on_tools_menu_settings_activate), ctx);
    }
}
