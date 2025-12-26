#include "ui/filters/filter_utils.h"
#include "document.h"
#include "filters.h"
#include "render/dirty.h"
#include "selection/selection_render.h"
#include <glib.h>

gboolean filter_utils_allocate_rgb_buffers(cairo_surface_t* surface,
                                           FilterRGBBuffers* buffers,
                                           const gchar* filter_name) {
    gint width, height;

    if (!surface || !buffers || !filter_name) {
        return FALSE;
    }

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Initialize buffer structure */
    buffers->width = width;
    buffers->height = height;
    buffers->stride = width * 3;
    buffers->rgb_input = NULL;
    buffers->rgb_output = NULL;

    /* Allocate buffers for RGB input and output */
    buffers->rgb_input = (guchar*)g_malloc(width * height * 3);
    buffers->rgb_output = (guchar*)g_malloc(width * height * 3);

    if (!buffers->rgb_input || !buffers->rgb_output) {
        g_warning("%s: Failed to allocate memory", filter_name);
        filter_utils_free_rgb_buffers(buffers);
        return FALSE;
    }

    return TRUE;
}

void filter_utils_free_rgb_buffers(FilterRGBBuffers* buffers) {
    if (buffers) {
        g_free(buffers->rgb_input);
        g_free(buffers->rgb_output);
        buffers->rgb_input = NULL;
        buffers->rgb_output = NULL;
    }
}

gboolean filter_utils_allocate_rgba_buffers(cairo_surface_t* surface,
                                            FilterRGBABuffers* buffers,
                                            const gchar* filter_name) {
    gint width, height;

    if (!surface || !buffers || !filter_name) {
        return FALSE;
    }

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    /* Initialize buffer structure */
    buffers->width = width;
    buffers->height = height;
    buffers->stride = width * 4;
    buffers->rgba_input = NULL;
    buffers->rgba_output = NULL;

    /* Allocate buffers for RGBA input and output */
    buffers->rgba_input = (guchar*)g_malloc(width * height * 4);
    buffers->rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!buffers->rgba_input || !buffers->rgba_output) {
        g_warning("%s: Failed to allocate memory", filter_name);
        filter_utils_free_rgba_buffers(buffers);
        return FALSE;
    }

    return TRUE;
}

void filter_utils_free_rgba_buffers(FilterRGBABuffers* buffers) {
    if (buffers) {
        g_free(buffers->rgba_input);
        g_free(buffers->rgba_output);
        buffers->rgba_input = NULL;
        buffers->rgba_output = NULL;
    }
}

gboolean filter_utils_cairo_to_rgb(cairo_surface_t* surface,
                                   FilterRGBBuffers* buffers,
                                   const gchar* filter_name) {
    if (!surface || !buffers || !filter_name || !buffers->rgb_input) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGB */
    if (!adjustments_cairo_to_rgb(surface, buffers->rgb_input)) {
        g_warning("%s: Failed to convert surface to RGB", filter_name);
        return FALSE;
    }

    return TRUE;
}

gboolean filter_utils_rgb_to_cairo(cairo_surface_t* surface,
                                   FilterRGBBuffers* buffers,
                                   const gchar* filter_name) {
    if (!surface || !buffers || !filter_name || !buffers->rgb_output) {
        return FALSE;
    }

    /* Convert back from RGB to Cairo ARGB32 */
    if (!adjustments_rgb_to_cairo(surface, buffers->rgb_output)) {
        g_warning("%s: Failed to convert RGB to surface", filter_name);
        return FALSE;
    }

    return TRUE;
}

gboolean filter_utils_cairo_to_rgba(cairo_surface_t* surface,
                                    FilterRGBABuffers* buffers,
                                    const gchar* filter_name) {
    if (!surface || !buffers || !filter_name || !buffers->rgba_input) {
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, buffers->rgba_input)) {
        g_warning("%s: Failed to convert surface to RGBA", filter_name);
        return FALSE;
    }

    return TRUE;
}

gboolean filter_utils_rgba_to_cairo(cairo_surface_t* surface,
                                    FilterRGBABuffers* buffers,
                                    const gchar* filter_name) {
    if (!surface || !buffers || !filter_name || !buffers->rgba_output) {
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, buffers->rgba_output)) {
        g_warning("%s: Failed to convert RGBA to surface", filter_name);
        return FALSE;
    }

    return TRUE;
}

/**
 * Apply selection masking to filter results
 * Blends filtered surface with original surface based on selection mask
 */
gboolean filter_utils_apply_selection_mask(cairo_surface_t* filtered_surface,
                                           cairo_surface_t* original_surface,
                                           struct ImageDocument* doc,
                                           struct ImageLayer* layer) {
    if (!filtered_surface || !original_surface || !doc || !layer) {
        return FALSE;
    }

    /* Check if there's an active selection */
    if (!doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        /* No selection, filtered result is already correct */
        return TRUE;
    }

    /* Get surface dimensions */
    gint width = cairo_image_surface_get_width(filtered_surface);
    gint height = cairo_image_surface_get_height(filtered_surface);

    if (width <= 0 || height <= 0) {
        return FALSE;
    }

    /* Create dirty rect for the entire layer (in document coordinates) */
    DirtyRect layer_rect;
    dirty_rect_set(&layer_rect, layer->offset_x, layer->offset_y, width, height);

    /* Get selection mask for this layer region */
    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data) {
        /* No selection in this region, filtered result is already correct */
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return TRUE;
    }

    /* Flush surfaces to ensure all drawing is complete */
    cairo_surface_flush(filtered_surface);
    cairo_surface_flush(original_surface);

    /* Get surface data */
    gint filtered_stride = cairo_image_surface_get_stride(filtered_surface);
    gint original_stride = cairo_image_surface_get_stride(original_surface);
    guchar* filtered_data = cairo_image_surface_get_data(filtered_surface);
    guchar* original_data = cairo_image_surface_get_data(original_surface);

    if (!filtered_data || !original_data) {
        selection_mask_free(region_mask);
        return FALSE;
    }

    /* Calculate mask offset relative to layer */
    gint mask_offset_x = actual_region.x - layer->offset_x;
    gint mask_offset_y = actual_region.y - layer->offset_y;

    /* Blend filtered result with original based on selection mask */
    for (gint y = 0; y < height; y++) {
        gint mask_y = y - mask_offset_y;

        if (mask_y < 0 || mask_y >= region_mask->height) {
            /* Outside mask region, keep original */
            continue;
        }

        guchar* filtered_row = filtered_data + y * filtered_stride;
        guchar* original_row = original_data + y * original_stride;
        const uint8_t* mask_row = region_mask->data + mask_y * region_mask->stride;

        for (gint x = 0; x < width; x++) {
            gint mask_x = x - mask_offset_x;

            if (mask_x < 0 || mask_x >= region_mask->width) {
                /* Outside mask region, keep original */
                continue;
            }

            uint8_t mask_alpha = mask_row[mask_x];

            if (mask_alpha == 0) {
                /* Outside selection: use original pixel */
                filtered_row[x * 4 + 0] = original_row[x * 4 + 0]; /* B */
                filtered_row[x * 4 + 1] = original_row[x * 4 + 1]; /* G */
                filtered_row[x * 4 + 2] = original_row[x * 4 + 2]; /* R */
                filtered_row[x * 4 + 3] = original_row[x * 4 + 3]; /* A */
            } else if (mask_alpha < 255) {
                /* Feather zone: blend filtered with original based on mask */
                /* Both surfaces use premultiplied alpha, so we need to handle this correctly */
                guchar filtered_b = filtered_row[x * 4 + 0];
                guchar filtered_g = filtered_row[x * 4 + 1];
                guchar filtered_r = filtered_row[x * 4 + 2];
                guchar filtered_a = filtered_row[x * 4 + 3];

                guchar original_b = original_row[x * 4 + 0];
                guchar original_g = original_row[x * 4 + 1];
                guchar original_r = original_row[x * 4 + 2];
                guchar original_a = original_row[x * 4 + 3];

                /* Un-premultiply both pixels */
                uint16_t filtered_r_straight = (filtered_a > 0) ? (filtered_r * 255 + filtered_a / 2) / filtered_a : 0;
                uint16_t filtered_g_straight = (filtered_a > 0) ? (filtered_g * 255 + filtered_a / 2) / filtered_a : 0;
                uint16_t filtered_b_straight = (filtered_a > 0) ? (filtered_b * 255 + filtered_a / 2) / filtered_a : 0;

                uint16_t original_r_straight = (original_a > 0) ? (original_r * 255 + original_a / 2) / original_a : 0;
                uint16_t original_g_straight = (original_a > 0) ? (original_g * 255 + original_a / 2) / original_a : 0;
                uint16_t original_b_straight = (original_a > 0) ? (original_b * 255 + original_a / 2) / original_a : 0;

                /* Clamp to valid range */
                if (filtered_r_straight > 255)
                    filtered_r_straight = 255;
                if (filtered_g_straight > 255)
                    filtered_g_straight = 255;
                if (filtered_b_straight > 255)
                    filtered_b_straight = 255;
                if (original_r_straight > 255)
                    original_r_straight = 255;
                if (original_g_straight > 255)
                    original_g_straight = 255;
                if (original_b_straight > 255)
                    original_b_straight = 255;

                /* Blend in straight alpha space */
                float mask_factor = (float)mask_alpha / 255.0f;
                float orig_factor = 1.0f - mask_factor;

                uint16_t result_r = (uint16_t)(filtered_r_straight * mask_factor + original_r_straight * orig_factor);
                uint16_t result_g = (uint16_t)(filtered_g_straight * mask_factor + original_g_straight * orig_factor);
                uint16_t result_b = (uint16_t)(filtered_b_straight * mask_factor + original_b_straight * orig_factor);
                uint8_t result_a = (uint8_t)(filtered_a * mask_factor + original_a * orig_factor);

                /* Re-premultiply with blended alpha */
                if (result_a > 0) {
                    filtered_row[x * 4 + 0] = (result_b * result_a + 127) / 255; /* B */
                    filtered_row[x * 4 + 1] = (result_g * result_a + 127) / 255; /* G */
                    filtered_row[x * 4 + 2] = (result_r * result_a + 127) / 255; /* R */
                    filtered_row[x * 4 + 3] = result_a;                          /* A */
                } else {
                    filtered_row[x * 4 + 0] = 0;
                    filtered_row[x * 4 + 1] = 0;
                    filtered_row[x * 4 + 2] = 0;
                    filtered_row[x * 4 + 3] = 0;
                }
            }
            /* If mask_alpha == 255, keep filtered pixel as-is (fully inside selection) */
        }
    }

    /* Mark surface as dirty */
    cairo_surface_mark_dirty(filtered_surface);

    /* Free selection mask */
    selection_mask_free(region_mask);

    return TRUE;
}

/**
 * Create a masked surface showing only selected pixels
 * Creates a copy of the layer surface with pixels outside selection cleared
 */
cairo_surface_t* filter_utils_create_masked_preview_surface(cairo_surface_t* layer_surface,
                                                            struct ImageDocument* doc,
                                                            struct ImageLayer* layer) {
    if (!layer_surface || !doc || !layer) {
        return NULL;
    }

    /* Check if there's an active selection */
    if (!doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        /* No selection, return a copy of the original surface */
        gint width = cairo_image_surface_get_width(layer_surface);
        gint height = cairo_image_surface_get_height(layer_surface);
        cairo_surface_t* copy = cairo_image_surface_create(
            cairo_image_surface_get_format(layer_surface), width, height);
        if (copy) {
            cairo_t* cr = cairo_create(copy);
            cairo_set_source_surface(cr, layer_surface, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
        return copy;
    }

    /* Get surface dimensions */
    gint width = cairo_image_surface_get_width(layer_surface);
    gint height = cairo_image_surface_get_height(layer_surface);

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    /* Create a copy of the layer surface */
    cairo_surface_t* masked_surface = cairo_image_surface_create(
        cairo_image_surface_get_format(layer_surface), width, height);
    if (!masked_surface) {
        return NULL;
    }

    /* Copy the original surface */
    cairo_t* cr = cairo_create(masked_surface);
    cairo_set_source_surface(cr, layer_surface, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Create dirty rect for the entire layer (in document coordinates) */
    DirtyRect layer_rect;
    dirty_rect_set(&layer_rect, layer->offset_x, layer->offset_y, width, height);

    /* Get selection mask for this layer region */
    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data) {
        /* No selection in this region, return copy as-is */
        selection_mask_free(region_mask);
        return masked_surface;
    }

    /* Clear pixels outside the selection */
    cairo_surface_flush(masked_surface);
    gint masked_stride = cairo_image_surface_get_stride(masked_surface);
    guchar* masked_data = cairo_image_surface_get_data(masked_surface);

    /* Calculate mask offset relative to layer */
    gint mask_offset_x = actual_region.x - layer->offset_x;
    gint mask_offset_y = actual_region.y - layer->offset_y;

    for (gint y = 0; y < height; y++) {
        gint mask_y = y - mask_offset_y;

        if (mask_y < 0 || mask_y >= region_mask->height) {
            /* Outside mask region, clear pixel */
            guchar* row = masked_data + y * masked_stride;
            for (gint x = 0; x < width; x++) {
                row[x * 4 + 0] = 0; /* B */
                row[x * 4 + 1] = 0; /* G */
                row[x * 4 + 2] = 0; /* R */
                row[x * 4 + 3] = 0; /* A */
            }
            continue;
        }

        const uint8_t* mask_row = region_mask->data + mask_y * region_mask->stride;
        guchar* masked_row = masked_data + y * masked_stride;

        for (gint x = 0; x < width; x++) {
            gint mask_x = x - mask_offset_x;

            if (mask_x < 0 || mask_x >= region_mask->width) {
                /* Outside mask region, clear pixel */
                masked_row[x * 4 + 0] = 0; /* B */
                masked_row[x * 4 + 1] = 0; /* G */
                masked_row[x * 4 + 2] = 0; /* R */
                masked_row[x * 4 + 3] = 0; /* A */
            } else {
                uint8_t mask_alpha = mask_row[mask_x];
                if (mask_alpha == 0) {
                    /* Outside selection, clear pixel */
                    masked_row[x * 4 + 0] = 0; /* B */
                    masked_row[x * 4 + 1] = 0; /* G */
                    masked_row[x * 4 + 2] = 0; /* R */
                    masked_row[x * 4 + 3] = 0; /* A */
                } else if (mask_alpha < 255) {
                    /* Feather zone: reduce alpha based on mask */
                    guchar* pixel = masked_row + x * 4;
                    uint8_t pixel_alpha = pixel[3];
                    uint8_t new_alpha = (uint8_t)((pixel_alpha * mask_alpha) / 255);

                    if (new_alpha == 0) {
                        pixel[0] = 0;
                        pixel[1] = 0;
                        pixel[2] = 0;
                        pixel[3] = 0;
                    } else if (pixel_alpha > 0) {
                        /* Un-premultiply, adjust alpha, re-premultiply */
                        uint16_t r = (pixel[2] * 255 + pixel_alpha / 2) / pixel_alpha;
                        uint16_t g = (pixel[1] * 255 + pixel_alpha / 2) / pixel_alpha;
                        uint16_t b = (pixel[0] * 255 + pixel_alpha / 2) / pixel_alpha;

                        if (r > 255)
                            r = 255;
                        if (g > 255)
                            g = 255;
                        if (b > 255)
                            b = 255;

                        pixel[0] = (b * new_alpha + 127) / 255; /* B */
                        pixel[1] = (g * new_alpha + 127) / 255; /* G */
                        pixel[2] = (r * new_alpha + 127) / 255; /* R */
                        pixel[3] = new_alpha;                   /* A */
                    }
                }
                /* If mask_alpha == 255, keep pixel as-is (fully inside selection) */
            }
        }
    }

    /* Mark surface as dirty */
    cairo_surface_mark_dirty(masked_surface);

    /* Free selection mask */
    selection_mask_free(region_mask);

    return masked_surface;
}
