#ifndef CANVAS_SIZE_DIALOG_H
#define CANVAS_SIZE_DIALOG_H

#include "document.h"
#include <gtk/gtk.h>

/**
 * Canvas size dialog structure
 */
typedef struct _CanvasSizeDialog CanvasSizeDialog;

/**
 * Result structure for canvas size dialog
 */
typedef struct {
    guint width;                 /* New width in pixels */
    guint height;                /* New height in pixels */
    gdouble resolution;          /* Resolution in PPI */
    CanvasAnchorPosition anchor; /* Anchor position */
} CanvasSizeDialogResult;

/**
 * Create a new canvas size dialog
 * @param doc The document to get initial values from
 * @return New CanvasSizeDialog instance, or NULL on error
 */
CanvasSizeDialog* canvas_size_dialog_new(ImageDocument* doc);

/**
 * Free canvas size dialog
 */
void canvas_size_dialog_free(CanvasSizeDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* canvas_size_dialog_get_window(CanvasSizeDialog* dialog);

/**
 * Run the dialog and get canvas size parameters
 * @param dialog The canvas size dialog
 * @param parent Parent window
 * @param result Output structure for dialog results (must be freed with canvas_size_dialog_result_free)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint canvas_size_dialog_run(CanvasSizeDialog* dialog, GtkWindow* parent, CanvasSizeDialogResult** result);

/**
 * Free dialog result structure
 */
void canvas_size_dialog_result_free(CanvasSizeDialogResult* result);

#endif /* CANVAS_SIZE_DIALOG_H */
