#include "ui/widgets/filter_preview.h"
#include "document.h"
#include "i18n.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "selection/selection_render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Private structure for FilterPreview widget
 */
struct _FilterPreview {
    GtkBox parent;

    /* Full resolution surfaces (original data) */
    cairo_surface_t* before_surface_full;
    cairo_surface_t* after_surface_full;

    /* Context for selection-aware preview rendering (not owned) */
    struct ImageDocument* doc;
    struct ImageLayer* layer;

    /* Viewport cache - only the visible portion with filter applied */
    cairo_surface_t* viewport_cache;
    gint cache_x, cache_y; /* Position of cached viewport in image coords */
    gint cache_width, cache_height;

    gint original_width;
    gint original_height;

    /* Display state */
    FilterPreviewMode mode;
    FilterPreviewView view;

    /* Panning state (for 1:1 mode) */
    gdouble pan_x;
    gdouble pan_y;
    gboolean is_dragging;
    gdouble drag_start_x;
    gdouble drag_start_y;
    gdouble drag_start_pan_x;
    gdouble drag_start_pan_y;

    /* Filter state */
    gpointer filter_params;
    FilterApplyFunc filter_apply_func;

    /* Async update handling */
    guint update_timeout_id;
    gboolean needs_update;
    GThread* filter_thread;
    GMutex cache_mutex;
    guint update_sequence; /* Sequence number to track latest update */
    gboolean force_update; /* Force update even if viewport hasn't changed */

    /* Control buttons */
    GtkWidget* before_button;
    GtkWidget* after_button;
    GtkWidget* fit_button;
    GtkWidget* one_to_one_button;

    /* Container widgets */
    GtkWidget* main_box;
    GtkWidget* preview_area;
    GtkWidget* controls_box;

    /* Flag to prevent recursive signal handling */
    gboolean updating_buttons;

    /* Property: allow zoom/pan (1:1 mode) */
    gboolean allow_zoom_pan;
};

G_DEFINE_TYPE(FilterPreview, filter_preview, GTK_TYPE_BOX)

/* Property IDs */
enum {
    PROP_0,
    PROP_ALLOW_ZOOM_PAN,
    N_PROPERTIES
};

/* Forward declarations for button click handlers */
static void on_before_clicked(GtkWidget* widget, gpointer user_data);
static void on_after_clicked(GtkWidget* widget, gpointer user_data);
static void on_fit_clicked(GtkWidget* widget, gpointer user_data);
static void on_one_to_one_clicked(GtkWidget* widget, gpointer user_data);

/* Forward declarations for viewport update functions */
static void request_viewport_update(FilterPreview* preview);
static gdouble calculate_fit_scale(gint img_width, gint img_height,
                                   gint container_width, gint container_height);
static gboolean on_preview_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data);
static void on_preview_realize(GtkWidget* widget, gpointer user_data);
static gboolean trigger_viewport_update_idle(gpointer user_data);

static void maybe_update_context_from_toplevel(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(preview));
    if (!toplevel || !GTK_IS_WINDOW(toplevel)) {
        return;
    }

    /* Many filter dialogs store these on the dialog window */
    struct ImageDocument* doc =
        (struct ImageDocument*)g_object_get_data(G_OBJECT(toplevel), "filter_doc");
    struct ImageLayer* layer =
        (struct ImageLayer*)g_object_get_data(G_OBJECT(toplevel), "original_layer");

    preview->doc = doc;
    preview->layer = layer;
}

static gboolean selection_active_for_preview(FilterPreview* preview) {
    if (!preview || !preview->doc || !preview->layer) {
        return FALSE;
    }

    if (!preview->doc->selection_mask) {
        return FALSE;
    }

    return !selection_mask_is_empty(preview->doc->selection_mask);
}

/* For temporary/region masks (e.g. returned by selection_build_combined_mask),
 * the authoritative data may be in `mask->data` even if `base_mask` is empty. */
static gboolean selection_mask_data_is_empty(SelectionMask* mask) {
    if (!mask || !mask->data) {
        return TRUE;
    }

    for (int y = 0; y < mask->height; y++) {
        uint8_t* row = mask->data + y * mask->stride;
        for (int x = 0; x < mask->width; x++) {
            if (row[x] != 0) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/* Returns selection bounds in LAYER coordinates (relative to layer surface origin). */
static gboolean get_selection_bounds_layer(FilterPreview* preview,
                                           gint* out_x,
                                           gint* out_y,
                                           gint* out_width,
                                           gint* out_height) {
    if (!preview || !out_x || !out_y || !out_width || !out_height) {
        return FALSE;
    }

    *out_x = 0;
    *out_y = 0;
    *out_width = 0;
    *out_height = 0;

    if (!selection_active_for_preview(preview)) {
        return FALSE;
    }

    if (preview->original_width <= 0 || preview->original_height <= 0) {
        return FALSE;
    }

    DirtyRect layer_rect_doc;
    dirty_rect_set(&layer_rect_doc,
                   preview->layer->offset_x,
                   preview->layer->offset_y,
                   preview->original_width,
                   preview->original_height);

    DirtyRect actual_region_doc;
    SelectionMask* region_mask = selection_build_combined_mask(
        preview->doc->selection_mask,
        &layer_rect_doc,
        FEATHER_QUALITY_NORMAL,
        &actual_region_doc);

    if (!region_mask || !region_mask->data) {
        selection_mask_free(region_mask);
        return FALSE;
    }

    gint sel_x_min_doc = actual_region_doc.x + actual_region_doc.width;
    gint sel_y_min_doc = actual_region_doc.y + actual_region_doc.height;
    gint sel_x_max_doc = actual_region_doc.x - 1;
    gint sel_y_max_doc = actual_region_doc.y - 1;

    for (gint y = 0; y < region_mask->height; y++) {
        const uint8_t* mask_row = region_mask->data + y * region_mask->stride;
        for (gint x = 0; x < region_mask->width; x++) {
            if (mask_row[x] > 0) {
                gint doc_x = actual_region_doc.x + x;
                gint doc_y = actual_region_doc.y + y;
                if (doc_x < sel_x_min_doc)
                    sel_x_min_doc = doc_x;
                if (doc_y < sel_y_min_doc)
                    sel_y_min_doc = doc_y;
                if (doc_x > sel_x_max_doc)
                    sel_x_max_doc = doc_x;
                if (doc_y > sel_y_max_doc)
                    sel_y_max_doc = doc_y;
            }
        }
    }

    selection_mask_free(region_mask);

    if (sel_x_max_doc < sel_x_min_doc || sel_y_max_doc < sel_y_min_doc) {
        return FALSE;
    }

    gint sel_x_min = sel_x_min_doc - preview->layer->offset_x;
    gint sel_y_min = sel_y_min_doc - preview->layer->offset_y;
    gint sel_x_max = sel_x_max_doc - preview->layer->offset_x;
    gint sel_y_max = sel_y_max_doc - preview->layer->offset_y;

    if (sel_x_min < 0)
        sel_x_min = 0;
    if (sel_y_min < 0)
        sel_y_min = 0;
    if (sel_x_max >= preview->original_width)
        sel_x_max = preview->original_width - 1;
    if (sel_y_max >= preview->original_height)
        sel_y_max = preview->original_height - 1;

    gint w = sel_x_max - sel_x_min + 1;
    gint h = sel_y_max - sel_y_min + 1;
    if (w <= 0 || h <= 0) {
        return FALSE;
    }

    *out_x = sel_x_min;
    *out_y = sel_y_min;
    *out_width = w;
    *out_height = h;
    return TRUE;
}

static cairo_surface_t* create_selection_cropped_masked_surface(FilterPreview* preview,
                                                                cairo_surface_t* full_surface) {
    if (!preview || !full_surface) {
        return NULL;
    }

    if (!selection_active_for_preview(preview)) {
        return cairo_surface_reference(full_surface);
    }

    gint full_w = cairo_image_surface_get_width(full_surface);
    gint full_h = cairo_image_surface_get_height(full_surface);
    if (full_w <= 0 || full_h <= 0) {
        return NULL;
    }

    /* Build selection mask for the entire layer area (document coordinates) */
    DirtyRect layer_rect_doc;
    dirty_rect_set(&layer_rect_doc, preview->layer->offset_x, preview->layer->offset_y, full_w, full_h);

    DirtyRect actual_region_doc;
    SelectionMask* region_mask = selection_build_combined_mask(
        preview->doc->selection_mask, &layer_rect_doc, FEATHER_QUALITY_NORMAL, &actual_region_doc);

    if (!region_mask || !region_mask->data) {
        selection_mask_free(region_mask);
        return cairo_surface_reference(full_surface);
    }

    /* Find bounds of selected pixels within the layer */
    gint sel_x_min_doc = actual_region_doc.x + actual_region_doc.width;
    gint sel_y_min_doc = actual_region_doc.y + actual_region_doc.height;
    gint sel_x_max_doc = actual_region_doc.x - 1;
    gint sel_y_max_doc = actual_region_doc.y - 1;

    for (gint y = 0; y < region_mask->height; y++) {
        const uint8_t* mask_row = region_mask->data + y * region_mask->stride;
        for (gint x = 0; x < region_mask->width; x++) {
            if (mask_row[x] > 0) {
                gint doc_x = actual_region_doc.x + x;
                gint doc_y = actual_region_doc.y + y;
                if (doc_x < sel_x_min_doc)
                    sel_x_min_doc = doc_x;
                if (doc_y < sel_y_min_doc)
                    sel_y_min_doc = doc_y;
                if (doc_x > sel_x_max_doc)
                    sel_x_max_doc = doc_x;
                if (doc_y > sel_y_max_doc)
                    sel_y_max_doc = doc_y;
            }
        }
    }

    if (sel_x_max_doc < sel_x_min_doc || sel_y_max_doc < sel_y_min_doc) {
        /* Selection exists, but not on this layer region */
        selection_mask_free(region_mask);
        return NULL;
    }

    gint sel_x_min = sel_x_min_doc - preview->layer->offset_x;
    gint sel_y_min = sel_y_min_doc - preview->layer->offset_y;
    gint sel_x_max = sel_x_max_doc - preview->layer->offset_x;
    gint sel_y_max = sel_y_max_doc - preview->layer->offset_y;

    /* Clamp to layer bounds */
    if (sel_x_min < 0)
        sel_x_min = 0;
    if (sel_y_min < 0)
        sel_y_min = 0;
    if (sel_x_max >= full_w)
        sel_x_max = full_w - 1;
    if (sel_y_max >= full_h)
        sel_y_max = full_h - 1;

    gint crop_w = sel_x_max - sel_x_min + 1;
    gint crop_h = sel_y_max - sel_y_min + 1;
    if (crop_w <= 0 || crop_h <= 0) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Build a mask surface for just the crop region for efficiency */
    DirtyRect crop_rect_doc;
    dirty_rect_set(&crop_rect_doc,
                   preview->layer->offset_x + sel_x_min,
                   preview->layer->offset_y + sel_y_min,
                   crop_w, crop_h);

    DirtyRect crop_actual_doc;
    SelectionMask* crop_mask = selection_build_combined_mask(
        preview->doc->selection_mask, &crop_rect_doc, FEATHER_QUALITY_NORMAL, &crop_actual_doc);

    /* We no longer need the full-layer region mask */
    selection_mask_free(region_mask);

    if (!crop_mask) {
        return NULL;
    }

    cairo_surface_t* mask_surface = selection_mask_get_surface(crop_mask);

    cairo_surface_t* out = cairo_image_surface_create(
        cairo_image_surface_get_format(full_surface), crop_w, crop_h);
    if (!out || cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
        if (out) {
            cairo_surface_destroy(out);
        }
        selection_mask_free(crop_mask);
        return NULL;
    }

    cairo_t* cr = cairo_create(out);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Paint the crop region from the full surface through the selection mask */
    cairo_set_source_surface(cr, full_surface, -sel_x_min, -sel_y_min);

    if (mask_surface) {
        /* Align the (potentially smaller) actual region mask within the crop */
        gdouble mask_x = (gdouble)(crop_actual_doc.x - crop_rect_doc.x);
        gdouble mask_y = (gdouble)(crop_actual_doc.y - crop_rect_doc.y);
        cairo_mask_surface(cr, mask_surface, mask_x, mask_y);
    } else {
        /* Fallback: no mask surface available, just paint crop */
        cairo_paint(cr);
    }

    cairo_destroy(cr);
    selection_mask_free(crop_mask);

    return out;
}

/**
 * Paint a surface through the current CTM at (0,0), optionally masked by the document selection.
 *
 * @param layer_origin_x Layer-space X coordinate of the surface's (0,0) in the original layer
 * @param layer_origin_y Layer-space Y coordinate of the surface's (0,0) in the original layer
 */
static void paint_surface_with_optional_selection_mask(FilterPreview* preview,
                                                       cairo_t* cr,
                                                       cairo_surface_t* surface,
                                                       gint layer_origin_x,
                                                       gint layer_origin_y) {
    if (!preview || !cr || !surface) {
        return;
    }

    cairo_set_source_surface(cr, surface, 0, 0);

    if (!selection_active_for_preview(preview)) {
        cairo_paint(cr);
        return;
    }

    gint width = cairo_image_surface_get_width(surface);
    gint height = cairo_image_surface_get_height(surface);
    if (width <= 0 || height <= 0) {
        return;
    }

    /* Region in document coordinates covered by this surface */
    DirtyRect region_doc;
    dirty_rect_set(&region_doc,
                   preview->layer->offset_x + layer_origin_x,
                   preview->layer->offset_y + layer_origin_y,
                   width, height);

    DirtyRect actual_region_doc;
    SelectionMask* region_mask = selection_build_combined_mask(
        preview->doc->selection_mask,
        &region_doc,
        FEATHER_QUALITY_NORMAL,
        &actual_region_doc);

    if (!region_mask) {
        /* Selection exists, but nothing selected in this region: paint nothing (transparent). */
        return;
    }

    if (selection_mask_data_is_empty(region_mask)) {
        selection_mask_free(region_mask);
        return;
    }

    cairo_surface_t* mask_surface = selection_mask_get_surface(region_mask);
    if (!mask_surface) {
        selection_mask_free(region_mask);
        return;
    }

    /* Align mask surface within the drawn surface */
    gdouble mask_x = (gdouble)(actual_region_doc.x - region_doc.x);
    gdouble mask_y = (gdouble)(actual_region_doc.y - region_doc.y);

    cairo_mask_surface(cr, mask_surface, mask_x, mask_y);

    selection_mask_free(region_mask);
}

/**
 * Calculate the viewport rectangle in image coordinates
 */
typedef struct {
    gint x, y;
    gint width, height;
} ViewportRect;

/**
 * Data for filter thread
 */
typedef struct {
    FilterPreview* preview;
    cairo_surface_t* input_viewport;
    ViewportRect viewport;
    gpointer filter_params;
    guint sequence; /* Sequence number for this update */
} FilterThreadData;

/* For 1:1 mode -  Render extra pixels around the viewport.
   Helps reduce visible updates when panning.
 */
const gint VIEWPORT_CACHE_PADDING = 50;

static ViewportRect calculate_viewport(FilterPreview* preview) {
    ViewportRect viewport = {0, 0, 0, 0};

    if (!preview || !preview->preview_area) {
        return viewport;
    }

    gint widget_width = gtk_widget_get_allocated_width(preview->preview_area);
    gint widget_height = gtk_widget_get_allocated_height(preview->preview_area);

    /* If widget isn't allocated yet, use size request as fallback */
    if (widget_width <= 0 || widget_height <= 0) {
        gtk_widget_get_size_request(preview->preview_area, &widget_width, &widget_height);
        if (widget_width <= 0 || widget_height <= 0) {
            /* Default size if no size request set */
            widget_width = 375;
            widget_height = 338;
        }
    }

    /* If zoom/pan is disabled, use scaled-down dimensions for performance */
    if (!preview->allow_zoom_pan) {
        /* Ensure we have valid original dimensions */
        if (preview->original_width > 0 && preview->original_height > 0) {
            /* Calculate scaled dimensions that fit in the widget */
            gdouble scale = calculate_fit_scale(preview->original_width, preview->original_height,
                                                widget_width, widget_height);
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = (gint)(preview->original_width * scale);
            viewport.height = (gint)(preview->original_height * scale);

            /* Ensure minimum dimensions */
            if (viewport.width <= 0)
                viewport.width = widget_width;
            if (viewport.height <= 0)
                viewport.height = widget_height;
        } else {
            /* Fallback to widget size if original dimensions not set yet */
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = widget_width;
            viewport.height = widget_height;
        }
    } else if (preview->mode == FILTER_PREVIEW_MODE_FIT) {
        /* In fit mode, entire image is visible */
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = preview->original_width;
        viewport.height = preview->original_height;
    } else {
        /* In 1:1 mode, calculate visible rectangle with some padding.
         * If a selection is active, treat the selection bounds as the "virtual image" space,
         * then convert back into full-image coordinates for extraction/caching. */
        gint virtual_origin_x = 0;
        gint virtual_origin_y = 0;
        gint virtual_width = preview->original_width;
        gint virtual_height = preview->original_height;

        if (selection_active_for_preview(preview)) {
            gint sel_x, sel_y, sel_w, sel_h;
            if (get_selection_bounds_layer(preview, &sel_x, &sel_y, &sel_w, &sel_h)) {
                virtual_origin_x = sel_x;
                virtual_origin_y = sel_y;
                virtual_width = sel_w;
                virtual_height = sel_h;
            }
        }

        /* Viewport in VIRTUAL coords */
        gint vx = (gint)preview->pan_x - VIEWPORT_CACHE_PADDING;
        gint vy = (gint)preview->pan_y - VIEWPORT_CACHE_PADDING;
        gint vw = widget_width + (VIEWPORT_CACHE_PADDING * 2);
        gint vh = widget_height + (VIEWPORT_CACHE_PADDING * 2);

        /* Clamp to virtual bounds */
        if (vx < 0) {
            vw += vx;
            vx = 0;
        }
        if (vy < 0) {
            vh += vy;
            vy = 0;
        }

        if (vx + vw > virtual_width) {
            vw = virtual_width - vx;
        }
        if (vy + vh > virtual_height) {
            vh = virtual_height - vy;
        }

        if (vw < 0)
            vw = 0;
        if (vh < 0)
            vh = 0;

        /* Convert to FULL image coordinates */
        viewport.x = vx + virtual_origin_x;
        viewport.y = vy + virtual_origin_y;
        viewport.width = vw;
        viewport.height = vh;
    }

    return viewport;
}

/**
 * Check if viewport has changed significantly
 */
static gboolean viewport_changed(FilterPreview* preview,
                                 ViewportRect new_viewport) {
    /* Force update if flag is set (e.g., filter parameters changed) */
    if (preview->force_update) {
        preview->force_update = FALSE;
        return TRUE;
    }

    if (!preview->viewport_cache) {
        return TRUE;
    }

    /* Check if new viewport is outside cached area */
    if (new_viewport.x < preview->cache_x || new_viewport.y < preview->cache_y ||
        new_viewport.x + new_viewport.width >
            preview->cache_x + preview->cache_width ||
        new_viewport.y + new_viewport.height >
            preview->cache_y + preview->cache_height) {
        return TRUE;
    }

    return FALSE;
}

/**
 * Create a scaled-down version of the source surface
 */
static cairo_surface_t* scale_surface(cairo_surface_t* source,
                                      gint target_width, gint target_height) {
    if (!source || target_width <= 0 || target_height <= 0) {
        return NULL;
    }

    gint source_width = cairo_image_surface_get_width(source);
    gint source_height = cairo_image_surface_get_height(source);

    if (source_width <= 0 || source_height <= 0) {
        return NULL;
    }

    /* Create scaled surface */
    cairo_surface_t* scaled = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, target_width, target_height);

    if (cairo_surface_status(scaled) != CAIRO_STATUS_SUCCESS) {
        return NULL;
    }

    cairo_t* cr = cairo_create(scaled);

    /* Scale and copy the source */
    gdouble scale_x = (gdouble)target_width / (gdouble)source_width;
    gdouble scale_y = (gdouble)target_height / (gdouble)source_height;

    cairo_scale(cr, scale_x, scale_y);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);

    cairo_destroy(cr);

    return scaled;
}

static cairo_surface_t* extract_viewport(cairo_surface_t* source,
                                         ViewportRect viewport) {
    if (!source || viewport.width <= 0 || viewport.height <= 0) {
        return NULL;
    }

    /* Create new surface for viewport */
    cairo_surface_t* cropped = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, viewport.width, viewport.height);

    cairo_t* cr = cairo_create(cropped);

    /* Copy the viewport region */
    cairo_set_source_surface(cr, source, -viewport.x, -viewport.y);
    cairo_paint(cr);

    cairo_destroy(cr);

    return cropped;
}

/**
 * Apply filter in background thread
 */
static gpointer filter_thread_func(gpointer user_data) {
    FilterThreadData* data = (FilterThreadData*)user_data;
    FilterPreview* preview = data->preview;
    guint my_sequence = data->sequence;

    /* Apply filter to viewport (your actual filter function here) */
    cairo_surface_t* filtered = NULL;

    /* Apply filter using the callback if provided */
    if (preview && preview->filter_apply_func) {
        filtered = preview->filter_apply_func(
            data->input_viewport,
            data->filter_params);
    } else {
        /* No filter set, just copy the input */
        filtered = cairo_surface_reference(data->input_viewport);
    }

    /* Lock mutex and update cache - check if preview still exists and this is still the latest update */
    if (preview) {
        g_mutex_lock(&preview->cache_mutex);

        /* Only update if this is still the latest sequence (newer updates may have started) */
        if (my_sequence == preview->update_sequence) {
            if (preview->viewport_cache) {
                cairo_surface_destroy(preview->viewport_cache);
            }

            preview->viewport_cache = filtered;
            preview->cache_x = data->viewport.x;
            preview->cache_y = data->viewport.y;
            preview->cache_width = data->viewport.width;
            preview->cache_height = data->viewport.height;

            /* Trigger redraw on main thread - only if preview still exists */
            if (preview->preview_area) {
                g_idle_add((GSourceFunc)gtk_widget_queue_draw, preview->preview_area);
            }
        } else {
            /* This update is stale, discard the result */
            if (filtered) {
                cairo_surface_destroy(filtered);
            }
        }

        g_mutex_unlock(&preview->cache_mutex);
    } else {
        /* Preview was disposed, just free the filtered surface */
        if (filtered) {
            cairo_surface_destroy(filtered);
        }
    }

    cairo_surface_destroy(data->input_viewport);
    g_free(data);

    return NULL;
}

/**
 * Timeout callback for delayed update (after panning stops)
 */
static gboolean update_timeout_callback(gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    preview->update_timeout_id = 0;
    request_viewport_update(preview);

    return G_SOURCE_REMOVE;
}

/**
 * Schedule a viewport update with debouncing
 */
static void schedule_viewport_update(FilterPreview* preview, guint delay_ms) {
    if (!preview) {
        return;
    }

    /* Cancel existing timeout */
    if (preview->update_timeout_id > 0) {
        g_source_remove(preview->update_timeout_id);
    }

    /* Schedule new update */
    preview->update_timeout_id =
        g_timeout_add(delay_ms, update_timeout_callback, preview);
}

/**
 * Request viewport update (called after panning or mode change)
 */
static void request_viewport_update(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    /* Only update "after" view with filters */
    if (preview->view != FILTER_PREVIEW_VIEW_AFTER) {
        return;
    }

    /* Ensure we have a before surface to work with */
    if (!preview->before_surface_full) {
        return;
    }

    ViewportRect viewport = calculate_viewport(preview);

    /* Validate viewport dimensions */
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }

    /* Check if update is needed */
    if (!viewport_changed(preview, viewport)) {
        return;
    }

    /* Extract or scale viewport from full resolution image */
    cairo_surface_t* viewport_input = NULL;

    if (!preview->allow_zoom_pan) {
        /* When zoom/pan is disabled, use scaled-down version for performance */
        if (viewport.width > 0 && viewport.height > 0) {
            viewport_input = scale_surface(preview->before_surface_full,
                                           viewport.width, viewport.height);
        }
    } else {
        /* Extract viewport from full resolution image */
        viewport_input = extract_viewport(preview->before_surface_full, viewport);
    }

    if (!viewport_input) {
        return;
    }

    /* Increment sequence number for this update */
    preview->update_sequence++;

    /* Prepare thread data */
    FilterThreadData* data = g_malloc(sizeof(FilterThreadData));
    data->preview = preview;
    data->input_viewport = viewport_input;
    data->viewport = viewport;
    data->filter_params = preview->filter_params;
    data->sequence = preview->update_sequence;

    /* If there's an existing thread, we'll let it finish but ignore its result */
    /* Store the old thread reference if it exists */
    GThread* old_thread = preview->filter_thread;

    /* Launch filter in background thread */
    preview->filter_thread =
        g_thread_new("filter_thread", filter_thread_func, data);

    /* Unref the old thread if it exists (we don't need to wait for it) */
    if (old_thread) {
        g_thread_unref(old_thread);
    }
}

/**
 * Get current surface based on view mode
 */
static cairo_surface_t* get_current_surface(FilterPreview* preview) {
    if (!preview) {
        return NULL;
    }

    if (preview->view == FILTER_PREVIEW_VIEW_BEFORE) {
        return preview->before_surface_full;
    } else {
        return preview->after_surface_full;
    }
}

/**
 * Get image dimensions from surface
 */
static void get_surface_size(cairo_surface_t* surface, gint* width,
                             gint* height) {
    if (!surface || !width || !height) {
        *width = 0;
        *height = 0;
        return;
    }

    *width = cairo_image_surface_get_width(surface);
    *height = cairo_image_surface_get_height(surface);
}

/**
 * Calculate fit scale to fit image in container while maintaining aspect ratio
 */
static gdouble calculate_fit_scale(gint img_width, gint img_height,
                                   gint container_width,
                                   gint container_height) {
    if (img_width <= 0 || img_height <= 0 || container_width <= 0 ||
        container_height <= 0) {
        return 1.0;
    }

    gdouble scale_x = (gdouble)container_width / (gdouble)img_width;
    gdouble scale_y = (gdouble)container_height / (gdouble)img_height;

    return (scale_x < scale_y) ? scale_x : scale_y;
}

/**
 * Clamp pan position to keep image within bounds
 */
static void clamp_pan(FilterPreview* preview, gint container_width, gint container_height) {
    if (!preview) {
        return;
    }

    /* In 1:1 mode, use original dimensions for clamping */
    gint virtual_width = preview->original_width;
    gint virtual_height = preview->original_height;

    if (preview->mode == FILTER_PREVIEW_MODE_1TO1 && selection_active_for_preview(preview)) {
        gint sel_x, sel_y, sel_w, sel_h;
        if (get_selection_bounds_layer(preview, &sel_x, &sel_y, &sel_w, &sel_h)) {
            (void)sel_x;
            (void)sel_y;
            virtual_width = sel_w;
            virtual_height = sel_h;
        }
    }

    /* Clamp pan so image doesn't go outside container */
    gint max_pan_x = (virtual_width > container_width) ? (virtual_width - container_width) : 0;
    gint max_pan_y = (virtual_height > container_height) ? (virtual_height - container_height) : 0;

    if (preview->pan_x < 0) {
        preview->pan_x = 0;
    } else if (preview->pan_x > max_pan_x) {
        preview->pan_x = max_pan_x;
    }

    if (preview->pan_y < 0) {
        preview->pan_y = 0;
    } else if (preview->pan_y > max_pan_y) {
        preview->pan_y = max_pan_y;
    }
}

/**
 * Draw callback for preview area
 */
static gboolean on_preview_draw(GtkWidget* widget, cairo_t* cr,
                                gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);
    cairo_surface_t* surface;
    gboolean cache_locked = FALSE;
    gboolean using_viewport_cache = FALSE;
    gint widget_width, widget_height;
    gdouble scale;
    gdouble draw_x, draw_y;

    if (!preview) {
        return FALSE;
    }

    /* Try to pick up doc/layer context from the toplevel (for selection masking) */
    maybe_update_context_from_toplevel(preview);

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    /* Draw checkered background */
    draw_checkered_background(cr, widget_width, widget_height);

    /* Determine which surface to use */
    if (preview->view == FILTER_PREVIEW_VIEW_BEFORE) {
        surface = preview->before_surface_full;
    } else {
        /* Use cached viewport for after view if filter function is set */
        if (preview->filter_apply_func) {
            /* Async filter mode - use viewport cache */
            g_mutex_lock(&preview->cache_mutex);
            cache_locked = TRUE;
            surface = preview->viewport_cache;

            if (!surface) {
                /* No cache yet: fall back to drawing the BEFORE surface using the normal
                 * sizing/cropping logic (prevents fit-mode flicker when selection-cropped). */
                g_mutex_unlock(&preview->cache_mutex);
                cache_locked = FALSE;
                surface = preview->before_surface_full;
                using_viewport_cache = FALSE;
            } else {
                using_viewport_cache = TRUE;
            }
        } else {
            /* Direct surface mode - use after_surface_full directly */
            surface = preview->after_surface_full;
            if (!surface) {
                /* Fall back to before surface if after is not set */
                surface = preview->before_surface_full;
            }
        }
    }

    if (preview->mode == FILTER_PREVIEW_MODE_FIT) {
        /* Fit mode: scale to fit container */
        gint img_width, img_height;
        gboolean is_scaled_cache = FALSE;
        cairo_surface_t* selection_surface = NULL;

        if (preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->filter_apply_func && using_viewport_cache) {
            img_width = preview->cache_width;
            img_height = preview->cache_height;
            /* If zoom/pan is disabled, cache is already at scaled size, no need to scale again */
            is_scaled_cache = !preview->allow_zoom_pan;
        } else if (preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->after_surface_full) {
            get_surface_size(preview->after_surface_full, &img_width, &img_height);
        } else {
            img_width = preview->original_width;
            img_height = preview->original_height;
        }

        /* If a selection is active, zoom preview to the selection bounds (fit mode only).
         * Skip when cache is already scaled (no reliable coordinate mapping). */
        if (!is_scaled_cache && selection_active_for_preview(preview) && surface) {
            gboolean can_crop = TRUE;
            if (using_viewport_cache && preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->filter_apply_func) {
                /* Only safe if cache represents the full image (fit mode does this) */
                if (preview->cache_x != 0 || preview->cache_y != 0 ||
                    preview->cache_width != preview->original_width ||
                    preview->cache_height != preview->original_height) {
                    can_crop = FALSE;
                }
            }

            if (can_crop) {
                selection_surface = create_selection_cropped_masked_surface(preview, surface);
                if (selection_surface) {
                    surface = selection_surface;
                    get_surface_size(surface, &img_width, &img_height);
                }
            }
        }

        if (is_scaled_cache) {
            /* Cache is already scaled to fit widget, draw at 1:1 */
            draw_x = (widget_width - img_width) / 2.0;
            draw_y = (widget_height - img_height) / 2.0;

            cairo_save(cr);
            cairo_translate(cr, draw_x, draw_y);
            /* For scaled cache mode we currently skip selection masking (cache is in scaled space). */
            cairo_set_source_surface(cr, surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            /* Calculate scale to fit */
            scale = calculate_fit_scale(img_width, img_height, widget_width, widget_height);

            gdouble scaled_width = img_width * scale;
            gdouble scaled_height = img_height * scale;

            draw_x = (widget_width - scaled_width) / 2.0;
            draw_y = (widget_height - scaled_height) / 2.0;

            cairo_save(cr);
            cairo_translate(cr, draw_x, draw_y);
            cairo_scale(cr, scale, scale);
            if (selection_surface) {
                /* Already selection-cropped + masked */
                cairo_set_source_surface(cr, surface, 0, 0);
                cairo_paint(cr);
            } else if (preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->filter_apply_func) {
                /* Viewport cache represents a sub-rect of the full image */
                paint_surface_with_optional_selection_mask(preview, cr, surface, preview->cache_x, preview->cache_y);
            } else {
                /* Full-surface path */
                paint_surface_with_optional_selection_mask(preview, cr, surface, 0, 0);
            }
            cairo_restore(cr);
        }

        if (cache_locked) {
            g_mutex_unlock(&preview->cache_mutex);
        }

        if (selection_surface) {
            cairo_surface_destroy(selection_surface);
        }
    } else {
        /* 1:1 mode: display at actual size */
        if (preview->view == FILTER_PREVIEW_VIEW_AFTER) {
            if (preview->filter_apply_func && using_viewport_cache) {
                gint sel_origin_x = 0;
                gint sel_origin_y = 0;
                gint sel_w = 0;
                gint sel_h = 0;
                if (selection_active_for_preview(preview)) {
                    if (get_selection_bounds_layer(preview, &sel_origin_x, &sel_origin_y, &sel_w, &sel_h)) {
                        /* ok */
                    } else {
                        sel_origin_x = 0;
                        sel_origin_y = 0;
                    }
                }

                /* Draw cached viewport at its correct position */
                draw_x = (preview->cache_x - sel_origin_x) - preview->pan_x;
                draw_y = (preview->cache_y - sel_origin_y) - preview->pan_y;

                cairo_save(cr);
                cairo_translate(cr, draw_x, draw_y);
                paint_surface_with_optional_selection_mask(preview, cr, surface, preview->cache_x, preview->cache_y);
                cairo_restore(cr);

                if (cache_locked) {
                    g_mutex_unlock(&preview->cache_mutex);
                }
            } else {
                /* Draw full after image */
                cairo_surface_t* selection_surface = NULL;
                if (selection_active_for_preview(preview) && surface) {
                    selection_surface = create_selection_cropped_masked_surface(preview, surface);
                }

                draw_x = -preview->pan_x;
                draw_y = -preview->pan_y;

                cairo_save(cr);
                cairo_translate(cr, draw_x, draw_y);
                if (selection_surface) {
                    cairo_set_source_surface(cr, selection_surface, 0, 0);
                    cairo_paint(cr);
                } else {
                    paint_surface_with_optional_selection_mask(preview, cr, surface, 0, 0);
                }
                cairo_restore(cr);

                if (selection_surface) {
                    cairo_surface_destroy(selection_surface);
                }
            }
        } else {
            /* Draw full before image */
            cairo_surface_t* selection_surface = NULL;
            if (selection_active_for_preview(preview) && surface) {
                selection_surface = create_selection_cropped_masked_surface(preview, surface);
            }

            draw_x = -preview->pan_x;
            draw_y = -preview->pan_y;

            cairo_save(cr);
            cairo_translate(cr, draw_x, draw_y);
            if (selection_surface) {
                cairo_set_source_surface(cr, selection_surface, 0, 0);
                cairo_paint(cr);
            } else {
                paint_surface_with_optional_selection_mask(preview, cr, surface, 0, 0);
            }
            cairo_restore(cr);

            if (selection_surface) {
                cairo_surface_destroy(selection_surface);
            }
        }
    }

    return FALSE;
}

/**
 * Button press handler for panning
 */
static gboolean on_preview_button_press(GtkWidget* widget,
                                        GdkEventButton* event,
                                        gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || event->button != 1) {
        return FALSE;
    }

    /* Only allow panning in 1:1 mode and if zoom/pan is enabled */
    if (preview->mode != FILTER_PREVIEW_MODE_1TO1 || !preview->allow_zoom_pan) {
        return FALSE;
    }

    preview->is_dragging = TRUE;
    preview->drag_start_x = event->x;
    preview->drag_start_y = event->y;
    preview->drag_start_pan_x = preview->pan_x;
    preview->drag_start_pan_y = preview->pan_y;

    /* Change cursor to move */
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }

    return TRUE;
}

/**
 * Button release handler
 */
static gboolean on_preview_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || event->button != 1) {
        return FALSE;
    }

    if (preview->is_dragging) {
        preview->is_dragging = FALSE;

        /* Trigger immediate viewport update after drag ends */
        schedule_viewport_update(preview, 50); /* Short delay */

        /* Restore hand cursor if in 1:1 mode */
        if (preview->mode == FILTER_PREVIEW_MODE_1TO1) {
            GdkWindow* window = gtk_widget_get_window(widget);
            if (window) {
                GdkDisplay* display = gdk_window_get_display(window);
                GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
        }
    }

    return FALSE;
}

/**
 * Motion notify handler for panning
 */
static gboolean on_preview_motion_notify(GtkWidget* widget,
                                         GdkEventMotion* event,
                                         gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);
    gint widget_width, widget_height;

    if (!preview) {
        return FALSE;
    }

    widget_width = gtk_widget_get_allocated_width(widget);
    widget_height = gtk_widget_get_allocated_height(widget);

    if (preview->is_dragging && preview->mode == FILTER_PREVIEW_MODE_1TO1) {
        /* Update pan position based on drag */
        gdouble delta_x = preview->drag_start_x - event->x;
        gdouble delta_y = preview->drag_start_y - event->y;

        preview->pan_x = preview->drag_start_pan_x + delta_x;
        preview->pan_y = preview->drag_start_pan_y + delta_y;

        /* Clamp pan to keep image in bounds */
        clamp_pan(preview, widget_width, widget_height);

        /* Queue redraw immediately (shows cached content during drag) */
        gtk_widget_queue_draw(widget);

        /* Schedule viewport update after panning stops */
        schedule_viewport_update(preview, 150); /* 150ms delay */
    } else if (preview->mode == FILTER_PREVIEW_MODE_1TO1) {
        /* Show hand cursor when hovering in 1:1 mode */
        GdkWindow* window = gtk_widget_get_window(widget);
        if (window) {
            GdkDisplay* display = gdk_window_get_display(window);
            GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }

    return FALSE;
}

/**
 * Leave notify handler - restore default cursor
 */
static gboolean on_preview_leave_notify(GtkWidget* widget,
                                        GdkEventCrossing* event,
                                        gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    (void)event;

    if (!preview || preview->is_dragging) {
        return FALSE;
    }

    /* Restore default cursor */
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        gdk_window_set_cursor(window, NULL);
    }

    return FALSE;
}

/**
 * Realize handler - trigger update when widget is first realized
 */
static void on_preview_realize(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || !widget) {
        return;
    }

    /* If we have a filter function and need an update, trigger it now that widget is realized */
    if (preview->filter_apply_func && preview->view == FILTER_PREVIEW_VIEW_AFTER &&
        preview->before_surface_full) {
        g_mutex_lock(&preview->cache_mutex);
        gboolean has_cache = (preview->viewport_cache != NULL);
        g_mutex_unlock(&preview->cache_mutex);

        if (!has_cache || preview->needs_update) {
            preview->force_update = TRUE;
            preview->needs_update = FALSE;

            /* Check if widget is allocated - if so, update directly, otherwise use idle callback */
            gint widget_width = gtk_widget_get_allocated_width(widget);
            if (widget_width > 0) {
                request_viewport_update(preview);
            } else {
                /* Widget not allocated yet - use idle callback */
                g_idle_add(trigger_viewport_update_idle, preview);
            }
        }
    }
}

/**
 * Size allocate handler - trigger update when widget is first allocated
 */
static gboolean on_preview_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);
    gboolean* first_allocate_done;

    (void)allocation;

    if (!preview || !widget) {
        return FALSE;
    }

    /* Get or create flag to track if we've handled first allocation */
    first_allocate_done = (gboolean*)g_object_get_data(G_OBJECT(preview), "first_allocate_done");
    if (!first_allocate_done) {
        gboolean* flag = g_new(gboolean, 1);
        *flag = FALSE;
        g_object_set_data_full(G_OBJECT(preview), "first_allocate_done", flag, g_free);
        first_allocate_done = flag;
    }

    gint widget_width = gtk_widget_get_allocated_width(widget);
    gint widget_height = gtk_widget_get_allocated_height(widget);

    /* If widget is now allocated and we need an update, trigger it */
    if (!*first_allocate_done && preview->before_surface_full) {
        if (widget_width > 0 && widget_height > 0) {
            /* Always trigger update on first allocation if filter function is set and we're in AFTER view */
            if (preview->filter_apply_func && preview->view == FILTER_PREVIEW_VIEW_AFTER) {
                g_mutex_lock(&preview->cache_mutex);
                gboolean has_cache = (preview->viewport_cache != NULL);
                gint cached_width = preview->cache_width;
                gint cached_height = preview->cache_height;
                g_mutex_unlock(&preview->cache_mutex);

                /* If zoom/pan is disabled, invalidate cache if it was created with wrong widget size */
                if (has_cache && !preview->allow_zoom_pan) {
                    /* Calculate what the viewport should be with current widget size */
                    ViewportRect expected_viewport = calculate_viewport(preview);
                    if (cached_width != expected_viewport.width || cached_height != expected_viewport.height) {
                        g_mutex_lock(&preview->cache_mutex);
                        if (preview->viewport_cache) {
                            cairo_surface_destroy(preview->viewport_cache);
                            preview->viewport_cache = NULL;
                        }
                        has_cache = FALSE;
                        g_mutex_unlock(&preview->cache_mutex);
                    }
                }

                if (preview->needs_update || !has_cache) {
                    /* Widget is now allocated, trigger the update */
                    preview->force_update = TRUE;
                    preview->needs_update = FALSE;
                    request_viewport_update(preview);
                }
            }
            *first_allocate_done = TRUE;
        }
    } else if (*first_allocate_done && preview->needs_update && preview->filter_apply_func &&
               preview->view == FILTER_PREVIEW_VIEW_AFTER && preview->before_surface_full) {
        /* Handle deferred update that was scheduled before first allocation */
        if (widget_width > 0 && widget_height > 0) {
            preview->force_update = TRUE;
            preview->needs_update = FALSE;
            request_viewport_update(preview);
        }
    }

    return FALSE;
}

/**
 * Before button clicked
 */
static void on_before_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only before is active in view group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->after_button) {
            g_signal_handlers_block_by_func(preview->after_button,
                                            (gpointer)on_after_clicked, preview);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->after_button),
                                         FALSE);
            g_signal_handlers_unblock_by_func(preview->after_button,
                                              (gpointer)on_after_clicked, preview);
        }
        filter_preview_set_view(preview, FILTER_PREVIEW_VIEW_BEFORE);
    } else {
        /* If before is being deactivated, activate after instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * After button clicked
 */
static void on_after_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only after is active in view group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->before_button) {
            g_signal_handlers_block_by_func(preview->before_button,
                                            (gpointer)on_before_clicked, preview);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->before_button),
                                         FALSE);
            g_signal_handlers_unblock_by_func(preview->before_button,
                                              (gpointer)on_before_clicked, preview);
        }
        filter_preview_set_view(preview, FILTER_PREVIEW_VIEW_AFTER);
    } else {
        /* If after is being deactivated, activate before instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * Fit button clicked
 */
static void on_fit_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only fit is active in mode group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->one_to_one_button) {
            g_signal_handlers_block_by_func(preview->one_to_one_button,
                                            (gpointer)on_one_to_one_clicked, preview);
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(preview->one_to_one_button), FALSE);
            g_signal_handlers_unblock_by_func(
                preview->one_to_one_button, (gpointer)on_one_to_one_clicked, preview);
        }
        filter_preview_set_mode(preview, FILTER_PREVIEW_MODE_FIT);
    } else {
        /* If fit is being deactivated, activate 1:1 instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * 1:1 button clicked
 */
static void on_one_to_one_clicked(GtkWidget* widget, gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (!preview || preview->updating_buttons) {
        return;
    }

    /* Prevent recursive updates */
    preview->updating_buttons = TRUE;

    /* Ensure only 1:1 is active in mode group */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        if (preview->fit_button) {
            g_signal_handlers_block_by_func(preview->fit_button,
                                            (gpointer)on_fit_clicked, preview);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->fit_button),
                                         FALSE);
            g_signal_handlers_unblock_by_func(preview->fit_button,
                                              (gpointer)on_fit_clicked, preview);
        }
        filter_preview_set_mode(preview, FILTER_PREVIEW_MODE_1TO1);
    } else {
        /* If 1:1 is being deactivated, activate fit instead */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }

    preview->updating_buttons = FALSE;
}

/**
 * Update button states and visibility
 */
static void update_button_states(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    /* Update before/after radio buttons */
    if (preview->before_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->before_button),
                                     preview->view == FILTER_PREVIEW_VIEW_BEFORE);
        gtk_widget_set_visible(preview->before_button, TRUE);
    }

    if (preview->after_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->after_button),
                                     preview->view == FILTER_PREVIEW_VIEW_AFTER);
        gtk_widget_set_visible(preview->after_button, TRUE);
    }

    /* Update fit/1:1 radio buttons - only show if allow_zoom_pan is enabled */
    if (preview->fit_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->fit_button),
                                     preview->mode == FILTER_PREVIEW_MODE_FIT);
        gtk_widget_set_visible(preview->fit_button, preview->allow_zoom_pan);
    }

    if (preview->one_to_one_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preview->one_to_one_button),
                                     preview->mode == FILTER_PREVIEW_MODE_1TO1);
        gtk_widget_set_visible(preview->one_to_one_button, preview->allow_zoom_pan);
    }
}

/**
 * Initialize FilterPreview instance
 */
static void filter_preview_init(FilterPreview* preview) {
    GtkWidget* button_box;

    /* Initialize parent class first */
    /* Note: G_DEFINE_TYPE automatically chains to parent init */

    preview->before_surface_full = NULL;
    preview->after_surface_full = NULL;
    preview->viewport_cache = NULL;
    preview->cache_x = 0;
    preview->cache_y = 0;
    preview->cache_width = 0;
    preview->cache_height = 0;
    preview->original_width = 0;
    preview->original_height = 0;

    preview->filter_params = NULL;
    preview->filter_apply_func = NULL;
    preview->update_timeout_id = 0;
    preview->needs_update = FALSE;
    preview->filter_thread = NULL;
    preview->update_sequence = 0;
    preview->force_update = FALSE;

    /* Initialize mutex - must be done before any thread uses it */
    g_mutex_init(&preview->cache_mutex);

    preview->mode = FILTER_PREVIEW_MODE_FIT;
    preview->view = FILTER_PREVIEW_VIEW_AFTER;
    preview->pan_x = 0.0;
    preview->pan_y = 0.0;
    preview->is_dragging = FALSE;
    preview->updating_buttons = FALSE;
    preview->allow_zoom_pan = TRUE; /* Default: enabled */

    /* Initialize as a vertical box - set orientation during construction */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(preview),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(preview), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(preview), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(preview), FALSE);

    /* Store reference to self as main_box for compatibility */
    preview->main_box = GTK_WIDGET(preview);

    /* Create preview drawing area directly (no scrolled window for manual panning
     * control) */
    preview->preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(preview->preview_area, 375, 338);
    gtk_widget_set_hexpand(preview->preview_area, FALSE);
    gtk_widget_set_vexpand(preview->preview_area, FALSE);
    gtk_box_pack_start(GTK_BOX(preview), preview->preview_area, FALSE, FALSE, 0);

    /* Enable mouse events */
    gtk_widget_set_events(preview->preview_area,
                          gtk_widget_get_events(preview->preview_area) |
                              GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);

    /* Connect signals */
    g_signal_connect(preview->preview_area, "draw", G_CALLBACK(on_preview_draw),
                     preview);
    g_signal_connect(preview->preview_area, "realize",
                     G_CALLBACK(on_preview_realize), preview);
    g_signal_connect(preview->preview_area, "size-allocate",
                     G_CALLBACK(on_preview_size_allocate), preview);
    g_signal_connect(preview->preview_area, "button-press-event",
                     G_CALLBACK(on_preview_button_press), preview);
    g_signal_connect(preview->preview_area, "button-release-event",
                     G_CALLBACK(on_preview_button_release), preview);
    g_signal_connect(preview->preview_area, "motion-notify-event",
                     G_CALLBACK(on_preview_motion_notify), preview);
    g_signal_connect(preview->preview_area, "leave-notify-event",
                     G_CALLBACK(on_preview_leave_notify), preview);

    /* Create controls box */
    preview->controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(preview->controls_box, 5);
    gtk_widget_set_margin_bottom(preview->controls_box, 5);
    gtk_widget_set_margin_start(preview->controls_box, 5);
    gtk_widget_set_margin_end(preview->controls_box, 5);
    gtk_box_pack_start(GTK_BOX(preview), preview->controls_box, FALSE, FALSE, 0);

    /* Create button box */
    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(preview->controls_box), button_box, FALSE, FALSE,
                       0);

    /* Create before button (toggle button in view group) */
    preview->before_button = gtk_toggle_button_new_with_label(_("before"));
    gtk_widget_set_margin_start(preview->before_button, 5);
    gtk_widget_set_margin_end(preview->before_button, 5);
    g_signal_connect(preview->before_button, "clicked",
                     G_CALLBACK(on_before_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->before_button, FALSE, FALSE,
                       0);

    /* Create after button (toggle button in view group) */
    preview->after_button = gtk_toggle_button_new_with_label(_("after"));
    gtk_widget_set_margin_start(preview->after_button, 5);
    gtk_widget_set_margin_end(preview->after_button, 5);
    g_signal_connect(preview->after_button, "clicked",
                     G_CALLBACK(on_after_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->after_button, FALSE, FALSE,
                       0);

    /* Create 1:1 button (toggle button in mode group) */
    preview->one_to_one_button = gtk_toggle_button_new_with_label("1:1");
    gtk_widget_set_margin_start(preview->one_to_one_button, 5);
    gtk_widget_set_margin_end(preview->one_to_one_button, 5);
    g_signal_connect(preview->one_to_one_button, "clicked",
                     G_CALLBACK(on_one_to_one_clicked), preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->one_to_one_button, FALSE,
                       FALSE, 0);

    /* Create fit button (toggle button in mode group) */
    preview->fit_button = gtk_toggle_button_new_with_label(_("fit"));
    gtk_widget_set_margin_start(preview->fit_button, 5);
    gtk_widget_set_margin_end(preview->fit_button, 5);
    g_signal_connect(preview->fit_button, "clicked", G_CALLBACK(on_fit_clicked),
                     preview);
    gtk_box_pack_start(GTK_BOX(button_box), preview->fit_button, FALSE, FALSE, 0);

    /* Update initial button states */
    update_button_states(preview);

    /* Show all widgets */
    gtk_widget_show_all(GTK_WIDGET(preview));
}

/**
 * Dispose handler
 */
static void filter_preview_dispose(GObject* object) {
    FilterPreview* preview = FILTER_PREVIEW(object);

    /* Cancel pending updates */
    if (preview->update_timeout_id > 0) {
        g_source_remove(preview->update_timeout_id);
        preview->update_timeout_id = 0;
    }

    /* Wait for filter thread to complete */
    if (preview->filter_thread) {
        /* Mark that we're disposing so thread knows not to access preview */
        /* Join the thread - this will wait for it to complete */
        GThread* thread = preview->filter_thread;
        preview->filter_thread = NULL; /* Clear reference before joining to avoid race */
        g_thread_join(thread);
    }

    /* Clean up surfaces - lock mutex first to ensure no thread is accessing them
     * This ensures the mutex is unlocked before we clear it */
    g_mutex_lock(&preview->cache_mutex);

    if (preview->before_surface_full) {
        cairo_surface_destroy(preview->before_surface_full);
        preview->before_surface_full = NULL;
    }

    if (preview->after_surface_full) {
        cairo_surface_destroy(preview->after_surface_full);
        preview->after_surface_full = NULL;
    }

    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    /* Unlock mutex before clearing it - this ensures it's in an unlocked state */
    g_mutex_unlock(&preview->cache_mutex);

    /* Clear mutex - must be unlocked at this point */
    g_mutex_clear(&preview->cache_mutex);

    G_OBJECT_CLASS(filter_preview_parent_class)->dispose(object);
}

/**
 * Finalize handler
 */
static void filter_preview_finalize(GObject* object) {
    (void)object;
    G_OBJECT_CLASS(filter_preview_parent_class)->finalize(object);
}

/**
 * Get property handler
 */
static void filter_preview_get_property(GObject* object, guint property_id,
                                        GValue* value, GParamSpec* pspec) {
    FilterPreview* preview = FILTER_PREVIEW(object);

    switch (property_id) {
        case PROP_ALLOW_ZOOM_PAN:
            g_value_set_boolean(value, preview->allow_zoom_pan);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

/**
 * Set property handler
 */
static void filter_preview_set_property(GObject* object, guint property_id,
                                        const GValue* value, GParamSpec* pspec) {
    FilterPreview* preview = FILTER_PREVIEW(object);

    switch (property_id) {
        case PROP_ALLOW_ZOOM_PAN: {
            gboolean new_value = g_value_get_boolean(value);
            if (preview->allow_zoom_pan != new_value) {
                preview->allow_zoom_pan = new_value;

                /* If disabling zoom/pan, force FIT mode */
                if (!new_value && preview->mode == FILTER_PREVIEW_MODE_1TO1) {
                    filter_preview_set_mode(preview, FILTER_PREVIEW_MODE_FIT);
                }

                /* Update button visibility */
                update_button_states(preview);
            }
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

/**
 * Class initialization
 */
static void filter_preview_class_init(FilterPreviewClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = filter_preview_dispose;
    object_class->finalize = filter_preview_finalize;
    object_class->get_property = filter_preview_get_property;
    object_class->set_property = filter_preview_set_property;

    /* Install allow_zoom_pan property */
    g_object_class_install_property(
        object_class, PROP_ALLOW_ZOOM_PAN,
        g_param_spec_boolean("allow-zoom-pan", "Allow Zoom/Pan",
                             "Allow zoom and pan (1:1 mode) in filter preview",
                             TRUE, /* Default: enabled */
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/**
 * Create a new filter preview widget
 */
GtkWidget* filter_preview_new(void) {
    return GTK_WIDGET(g_object_new(FILTER_PREVIEW_TYPE, "orientation",
                                   GTK_ORIENTATION_VERTICAL, "spacing", 0, NULL));
}

/**
 * Set the before image surface
 */
void filter_preview_set_before_surface(FilterPreview* preview, cairo_surface_t* surface) {
    if (!preview) {
        return;
    }

    if (preview->before_surface_full) {
        cairo_surface_destroy(preview->before_surface_full);
    }

    if (surface) {
        preview->before_surface_full = cairo_surface_reference(surface);
        preview->original_width = cairo_image_surface_get_width(surface);
        preview->original_height = cairo_image_surface_get_height(surface);
    } else {
        preview->before_surface_full = NULL;
        preview->original_width = 0;
        preview->original_height = 0;
    }

    /* Force immediate redraw - use show_all to ensure widget is allocated if needed */
    if (preview->preview_area) {
        if (!gtk_widget_get_visible(preview->preview_area)) {
            gtk_widget_show_all(GTK_WIDGET(preview));
        }
        gtk_widget_queue_draw(preview->preview_area);
    }

    /* If we have a filter function set and we're in AFTER view, trigger viewport update */
    if (preview->filter_apply_func && preview->view == FILTER_PREVIEW_VIEW_AFTER && surface) {
        /* Invalidate cache to force update */
        if (preview->viewport_cache) {
            cairo_surface_destroy(preview->viewport_cache);
            preview->viewport_cache = NULL;
        }
        preview->force_update = TRUE;

        /* Try to update immediately if widget is allocated, otherwise schedule for later */
        if (preview->preview_area && gtk_widget_get_allocated_width(preview->preview_area) > 0) {
            request_viewport_update(preview);
        } else {
            preview->needs_update = TRUE;
            g_idle_add(trigger_viewport_update_idle, preview);
        }
    }
}

/**
 * Set the after image surface
 */
void filter_preview_set_after_surface(FilterPreview* preview,
                                      cairo_surface_t* surface) {
    if (!preview) {
        return;
    }

    if (preview->after_surface_full) {
        cairo_surface_destroy(preview->after_surface_full);
    }

    if (surface) {
        preview->after_surface_full = cairo_surface_reference(surface);
    } else {
        preview->after_surface_full = NULL;
    }

    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Set the display mode
 */
void filter_preview_set_mode(FilterPreview* preview, FilterPreviewMode mode) {
    if (!preview) {
        return;
    }

    /* Prevent switching to 1:1 mode if zoom/pan is disabled */
    if (mode == FILTER_PREVIEW_MODE_1TO1 && !preview->allow_zoom_pan) {
        mode = FILTER_PREVIEW_MODE_FIT;
    }

    preview->mode = mode;

    /* Reset pan position when switching modes */
    if (mode == FILTER_PREVIEW_MODE_FIT) {
        preview->pan_x = 0.0;
        preview->pan_y = 0.0;
    }

    update_button_states(preview);
    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Get the display mode
 */
FilterPreviewMode filter_preview_get_mode(FilterPreview* preview) {
    if (!preview) {
        return FILTER_PREVIEW_MODE_FIT;
    }

    return preview->mode;
}

/**
 * Set the view mode
 */
void filter_preview_set_view(FilterPreview* preview, FilterPreviewView view) {
    if (!preview) {
        return;
    }

    preview->view = view;
    update_button_states(preview);
    gtk_widget_queue_draw(preview->preview_area);
}

/**
 * Get the view mode
 */
FilterPreviewView filter_preview_get_view(FilterPreview* preview) {
    if (!preview) {
        return FILTER_PREVIEW_VIEW_AFTER;
    }

    return preview->view;
}

/**
 * Set filter parameters and trigger update
 */
void filter_preview_set_filter_params(FilterPreview* preview, gpointer params) {
    if (!preview) {
        return;
    }

    preview->filter_params = params;

    /* Invalidate cache */
    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    /* Force update even if viewport rectangle hasn't changed */
    preview->force_update = TRUE;

    /* Request immediate update */
    request_viewport_update(preview);
}

/**
 * Force immediate viewport update
 */
void filter_preview_refresh(FilterPreview* preview) {
    if (!preview) {
        return;
    }

    /* Invalidate cache */
    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    request_viewport_update(preview);
}

/**
 * Set the filter function to apply
 */
/**
 * Timeout callback to trigger viewport update when widget is ready
 * Uses a small delay to ensure dialog is shown and widget is allocated
 */
static gboolean trigger_viewport_update_timeout(gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (preview && preview->filter_apply_func && preview->view == FILTER_PREVIEW_VIEW_AFTER &&
        preview->before_surface_full) {
        /* Force update - widget should be allocated by now */
        preview->force_update = TRUE;
        preview->needs_update = FALSE;
        request_viewport_update(preview);
    }
    return G_SOURCE_REMOVE;
}

/**
 * Idle callback to trigger viewport update when widget is ready
 */
static gboolean trigger_viewport_update_idle(gpointer user_data) {
    FilterPreview* preview = FILTER_PREVIEW(user_data);

    if (preview && preview->filter_apply_func && preview->view == FILTER_PREVIEW_VIEW_AFTER &&
        preview->before_surface_full) {
        /* Check if widget is now allocated */
        gint widget_width = 0;
        if (preview->preview_area) {
            widget_width = gtk_widget_get_allocated_width(preview->preview_area);
        }

        if (widget_width > 0) {
            /* Widget is allocated, trigger update */
            preview->force_update = TRUE;
            preview->needs_update = FALSE;
            request_viewport_update(preview);
            return G_SOURCE_REMOVE;
        } else {
            /* Widget still not allocated, use timeout as fallback */
            preview->needs_update = TRUE;
            g_timeout_add(100, trigger_viewport_update_timeout, preview);
            return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_REMOVE;
}

void filter_preview_set_filter_function(FilterPreview* preview,
                                        FilterApplyFunc filter_func,
                                        gpointer params) {
    if (!preview) {
        return;
    }

    preview->filter_apply_func = filter_func;
    preview->filter_params = params;

    /* Invalidate cache */
    if (preview->viewport_cache) {
        cairo_surface_destroy(preview->viewport_cache);
        preview->viewport_cache = NULL;
    }

    /* Force update even if viewport rectangle hasn't changed */
    preview->force_update = TRUE;

    /* Check widget allocation status */
    gint widget_width = 0;
    if (preview->preview_area) {
        widget_width = gtk_widget_get_allocated_width(preview->preview_area);
    }

    /* Request immediate update - try now, and if widget isn't ready, schedule for later */
    if (preview->preview_area && widget_width > 0 &&
        preview->before_surface_full) {
        request_viewport_update(preview);
    } else {
        /* Widget not allocated yet or no before surface - schedule update for when ready */
        preview->needs_update = TRUE;
        /* Use idle callback to try again once GTK has processed pending events */
        g_idle_add(trigger_viewport_update_idle, preview);
    }
}

/**
 * Get allow_zoom_pan property
 */
gboolean filter_preview_get_allow_zoom_pan(FilterPreview* preview) {
    if (!preview) {
        return TRUE; /* Default */
    }
    return preview->allow_zoom_pan;
}

/**
 * Set allow_zoom_pan property
 */
void filter_preview_set_allow_zoom_pan(FilterPreview* preview, gboolean allow) {
    if (!preview) {
        return;
    }
    g_object_set(G_OBJECT(preview), "allow-zoom-pan", allow, NULL);
}