#ifndef LAYER_H
#define LAYER_H

#include <glib.h>
#include <cairo/cairo.h>
#include "document.h"

/**
 * Create a new image layer
 * @param name Layer name
 * @param width Layer width
 * @param height Layer height
 * @param has_alpha Whether layer has alpha channel
 * @return New layer, or NULL on error. Caller must call layer_free().
 */
ImageLayer* layer_new(const gchar *name, guint width, guint height, gboolean has_alpha);

/**
 * Free an image layer
 * @param layer Layer to free
 */
void layer_free(ImageLayer *layer);

/**
 * Add a new empty layer to the document
 * @param doc The document
 * @param name Layer name
 * @return New layer, or NULL on error
 */
ImageLayer* document_add_layer(ImageDocument *doc, const gchar *name);

/**
 * Delete a layer from the document
 * @param doc The document
 * @param layer Layer to delete
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_delete_layer(ImageDocument *doc, ImageLayer *layer);

/**
 * Duplicate an existing layer
 * @param doc The document
 * @param layer Layer to duplicate
 * @param name Name for the new layer
 * @return New duplicated layer, or NULL on error
 */
ImageLayer* document_duplicate_layer(ImageDocument *doc, ImageLayer *layer, const gchar *name);

/**
 * Move a layer up in the stack
 * @param doc The document
 * @param layer Layer to move
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_layer_move_up(ImageDocument *doc, ImageLayer *layer);

/**
 * Move a layer down in the stack
 * @param doc The document
 * @param layer Layer to move
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_layer_move_down(ImageDocument *doc, ImageLayer *layer);

/**
 * Check if a layer can be moved up in the stack
 * @param doc The document
 * @param layer Layer to check
 * @return TRUE if can move up, FALSE otherwise
 */
gboolean document_layer_can_move_up(ImageDocument *doc, ImageLayer *layer);

/**
 * Check if a layer can be moved down in the stack
 * @param doc The document
 * @param layer Layer to check
 * @return TRUE if can move down, FALSE otherwise
 */
gboolean document_layer_can_move_down(ImageDocument *doc, ImageLayer *layer);

/**
 * Get the layer at a specific index
 * @param doc The document
 * @param index Layer index (0-based)
 * @return Layer at index, or NULL if not found
 */
ImageLayer* document_get_layer(ImageDocument *doc, guint index);

/**
 * Get the number of layers in the document
 * @param doc The document
 * @return Number of layers
 */
guint document_get_layer_count(ImageDocument *doc);

/**
 * Get the top (active) layer
 * @param doc The document
 * @return Top layer, or NULL if no layers
 */
ImageLayer* document_get_active_layer(ImageDocument *doc);

/**
 * Set the selected layer (for tool operations)
 * @param doc The document
 * @param layer Layer to select
 */
void document_set_selected_layer(ImageDocument *doc, ImageLayer *layer);

/**
 * Get the selected layer (for tool operations)
 * @param doc The document
 * @return Selected layer, or active layer if none selected
 */
ImageLayer* document_get_selected_layer(ImageDocument *doc);

/**
 * Invalidate layer cache (mark as needing regeneration)
 * @param layer The layer to invalidate
 */
void layer_invalidate_cache(ImageLayer *layer);

/**
 * Ensure layer cache is up to date (regenerate if dirty)
 * @param layer The layer to update
 * @return TRUE if successful, FALSE otherwise
 */
gboolean layer_ensure_cache(ImageLayer *layer);

#endif /* LAYER_H */

