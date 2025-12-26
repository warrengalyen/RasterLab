#ifndef BEEPS_DIALOG_H
#define BEEPS_DIALOG_H

#include "render/layer.h"
#include <gtk/gtk.h>

/* Forward declaration */
typedef struct _BEEPSDialog BEEPSDialog;

/**
 * BEEPS parameters structure
 */
typedef struct {
    gfloat photometric_std_dev;
    gfloat spatial_decay;
    gint range_filter;
    BEEPSDialog* dialog; /* Dialog pointer for accessing document/layer */
} BEEPSParams;

/**
 * Create a new BEEPS dialog
 * @param title Dialog title
 * @return New BEEPSDialog instance, or NULL on error
 */
BEEPSDialog* beeps_dialog_new(const gchar* title);

/**
 * Free BEEPS dialog
 */
void beeps_dialog_free(BEEPSDialog* dialog);

/**
 * Get the dialog window
 */
GtkWindow* beeps_dialog_get_window(BEEPSDialog* dialog);

/**
 * Set the layers for preview
 */
void beeps_dialog_set_layers(BEEPSDialog* dialog, ImageLayer* original, ImageLayer* temp);

/**
 * Run the dialog and get BEEPS parameters
 * @param dialog The BEEPS dialog
 * @param parent Parent window
 * @param params Output BEEPS parameters
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint beeps_dialog_run(BEEPSDialog* dialog, GtkWindow* parent, BEEPSParams* params);

/**
 * Reset all controls to default values
 */
void beeps_dialog_reset(BEEPSDialog* dialog);

#endif /* BEEPS_DIALOG_H */
