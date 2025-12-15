#ifndef FILTER_PALETTIZE_H
#define FILTER_PALETTIZE_H

#include "ocular.h"
#include "render/layer.h"
#include "ui/dialogs/palettize_dialog.h"

/**
 * Apply palettize filter to a layer using Ocular library
 * @param layer Layer to apply filter to
 * @param params Palettize parameters
 * @return TRUE on success, FALSE on error
 */
gboolean filter_palettize_apply(ImageLayer* layer, const PalettizeParams* params);

#endif /* FILTER_PALETTIZE_H */
