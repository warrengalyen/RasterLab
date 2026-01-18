#ifndef SAVE_OPTIONS_DIALOG_H
#define SAVE_OPTIONS_DIALOG_H

#include "image_format_plugin.h"
#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show save options dialog for a given format
 * @param parent Parent window (can be NULL)
 * @param filename Filename being saved (used to determine format)
 * @param opts SaveOptions structure to populate (must be pre-allocated with plugin_data if needed)
 * @param doc Document being saved (can be NULL, used for layer count checks)
 * @return TRUE if user clicked OK, FALSE if cancelled
 */
gboolean save_options_dialog_show(GtkWindow* parent, const char* filename, SaveOptions* opts, ImageDocument* doc);

#ifdef __cplusplus
}
#endif

#endif /* SAVE_OPTIONS_DIALOG_H */
