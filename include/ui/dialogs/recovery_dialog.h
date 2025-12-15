#ifndef RECOVERY_DIALOG_H
#define RECOVERY_DIALOG_H

#include "app/autosave.h"
#include "ui.h"
#include <gtk/gtk.h>

/**
 * Show recovery dialog for autosave files
 * @param ctx Application context
 */
void recovery_dialog_show(AppContext* ctx);

#endif /* RECOVERY_DIALOG_H */
