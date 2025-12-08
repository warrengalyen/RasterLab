#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <glib.h>
#include <cairo/cairo.h>
#include "document.h"

/**
 * Render all layers to composite surface
 * @param doc The document
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_render_composite(ImageDocument *doc);

/**
 * Get the composite rendered surface
 * @param doc The document
 * @return Composite surface, or NULL on error
 */
cairo_surface_t* document_get_composite_surface(ImageDocument *doc);

/**
 * Mark composite surface as needing re-render
 * @param doc The document
 */
void document_invalidate_composite(ImageDocument *doc);

/**
 * Flatten image to white background (for JPEG)
 * @param composite The composite surface to flatten
 * @param width Image width
 * @param height Image height
 * @return Flattened surface, or NULL on error. Caller must call cairo_surface_destroy().
 */
cairo_surface_t* compositor_flatten_to_white_background(cairo_surface_t *composite, guint width, guint height);

#endif /* COMPOSITOR_H */

