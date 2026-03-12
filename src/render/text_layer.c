#include "render/text_layer.h"
#include "render/layer.h"
#include <pango/pangocairo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Free a TextLayer and its owned strings.
 */
void text_layer_free(TextLayer* text) {
    if (!text) {
        return;
    }

    g_free(text->text);
    g_free(text->font_family);
    g_free(text);
}

/**
 * Create a new text layer with default properties.
 * The returned ImageLayer has layer_type == LAYER_TYPE_TEXT and text_data set.
 */
ImageLayer* layer_create_text(const gchar* name, guint width, guint height,
                               struct ImageDocument* doc) {
    ImageLayer* layer = layer_new(name, width, height, TRUE,
                                  LAYER_BACKGROUND_TRANSPARENT,
                                  LAYER_POSITION_ABOVE_CURRENT,
                                  NULL, doc);
    if (!layer) {
        return NULL;
    }

    TextLayer* text = (TextLayer*)g_malloc0(sizeof(TextLayer));
    if (!text) {
        layer_free(layer);
        return NULL;
    }

    text->text          = g_strdup("");
    text->font_family   = g_strdup("Sans");
    text->font_size     = 24;
    text->font_weight   = 400;   /* PANGO_WEIGHT_NORMAL */
    text->italic        = FALSE;
    text->color_r       = 0.0;
    text->color_g       = 0.0;
    text->color_b       = 0.0;
    text->color_a       = 1.0;
    text->line_spacing  = 0.0;
    text->letter_spacing = 0.0;
    text->alignment     = 0;     /* left */
    text->rotation      = 0.0;
    text->box_x         = 0.0;
    text->box_y         = 0.0;
    text->box_width     = (double)width;
    text->box_height    = (double)height;
    text->antialias     = TRUE;

    layer->layer_type = LAYER_TYPE_TEXT;
    layer->text_data  = text;

    return layer;
}

/**
 * Render text onto the layer's Cairo surface using Pango+Cairo.
 *
 * The surface is fully cleared first so deleted characters don't linger.
 * Rotation is applied around the center of the text bounding box.
 */
void text_layer_render_to_surface(ImageLayer* layer) {
    if (!layer || !layer->surface) {
        return;
    }
    if (layer->layer_type != LAYER_TYPE_TEXT || !layer->text_data) {
        return;
    }

    TextLayer* text = (TextLayer*)layer->text_data;
    if (!text->text) {
        return;
    }

    cairo_t* cr = cairo_create(layer->surface);
    if (!cr) {
        return;
    }

    /* Wipe layer surface to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Antialias hint */
    cairo_set_antialias(cr,
        text->antialias ? CAIRO_ANTIALIAS_DEFAULT : CAIRO_ANTIALIAS_NONE);

    /* Rotation around the text-box centre */
    if (text->rotation != 0.0) {
        double cx = text->box_x + text->box_width  * 0.5;
        double cy = text->box_y + text->box_height * 0.5;
        cairo_translate(cr,  cx,  cy);
        cairo_rotate(cr, text->rotation * (G_PI / 180.0));
        cairo_translate(cr, -cx, -cy);
    }

    /* ---- Pango layout ---- */
    PangoLayout* layout = pango_cairo_create_layout(cr);

    pango_layout_set_text(layout, text->text, -1);

    /* Font description */
    PangoFontDescription* font_desc = pango_font_description_new();
    pango_font_description_set_family(font_desc,
        text->font_family ? text->font_family : "Sans");
    pango_font_description_set_size(font_desc, text->font_size * PANGO_SCALE);
    pango_font_description_set_weight(font_desc, (PangoWeight)text->font_weight);
    if (text->italic) {
        pango_font_description_set_style(font_desc, PANGO_STYLE_ITALIC);
    }
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    /* Paragraph alignment */
    PangoAlignment pango_align;
    switch (text->alignment) {
        case 1:  pango_align = PANGO_ALIGN_CENTER; break;
        case 2:  pango_align = PANGO_ALIGN_RIGHT;  break;
        default: pango_align = PANGO_ALIGN_LEFT;   break;
    }
    pango_layout_set_alignment(layout, pango_align);

    /* Constrain layout width to the text box so Pango word-wraps */
    if (text->box_width > 0.0) {
        pango_layout_set_width(layout, (gint)(text->box_width * PANGO_SCALE));
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }

    /* Line spacing (additional inter-line gap in Pango units) */
    if (text->line_spacing != 0.0) {
        pango_layout_set_spacing(layout,
            (gint)(text->line_spacing * PANGO_SCALE));
    }

    /* Letter spacing via attribute list */
    if (text->letter_spacing != 0.0) {
        PangoAttrList*  attrs = pango_attr_list_new();
        PangoAttribute* attr  =
            pango_attr_letter_spacing_new(
                (gint)(text->letter_spacing * PANGO_SCALE));
        pango_attr_list_insert(attrs, attr);
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    /* Paint text */
    cairo_set_source_rgba(cr,
        text->color_r, text->color_g, text->color_b, text->color_a);
    cairo_move_to(cr, text->box_x, text->box_y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_destroy(cr);
}
