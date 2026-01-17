#ifndef WEBP_OPTIONS_DIALOG_H
#define WEBP_OPTIONS_DIALOG_H

#include "image_format_plugin.h"
#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show WebP save options dialog
 * @param parent Parent window (can be NULL)
 * @param opts SaveOptions structure with plugin_data pointing to WebPSaveOptions
 * @return TRUE if user clicked OK, FALSE if cancelled
 */
gboolean webp_options_dialog_show(GtkWindow* parent, SaveOptions* opts);

#ifdef __cplusplus
}
#endif

#endif /* WEBP_OPTIONS_DIALOG_H */
