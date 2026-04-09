/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FILTER_UTILS_H
#define FILTER_UTILS_H

#include "render/layer.h"
#include <cairo.h>
#include <glib.h>

/**
 * Structure to hold RGB buffers for filter processing
 */
typedef struct {
    guchar* rgb_input;
    guchar* rgb_output;
    gint width;
    gint height;
    gint stride;
} FilterRGBBuffers;

/**
 * Structure to hold RGBA buffers for filter processing
 */
typedef struct {
    guchar* rgba_input;
    guchar* rgba_output;
    gint width;
    gint height;
    gint stride;
} FilterRGBABuffers;

/**
 * Allocate and initialize RGB buffers for filter processing
 * @param surface The Cairo surface to process
 * @param buffers Output structure to hold allocated buffers
 * @param filter_name Name of the filter (for error messages)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_allocate_rgb_buffers(cairo_surface_t* surface,
                                           FilterRGBBuffers* buffers,
                                           const gchar* filter_name);

/**
 * Free RGB buffers allocated by filter_utils_allocate_rgb_buffers
 * @param buffers The buffers structure to free
 */
void filter_utils_free_rgb_buffers(FilterRGBBuffers* buffers);

/**
 * Allocate and initialize RGBA buffers for filter processing
 * @param surface The Cairo surface to process
 * @param buffers Output structure to hold allocated buffers
 * @param filter_name Name of the filter (for error messages)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_allocate_rgba_buffers(cairo_surface_t* surface,
                                            FilterRGBABuffers* buffers,
                                            const gchar* filter_name);

/**
 * Free RGBA buffers allocated by filter_utils_allocate_rgba_buffers
 * @param buffers The buffers structure to free
 */
void filter_utils_free_rgba_buffers(FilterRGBABuffers* buffers);

/**
 * Convert Cairo surface to RGB and prepare buffers for filter processing
 * @param surface The Cairo surface to convert
 * @param buffers Pre-allocated RGB buffers
 * @param filter_name Name of the filter (for error messages)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_cairo_to_rgb(cairo_surface_t* surface,
                                   FilterRGBBuffers* buffers,
                                   const gchar* filter_name);

/**
 * Convert RGB buffer back to Cairo surface after filter processing
 * @param surface The Cairo surface to update
 * @param buffers RGB buffers containing processed data
 * @param filter_name Name of the filter (for error messages)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_rgb_to_cairo(cairo_surface_t* surface,
                                   FilterRGBBuffers* buffers,
                                   const gchar* filter_name);

/**
 * Convert Cairo surface to RGBA and prepare buffers for filter processing
 * @param surface The Cairo surface to convert
 * @param buffers Pre-allocated RGBA buffers
 * @param filter_name Name of the filter (for error messages)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_cairo_to_rgba(cairo_surface_t* surface,
                                    FilterRGBABuffers* buffers,
                                    const gchar* filter_name);

/**
 * Convert RGBA buffer back to Cairo surface after filter processing
 * @param surface The Cairo surface to update
 * @param buffers RGBA buffers containing processed data
 * @param filter_name Name of the filter (for error messages)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_rgba_to_cairo(cairo_surface_t* surface,
                                    FilterRGBABuffers* buffers,
                                    const gchar* filter_name);

/**
 * Apply selection masking to filter results
 * Blends filtered surface with original surface based on selection mask
 * @param filtered_surface The filtered Cairo surface (will be modified)
 * @param original_surface The original Cairo surface before filtering
 * @param doc The document containing the selection mask
 * @param layer The layer being filtered (for coordinate calculation)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean filter_utils_apply_selection_mask(cairo_surface_t* filtered_surface,
                                           cairo_surface_t* original_surface,
                                           struct ImageDocument* doc,
                                           struct ImageLayer* layer);

/**
 * Create a masked surface showing only selected pixels
 * Creates a copy of the layer surface with pixels outside selection cleared
 * @param layer_surface The original layer surface
 * @param doc The document containing the selection mask
 * @param layer The layer being filtered (for coordinate calculation)
 * @return New Cairo surface with only selected pixels visible, or NULL on error
 *         Caller must free with cairo_surface_destroy()
 */
cairo_surface_t* filter_utils_create_masked_preview_surface(cairo_surface_t* layer_surface,
                                                            struct ImageDocument* doc,
                                                            struct ImageLayer* layer);

#endif /* FILTER_UTILS_H */
