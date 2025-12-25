#ifndef RENDER_UTILS_H
#define RENDER_UTILS_H

#include "render/dirty.h"
#include <cairo/cairo.h>
#include <gtk/gtk.h>
#include <stdint.h>

/* Forward declaration */
typedef struct SelectionMask SelectionMask;

/**
 * Unified rendering context for dirty rectangle optimization
 * All rendering functions should optionally accept this to clip operations
 */
typedef struct {
    cairo_t* cr;             /* Cairo context for drawing */
    DirtyRect dirty_rect;    /* Current dirty rectangle region */
    gboolean use_dirty_rect; /* Whether to use dirty rectangle clipping */
} RenderContext;

/**
 * Clip Cairo context to dirty rectangle
 * @param ctx Render context
 */
void render_clip_to_dirty(RenderContext* ctx);

/**
 * Convert GdkPixbuf to Cairo image surface
 * @param pixbuf The GdkPixbuf to convert
 * @return A new Cairo surface, or NULL on error. Caller must call cairo_surface_destroy().
 */
cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf* pixbuf);

/**
 * Convert Cairo image surface to GdkPixbuf
 * @param surface The Cairo surface to convert
 * @param keep_alpha Whether to preserve the alpha channel
 * @return A new GdkPixbuf, or NULL on error. Caller must call g_object_unref().
 */
GdkPixbuf* cairo_surface_to_pixbuf(cairo_surface_t* surface, gboolean keep_alpha);

/**
 * Draw a checkered background pattern for transparency visualization
 * @param cr The Cairo context to draw on
 * @param image_width Width of the area to fill
 * @param image_height Height of the area to fill
 */
void draw_checkered_background(cairo_t* cr, gint image_width, gint image_height);

/**
 * Draw a checkered background pattern starting from a specific offset
 * This is useful when drawing only a portion of the canvas (e.g., when zoomed/scrolled)
 * @param cr The Cairo context to draw on
 * @param offset_x X offset in document coordinates where drawing starts
 * @param offset_y Y offset in document coordinates where drawing starts
 * @param image_width Width of the area to draw
 * @param image_height Height of the area to draw
 */
void draw_checkered_background_offset(cairo_t* cr, gint offset_x, gint offset_y, gint image_width, gint image_height);

/**
 * Apply a selection mask to a Cairo surface by multiplying alpha values
 * Modifies the surface in-place: each pixel's alpha is multiplied by the corresponding mask value
 * @param surface The Cairo surface to mask (must be ARGB32 format)
 * @param mask The selection mask (8-bit alpha values)
 * @param mask_x X offset of mask in surface coordinates
 * @param mask_y Y offset of mask in surface coordinates
 * @param mask_width Width of mask region
 * @param mask_height Height of mask region
 * @param mask_stride Stride of mask buffer (bytes per row)
 */
void render_utils_apply_selection_mask(
    cairo_surface_t* surface,
    const uint8_t* mask,
    gint mask_x,
    gint mask_y,
    gint mask_width,
    gint mask_height,
    gint mask_stride);

/**
 * Apply selection mask to eraser result: restore original pixels outside selection
 * For eraser tool: pixels erased outside selection are restored from original surface
 * @param erased_surface The surface with erased pixels (temp surface, ARGB32)
 * @param original_surface The original layer surface to restore from (ARGB32)
 * @param mask The selection mask (8-bit alpha values)
 * @param mask_x X offset of mask in erased_surface coordinates
 * @param mask_y Y offset of mask in erased_surface coordinates
 * @param mask_width Width of mask region
 * @param mask_height Height of mask region
 * @param mask_stride Stride of mask buffer (bytes per row)
 * @param original_x X offset in original_surface where erased_surface starts
 * @param original_y Y offset in original_surface where erased_surface starts
 */
void render_utils_apply_selection_mask_to_eraser(
    cairo_surface_t* erased_surface,
    cairo_surface_t* original_surface,
    const uint8_t* mask,
    gint mask_x,
    gint mask_y,
    gint mask_width,
    gint mask_height,
    gint mask_stride,
    gint original_x,
    gint original_y);

/**
 * TEMPORARY: Visualize selection mask as a semi-transparent overlay
 * Draws the mask as a red overlay (white = fully selected, transparent = not selected)
 * This is for debugging feathered selection masks
 * @param cr Cairo context to draw on
 * @param mask The selection mask to visualize
 */
void render_utils_visualize_selection_mask(cairo_t* cr, SelectionMask* mask);

#endif /* RENDER_UTILS_H */
