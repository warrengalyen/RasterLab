/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_LENS_CORRECTION_H
#define FILTER_LENS_CORRECTION_H

#include "render/layer.h"
#include "ui/dialogs/lens_correction_dialog.h"
#include <glib.h>

/**
 * Apply lens correction to an image layer using lensfun
 * @param layer The image layer to correct
 * @param params Lens correction parameters (camera, lens, focal length, etc.)
 * @param app_dir Application directory for locating lens database
 * @return TRUE on success, FALSE on error
 */
gboolean filter_lens_correction_apply(ImageLayer* layer,
                                      const LensCorrectionParams* params,
                                      const gchar* app_dir);

#endif /* FILTER_LENS_CORRECTION_H */
