#ifndef FILTER_WAVE_H
#define FILTER_WAVE_H

#include "render/layer.h"
#include <glib.h>

/**
 * Apply wave distortion filter to a layer using Ocular library.
 * @param values[0] num_generators
 * @param values[1] min_wavelength
 * @param values[2] max_wavelength
 * @param values[3] min_amplitude
 * @param values[4] max_amplitude
 * @param values[5] scale_x
 * @param values[6] scale_y
 * @param values[7] wave_type (0=sine, 1=triangle, 2=square, 3=sawtooth)
 * @param values[8] seed (integer, same style as Clouds seed)
 */
gboolean filter_wave_apply(ImageLayer* layer, const gfloat* values, gint num_values);

#endif /* FILTER_WAVE_H */
