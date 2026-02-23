#include "commands/command_image.h"
#include "../lib/ocular.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "render/layer.h"
#include "render/tile.h"
#include "selection/selection_mask.h"
#include <cairo.h>
#include <glib.h>
#include <stdlib.h>

/**
 * Canvas resize command apply callback (restore to new size)
 */
static void canvas_resize_command_apply(Command* cmd, struct ImageDocument* doc) {
    CanvasResizeCommandData* data;
    GList* iter;
    LayerOffsetPair* pair;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (CanvasResizeCommandData*)cmd->user_data;

    /* Restore new canvas dimensions */
    doc->width = data->new_width;
    doc->height = data->new_height;

    /* Restore layer offsets to new positions */
    for (iter = data->layer_offsets; iter; iter = iter->next) {
        pair = (LayerOffsetPair*)iter->data;
        if (pair && pair->layer) {
            /* Apply offset adjustment */
            pair->layer->offset_x = pair->old_offset_x + data->offset_x;
            pair->layer->offset_y = pair->old_offset_y + data->offset_y;
            layer_invalidate_cache(pair->layer);
        }
    }

    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after canvas resize redo");
    }

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);
}

/**
 * Canvas resize command revert callback (restore to old size)
 */
static void canvas_resize_command_revert(Command* cmd, struct ImageDocument* doc) {
    CanvasResizeCommandData* data;
    GList* iter;
    LayerOffsetPair* pair;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (CanvasResizeCommandData*)cmd->user_data;

    /* Restore old canvas dimensions */
    doc->width = data->old_width;
    doc->height = data->old_height;

    /* Restore layer offsets to old positions */
    for (iter = data->layer_offsets; iter; iter = iter->next) {
        pair = (LayerOffsetPair*)iter->data;
        if (pair && pair->layer) {
            /* Restore old offset */
            pair->layer->offset_x = pair->old_offset_x;
            pair->layer->offset_y = pair->old_offset_y;
            layer_invalidate_cache(pair->layer);
        }
    }

    /* Recreate tile grid with old dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after canvas size undo");
    }

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);
}

/**
 * Canvas resize command destroy callback
 */
static void canvas_resize_command_destroy(Command* cmd) {
    CanvasResizeCommandData* data;
    GList* iter;
    LayerOffsetPair* pair;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (CanvasResizeCommandData*)cmd->user_data;

    /* Free layer offset pairs */
    if (data->layer_offsets) {
        for (iter = data->layer_offsets; iter; iter = iter->next) {
            pair = (LayerOffsetPair*)iter->data;
            if (pair) {
                g_free(pair);
            }
        }
        g_list_free(data->layer_offsets);
    }

    g_free(data);
}

/**
 * Create a canvas resize command
 */
Command* command_create_canvas_resize(guint old_width, guint old_height,
                                      guint new_width, guint new_height,
                                      gdouble old_resolution, gdouble new_resolution,
                                      gint offset_x, gint offset_y,
                                      struct ImageDocument* doc) {
    Command* cmd;
    CanvasResizeCommandData* data;
    GList* iter;
    ImageLayer* layer;
    LayerOffsetPair* pair;

    if (!doc) {
        return NULL;
    }

    /* Create command data */
    data = (CanvasResizeCommandData*)g_malloc(sizeof(CanvasResizeCommandData));
    data->old_width = old_width;
    data->old_height = old_height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->old_resolution = old_resolution;
    data->new_resolution = new_resolution;
    data->offset_x = offset_x;
    data->offset_y = offset_y;
    data->layer_offsets = NULL;

    /* Store old offsets for all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (layer) {
            pair = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
            pair->layer = layer;
            pair->old_offset_x = layer->offset_x;
            pair->old_offset_y = layer->offset_y;
            data->layer_offsets = g_list_append(data->layer_offsets, pair);
        }
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_CANVAS_SIZE),
                      COMMAND_CANVAS_RESIZE,
                      canvas_resize_command_apply,
                      canvas_resize_command_revert,
                      canvas_resize_command_destroy);

    if (!cmd) {
        /* Free layer offset pairs */
        if (data->layer_offsets) {
            for (iter = data->layer_offsets; iter; iter = iter->next) {
                pair = (LayerOffsetPair*)iter->data;
                if (pair) {
                    g_free(pair);
                }
            }
            g_list_free(data->layer_offsets);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Helper function to flip a layer using Ocular library
 */
static gboolean flip_layer_impl(struct ImageLayer* layer, OcDirection direction) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    if (width == 0 || height == 0) {
        return FALSE;
    }

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Flip layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        g_warning("Flip layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply flip using Ocular library (4 channels for RGBA) */
    status = ocularFlipImage(rgba_input, rgba_output, width, height, width * 4, direction);

    if (status != OC_STATUS_OK) {
        g_warning("Flip layer: Ocular flip returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        g_warning("Flip layer: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper function to transpose a layer using Ocular library
 */
static gboolean transpose_layer_impl(struct ImageLayer* layer) {
    cairo_surface_t* old_surface;
    cairo_surface_t* new_surface;
    gint old_width, old_height;
    gint new_width, new_height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    old_surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(old_surface, &old_width, &old_height)) {
        return FALSE;
    }

    if (old_width == 0 || old_height == 0) {
        return FALSE;
    }

    /* Transpose swaps width and height */
    new_width = old_height;
    new_height = old_width;

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(old_width * old_height * 4);
    rgba_output = (guchar*)g_malloc(new_width * new_height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Transpose layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(old_surface, rgba_input)) {
        g_warning("Transpose layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply transpose using Ocular library
       Stride is width * 4 for RGBA format */
    status = ocularTransposeImage(rgba_input, rgba_output, old_width, old_height, old_width * 4);

    if (status != OC_STATUS_OK) {
        g_warning("Transpose layer: Ocular transpose returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Create new surface with swapped dimensions */
    new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!new_surface) {
        g_warning("Transpose layer: Failed to create new surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert RGBA output to new Cairo surface */
    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        g_warning("Transpose layer: Failed to convert RGBA to new surface");
        cairo_surface_destroy(new_surface);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Replace old surface with new one */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;

    /* Update layer dimensions */
    layer->width = new_width;
    layer->height = new_height;

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper: Resize a layer using ocularResamplingFilter
 */
static gboolean resize_layer_impl(struct ImageLayer* layer, gint new_width, gint new_height,
                                 OcInterpolationMode interpolation_mode) {
    cairo_surface_t* old_surface;
    cairo_surface_t* new_surface;
    gint old_width, old_height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    old_surface = layer->surface;
    if (!adjustments_validate_surface(old_surface, &old_width, &old_height)) {
        return FALSE;
    }

    if (old_width <= 0 || old_height <= 0 || new_width <= 0 || new_height <= 0) {
        return FALSE;
    }

    if (old_width == new_width && old_height == new_height) {
        return TRUE;
    }

    rgba_input = (guchar*)g_malloc((gsize)old_width * old_height * 4);
    rgba_output = (guchar*)g_malloc((gsize)new_width * new_height * 4);
    if (!rgba_input || !rgba_output) {
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgba(old_surface, rgba_input)) {
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    status = ocularResamplingFilter(rgba_input, old_width, old_height, old_width * 4,
                                   rgba_output, new_width, new_height, new_width * 4,
                                   interpolation_mode);
    g_free(rgba_input);

    if (status != OC_STATUS_OK) {
        g_warning("Resize layer: ocularResamplingFilter returned %d", status);
        g_free(rgba_output);
        return FALSE;
    }

    new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!new_surface) {
        g_free(rgba_output);
        return FALSE;
    }

    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        cairo_surface_destroy(new_surface);
        g_free(rgba_output);
        return FALSE;
    }
    g_free(rgba_output);

    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;
    layer->width = new_width;
    layer->height = new_height;
    layer_invalidate_cache(layer);
    return TRUE;
}

/**
 * Image resize command apply: resize all layers and document to new dimensions
 */
static void image_resize_command_apply(Command* cmd, struct ImageDocument* doc) {
    ImageResizeCommandData* data;
    GList* iter_snap, *iter_layer;
    struct ImageLayer* layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }
    data = (ImageResizeCommandData*)cmd->user_data;

    doc->width = data->new_width;
    doc->height = data->new_height;

    for (iter_snap = data->layer_snapshots, iter_layer = data->layers;
         iter_snap && iter_layer;
         iter_snap = iter_snap->next, iter_layer = iter_layer->next) {
        cairo_surface_t* snapshot = (cairo_surface_t*)iter_snap->data;
        layer = (struct ImageLayer*)iter_layer->data;
        if (!layer || !snapshot) {
            continue;
        }
        gint ow = 0, oh = 0;
        if (!adjustments_validate_surface(snapshot, &ow, &oh)) {
            continue;
        }
        if (!resize_layer_impl(layer, (gint)data->new_width, (gint)data->new_height,
                              (OcInterpolationMode)data->interpolation_mode)) {
            continue;
        }
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after image resize redo");
    }

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }
    document_invalidate_composite(doc);
}

/**
 * Image resize command revert: restore document size and layer surfaces
 */
static void image_resize_command_revert(Command* cmd, struct ImageDocument* doc) {
    ImageResizeCommandData* data;
    GList* iter_snap, *iter_layer;
    struct ImageLayer* layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }
    data = (ImageResizeCommandData*)cmd->user_data;

    doc->width = data->old_width;
    doc->height = data->old_height;

    for (iter_snap = data->layer_snapshots, iter_layer = data->layers;
         iter_snap && iter_layer;
         iter_snap = iter_snap->next, iter_layer = iter_layer->next) {
        cairo_surface_t* snapshot = (cairo_surface_t*)iter_snap->data;
        layer = (struct ImageLayer*)iter_layer->data;
        if (!layer || !snapshot) {
            continue;
        }
        gint w = 0, h = 0;
        if (!adjustments_validate_surface(snapshot, &w, &h)) {
            continue;
        }
        cairo_surface_t* restored = cairo_surface_snapshot(snapshot);
        if (restored) {
            if (layer->surface) {
                cairo_surface_destroy(layer->surface);
            }
            layer->surface = restored;
            layer->width = w;
            layer->height = h;
            layer_invalidate_cache(layer);
        }
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after image resize undo");
    }

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }
    document_invalidate_composite(doc);
}

/**
 * Image resize command destroy
 */
static void image_resize_command_destroy(Command* cmd) {
    ImageResizeCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data) {
        return;
    }
    data = (ImageResizeCommandData*)cmd->user_data;
    if (data->layer_snapshots) {
        for (iter = data->layer_snapshots; iter; iter = iter->next) {
            cairo_surface_t* s = (cairo_surface_t*)iter->data;
            if (s) {
                cairo_surface_destroy(s);
            }
        }
        g_list_free(data->layer_snapshots);
    }
    if (data->layers) {
        g_list_free(data->layers);
    }
    g_free(data);
}

/**
 * Create image resize command
 */
Command* command_create_image_resize(struct ImageDocument* doc,
                                     guint new_width, guint new_height,
                                     gint interpolation_mode) {
    Command* cmd;
    ImageResizeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;

    if (!doc || !doc->layers) {
        return NULL;
    }

    if (new_width == 0 || new_height == 0) {
        return NULL;
    }

    if (doc->width == new_width && doc->height == new_height) {
        return NULL;
    }

    data = (ImageResizeCommandData*)g_malloc0(sizeof(ImageResizeCommandData));
    if (!data) {
        return NULL;
    }

    data->old_width = doc->width;
    data->old_height = doc->height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->interpolation_mode = interpolation_mode;
    if (data->interpolation_mode < 0) data->interpolation_mode = 0;
    if (data->interpolation_mode > 3) data->interpolation_mode = 3;

    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface) {
            continue;
        }
        cairo_surface_t* snapshot = cairo_surface_snapshot(layer->surface);
        if (snapshot) {
            data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
            data->layers = g_list_append(data->layers, layer);
        }
    }

    if (!data->layers) {
        g_free(data);
        return NULL;
    }

    cmd = command_new(command_get_name_string(CMD_NAME_IMAGE_RESIZE),
                      COMMAND_LAYER_EDIT,
                      image_resize_command_apply,
                      image_resize_command_revert,
                      image_resize_command_destroy);
    if (!cmd) {
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                cairo_surface_destroy((cairo_surface_t*)iter->data);
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

/**
 * Helper: Rotate a layer using Ocular library
 */
static gboolean rotate_layer_impl(struct ImageLayer* layer,
                                  gfloat angle_degrees,
                                  gboolean preserve_size,
                                  gboolean use_transparency,
                                  OcInterpolationMode interpolation_mode,
                                  guchar fill_r,
                                  guchar fill_g,
                                  guchar fill_b,
                                  gint target_width,
                                  gint target_height,
                                  gint doc_width,
                                  gint doc_height) {
    if (!layer || !layer->surface) {
        return FALSE;
    }

    cairo_surface_t* old_surface = layer->surface;
    gint layer_w, layer_h;
    if (!adjustments_validate_surface(old_surface, &layer_w, &layer_h)) {
        return FALSE;
    }

    if (layer_w <= 0 || layer_h <= 0 || doc_width <= 0 || doc_height <= 0) {
        return FALSE;
    }

    /* Build a full-canvas surface with the layer painted at its document offset.
     * This makes Image-menu rotation apply to ALL layers consistently in document space. */
    cairo_surface_t* doc_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, doc_width, doc_height);
    if (!doc_surface || cairo_surface_status(doc_surface) != CAIRO_STATUS_SUCCESS) {
        if (doc_surface) {
            cairo_surface_destroy(doc_surface);
        }
        return FALSE;
    }

    cairo_t* cr = cairo_create(doc_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, old_surface, layer->offset_x, layer->offset_y);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Convert to RGBA for Ocular */
    guchar* rgba_input = (guchar*)g_malloc((gsize)doc_width * (gsize)doc_height * 4);
    if (!rgba_input) {
        cairo_surface_destroy(doc_surface);
        return FALSE;
    }
    if (!adjustments_cairo_to_rgba(doc_surface, rgba_input)) {
        cairo_surface_destroy(doc_surface);
        g_free(rgba_input);
        return FALSE;
    }
    cairo_surface_destroy(doc_surface);

    gint new_w = target_width;
    gint new_h = target_height;
    if (preserve_size) {
        new_w = doc_width;
        new_h = doc_height;
    }
    if (new_w <= 0 || new_h <= 0) {
        g_free(rgba_input);
        return FALSE;
    }

    guchar* rgba_output = (guchar*)g_malloc((gsize)new_w * (gsize)new_h * 4);
    if (!rgba_output) {
        g_free(rgba_input);
        return FALSE;
    }

    OC_STATUS status = ocularRotateImage(rgba_input, doc_width, doc_height, doc_width * 4, rgba_output,
                                         &new_w, &new_h,
                                         angle_degrees,
                                         preserve_size ? true : false,
                                         use_transparency ? true : false,
                                         interpolation_mode,
                                         fill_r, fill_g, fill_b);
    g_free(rgba_input);

    if (status != OC_STATUS_OK) {
        g_warning("Rotate layer: Ocular rotate returned error %d", status);
        g_free(rgba_output);
        return FALSE;
    }

    if (new_w <= 0 || new_h <= 0) {
        g_free(rgba_output);
        return FALSE;
    }

    cairo_surface_t* new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
    if (!new_surface || cairo_surface_status(new_surface) != CAIRO_STATUS_SUCCESS) {
        if (new_surface) {
            cairo_surface_destroy(new_surface);
        }
        g_free(rgba_output);
        return FALSE;
    }

    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        cairo_surface_destroy(new_surface);
        g_free(rgba_output);
        return FALSE;
    }

    g_free(rgba_output);

    /* Replace surface (layer becomes full-canvas after document-space rotation) */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;
    layer->width = new_w;
    layer->height = new_h;
    layer->offset_x = 0;
    layer->offset_y = 0;
    layer_invalidate_cache(layer);

    return TRUE;
}

typedef struct {
    struct ImageDocument* doc;
    GList* layer_snapshots;
    GList* layers;
    GList* layer_offsets; /* List of LayerOffset* */
    guint old_width;
    guint old_height;
    guint new_width;
    guint new_height;
    gfloat angle_degrees;
    gboolean preserve_size;
    gboolean use_transparency;
    gint interpolation_mode;
    guchar fill_r, fill_g, fill_b;
} RotateCommandData;

typedef struct {
    gint x;
    gint y;
} LayerOffset;

static void rotate_command_data_free(RotateCommandData* data) {
    if (!data) {
        return;
    }

    if (data->layer_snapshots) {
        for (GList* iter = data->layer_snapshots; iter; iter = iter->next) {
            cairo_surface_t* snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    if (data->layers) {
        g_list_free(data->layers);
    }

    if (data->layer_offsets) {
        for (GList* iter = data->layer_offsets; iter; iter = iter->next) {
            LayerOffset* off = (LayerOffset*)iter->data;
            g_free(off);
        }
        g_list_free(data->layer_offsets);
    }

    g_free(data);
}

static void rotate_command_apply(Command* cmd, struct ImageDocument* doc) {
    RotateCommandData* data;
    if (!cmd || !cmd->user_data || !doc) {
        return;
    }
    data = (RotateCommandData*)cmd->user_data;

    OcInterpolationMode interp = (OcInterpolationMode)data->interpolation_mode;

    for (GList* iter = data->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface) {
            continue;
        }
        if (!rotate_layer_impl(layer,
                               data->angle_degrees,
                               data->preserve_size,
                               data->use_transparency,
                               interp,
                               data->fill_r, data->fill_g, data->fill_b,
                               (gint)data->new_width, (gint)data->new_height,
                               (gint)data->old_width, (gint)data->old_height)) {
            g_warning("Failed to rotate layer: %s", layer->name);
        }
    }

    doc->width = data->new_width;
    doc->height = data->new_height;

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after rotate");
    }

    document_invalidate_composite(doc);
}

static void rotate_command_revert(Command* cmd, struct ImageDocument* doc) {
    RotateCommandData* data;
    if (!cmd || !cmd->user_data || !doc) {
        return;
    }
    data = (RotateCommandData*)cmd->user_data;

    GList* layer_iter = data->layers;
    GList* snapshot_iter = data->layer_snapshots;
    GList* offset_iter = data->layer_offsets;
    while (layer_iter && snapshot_iter && offset_iter) {
        struct ImageLayer* layer = (struct ImageLayer*)layer_iter->data;
        cairo_surface_t* snapshot = (cairo_surface_t*)snapshot_iter->data;
        LayerOffset* off = (LayerOffset*)offset_iter->data;
        if (layer && snapshot) {
            gint w = cairo_image_surface_get_width(snapshot);
            gint h = cairo_image_surface_get_height(snapshot);

            if (layer->surface) {
                cairo_surface_destroy(layer->surface);
            }
            layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            if (layer->surface) {
                cairo_t* cr = cairo_create(layer->surface);
                cairo_set_source_surface(cr, snapshot, 0, 0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_destroy(cr);
                layer->width = w;
                layer->height = h;
                if (off) {
                    layer->offset_x = off->x;
                    layer->offset_y = off->y;
                }
                layer_invalidate_cache(layer);
            }
        }
        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
        offset_iter = offset_iter->next;
    }

    doc->width = data->old_width;
    doc->height = data->old_height;

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after rotate revert");
    }

    document_invalidate_composite(doc);
}

static void rotate_command_destroy(Command* cmd) {
    RotateCommandData* data;
    if (!cmd || !cmd->user_data) {
        return;
    }
    data = (RotateCommandData*)cmd->user_data;
    rotate_command_data_free(data);
}

Command* command_create_rotate_arbitrary_named(const gchar* name,
                                               struct ImageDocument* doc,
                                               gfloat angle_degrees,
                                               gboolean preserve_size,
                                               gboolean use_transparency,
                                               gint interpolation_mode,
                                               guchar fill_r,
                                               guchar fill_g,
                                               guchar fill_b) {
    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    RotateCommandData* data = (RotateCommandData*)g_malloc0(sizeof(RotateCommandData));
    if (!data) {
        return NULL;
    }

    data->doc = doc;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->angle_degrees = angle_degrees;
    data->preserve_size = preserve_size;
    data->use_transparency = use_transparency;
    data->interpolation_mode = interpolation_mode;
    data->fill_r = fill_r;
    data->fill_g = fill_g;
    data->fill_b = fill_b;

    /* Compute new doc dimensions */
    if (preserve_size) {
        data->new_width = doc->width;
        data->new_height = doc->height;
    } else {
        /* Avoid +1px from floating point error at exact right angles */
        gdouble a = fmod(fabs((gdouble)angle_degrees), 360.0);
        if (fabs(a - 90.0) < 1e-6 || fabs(a - 270.0) < 1e-6) {
            data->new_width = doc->height;
            data->new_height = doc->width;
        } else if (fabs(a - 180.0) < 1e-6 || fabs(a) < 1e-6) {
            data->new_width = doc->width;
            data->new_height = doc->height;
        } else {
            gdouble rad = (gdouble)angle_degrees * (G_PI / 180.0);
            gdouble c = fabs(cos(rad));
            gdouble s = fabs(sin(rad));
            if (c < 1e-12)
                c = 0.0;
            if (s < 1e-12)
                s = 0.0;
            if (fabs(1.0 - c) < 1e-12)
                c = 1.0;
            if (fabs(1.0 - s) < 1e-12)
                s = 1.0;
            data->new_width = (guint)ceil((gdouble)doc->width * c + (gdouble)doc->height * s);
            data->new_height = (guint)ceil((gdouble)doc->width * s + (gdouble)doc->height * c);
        }
        if (data->new_width == 0)
            data->new_width = doc->width;
        if (data->new_height == 0)
            data->new_height = doc->height;
    }

    /* Snapshot all layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface) {
            continue;
        }
        cairo_surface_t* snapshot = cairo_surface_snapshot(layer->surface);
        if (snapshot) {
            data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
            data->layers = g_list_append(data->layers, layer);
            LayerOffset* off = (LayerOffset*)g_malloc(sizeof(LayerOffset));
            if (off) {
                off->x = layer->offset_x;
                off->y = layer->offset_y;
                data->layer_offsets = g_list_append(data->layer_offsets, off);
            } else {
                data->layer_offsets = g_list_append(data->layer_offsets, NULL);
            }
        }
    }

    if (!data->layers) {
        rotate_command_data_free(data);
        return NULL;
    }

    const gchar* cmd_name = (name && name[0]) ? name : "Arbitrary image rotation";
    Command* cmd = command_new(cmd_name,
                               COMMAND_LAYER_EDIT,
                               rotate_command_apply,
                               rotate_command_revert,
                               rotate_command_destroy);
    if (!cmd) {
        rotate_command_data_free(data);
        return NULL;
    }
    cmd->user_data = data;
    return cmd;
}

Command* command_create_rotate_arbitrary(struct ImageDocument* doc,
                                         gfloat angle_degrees,
                                         gboolean preserve_size,
                                         gboolean use_transparency,
                                         gint interpolation_mode,
                                         guchar fill_r,
                                         guchar fill_g,
                                         guchar fill_b) {
    return command_create_rotate_arbitrary_named("Arbitrary image rotation",
                                                 doc,
                                                 angle_degrees,
                                                 preserve_size,
                                                 use_transparency,
                                                 interpolation_mode,
                                                 fill_r, fill_g, fill_b);
}

/**
 * Flip command apply callback (apply flip)
 */
static void flip_command_apply(Command* cmd, struct ImageDocument* doc) {
    FlipCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    OcDirection direction;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (FlipCommandData*)cmd->user_data;

    /* Determine direction from command name */
    if (g_strcmp0(cmd->name, command_get_name_string(CMD_NAME_FLIP_HORIZONTAL)) == 0) {
        direction = OC_DIRECTION_HORIZONTAL;
    } else {
        direction = OC_DIRECTION_VERTICAL;
    }

    /* Apply flip to all layers */
    for (iter = data->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            if (!flip_layer_impl(layer, direction)) {
                g_warning("Failed to flip layer: %s", layer->name);
            }
        }
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flip command revert callback (restore from snapshots)
 */
static void flip_command_revert(Command* cmd, struct ImageDocument* doc) {
    FlipCommandData* data;
    GList *layer_iter, *snapshot_iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;
    cairo_t* cr;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (FlipCommandData*)cmd->user_data;

    /* Restore all layers from snapshots */
    layer_iter = data->layers;
    snapshot_iter = data->layer_snapshots;
    while (layer_iter && snapshot_iter) {
        layer = (struct ImageLayer*)layer_iter->data;
        snapshot = (cairo_surface_t*)snapshot_iter->data;

        if (layer && layer->surface && snapshot) {
            /* Restore layer from snapshot */
            cr = cairo_create(layer->surface);
            cairo_set_source_surface(cr, snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Invalidate layer cache */
            layer_invalidate_cache(layer);
        }

        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flip command destroy callback
 */
static void flip_command_destroy(Command* cmd) {
    FlipCommandData* data;
    GList* iter;
    cairo_surface_t* snapshot;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (FlipCommandData*)cmd->user_data;

    /* Free all snapshots */
    if (data->layer_snapshots) {
        for (iter = data->layer_snapshots; iter; iter = iter->next) {
            snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    /* Free layers list (but don't free the layers themselves) */
    if (data->layers) {
        g_list_free(data->layers);
    }

    g_free(data);
}

/**
 * Transpose command apply callback (apply transpose)
 */
static void transpose_command_apply(Command* cmd, struct ImageDocument* doc) {
    TransposeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (TransposeCommandData*)cmd->user_data;

    /* Apply transpose to all layers */
    for (iter = data->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            if (!transpose_layer_impl(layer)) {
                g_warning("Failed to transpose layer: %s", layer->name);
            }
        }
    }

    /* Update document dimensions */
    doc->width = data->new_width;
    doc->height = data->new_height;

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after transpose");
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Transpose command revert callback (restore from snapshots)
 */
static void transpose_command_revert(Command* cmd, struct ImageDocument* doc) {
    TransposeCommandData* data;
    GList *layer_iter, *snapshot_iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;
    cairo_t* cr;
    gint old_width, old_height;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (TransposeCommandData*)cmd->user_data;

    /* Restore all layers from snapshots */
    layer_iter = data->layers;
    snapshot_iter = data->layer_snapshots;
    while (layer_iter && snapshot_iter) {
        layer = (struct ImageLayer*)layer_iter->data;
        snapshot = (cairo_surface_t*)snapshot_iter->data;

        if (layer && snapshot) {
            /* Get original dimensions from snapshot */
            old_width = cairo_image_surface_get_width(snapshot);
            old_height = cairo_image_surface_get_height(snapshot);

            /* Destroy current surface and create new one with original dimensions */
            if (layer->surface) {
                cairo_surface_destroy(layer->surface);
            }
            layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, old_width, old_height);
            if (layer->surface) {
                /* Restore layer from snapshot */
                cr = cairo_create(layer->surface);
                cairo_set_source_surface(cr, snapshot, 0, 0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_destroy(cr);

                /* Update layer dimensions */
                layer->width = old_width;
                layer->height = old_height;

                /* Invalidate layer cache */
                layer_invalidate_cache(layer);
            }
        }

        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
    }

    /* Restore document dimensions */
    doc->width = data->old_width;
    doc->height = data->old_height;

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate tile grid with original dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after transpose revert");
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Transpose command destroy callback
 */
static void transpose_command_destroy(Command* cmd) {
    TransposeCommandData* data;
    GList* iter;
    cairo_surface_t* snapshot;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (TransposeCommandData*)cmd->user_data;

    /* Free all snapshots */
    if (data->layer_snapshots) {
        for (iter = data->layer_snapshots; iter; iter = iter->next) {
            snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    /* Free layers list (but don't free the layers themselves) */
    if (data->layers) {
        g_list_free(data->layers);
    }

    g_free(data);
}

/**
 * Create a flip horizontal command
 */
Command* command_create_flip_horizontal(struct ImageDocument* doc) {
    Command* cmd;
    FlipCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Create command data */
    data = (FlipCommandData*)g_malloc(sizeof(FlipCommandData));
    data->doc = doc;
    data->layer_snapshots = NULL;
    data->layers = NULL;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);
            }
        }
    }

    if (!data->layers || g_list_length(data->layers) == 0) {
        /* No valid layers found */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_FLIP_HORIZONTAL),
                      COMMAND_LAYER_EDIT,
                      flip_command_apply,
                      flip_command_revert,
                      flip_command_destroy);

    if (!cmd) {
        /* Free snapshots */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a flip vertical command
 */
Command* command_create_flip_vertical(struct ImageDocument* doc) {
    Command* cmd;
    FlipCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Create command data */
    data = (FlipCommandData*)g_malloc(sizeof(FlipCommandData));
    data->doc = doc;
    data->layer_snapshots = NULL;
    data->layers = NULL;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);
            }
        }
    }

    if (!data->layers || g_list_length(data->layers) == 0) {
        /* No valid layers found */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_FLIP_VERTICAL),
                      COMMAND_LAYER_EDIT,
                      flip_command_apply,
                      flip_command_revert,
                      flip_command_destroy);

    if (!cmd) {
        /* Free snapshots */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a transpose command
 */
Command* command_create_transpose(struct ImageDocument* doc) {
    Command* cmd;
    TransposeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Create command data */
    data = (TransposeCommandData*)g_malloc(sizeof(TransposeCommandData));
    data->doc = doc;
    data->layer_snapshots = NULL;
    data->layers = NULL;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->new_width = doc->height; /* Transpose swaps width/height */
    data->new_height = doc->width;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);
            }
        }
    }

    if (!data->layers || g_list_length(data->layers) == 0) {
        /* No valid layers found */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_TRANSPOSE),
                      COMMAND_LAYER_EDIT,
                      transpose_command_apply,
                      transpose_command_revert,
                      transpose_command_destroy);

    if (!cmd) {
        /* Free snapshots */
        if (data->layer_snapshots) {
            for (iter = data->layer_snapshots; iter; iter = iter->next) {
                snapshot = (cairo_surface_t*)iter->data;
                if (snapshot) {
                    cairo_surface_destroy(snapshot);
                }
            }
            g_list_free(data->layer_snapshots);
        }
        if (data->layers) {
            g_list_free(data->layers);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a fit canvas to active layer command
 */
Command* command_create_fit_active_layer(guint old_width, guint old_height,
                                         guint new_width, guint new_height,
                                         gdouble old_resolution, gdouble new_resolution,
                                         gint offset_x, gint offset_y,
                                         struct ImageDocument* doc) {
    Command* cmd;
    CanvasResizeCommandData* data;
    GList* iter;
    ImageLayer* layer;
    LayerOffsetPair* pair;

    if (!doc) {
        return NULL;
    }

    /* Create command data (same structure as canvas resize) */
    data = (CanvasResizeCommandData*)g_malloc(sizeof(CanvasResizeCommandData));
    data->old_width = old_width;
    data->old_height = old_height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->old_resolution = old_resolution;
    data->new_resolution = new_resolution;
    data->offset_x = offset_x;
    data->offset_y = offset_y;
    data->layer_offsets = NULL;

    /* Store old offsets for all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (layer) {
            pair = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
            pair->layer = layer;
            pair->old_offset_x = layer->offset_x;
            pair->old_offset_y = layer->offset_y;
            data->layer_offsets = g_list_append(data->layer_offsets, pair);
        }
    }

    /* Create command with specific name but reuse canvas resize callbacks */
    cmd = command_new(command_get_name_string(CMD_NAME_FIT_ACTIVE_LAYER),
                      COMMAND_CANVAS_RESIZE,
                      canvas_resize_command_apply,
                      canvas_resize_command_revert,
                      canvas_resize_command_destroy);

    if (!cmd) {
        /* Free layer offset pairs */
        if (data->layer_offsets) {
            for (iter = data->layer_offsets; iter; iter = iter->next) {
                pair = (LayerOffsetPair*)iter->data;
                if (pair) {
                    g_free(pair);
                }
            }
            g_list_free(data->layer_offsets);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a fit canvas to all layers command
 */
Command* command_create_fit_all_layers(guint old_width, guint old_height,
                                       guint new_width, guint new_height,
                                       gdouble old_resolution, gdouble new_resolution,
                                       gint offset_x, gint offset_y,
                                       struct ImageDocument* doc) {
    Command* cmd;
    CanvasResizeCommandData* data;
    GList* iter;
    ImageLayer* layer;
    LayerOffsetPair* pair;

    if (!doc) {
        return NULL;
    }

    /* Create command data (same structure as canvas resize) */
    data = (CanvasResizeCommandData*)g_malloc(sizeof(CanvasResizeCommandData));
    data->old_width = old_width;
    data->old_height = old_height;
    data->new_width = new_width;
    data->new_height = new_height;
    data->old_resolution = old_resolution;
    data->new_resolution = new_resolution;
    data->offset_x = offset_x;
    data->offset_y = offset_y;
    data->layer_offsets = NULL;

    /* Store old offsets for all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (layer) {
            pair = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
            pair->layer = layer;
            pair->old_offset_x = layer->offset_x;
            pair->old_offset_y = layer->offset_y;
            data->layer_offsets = g_list_append(data->layer_offsets, pair);
        }
    }

    /* Create command with specific name but reuse canvas resize callbacks */
    cmd = command_new(command_get_name_string(CMD_NAME_FIT_ALL_LAYERS),
                      COMMAND_CANVAS_RESIZE,
                      canvas_resize_command_apply,
                      canvas_resize_command_revert,
                      canvas_resize_command_destroy);

    if (!cmd) {
        /* Free layer offset pairs */
        if (data->layer_offsets) {
            for (iter = data->layer_offsets; iter; iter = iter->next) {
                pair = (LayerOffsetPair*)iter->data;
                if (pair) {
                    g_free(pair);
                }
            }
            g_list_free(data->layer_offsets);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Merge visible command apply callback (merge visible layers)
 */
static void merge_visible_command_apply(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    if (!data->merged_layer || !data->merged_layer->surface) {
        return;
    }

    /* Composite visible layers into merged layer */
    composite_layers_to_surface(data->merged_layer->surface, doc, TRUE);

    /* Delete all visible layers except the merged layer */
    iter = doc->layers;
    while (iter) {
        layer = (struct ImageLayer*)iter->data;
        GList* next = iter->next;

        if (layer && layer != data->merged_layer && layer->visible && layer->opacity > 0.0) {
            /* Remove from document */
            doc->layers = g_list_remove(doc->layers, layer);

            /* Update selected layer if needed */
            if (doc->selected_layer == layer) {
                doc->selected_layer = data->merged_layer;
            }

            /* Free the layer */
            layer_free(layer);
        }

        iter = next;
    }

    /* Add merged layer to document at the position where first visible layer was */
    if (!g_list_find(doc->layers, data->merged_layer)) {
        /* Recalculate position after deletions - count non-visible layers before original position */
        gint insert_pos = 0;
        GList* current = doc->layers;
        gint pos = 0;
        while (current && pos < data->merged_layer_position) {
            layer = (struct ImageLayer*)current->data;
            if (layer && (!layer->visible || layer->opacity <= 0.0)) {
                insert_pos++;
            }
            current = current->next;
            pos++;
        }

        GList* insert_point = g_list_nth(doc->layers, insert_pos);
        if (insert_point) {
            doc->layers = g_list_insert_before(doc->layers, insert_point, data->merged_layer);
        } else {
            doc->layers = g_list_append(doc->layers, data->merged_layer);
        }
        doc->selected_layer = data->merged_layer;
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Merge visible command revert callback (restore deleted layers)
 */
static void merge_visible_command_revert(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* info_iter;
    MergedLayerInfo* info;
    struct ImageLayer* restored_layer;
    cairo_t* cr;
    GList* insert_point;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    /* Remove merged layer from document */
    if (data->merged_layer && g_list_find(doc->layers, data->merged_layer)) {
        doc->layers = g_list_remove(doc->layers, data->merged_layer);

        /* Update selected layer if needed */
        if (doc->selected_layer == data->merged_layer) {
            if (doc->layers && doc->layers->data) {
                doc->selected_layer = (struct ImageLayer*)doc->layers->data;
            } else {
                doc->selected_layer = NULL;
            }
        }
    }

    /* Restore deleted layers from stored metadata and snapshots */
    if (data->layer_infos) {
        /* Sort by position (descending) to insert from back to front */
        GList* sorted_infos = NULL;
        for (info_iter = data->layer_infos; info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (info) {
                /* Insert in position order (highest position first) */
                GList* insert = sorted_infos;
                GList* prev = NULL;
                while (insert) {
                    MergedLayerInfo* existing = (MergedLayerInfo*)insert->data;
                    if (existing && existing->position < info->position) {
                        break;
                    }
                    prev = insert;
                    insert = insert->next;
                }
                if (prev) {
                    sorted_infos = g_list_insert_before(sorted_infos, insert, info);
                } else {
                    sorted_infos = g_list_prepend(sorted_infos, info);
                }
            }
        }

        /* Restore layers in reverse position order (from highest to lowest)
         * This way, each insertion doesn't affect subsequent insertions */
        for (info_iter = sorted_infos; info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (!info || !info->snapshot) {
                continue;
            }

            /* Recreate layer */
            restored_layer = layer_new(info->layer_name, info->width, info->height, TRUE,
                                       LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
            if (!restored_layer) {
                continue;
            }

            /* Restore content from snapshot */
            cr = cairo_create(restored_layer->surface);
            cairo_set_source_surface(cr, info->snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Flush the restored layer surface */
            if (restored_layer->surface) {
                cairo_surface_flush(restored_layer->surface);
            }

            /* Restore properties */
            restored_layer->opacity = info->opacity;
            restored_layer->blend_mode = info->blend_mode;
            restored_layer->offset_x = info->offset_x;
            restored_layer->offset_y = info->offset_y;
            restored_layer->visible = info->visible;

            /* Invalidate layer cache */
            layer_invalidate_cache(restored_layer);

            /* Calculate insertion position.
             * After removing merged layer, layers originally after merged_layer_position
             * have shifted down by 1. Since we're inserting in descending order,
             * we can use the original position directly (adjusted for the removed merged layer). */
            gint insert_pos = info->position;
            if (info->position > data->merged_layer_position) {
                /* This layer was originally after the merged layer position,
                 * so after removing merged layer, it should be at position-1 */
                insert_pos = info->position - 1;
            }
            /* Layers originally at or before merged_layer_position use their original position */

            /* Insert at calculated position */
            insert_point = g_list_nth(doc->layers, insert_pos);
            if (insert_point) {
                doc->layers = g_list_insert_before(doc->layers, insert_point, restored_layer);
            } else {
                doc->layers = g_list_append(doc->layers, restored_layer);
            }

            /* Update info with restored layer pointer */
            info->layer = restored_layer;
        }

        g_list_free(sorted_infos);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flatten command apply callback (merge all layers into bottom)
 */
static void flatten_command_apply(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    struct ImageLayer* bottom_layer;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    if (!data->merged_layer || !data->merged_layer->surface) {
        return;
    }

    bottom_layer = data->merged_layer;

    /* Composite all layers onto bottom layer (don't clear bottom layer first) */
    composite_layers_onto_surface(bottom_layer->surface, doc, bottom_layer);

    /* Invalidate bottom layer cache so thumbnail updates */
    layer_invalidate_cache(bottom_layer);

    /* Delete all layers except the bottom layer */
    iter = doc->layers;
    while (iter) {
        layer = (struct ImageLayer*)iter->data;
        GList* next = iter->next;

        if (layer && layer != bottom_layer) {
            /* Remove from document */
            doc->layers = g_list_remove(doc->layers, layer);

            /* Update selected layer if needed */
            if (doc->selected_layer == layer) {
                doc->selected_layer = bottom_layer;
            }

            /* Free the layer */
            layer_free(layer);
        }

        iter = next;
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Flatten command revert callback (restore deleted layers)
 */
static void flatten_command_revert(Command* cmd, struct ImageDocument* doc) {
    MergeCommandData* data;
    GList* info_iter;
    MergedLayerInfo* info;
    struct ImageLayer* restored_layer;
    cairo_t* cr;
    GList* insert_point;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    /* Restore bottom layer from first layer info (if it exists) */
    if (data->merged_layer && data->layer_infos) {
        info = (MergedLayerInfo*)g_list_nth_data(data->layer_infos, 0);
        if (info && info->snapshot && data->merged_layer->surface) {
            cr = cairo_create(data->merged_layer->surface);
            cairo_set_source_surface(cr, info->snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Restore bottom layer properties */
            if (info) {
                data->merged_layer->opacity = info->opacity;
                data->merged_layer->blend_mode = info->blend_mode;
                data->merged_layer->offset_x = info->offset_x;
                data->merged_layer->offset_y = info->offset_y;
                data->merged_layer->visible = info->visible;
            }

            layer_invalidate_cache(data->merged_layer);
        }
    }

    /* Restore deleted layers from layer infos (skip first which is bottom layer) */
    if (data->layer_infos) {
        /* Sort by position (descending) to insert from back to front, so positions don't shift */
        GList* sorted_infos = NULL;
        for (info_iter = g_list_next(data->layer_infos); info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (info) {
                /* Insert in position order (highest position first) */
                GList* insert = sorted_infos;
                GList* prev = NULL;
                while (insert) {
                    MergedLayerInfo* existing = (MergedLayerInfo*)insert->data;
                    if (existing && existing->position < info->position) {
                        break;
                    }
                    prev = insert;
                    insert = insert->next;
                }
                if (prev) {
                    sorted_infos = g_list_insert_before(sorted_infos, insert, info);
                } else {
                    sorted_infos = g_list_prepend(sorted_infos, info);
                }
            }
        }

        /* Restore layers in position order */
        for (info_iter = sorted_infos; info_iter; info_iter = info_iter->next) {
            info = (MergedLayerInfo*)info_iter->data;
            if (!info || !info->snapshot) {
                continue;
            }

            /* Recreate layer */
            restored_layer = layer_new(info->layer_name, info->width, info->height, TRUE,
                                       LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
            if (!restored_layer) {
                continue;
            }

            /* Restore content from snapshot */
            cr = cairo_create(restored_layer->surface);
            cairo_set_source_surface(cr, info->snapshot, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);

            /* Flush the restored layer surface */
            if (restored_layer->surface) {
                cairo_surface_flush(restored_layer->surface);
            }

            /* Restore properties */
            restored_layer->opacity = info->opacity;
            restored_layer->blend_mode = info->blend_mode;
            restored_layer->offset_x = info->offset_x;
            restored_layer->offset_y = info->offset_y;
            restored_layer->visible = info->visible;

            /* Invalidate layer cache */
            layer_invalidate_cache(restored_layer);

            /* Insert at original position */
            insert_point = g_list_nth(doc->layers, info->position);
            if (insert_point) {
                doc->layers = g_list_insert_before(doc->layers, insert_point, restored_layer);
            } else {
                doc->layers = g_list_append(doc->layers, restored_layer);
            }

            /* Update info with restored layer pointer */
            info->layer = restored_layer;
        }

        g_list_free(sorted_infos);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);
}

/**
 * Merge command destroy callback
 */
static void merge_command_destroy(Command* cmd) {
    MergeCommandData* data;
    GList* iter;
    MergedLayerInfo* info;

    if (!cmd || !cmd->user_data) {
        return;
    }

    data = (MergeCommandData*)cmd->user_data;

    /* Free all layer info structures */
    if (data->layer_infos) {
        for (iter = data->layer_infos; iter; iter = iter->next) {
            info = (MergedLayerInfo*)iter->data;
            if (info) {
                if (info->snapshot) {
                    cairo_surface_destroy(info->snapshot);
                }
                if (info->layer_name) {
                    g_free(info->layer_name);
                }
                /* Free layer if it's not in the document
                 * IMPORTANT: If doc->layers is NULL, the document is being freed and
                 * document_free() will handle freeing all layers. Don't free here to avoid double-free. */
                if (info->layer) {
                    if (!data->doc) {
                        /* Document pointer is NULL - document was already freed, free the layer */
                        layer_free(info->layer);
                    } else if (!data->doc->layers) {
                        /* Document is being freed (layers list is NULL) - DON'T free the layer here.
                         * document_free() will free all layers. Freeing here would cause double-free. */
                    } else {
                        /* Document still exists - only free if layer is not in the list */
                        GList* found = g_list_find(data->doc->layers, info->layer);
                        if (!found) {
                            layer_free(info->layer);
                        }
                    }
                }
                g_free(info);
            }
        }
        g_list_free(data->layer_infos);
    }

    /* Free merged layer if it's not in the document
     * IMPORTANT: If doc->layers is NULL, the document is being freed and
     * document_free() will handle freeing all layers. Don't free here to avoid double-free. */
    if (data->merged_layer) {
        if (!data->doc) {
            /* Document pointer is NULL - document was already freed, free the layer */
            layer_free(data->merged_layer);
        } else if (!data->doc->layers) {
            /* Document is being freed (layers list is NULL) - DON'T free the layer here.
             * document_free() will free all layers. Freeing here would cause double-free. */
        } else {
            /* Document still exists - only free if layer is not in the list */
            GList* found = g_list_find(data->doc->layers, data->merged_layer);
            if (!found) {
                layer_free(data->merged_layer);
            }
        }
    }

    g_free(data);
}

/**
 * Create a merge visible layers command
 */
Command* command_create_merge_visible(struct ImageDocument* doc) {
    Command* cmd;
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    struct ImageLayer* merged_layer;
    cairo_surface_t* snapshot;
    gint visible_count = 0;
    gint merged_position = 0;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Count visible layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->visible && layer->opacity > 0.0) {
            visible_count++;
        }
    }

    if (visible_count == 0) {
        g_warning("No visible layers to merge");
        return NULL;
    }

    if (visible_count == 1) {
        g_warning("Only one visible layer, nothing to merge");
        return NULL;
    }

    /* Create command data */
    data = (MergeCommandData*)g_malloc(sizeof(MergeCommandData));
    data->doc = doc;
    data->merged_layer = NULL;
    data->layer_infos = NULL;
    data->merged_layer_position = 0;

    /* Create layer info structures for visible layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer && layer->visible && layer->opacity > 0.0) {
            gint position = g_list_position(doc->layers, iter);
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                MergedLayerInfo* info = (MergedLayerInfo*)g_malloc(sizeof(MergedLayerInfo));
                info->layer = layer;
                info->position = position;
                info->layer_name = g_strdup(layer->name);
                info->width = layer->width;
                info->height = layer->height;
                info->snapshot = snapshot;
                info->opacity = layer->opacity;
                info->blend_mode = layer->blend_mode;
                info->offset_x = layer->offset_x;
                info->offset_y = layer->offset_y;
                info->visible = layer->visible;

                data->layer_infos = g_list_append(data->layer_infos, info);
            }
            if (merged_position == 0) {
                merged_position = position;
            }
        }
    }

    if (!data->layer_infos || g_list_length(data->layer_infos) == 0) {
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    data->merged_layer_position = merged_position;

    /* Create new merged layer */
    merged_layer = layer_new("Merged layers", doc->width, doc->height, TRUE,
                             LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL, doc);
    if (!merged_layer) {
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    data->merged_layer = merged_layer;

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_MERGE_VISIBLE),
                      COMMAND_LAYER_EDIT,
                      merge_visible_command_apply,
                      merge_visible_command_revert,
                      merge_command_destroy);

    if (!cmd) {
        /* Free merged layer */
        layer_free(merged_layer);
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a flatten image command
 */
Command* command_create_flatten(struct ImageDocument* doc) {
    Command* cmd;
    MergeCommandData* data;
    GList* iter;
    struct ImageLayer* layer;
    struct ImageLayer* bottom_layer;
    cairo_surface_t* snapshot;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    if (g_list_length(doc->layers) == 1) {
        g_warning("Only one layer, nothing to flatten");
        return NULL;
    }

    /* Get bottom layer */
    bottom_layer = (struct ImageLayer*)g_list_nth_data(doc->layers, 0);
    if (!bottom_layer) {
        return NULL;
    }

    /* Create command data */
    data = (MergeCommandData*)g_malloc(sizeof(MergeCommandData));
    data->doc = doc;
    data->merged_layer = bottom_layer;
    data->layer_infos = NULL;
    data->merged_layer_position = 0;

    /* Create layer info for bottom layer (first in list) */
    snapshot = cairo_surface_snapshot(bottom_layer->surface);
    if (snapshot) {
        MergedLayerInfo* info = (MergedLayerInfo*)g_malloc(sizeof(MergedLayerInfo));
        info->layer = bottom_layer;
        info->position = 0;
        info->layer_name = g_strdup(bottom_layer->name);
        info->width = bottom_layer->width;
        info->height = bottom_layer->height;
        info->snapshot = snapshot;
        info->opacity = bottom_layer->opacity;
        info->blend_mode = bottom_layer->blend_mode;
        info->offset_x = bottom_layer->offset_x;
        info->offset_y = bottom_layer->offset_y;
        info->visible = bottom_layer->visible;

        data->layer_infos = g_list_append(data->layer_infos, info);
    }

    /* Create layer info structures for all other layers */
    for (iter = g_list_next(doc->layers); iter; iter = iter->next) {
        layer = (struct ImageLayer*)iter->data;
        if (layer) {
            gint position = g_list_position(doc->layers, iter);
            snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                MergedLayerInfo* info = (MergedLayerInfo*)g_malloc(sizeof(MergedLayerInfo));
                info->layer = layer;
                info->position = position;
                info->layer_name = g_strdup(layer->name);
                info->width = layer->width;
                info->height = layer->height;
                info->snapshot = snapshot;
                info->opacity = layer->opacity;
                info->blend_mode = layer->blend_mode;
                info->offset_x = layer->offset_x;
                info->offset_y = layer->offset_y;
                info->visible = layer->visible;

                data->layer_infos = g_list_append(data->layer_infos, info);
            }
        }
    }

    if (!data->layer_infos || g_list_length(data->layer_infos) < 2) {
        /* Need at least bottom layer + one other layer */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_FLATTEN),
                      COMMAND_LAYER_EDIT,
                      flatten_command_apply,
                      flatten_command_revert,
                      merge_command_destroy);

    if (!cmd) {
        /* Free layer infos */
        if (data->layer_infos) {
            for (iter = data->layer_infos; iter; iter = iter->next) {
                MergedLayerInfo* info = (MergedLayerInfo*)iter->data;
                if (info) {
                    if (info->snapshot) {
                        cairo_surface_destroy(info->snapshot);
                    }
                    if (info->layer_name) {
                        g_free(info->layer_name);
                    }
                    g_free(info);
                }
            }
            g_list_free(data->layer_infos);
        }
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/* ============================================================
 * Crop Commands (Crop to Selection, Trim Borders)
 * ============================================================ */

/**
 * Free crop command data
 */
static void crop_command_data_free(CropCommandData* data) {
    GList* iter;

    if (!data) {
        return;
    }

    /* Free layer snapshots */
    if (data->layer_snapshots) {
        for (iter = data->layer_snapshots; iter; iter = iter->next) {
            cairo_surface_t* snapshot = (cairo_surface_t*)iter->data;
            if (snapshot) {
                cairo_surface_destroy(snapshot);
            }
        }
        g_list_free(data->layer_snapshots);
    }

    /* Free layers list (don't free the layers themselves) */
    if (data->layers) {
        g_list_free(data->layers);
    }

    /* Free layer offsets */
    if (data->layer_offsets) {
        for (iter = data->layer_offsets; iter; iter = iter->next) {
            LayerOffsetPair* pair = (LayerOffsetPair*)iter->data;
            if (pair) {
                g_free(pair);
            }
        }
        g_list_free(data->layer_offsets);
    }

    g_free(data);
}

/**
 * Crop command apply callback
 * Destructive: crops all layers by creating new surfaces with cropped content.
 * Non-destructive: only updates document size and layer offsets (pixels hidden, not removed).
 */
static void crop_command_apply(Command* cmd, struct ImageDocument* doc) {
    CropCommandData* data;
    GList* iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (CropCommandData*)cmd->user_data;

    if (!data->delete_pixels) {
        /* Non-destructive: change doc size and layer offsets only; layer surfaces unchanged */
        doc->width = data->new_width;
        doc->height = data->new_height;
        iter = data->layer_offsets;
        while (iter) {
            LayerOffsetPair* pair = (LayerOffsetPair*)iter->data;
            if (pair && pair->layer) {
                pair->layer->offset_x = pair->old_offset_x - data->crop_x;
                pair->layer->offset_y = pair->old_offset_y - data->crop_y;
                layer_invalidate_cache(pair->layer);
            }
            iter = iter->next;
        }
        goto crop_apply_common;
    }

    /* Destructive: crop each layer */
    for (iter = data->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface) {
            continue;
        }

        /* Calculate the intersection of layer bounds with crop region */
        gint layer_left = layer->offset_x;
        gint layer_top = layer->offset_y;
        gint layer_right = layer->offset_x + (gint)layer->width;
        gint layer_bottom = layer->offset_y + (gint)layer->height;

        gint crop_left = data->crop_x;
        gint crop_top = data->crop_y;
        gint crop_right = data->crop_x + (gint)data->new_width;
        gint crop_bottom = data->crop_y + (gint)data->new_height;

        /* Find intersection */
        gint intersect_left = MAX(layer_left, crop_left);
        gint intersect_top = MAX(layer_top, crop_top);
        gint intersect_right = MIN(layer_right, crop_right);
        gint intersect_bottom = MIN(layer_bottom, crop_bottom);

        if (intersect_left >= intersect_right || intersect_top >= intersect_bottom) {
            /* Layer doesn't intersect crop region - create empty 1x1 layer */
            cairo_surface_t* new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            if (new_surface) {
                cairo_surface_destroy(layer->surface);
                layer->surface = new_surface;
                layer->width = 1;
                layer->height = 1;
                layer->offset_x = 0;
                layer->offset_y = 0;
                layer_invalidate_cache(layer);
            }
            continue;
        }

        /* Calculate new layer dimensions and offsets */
        gint new_width = intersect_right - intersect_left;
        gint new_height = intersect_bottom - intersect_top;
        gint src_x = intersect_left - layer_left;       /* Source x in layer coords */
        gint src_y = intersect_top - layer_top;         /* Source y in layer coords */
        gint new_offset_x = intersect_left - crop_left; /* New offset in cropped canvas coords */
        gint new_offset_y = intersect_top - crop_top;

        /* Create new cropped surface */
        cairo_surface_t* new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                                  new_width, new_height);
        if (!new_surface) {
            g_warning("Failed to create cropped surface for layer: %s", layer->name);
            continue;
        }

        /* Copy cropped region from old surface */
        cairo_t* cr = cairo_create(new_surface);
        cairo_set_source_surface(cr, layer->surface, -src_x, -src_y);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);

        /* Replace layer surface */
        cairo_surface_destroy(layer->surface);
        layer->surface = new_surface;
        layer->width = (guint)new_width;
        layer->height = (guint)new_height;
        layer->offset_x = new_offset_x;
        layer->offset_y = new_offset_y;
        layer_invalidate_cache(layer);
    }

    /* Update document dimensions (destructive path only; non-destructive already set them) */
    doc->width = data->new_width;
    doc->height = data->new_height;

crop_apply_common:
    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->new_width, data->new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after crop");
    }

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Clear selection after crop */
    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
        doc->selection_mask = selection_mask_new(data->new_width, data->new_height);
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);
}

/**
 * Crop command revert callback
 * Destructive: restores layers from snapshots. Non-destructive: restores doc size and layer offsets only.
 */
static void crop_command_revert(Command* cmd, struct ImageDocument* doc) {
    CropCommandData* data;
    GList *layer_iter, *snapshot_iter, *offset_iter;

    if (!cmd || !cmd->user_data || !doc) {
        return;
    }

    data = (CropCommandData*)cmd->user_data;

    if (!data->delete_pixels) {
        /* Non-destructive: restore document dimensions and layer offsets only */
        for (offset_iter = data->layer_offsets; offset_iter; offset_iter = offset_iter->next) {
            LayerOffsetPair* pair = (LayerOffsetPair*)offset_iter->data;
            if (pair && pair->layer) {
                pair->layer->offset_x = pair->old_offset_x;
                pair->layer->offset_y = pair->old_offset_y;
                layer_invalidate_cache(pair->layer);
            }
        }
        doc->width = data->old_width;
        doc->height = data->old_height;
        goto crop_revert_common;
    }

    /* Restore all layers from snapshots (destructive) */
    layer_iter = data->layers;
    snapshot_iter = data->layer_snapshots;
    offset_iter = data->layer_offsets;
    while (layer_iter && snapshot_iter && offset_iter) {
        struct ImageLayer* layer = (struct ImageLayer*)layer_iter->data;
        cairo_surface_t* snapshot = (cairo_surface_t*)snapshot_iter->data;
        LayerOffsetPair* offset = (LayerOffsetPair*)offset_iter->data;

        if (layer && snapshot) {
            gint old_width = cairo_image_surface_get_width(snapshot);
            gint old_height = cairo_image_surface_get_height(snapshot);

            /* Destroy current surface and create new one with original dimensions */
            if (layer->surface) {
                cairo_surface_destroy(layer->surface);
            }
            layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, old_width, old_height);
            if (layer->surface) {
                /* Restore layer from snapshot */
                cairo_t* cr = cairo_create(layer->surface);
                cairo_set_source_surface(cr, snapshot, 0, 0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_destroy(cr);

                /* Update layer dimensions */
                layer->width = (guint)old_width;
                layer->height = (guint)old_height;

                /* Restore offset */
                if (offset) {
                    layer->offset_x = offset->old_offset_x;
                    layer->offset_y = offset->old_offset_y;
                }

                layer_invalidate_cache(layer);
            }
        }

        layer_iter = layer_iter->next;
        snapshot_iter = snapshot_iter->next;
        offset_iter = offset_iter->next;
    }

    /* Restore document dimensions (destructive path; non-destructive already restored above) */
    doc->width = data->old_width;
    doc->height = data->old_height;

crop_revert_common:
    /* Recreate tile grid with old dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(data->old_width, data->old_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after crop revert");
    }

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate selection mask with original dimensions */
    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
        doc->selection_mask = selection_mask_new(data->old_width, data->old_height);
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);
}

/**
 * Crop command destroy callback
 */
static void crop_command_destroy(Command* cmd) {
    if (!cmd || !cmd->user_data) {
        return;
    }
    crop_command_data_free((CropCommandData*)cmd->user_data);
}

/**
 * Helper: Calculate selection bounds from selection mask
 * Returns TRUE if there is a selection, FALSE if empty
 */
static gboolean get_selection_bounds(SelectionMask* mask,
                                     gint* out_x, gint* out_y,
                                     gint* out_width, gint* out_height) {
    if (!mask || !mask->base_mask || selection_mask_is_empty(mask)) {
        return FALSE;
    }

    gint min_x = mask->width;
    gint min_y = mask->height;
    gint max_x = -1;
    gint max_y = -1;

    /* Scan mask to find bounds of selected pixels */
    for (gint y = 0; y < mask->height; y++) {
        uint8_t* row = mask->base_mask + y * mask->stride;
        for (gint x = 0; x < mask->width; x++) {
            if (row[x] > 0) {
                if (x < min_x)
                    min_x = x;
                if (y < min_y)
                    min_y = y;
                if (x > max_x)
                    max_x = x;
                if (y > max_y)
                    max_y = y;
            }
        }
    }

    if (max_x < min_x || max_y < min_y) {
        return FALSE;
    }

    *out_x = min_x;
    *out_y = min_y;
    *out_width = max_x - min_x + 1;
    *out_height = max_y - min_y + 1;
    return TRUE;
}

/**
 * Create a crop to selection command
 */
Command* command_create_crop_to_selection(struct ImageDocument* doc) {
    Command* cmd;
    CropCommandData* data;
    GList* iter;
    gint sel_x, sel_y, sel_width, sel_height;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Get selection bounds */
    if (!doc->selection_mask || selection_mask_is_empty(doc->selection_mask)) {
        g_warning("No selection to crop to");
        return NULL;
    }

    if (!get_selection_bounds(doc->selection_mask, &sel_x, &sel_y, &sel_width, &sel_height)) {
        g_warning("Failed to get selection bounds");
        return NULL;
    }

    if (sel_width <= 0 || sel_height <= 0) {
        g_warning("Selection has invalid dimensions");
        return NULL;
    }

    /* Create command data */
    data = (CropCommandData*)g_malloc0(sizeof(CropCommandData));
    data->doc = doc;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->new_width = (guint)sel_width;
    data->new_height = (guint)sel_height;
    data->crop_x = sel_x;
    data->crop_y = sel_y;
    data->layer_snapshots = NULL;
    data->layers = NULL;
    data->layer_offsets = NULL;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            cairo_surface_t* snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);

                LayerOffsetPair* offset = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
                offset->layer = layer;
                offset->old_offset_x = layer->offset_x;
                offset->old_offset_y = layer->offset_y;
                data->layer_offsets = g_list_append(data->layer_offsets, offset);
            }
        }
    }

    if (!data->layers) {
        crop_command_data_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_CROP_TO_SELECTION),
                      COMMAND_CANVAS_RESIZE,
                      crop_command_apply,
                      crop_command_revert,
                      crop_command_destroy);

    if (!cmd) {
        crop_command_data_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Create a crop to rect command
 * Crops all layers and canvas to the given rectangle
 * @param grow_canvas If TRUE, crop rect may extend beyond image (canvas grows)
 * @param delete_pixels If TRUE, remove pixels; if FALSE, non-destructive (doc size + offsets only)
 */
Command* command_create_crop_to_rect(struct ImageDocument* doc,
                                     gint x, gint y, guint w, guint h,
                                     gboolean grow_canvas, gboolean delete_pixels) {
    Command* cmd;
    CropCommandData* data;
    GList* iter;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    if (w <= 0 || h <= 0) {
        g_warning("Invalid crop dimensions");
        return NULL;
    }

    if (!grow_canvas) {
        /* Clamp rect to document bounds */
        if (x < 0) {
            w = (guint)((gint)w + x);
            x = 0;
        }
        if (y < 0) {
            h = (guint)((gint)h + y);
            y = 0;
        }
        if (x + (gint)w > (gint)doc->width) {
            w = (guint)(doc->width - x);
        }
        if (y + (gint)h > (gint)doc->height) {
            h = (guint)(doc->height - y);
        }
        if (w <= 0 || h <= 0) {
            g_warning("Crop rect outside document bounds");
            return NULL;
        }
    }

    /* Create command data */
    data = (CropCommandData*)g_malloc0(sizeof(CropCommandData));
    data->doc = doc;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->new_width = w;
    data->new_height = h;
    data->crop_x = x;
    data->crop_y = y;
    data->grow_canvas = grow_canvas ? TRUE : FALSE;
    data->delete_pixels = delete_pixels ? TRUE : FALSE;
    data->layer_snapshots = NULL;
    data->layers = NULL;
    data->layer_offsets = NULL;

    /* Build layers and layer_offsets; snapshots only when destructive */
    for (iter = doc->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            if (delete_pixels) {
                cairo_surface_t* snapshot = cairo_surface_snapshot(layer->surface);
                if (snapshot) {
                    data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                }
            }
            data->layers = g_list_append(data->layers, layer);
            {
                LayerOffsetPair* offset = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
                offset->layer = layer;
                offset->old_offset_x = layer->offset_x;
                offset->old_offset_y = layer->offset_y;
                data->layer_offsets = g_list_append(data->layer_offsets, offset);
            }
        }
    }

    if (!data->layers) {
        crop_command_data_free(data);
        return NULL;
    }

    if (delete_pixels && data->layer_snapshots && g_list_length(data->layer_snapshots) != g_list_length(data->layers)) {
        crop_command_data_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_CROP_TOOL),
                      COMMAND_CANVAS_RESIZE,
                      crop_command_apply,
                      crop_command_revert,
                      crop_command_destroy);

    if (!cmd) {
        crop_command_data_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}

/**
 * Helper: Calculate non-transparent bounds from composited image
 * Returns TRUE if there are non-transparent pixels, FALSE if empty
 */
static gboolean get_content_bounds(struct ImageDocument* doc,
                                   gint* out_x, gint* out_y,
                                   gint* out_width, gint* out_height) {
    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return FALSE;
    }

    gint min_x = (gint)doc->width;
    gint min_y = (gint)doc->height;
    gint max_x = -1;
    gint max_y = -1;

    /* Iterate through all layers and find content bounds */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (!layer || !layer->surface || !layer->visible) {
            continue;
        }

        cairo_surface_flush(layer->surface);
        guchar* surface_data = cairo_image_surface_get_data(layer->surface);
        gint stride = cairo_image_surface_get_stride(layer->surface);

        if (!surface_data) {
            continue;
        }

        /* Scan layer surface for non-transparent pixels */
        for (gint y = 0; y < (gint)layer->height; y++) {
            guint32* row = (guint32*)(surface_data + y * stride);
            for (gint x = 0; x < (gint)layer->width; x++) {
                /* Check alpha channel (high byte in ARGB32) */
                guchar alpha = (row[x] >> 24) & 0xFF;
                if (alpha > 0) {
                    gint doc_x = layer->offset_x + x;
                    gint doc_y = layer->offset_y + y;

                    /* Only consider pixels within document bounds */
                    if (doc_x >= 0 && doc_x < (gint)doc->width &&
                        doc_y >= 0 && doc_y < (gint)doc->height) {
                        if (doc_x < min_x)
                            min_x = doc_x;
                        if (doc_y < min_y)
                            min_y = doc_y;
                        if (doc_x > max_x)
                            max_x = doc_x;
                        if (doc_y > max_y)
                            max_y = doc_y;
                    }
                }
            }
        }
    }

    if (max_x < min_x || max_y < min_y) {
        return FALSE;
    }

    *out_x = min_x;
    *out_y = min_y;
    *out_width = max_x - min_x + 1;
    *out_height = max_y - min_y + 1;
    return TRUE;
}

/**
 * Create a trim borders command
 */
Command* command_create_trim_borders(struct ImageDocument* doc) {
    Command* cmd;
    CropCommandData* data;
    GList* iter;
    gint content_x, content_y, content_width, content_height;

    if (!doc || !doc->layers || g_list_length(doc->layers) == 0) {
        return NULL;
    }

    /* Get content bounds (non-transparent area) */
    if (!get_content_bounds(doc, &content_x, &content_y, &content_width, &content_height)) {
        g_warning("No non-transparent content to trim to");
        return NULL;
    }

    if (content_width <= 0 || content_height <= 0) {
        g_warning("Content has invalid dimensions");
        return NULL;
    }

    /* Check if already trimmed */
    if (content_x == 0 && content_y == 0 &&
        (guint)content_width == doc->width && (guint)content_height == doc->height) {
        g_warning("Image already trimmed - no transparent borders to remove");
        return NULL;
    }

    /* Create command data */
    data = (CropCommandData*)g_malloc0(sizeof(CropCommandData));
    data->doc = doc;
    data->old_width = doc->width;
    data->old_height = doc->height;
    data->new_width = (guint)content_width;
    data->new_height = (guint)content_height;
    data->crop_x = content_x;
    data->crop_y = content_y;
    data->layer_snapshots = NULL;
    data->layers = NULL;
    data->layer_offsets = NULL;

    /* Create snapshots of all layers */
    for (iter = doc->layers; iter; iter = iter->next) {
        struct ImageLayer* layer = (struct ImageLayer*)iter->data;
        if (layer && layer->surface) {
            cairo_surface_t* snapshot = cairo_surface_snapshot(layer->surface);
            if (snapshot) {
                data->layer_snapshots = g_list_append(data->layer_snapshots, snapshot);
                data->layers = g_list_append(data->layers, layer);

                LayerOffsetPair* offset = (LayerOffsetPair*)g_malloc(sizeof(LayerOffsetPair));
                offset->layer = layer;
                offset->old_offset_x = layer->offset_x;
                offset->old_offset_y = layer->offset_y;
                data->layer_offsets = g_list_append(data->layer_offsets, offset);
            }
        }
    }

    if (!data->layers) {
        crop_command_data_free(data);
        return NULL;
    }

    /* Create command */
    cmd = command_new(command_get_name_string(CMD_NAME_TRIM_BORDERS),
                      COMMAND_CANVAS_RESIZE,
                      crop_command_apply,
                      crop_command_revert,
                      crop_command_destroy);

    if (!cmd) {
        crop_command_data_free(data);
        return NULL;
    }

    cmd->user_data = data;

    return cmd;
}