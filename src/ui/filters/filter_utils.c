#include "ui/filters/filter_utils.h"
#include "filters.h"
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
