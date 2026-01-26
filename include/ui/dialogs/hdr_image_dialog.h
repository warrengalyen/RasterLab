#ifndef HDR_IMAGE_DIALOG_H
#define HDR_IMAGE_DIALOG_H

#include "tone_mapping.h"
#include <gtk/gtk.h>

/**
 * Show HDR image tone mapping dialog
 * @param parent Parent window (can be NULL)
 * @param params Input/output tone mapping parameters (will be updated with user selections)
 * @param auto_apply Output parameter: will be set to TRUE if user checked "auto apply" checkbox
 * @param rgbe_data RGBE pixel data (4 bytes per pixel: R, G, B, E)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param settings Settings structure (can be NULL) - if provided, will save settings when auto apply is checked
 * @param app_dir Application directory for saving settings (can be NULL if settings is NULL)
 * @return GTK_RESPONSE_OK if user clicked OK, GTK_RESPONSE_CANCEL otherwise
 */
gint hdr_image_dialog_show(GtkWindow* parent, ToneMapParams* params, gboolean* auto_apply,
                           const uint8_t* rgbe_data, uint32_t width, uint32_t height,
                           void* settings, const char* app_dir);

#endif /* HDR_IMAGE_DIALOG_H */
