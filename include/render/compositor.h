#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <glib.h>
#include <cairo/cairo.h>
#include "document.h"
#include "render/dirty.h"

/**
 * Map BlendMode enum to Cairo operator
 * Exported for use by tile rendering
 */
cairo_operator_t blend_mode_to_cairo_operator(BlendMode blend_mode);

/**
 * Render all layers to composite surface (full render)
 * @param doc The document
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_render_composite(ImageDocument *doc);

/**
 * Render only dirty regions to composite surface (optimized)
 * @param doc The document
 * @param dirty_rect The dirty rectangle to render
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_render_composite_dirty(ImageDocument *doc, const DirtyRect *dirty_rect);

/**
 * Get the composite rendered surface
 * @param doc The document
 * @return Composite surface, or NULL on error
 */
cairo_surface_t* document_get_composite_surface(ImageDocument *doc);

/**
 * Generate a thumbnail surface directly from tiles at the specified size
 * This is much more efficient than creating a full composite surface for thumbnails
 * @param doc The document
 * @param thumb_width Desired thumbnail width
 * @param thumb_height Desired thumbnail height
 * @return Thumbnail surface, or NULL on error. Caller must call cairo_surface_destroy().
 */
cairo_surface_t* document_generate_thumbnail_from_tiles(ImageDocument *doc, gint thumb_width, gint thumb_height);

/**
 * Mark composite surface as needing re-render
 * @param doc The document
 */
void document_invalidate_composite(ImageDocument *doc);

/**
 * Mark a specific region as dirty
 * @param doc The document
 * @param dirty_rect The dirty rectangle region
 */
void document_invalidate_region(ImageDocument *doc, const DirtyRect *dirty_rect);

/**
 * Flatten image to white background (for JPEG)
 * @param composite The composite surface to flatten
 * @param width Image width
 * @param height Image height
 * @return Flattened surface, or NULL on error. Caller must call cairo_surface_destroy().
 */
cairo_surface_t* compositor_flatten_to_white_background(cairo_surface_t *composite, guint width, guint height);

#endif /* COMPOSITOR_H */

