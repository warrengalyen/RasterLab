#ifndef UI_FILTER_EFFECTS_H
#define UI_FILTER_EFFECTS_H

#include "ui.h"
#include <gtk/gtk.h>

/**
 * Setup Effects menu from Glade builder
 * @param builder The GtkBuilder instance
 * @param ctx The application context
 */
void ui_filter_effects_setup_menu(GtkBuilder *builder, AppContext *ctx);

#endif /* UI_FILTER_EFFECTS_H */

