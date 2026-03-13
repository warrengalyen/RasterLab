#include "render/text_layer.h"
#include "render/layer.h"
#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>


/* -----------------------------------------------------------------------
 * PangoLayout builder
 * --------------------------------------------------------------------- */

/**
 * Build a fully configured PangoLayout from @tl's properties.
 *
 * Character-level attributes (letter spacing, OpenType features, kerning)
 * are collected into a single PangoAttrList and applied once.  The list is
 * released after being handed to the layout, so the caller only needs to
 * unref the returned layout.
 *
 * HarfBuzz note: pango_cairo_create_layout() creates the layout through the
 * default PangoCairo font map.  On all supported platforms that font map uses
 * FreeType2 + HarfBuzz for shaping, so every OpenType feature string is
 * automatically passed to HarfBuzz — no extra setup is required.
 */
PangoLayout* text_layer_create_layout(const TextLayer* tl, cairo_t* cr) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    pango_layout_set_text(layout, tl->text ? tl->text : "", -1);

    /* Font description */
    PangoFontDescription* font_desc = pango_font_description_new();
    pango_font_description_set_family(font_desc,
                                      tl->font_family ? tl->font_family : "Sans");
    pango_font_description_set_size(font_desc, tl->font_size * PANGO_SCALE);
    pango_font_description_set_weight(font_desc, tl->font_weight);
    pango_font_description_set_style(font_desc, tl->font_style);
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    /* Paragraph alignment */
    PangoAlignment pango_align;
    switch (tl->alignment) {
        case 1:
            pango_align = PANGO_ALIGN_CENTER;
            break;
        case 2:
            pango_align = PANGO_ALIGN_RIGHT;
            break;
        default:
            pango_align = PANGO_ALIGN_LEFT;
            break;
    }
    pango_layout_set_alignment(layout, pango_align);

    if (tl->box_width > 0.0) {
        pango_layout_set_width(layout, (gint)(tl->box_width * PANGO_SCALE));
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }

    if (tl->line_spacing != 0.0)
        pango_layout_set_spacing(layout, (gint)(tl->line_spacing * PANGO_SCALE));

    /* Character-level attribute list */
    /*
     * All per-character attributes share one PangoAttrList so they compose
     * correctly and are applied in a single pass.
     *
     * 1. Letter spacing  — PangoAttrLetterSpacing
     * 2. OpenType features + kerning — PangoAttrFontFeatures (Pango ≥ 1.38)
     *
     * The font-features attribute is passed directly to HarfBuzz, which uses
     * it to enable or disable individual OpenType features during shaping.
     */
    PangoAttrList* attrs = NULL;

    /* Letter spacing */
    if (tl->letter_spacing != 0.0) {
        if (!attrs)
            attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs,
                               pango_attr_letter_spacing_new(
                                   (gint)(tl->letter_spacing * PANGO_SCALE)));
    }

#if PANGO_VERSION_CHECK(1, 38, 0)
    /*
     * Build the combined OpenType feature string.
     *
     * The user-provided opentype_features string is taken as-is, then the
     * kerning flag is appended.  "kern=0" disables HarfBuzz kerning; omitting
     * it leaves the default behaviour (on for fonts that carry GPOS kern data,
     * off for those that rely purely on kern table pairs).
     *
     * Format: CSS font-feature-settings — e.g. "liga=1, smcp=1, kern=0"
     */
    {
        const gboolean has_user_feat =
            (tl->opentype_features && tl->opentype_features[0] != '\0');
        const gboolean disable_kern = !tl->kerning;

        if (has_user_feat || disable_kern) {
            GString* feat = g_string_new(has_user_feat ? tl->opentype_features : "");
            if (disable_kern) {
                if (feat->len > 0)
                    g_string_append(feat, ", ");
                g_string_append(feat, "kern=0");
            }
            if (!attrs)
                attrs = pango_attr_list_new();
            pango_attr_list_insert(attrs,
                                   pango_attr_font_features_new(feat->str));
            g_string_free(feat, TRUE);
        }
    }
#endif /* PANGO_VERSION_CHECK(1, 38, 0) */

    if (attrs) {
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    return layout;
}

/* -----------------------------------------------------------------------
 * Private paint helper
 * --------------------------------------------------------------------- */

static void text_layer_apply_to_cr(TextLayer* text, cairo_t* cr) {
    cairo_save(cr);

    cairo_set_antialias(cr,
                        text->antialias ? CAIRO_ANTIALIAS_DEFAULT : CAIRO_ANTIALIAS_NONE);

    if (text->rotation != 0.0) {
        double cx = text->box_x + text->box_width * 0.5;
        double cy = text->box_y + text->box_height * 0.5;
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, text->rotation * (G_PI / 180.0));
        cairo_translate(cr, -cx, -cy);
    }

    PangoLayout* layout = text_layer_create_layout(text, cr);

    cairo_set_source_rgba(cr,
                          text->color_r, text->color_g, text->color_b, text->color_a);
    cairo_move_to(cr, text->box_x, text->box_y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_restore(cr);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

void text_layer_free(TextLayer* text) {
    if (!text)
        return;
    g_free(text->text);
    g_free(text->font_family);
    g_free(text->opentype_features);
    g_free(text);
}

ImageLayer* layer_create_text(const gchar* name, guint width, guint height,
                              struct ImageDocument* doc) {
    ImageLayer* layer = layer_new(name, width, height, TRUE,
                                  LAYER_BACKGROUND_TRANSPARENT,
                                  LAYER_POSITION_ABOVE_CURRENT,
                                  NULL, doc);
    if (!layer)
        return NULL;

    TextLayer* text = (TextLayer*)g_malloc0(sizeof(TextLayer));
    if (!text) {
        layer_free(layer);
        return NULL;
    }

    text->text = g_strdup("");
    text->font_family = g_strdup("Sans");
    text->font_size = 24;
    text->font_weight = PANGO_WEIGHT_NORMAL; /* 400 */
    text->font_style = PANGO_STYLE_NORMAL;
    text->color_r = 0.0;
    text->color_g = 0.0;
    text->color_b = 0.0;
    text->color_a = 1.0;
    text->line_spacing = 0.0;
    text->letter_spacing = 0.0;
    text->alignment = 0; /* left */
    text->rotation = 0.0;
    text->box_x = 0.0;
    text->box_y = 0.0;
    text->box_width = (double)width;
    text->box_height = (double)height;
    text->antialias = TRUE;
    text->kerning = TRUE;
    text->opentype_features = NULL;

    layer->layer_type = LAYER_TYPE_TEXT;
    layer->text_data = text;

    return layer;
}

void text_layer_render(TextLayer* layer, cairo_t* cr) {
    if (!layer || !cr)
        return;
    if (!layer->text || layer->text[0] == '\0')
        return;
    text_layer_apply_to_cr(layer, cr);
}

void text_layer_render_to_surface(ImageLayer* layer) {
    if (!layer || !layer->surface)
        return;
    if (layer->layer_type != LAYER_TYPE_TEXT || !layer->text_data)
        return;

    TextLayer* text = (TextLayer*)layer->text_data;
    if (!text->text)
        return;

    cairo_t* cr = cairo_create(layer->surface);
    if (!cr)
        return;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    text_layer_apply_to_cr(text, cr);
    cairo_destroy(cr);
}
