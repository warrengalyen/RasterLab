#ifndef FILTER_CURVES_H
#define FILTER_CURVES_H

#include "render/layer.h"
#include "ui/widgets/curves_widget.h"
#include <glib.h>

/**
 * Apply curves filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param curves The curves widget containing the curve data
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_curves_apply(ImageLayer* layer, CurvesWidget* curves);

#endif /* FILTER_CURVES_H */
