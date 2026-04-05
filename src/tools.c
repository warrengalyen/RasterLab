#include "tools.h"
#include <cairo.h>
#include <gdk/gdk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * Create a new tool
 */
Tool* tool_new(const gchar* name, ToolType type, GdkCursorType cursor_type, ToolOptionFlags options) {
    Tool* tool;
    GdkDisplay* display;

    if (!name) {
        return NULL;
    }

    tool = (Tool*)g_malloc(sizeof(Tool));
    tool->name = g_strdup(name);
    tool->type = type;
    tool->user_data = NULL;
    tool->app_context = NULL;
    tool->options = options;

    /* Create cursor */
    display = gdk_display_get_default();
    if (display) {
        tool->cursor = gdk_cursor_new_for_display(display, cursor_type);
        if (!tool->cursor) {
            tool->cursor = gdk_cursor_new_for_display(display, GDK_ARROW);
        }
    } else {
        tool->cursor = NULL;
    }

    /* Set default handlers to NULL (will be assigned per tool) */
    tool->mouse_down = NULL;
    tool->mouse_move = NULL;
    tool->mouse_up = NULL;

    return tool;
}

/**
 * Free a tool
 */
void tool_free(Tool* tool) {
    if (!tool) {
        return;
    }

    /* Free name if it exists and appears valid */
    if (tool->name) {
        g_free(tool->name);
        tool->name = NULL; /* Prevent double-free */
    }

    if (tool->cursor) {
        g_object_unref(tool->cursor);
        tool->cursor = NULL; /* Prevent double-unref */
    }

    g_free(tool);
}

/* Max cursor bitmap dimension (GDK/display limits; avoids huge surfaces at extreme zoom) */
#define TOOL_BRUSH_CURSOR_MAX_PX 512

/**
 * Create a custom brush cursor based on brush size (image space) and zoom.
 * Returns a crosshair for small on-screen sizes, otherwise a double-ringed circle.
 */
GdkCursor* tool_create_brush_cursor(gfloat brush_size, gdouble zoom_factor) {
    GdkDisplay* display;
    GdkCursor* cursor;
    gdouble display_diameter;

    display = gdk_display_get_default();
    if (!display) {
        return NULL;
    }

    if (zoom_factor <= 0.0 || zoom_factor > 64.0) {
        zoom_factor = 1.0;
    }

    display_diameter = (gdouble)brush_size * zoom_factor;

    /* Use crosshair when the brush would be tiny on screen */
    if (display_diameter < 7.0) {
        cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
        return cursor;
    }

    /* Create custom cursor for larger on-screen sizes */
    if (display_diameter > (gdouble)TOOL_BRUSH_CURSOR_MAX_PX) {
        display_diameter = (gdouble)TOOL_BRUSH_CURSOR_MAX_PX;
    }
    gint size = (gint)(display_diameter + 0.5);
    gint cursor_size = size + 8; /* Add padding around the circle */
    gint center = cursor_size / 2;

    /* Create Cairo surface for cursor */
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cursor_size, cursor_size);
    if (!surface) {
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    cairo_t* cr = cairo_create(surface);
    if (!cr) {
        cairo_surface_destroy(surface);
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Draw double-ringed circle */
    gdouble outer_radius = size / 2.0;
    gdouble ring_width = 1.0; /* Width of each ring */

    /* Set color to dark gray (almost black) */
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width(cr, ring_width);

    /* Draw outer ring */
    cairo_arc(cr, center, center, outer_radius, 0, 2 * M_PI);
    cairo_stroke(cr);

    /* Draw inner ring */
    cairo_arc(cr, center, center, outer_radius - ring_width * 1.5, 0, 2 * M_PI);
    cairo_stroke(cr);

    /* Flush Cairo operations */
    cairo_surface_flush(surface);

    /* Convert Cairo ARGB32 (BGRA in memory) to GdkPixbuf RGBA */
    gint width = cairo_image_surface_get_width(surface);
    gint height = cairo_image_surface_get_height(surface);
    gint stride = cairo_image_surface_get_stride(surface);
    guchar* surface_data = cairo_image_surface_get_data(surface);

    /* Create pixbuf with RGBA format */
    GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
    if (!pixbuf) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    guchar* pixbuf_data = gdk_pixbuf_get_pixels(pixbuf);
    gint pixbuf_rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    /* Convert BGRA (Cairo) to RGBA (Pixbuf) */
    for (gint y = 0; y < height; y++) {
        guchar* src = surface_data + y * stride;
        guchar* dst = pixbuf_data + y * pixbuf_rowstride;

        for (gint x = 0; x < width; x++) {
            /* Cairo ARGB32: BGRA in memory */
            guchar b = src[x * 4 + 0];
            guchar g = src[x * 4 + 1];
            guchar r = src[x * 4 + 2];
            guchar a = src[x * 4 + 3];

            /* Un-premultiply alpha if needed */
            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            }

            /* Write RGBA to pixbuf */
            dst[x * 4 + 0] = r;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = b;
            dst[x * 4 + 3] = a;
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    /* Create cursor from pixbuf with hotspot at center */
    cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, center, center);

    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    return cursor;
}

/**
 * Update a tool's cursor (for brush/eraser tools that need dynamic cursors)
 */
void tool_update_cursor(Tool* tool, gfloat brush_size, gdouble zoom_factor) {
    GdkDisplay* display;
    GdkCursor* new_cursor;

    if (!tool) {
        return;
    }

    display = gdk_display_get_default();
    if (!display) {
        return;
    }

    /* Create new cursor based on brush size and zoom */
    new_cursor = tool_create_brush_cursor(brush_size, zoom_factor);
    if (!new_cursor) {
        return;
    }

    /* Unref old cursor */
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }

    /* Set new cursor */
    tool->cursor = new_cursor;
}
