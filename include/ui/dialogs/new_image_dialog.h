#ifndef NEW_IMAGE_DIALOG_H
#define NEW_IMAGE_DIALOG_H

#include "render/layer.h"
#include <gtk/gtk.h>

/**
 * New image dialog structure
 */
typedef struct _NewImageDialog NewImageDialog;

/**
 * Result structure for new image dialog
 */
typedef struct {
    guint width;                    /* Width in pixels */
    guint height;                   /* Height in pixels */
    gdouble resolution;             /* Resolution in PPI */
    LayerBackgroundType background; /* Background type */
    gdouble custom_color[4];        /* RGBA custom color (only used if background is LAYER_BACKGROUND_CUSTOM) */
} NewImageDialogResult;

/**
 * Create a new image dialog
 * @return New NewImageDialog instance, or NULL on error
 */
NewImageDialog* new_image_dialog_new(void);

/**
 * Free new image dialog
 */
void new_image_dialog_free(NewImageDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* new_image_dialog_get_window(NewImageDialog* dialog);

/**
 * Run the dialog and get image parameters
 * @param dialog The new image dialog
 * @param parent Parent window
 * @param result Output structure for dialog results (must be freed with new_image_dialog_result_free)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint new_image_dialog_run(NewImageDialog* dialog, GtkWindow* parent, NewImageDialogResult** result);

/**
 * Free dialog result structure
 */
void new_image_dialog_result_free(NewImageDialogResult* result);

#endif /* NEW_IMAGE_DIALOG_H */
