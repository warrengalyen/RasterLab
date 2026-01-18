#include "ui/dialogs/save_options_dialog.h"
#include "plugins/format_registry.h"
#include "ui/dialogs/formats/bmp_options_dialog.h"
#include "ui/dialogs/formats/jpeg_options_dialog.h"
#include "ui/dialogs/formats/png_options_dialog.h"
#include "ui/dialogs/formats/tiff_options_dialog.h"
#include "ui/dialogs/formats/webp_options_dialog.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

/**
 * Show save options dialog for a given format
 */
gboolean save_options_dialog_show(GtkWindow* parent, const char* filename, SaveOptions* opts, ImageDocument* doc) {
    FormatHandler* handler;
    const char* ext;

    if (!filename || !opts) {
        return FALSE;
    }

    /* Find handler for this filename */
    handler = format_registry_find_saver(filename);
    if (!handler) {
        return FALSE; /* No handler found, skip dialog */
    }

    /* Get file extension */
    ext = strrchr(filename, '.');
    if (!ext) {
        return FALSE; /* No extension */
    }
    ext++; /* Skip the dot */

    /* Check format and show appropriate dialog */
    if (g_ascii_strcasecmp(ext, "bmp") == 0) {
        return bmp_options_dialog_show(parent, opts);
    }
    if (g_ascii_strcasecmp(ext, "jpg") == 0 || g_ascii_strcasecmp(ext, "jpeg") == 0) {
        return jpeg_options_dialog_show(parent, opts);
    }
    if (g_ascii_strcasecmp(ext, "png") == 0) {
        return png_options_dialog_show(parent, opts);
    }
    if (g_ascii_strcasecmp(ext, "webp") == 0) {
        return webp_options_dialog_show(parent, opts);
    }
    if (g_ascii_strcasecmp(ext, "tif") == 0 || g_ascii_strcasecmp(ext, "tiff") == 0) {
        return tiff_options_dialog_show(parent, opts, doc);
    }

    /* No dialog for this format */
    return TRUE; /* Continue with save (no dialog needed) */
}
