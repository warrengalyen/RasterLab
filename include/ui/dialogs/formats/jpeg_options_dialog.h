#ifndef JPEG_OPTIONS_DIALOG_H
#define JPEG_OPTIONS_DIALOG_H

#include "image_format_plugin.h"
#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show JPEG save options dialog
 * @param parent Parent window (can be NULL)
 * @param opts SaveOptions structure with plugin_data pointing to JPEGSaveOptions
 * @return TRUE if user clicked OK, FALSE if cancelled
 */
gboolean jpeg_options_dialog_show(GtkWindow* parent, SaveOptions* opts);

#ifdef __cplusplus
}
#endif

#endif /* JPEG_OPTIONS_DIALOG_H */
