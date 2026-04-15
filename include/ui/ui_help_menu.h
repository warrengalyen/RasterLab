#ifndef UI_HELP_MENU_H
#define UI_HELP_MENU_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Setup Help menu from Glade (User Guide, About).
 */
void ui_help_menu_setup(GtkBuilder* builder, AppContext* ctx);

#endif /* UI_HELP_MENU_H */
