#ifndef UI_FILTER_ADJUST_H
#define UI_FILTER_ADJUST_H

#include <gtk/gtk.h>
#include "ui.h"

/**
 * Setup Adjustments menu from Glade builder
 * @param builder The GtkBuilder instance
 * @param ctx The application context
 */
void ui_filter_adjust_setup_menu(GtkBuilder *builder, AppContext *ctx);

#endif /* UI_FILTER_ADJUST_H */

