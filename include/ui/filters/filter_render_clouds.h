#ifndef FILTER_RENDER_CLOUDS_H
#define FILTER_RENDER_CLOUDS_H

#include "ocular.h"
#include "render/layer.h"
#include <glib.h>

/**
 * Apply render clouds filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param params CloudParams structure with all cloud parameters
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_render_clouds_apply(ImageLayer* layer, const CloudParams* params);

#endif /* FILTER_RENDER_CLOUDS_H */
