#ifndef UI_TOOLS_MENU_H
#define UI_TOOLS_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Setup Tools menu from Glade builder (submenu + Settings... item)
 * @param builder GtkBuilder that loaded main_window.glade
 * @param ctx Application context
 */
void ui_tools_menu_setup(GtkBuilder* builder, AppContext* ctx);

/**
 * Fill Tools > Language from app_dir/languages (after ctx->app_dir is set).
 */
void ui_tools_menu_populate_language(AppContext* ctx);

#endif /* UI_TOOLS_MENU_H */
