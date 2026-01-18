#ifndef TIFF_OPTIONS_DIALOG_H
#define TIFF_OPTIONS_DIALOG_H

#include "image_format_plugin.h"
#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show TIFF save options dialog
 * @param parent Parent window (can be NULL)
 * @param opts SaveOptions structure with plugin_data pointing to TIFFSaveOptions
 * @param doc Document being saved (can be NULL, used for layer count checks)
 * @return TRUE if user clicked OK, FALSE if cancelled
 */
gboolean tiff_options_dialog_show(GtkWindow* parent, SaveOptions* opts, ImageDocument* doc);

#ifdef __cplusplus
}
#endif

#endif /* TIFF_OPTIONS_DIALOG_H */
