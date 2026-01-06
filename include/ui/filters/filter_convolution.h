#ifndef FILTER_CONVOLUTION_H
#define FILTER_CONVOLUTION_H

#include "ocular.h"
#include "render/layer.h"
#include <glib.h>

/**
 * Apply convolution filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param kernel The convolution kernel (5x5 = 25 floats)
 * @param divisor The divisor value
 * @param bias The bias value
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_convolution_apply(ImageLayer* layer, float* kernel, unsigned char divisor, unsigned char bias);

#endif /* FILTER_CONVOLUTION_H */