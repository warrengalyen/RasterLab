#ifndef ADJUSTMENTS_H
#define ADJUSTMENTS_H

#include "render/layer.h"
#include <glib.h>
#include <cairo.h>

/**
 * Convert Cairo ARGB32 surface to RGB buffer for Ocular library
 * @param surface The Cairo surface (must be ARGB32 format)
 * @param rgb_output Output buffer (must be allocated: width * height * 3 bytes)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_cairo_to_rgb(cairo_surface_t *surface, guchar *rgb_output);

/**
 * Convert RGB buffer to Cairo ARGB32 surface
 * @param surface The Cairo surface (must be ARGB32 format)
 * @param rgb_input Input buffer (width * height * 3 bytes)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_rgb_to_cairo(cairo_surface_t *surface, const guchar *rgb_input);

/**
 * Convert single-channel grayscale buffer to Cairo ARGB32 surface
 * @param surface The Cairo surface (must be ARGB32 format)
 * @param grayscale_input Input buffer (width * height bytes)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_grayscale_to_cairo(cairo_surface_t *surface, const guchar *grayscale_input);

/**
 * Validate surface format and get dimensions
 * @param surface The Cairo surface to validate
 * @param width Output parameter for width
 * @param height Output parameter for height
 * @return TRUE if surface is valid ARGB32, FALSE otherwise
 */
gboolean adjustments_validate_surface(cairo_surface_t *surface, gint *width, gint *height);

/**
 * Apply grayscale filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_apply_grayscale(ImageLayer *layer);

/**
 * Scale a value from UI range to filter range
 * @param ui_value Value in UI range
 * @param ui_min Minimum value in UI range
 * @param ui_max Maximum value in UI range
 * @param filter_min Minimum value in filter range
 * @param filter_max Maximum value in filter range
 * @return Scaled value in filter range
 */
gdouble adjustments_scale_value(gdouble ui_value,
                                gdouble ui_min,
                                gdouble ui_max,
                                gdouble filter_min,
                                gdouble filter_max);

/**
 * Apply vibrance filter to a layer using Ocular library
 * @param layer The layer to apply the filter to
 * @param vibrance Vibrance value (0.0-1.0)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean adjustments_apply_vibrance(ImageLayer *layer, gfloat vibrance);

#endif /* ADJUSTMENTS_H */

