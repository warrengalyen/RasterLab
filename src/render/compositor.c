#include "render/compositor.h"
#include "ui/layers_panel.h"
#include <stdio.h>

/**
 * Map BlendMode enum to Cairo operator
 */
static cairo_operator_t blend_mode_to_cairo_operator(BlendMode blend_mode)
{
    switch (blend_mode) {
        case BLEND_MODE_NORMAL:
            return CAIRO_OPERATOR_OVER;
        case BLEND_MODE_MULTIPLY:
            return CAIRO_OPERATOR_MULTIPLY;
        case BLEND_MODE_SCREEN:
            return CAIRO_OPERATOR_SCREEN;
        case BLEND_MODE_OVERLAY:
            return CAIRO_OPERATOR_OVERLAY;
        default:
            return CAIRO_OPERATOR_OVER;
    }
}

/**
 * Render all layers to composite surface
 */
gboolean document_render_composite(ImageDocument *doc)
{
    cairo_t *cr;
    GList *iter;
    ImageLayer *layer;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    /* Create composite surface if needed */
    if (!doc->composite_surface) {
        doc->composite_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    }

    if (cairo_surface_status(doc->composite_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create composite surface");
        return FALSE;
    }

    /* Clear composite surface to transparent */
    cr = cairo_create(doc->composite_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    
    /* Set operator for proper alpha blending */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Track if this is the first visible layer */
    gboolean is_first_visible_layer = TRUE;

    /* Composite each visible layer */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer *)iter->data;

        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Draw layer with offset and opacity */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        
        /* Set operator based on layer's blend mode
           First visible layer always uses OVER to establish the base */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        
        if (layer->opacity < 1.0) {
            cairo_paint_with_alpha(cr, layer->opacity);
        } else {
            cairo_paint(cr);
        }
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    doc->composite_dirty = FALSE;

    return TRUE;
}

/**
 * Get the composite rendered surface
 */
cairo_surface_t* document_get_composite_surface(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    /* Re-render if composite is dirty */
    if (doc->composite_dirty) {
        document_render_composite(doc);
    }

    return doc->composite_surface;
}

/**
 * Mark composite surface as needing re-render
 */
void document_invalidate_composite(ImageDocument *doc)
{
    LayersPanel *layers_panel;

    if (!doc) {
        return;
    }

    doc->composite_dirty = TRUE;

    /* Trigger redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
        
        /* Update selected layer thumbnail if layers panel is available */
        layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(doc->drawing_area), "layers_panel");
        if (layers_panel) {
            /* Only update if this document is the current one in the layers panel */
            if (layers_panel->current_doc == doc) {
                layers_panel_update_selected_thumbnail(layers_panel);
                
                /* Update overview widget */
                if (layers_panel->overview_widget) {
                    gtk_widget_queue_draw(layers_panel->overview_widget);
                }
            }
        }
    }
}

/**
 * Flatten image to white background (for JPEG)
 */
cairo_surface_t* compositor_flatten_to_white_background(cairo_surface_t *composite, guint width, guint height)
{
    cairo_surface_t *flattened;
    cairo_t *cr;

    /* Create RGB surface (no alpha) */
    flattened = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (!flattened) {
        return NULL;
    }

    cr = cairo_create(flattened);

    /* Paint white background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Composite the image on top */
    cairo_set_source_surface(cr, composite, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);

    cairo_destroy(cr);

    return flattened;
}

