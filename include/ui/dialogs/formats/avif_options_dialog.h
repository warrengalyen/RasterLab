#ifndef AVIF_OPTIONS_DIALOG_H
#define AVIF_OPTIONS_DIALOG_H

#include "document.h"
#include "image_format_plugin.h"
#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show AVIF save options dialog
 * @param parent Parent window (can be NULL)
 * @param opts SaveOptions structure with plugin_data pointing to AVIFSaveOptions
 * @return TRUE if user clicked OK, FALSE if cancelled
 */
gboolean avif_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc);

#ifdef __cplusplus
}
#endif

#endif /* AVIF_OPTIONS_DIALOG_H */
