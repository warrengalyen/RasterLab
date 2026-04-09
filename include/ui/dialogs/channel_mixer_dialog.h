/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef CHANNEL_MIXER_DIALOG_H
#define CHANNEL_MIXER_DIALOG_H

#include <gtk/gtk.h>
#include "render/layer.h"

/**
 * Callback for channel mixer preview updates.
 * @param dialog The channel mixer dialog
 * @param mixer 16 floats: 4 rows (R,G,B,Gray out) × 4 inputs (R,G,B,Constant) each
 * @param monochrome Whether monochrome mode is enabled
 * @param preserve_luminance Whether preserve luminance is enabled
 * @param user_data User data (e.g. temp layer)
 * @return TRUE if preview was updated successfully
 */
typedef gboolean (*ChannelMixerDialogPreviewCallback)(void* dialog,
                                                       const gfloat* mixer,
                                                       gboolean monochrome,
                                                       gboolean preserve_luminance,
                                                       gpointer user_data);

typedef struct _ChannelMixerDialog ChannelMixerDialog;

ChannelMixerDialog* channel_mixer_dialog_new(const gchar* title);
void channel_mixer_dialog_free(ChannelMixerDialog* dialog);
GtkWindow* channel_mixer_dialog_get_window(ChannelMixerDialog* dialog);
void channel_mixer_dialog_set_layers(ChannelMixerDialog* dialog, ImageLayer* original, ImageLayer* temp);
void channel_mixer_dialog_update_after_layer(ChannelMixerDialog* dialog, ImageLayer* layer);
void channel_mixer_dialog_set_preview_callback(ChannelMixerDialog* dialog,
                                               ChannelMixerDialogPreviewCallback callback,
                                               gpointer user_data);
void channel_mixer_dialog_reset(ChannelMixerDialog* dialog);

/**
 * Run the dialog.
 * @return GTK_RESPONSE_OK if user clicked OK.
 * On OK, out_mixer (if non-NULL) is filled with 16 floats, and out_monochrome/out_preserve_luminance are set.
 */
gint channel_mixer_dialog_run(ChannelMixerDialog* dialog,
                              GtkWindow* parent,
                              gfloat* out_mixer,
                              gboolean* out_monochrome,
                              gboolean* out_preserve_luminance);

#endif /* CHANNEL_MIXER_DIALOG_H */
