#ifndef TEXT_LAYER_H
#define TEXT_LAYER_H

#include "document.h"
#include <cairo/cairo.h>
#include <glib.h>

/**
 * Vector text layer data — rendered dynamically via Pango+Cairo each composite.
 *
 * All string fields are heap-allocated (g_strdup / g_malloc).
 * Alignment: 0 = left, 1 = center, 2 = right.
 * font_weight maps directly to PangoWeight (e.g. 400 = normal, 700 = bold).
 * rotation is in degrees; box coordinates are in layer-local pixels.
 */
typedef struct {
    char *text;
    char *font_family;
    int   font_size;
    int   font_weight;
    gboolean italic;

    double color_r;
    double color_g;
    double color_b;
    double color_a;

    double line_spacing;
    double letter_spacing;

    int alignment;

    double rotation;

    double box_x;
    double box_y;
    double box_width;
    double box_height;

    gboolean antialias;
} TextLayer;

/**
 * Create a new text layer with default properties.
 * The returned ImageLayer has layer_type == LAYER_TYPE_TEXT and text_data set.
 * @param name    Layer name (made unique if doc is non-NULL)
 * @param width   Layer surface width (should match document width)
 * @param height  Layer surface height (should match document height)
 * @param doc     Optional document for unique-name generation; may be NULL
 * @return New layer, or NULL on error. Caller must call layer_free().
 */
ImageLayer* layer_create_text(const gchar* name, guint width, guint height,
                               struct ImageDocument* doc);

/**
 * Free a TextLayer and its owned strings.
 * Called automatically by layer_free() for LAYER_TYPE_TEXT layers.
 * @param text TextLayer to free
 */
void text_layer_free(TextLayer* text);

/**
 * Render the text described by layer->text_data onto layer->surface using
 * Pango+Cairo.  The surface is cleared to transparent first.
 * This is called before compositing whenever the layer cache is dirty.
 * @param layer An ImageLayer with layer_type == LAYER_TYPE_TEXT
 */
void text_layer_render_to_surface(ImageLayer* layer);

#endif /* TEXT_LAYER_H */
