#ifndef TEXT_LAYER_H
#define TEXT_LAYER_H

#include "document.h"
#include <cairo/cairo.h>
#include <glib.h>
#include <pango/pangocairo.h>

/**
 * Vector text layer data — rendered dynamically via Pango+Cairo each composite.
 *
 * Typography notes:
 *   font_weight  — PangoWeight value (e.g. PANGO_WEIGHT_NORMAL=400, PANGO_WEIGHT_BOLD=700).
 *   font_style   — PangoStyle: PANGO_STYLE_NORMAL / OBLIQUE / ITALIC.
 *   kerning      — When FALSE the OpenType kern feature is explicitly disabled.
 *   opentype_features — CSS font-feature-settings string applied via
 *                        PangoAttrFontFeatures (Pango ≥ 1.38 / HarfBuzz back-end).
 *                        Example: "liga=1, smcp=1, onum=1"
 *                        NULL or "" means no additional features.
 *
 * HarfBuzz shaping is automatic: pango_cairo_create_layout() uses the default
 * PangoCairo font-map which delegates shaping to HarfBuzz when available.
 *
 * Alignment: 0 = left, 1 = center, 2 = right.
 * rotation is in degrees; box coordinates are in layer-local pixels.
 */
typedef struct {
    char       *text;
    char       *font_family;
    int         font_size;
    PangoWeight font_weight;    /* PANGO_WEIGHT_NORMAL=400, PANGO_WEIGHT_BOLD=700, … */
    PangoStyle  font_style;     /* PANGO_STYLE_NORMAL / OBLIQUE / ITALIC              */

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

    /* Extended typography */
    gboolean  kerning;           /* FALSE disables the OpenType kern feature           */
    char     *opentype_features; /* CSS font-feature-settings string; may be NULL/"" */
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
 * Deep copy of text layer properties (all owned strings duplicated).
 */
TextLayer* text_layer_duplicate(const TextLayer* src);

/**
 * Render text directly into an arbitrary Cairo context.
 *
 * The caller is responsible for all canvas/zoom transforms already being
 * applied to @cr before this call.  Because @cr carries those transforms,
 * Pango measures and lays out glyphs at the effective on-screen resolution,
 * giving crisp vector-quality text at any zoom level.
 *
 * @param layer TextLayer that holds all text properties
 * @param cr    Cairo context to draw into (must not be NULL)
 */
void text_layer_render(TextLayer* layer, cairo_t* cr);

/**
 * Render the text described by layer->text_data onto layer->surface using
 * Pango+Cairo.  The surface is cleared to transparent first.
 * Used for offline paths: tile compositing, export, and thumbnail generation.
 * @param layer An ImageLayer with layer_type == LAYER_TYPE_TEXT
 */
void text_layer_render_to_surface(ImageLayer* layer);

/**
 * Create a PangoLayout configured with all of @tl's text, font, alignment,
 * wrapping, spacing, and OpenType feature settings.
 *
 * Letter spacing is applied via PangoAttrLetterSpacing.
 * OpenType features (including kerning) are applied via PangoAttrFontFeatures,
 * which are passed through to the HarfBuzz shaper automatically.
 *
 * The caller owns the returned layout and must call g_object_unref() when done.
 * Rotation is NOT applied (the layout is in the text box's local frame).
 *
 * @param tl  TextLayer whose settings to apply
 * @param cr  Cairo context that provides the PangoCairo font map / DPI basis
 * @return    A new PangoLayout (never NULL)
 */
PangoLayout* text_layer_create_layout(const TextLayer* tl, cairo_t* cr);

#endif /* TEXT_LAYER_H */
