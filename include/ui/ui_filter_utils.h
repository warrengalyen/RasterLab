/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef UI_FILTER_UTILS_H
#define UI_FILTER_UTILS_H

#include "document.h"
#include "render/layer.h"
#include "ui.h"
#include "ui/widgets/filter_dialog.h"
#include <gtk/gtk.h>

/**
 * Scale multiple UI values to filter range
 * @param ui_values Array of UI values to scale
 * @param filter_values Output array for scaled values (must be pre-allocated)
 * @param controls Array of control parameters
 * @param num_values Number of values to scale
 * @return TRUE if all values were scaled successfully, FALSE otherwise
 */
gboolean ui_filter_utils_scale_values(const gdouble* ui_values,
                                      gfloat* filter_values,
                                      const FilterControlParam* controls,
                                      gint num_values);

/**
 * Generic preview update callback for filters that need value scaling
 * This creates a preview callback that scales UI values and sets up viewport filter
 * @param dialog The filter dialog
 * @param values Array of current control values
 * @param num_values Number of values
 * @param user_data Pointer to FilterApplyFuncData structure
 * @return TRUE if preview was updated successfully, FALSE otherwise
 */
gboolean ui_filter_utils_preview_update_scaled(FilterDialog* dialog,
                                               const gdouble* values,
                                               gint num_values,
                                               gpointer user_data);

/**
 * Structure to pass filter function and metadata to preview callback
 */
typedef struct {
    gboolean (*filter_apply_func)(ImageLayer* layer, const gfloat* values, gint num_values);
    gint num_values;
} FilterApplyFuncData;

/**
 * Create a temporary layer copy for preview
 * @param source_layer The source layer to copy
 * @return New temporary layer, or NULL on failure
 */
ImageLayer* ui_filter_utils_create_temp_layer(ImageLayer* source_layer);

/**
 * Copy layer surface to another layer
 * @param dest_layer Destination layer
 * @param source_layer Source layer
 * @return TRUE on success, FALSE otherwise
 */
gboolean ui_filter_utils_copy_layer_surface(ImageLayer* dest_layer, ImageLayer* source_layer);

/**
 * Set up viewport-based filter for preview
 * @param dialog The filter dialog
 * @param filter_func Filter function to apply
 * @param filter_values Array of filter parameter values
 * @param num_values Number of values
 */
void ui_filter_utils_setup_viewport_filter(FilterDialog* dialog,
                                           gboolean (*filter_func)(ImageLayer*, const gfloat*, gint),
                                           const gfloat* filter_values,
                                           gint num_values);

/**
 * Apply filter with command, timing, and document updates
 * @param ctx Application context
 * @param layer Layer to apply filter to
 * @param filter_func Filter function to apply
 * @param filter_name Name of the filter for command history
 * @param values Filter parameter values (can be NULL if filter_func doesn't need them)
 * @param num_values Number of values (0 if values is NULL)
 * @return TRUE on success, FALSE otherwise
 */
gboolean ui_filter_utils_apply_with_command(AppContext* ctx,
                                            ImageLayer* layer,
                                            gboolean (*filter_func)(ImageLayer* layer, const gfloat* values, gint num_values),
                                            const gchar* filter_name,
                                            const gfloat* values,
                                            gint num_values);

#endif /* UI_FILTER_UTILS_H */
