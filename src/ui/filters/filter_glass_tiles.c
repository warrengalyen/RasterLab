#include "ui/filters/filter_glass_tiles.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/* Map combo index (clamp=0, reflect=1, wrap=2, erase=3, ignore=4) to OcEdgeMode */
static const OcEdgeMode GLASS_TILES_EDGE_MAP[] = {
    OC_EDGE_CLAMP,  /* 0: clamp */
    OC_EDGE_MIRROR, /* 1: reflect */
    OC_EDGE_WRAP,   /* 2: wrap */
    OC_EDGE_ERASE,  /* 3: erase */
    OC_EDGE_IGNORE  /* 4: ignore */
};
#define GLASS_TILES_EDGE_MAP_SIZE (sizeof(GLASS_TILES_EDGE_MAP) / sizeof(GLASS_TILES_EDGE_MAP[0]))

gboolean filter_glass_tiles_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    gint stride;
    OC_STATUS status;
    gfloat angle;
    gfloat size_f;
    gint tile_size;
    gfloat curvature;
    gfloat quality_f;
    gint quality;
    gint edge_index;
    OcEdgeMode edge_mode;

    if (!layer || !layer->surface || !values || num_values < 5) {
        return FALSE;
    }

    angle = values[0];
    size_f = values[1];
    tile_size = (gint)(size_f < 1.0f ? 1.0f : (size_f > 100.0f ? 100.0f : size_f));
    curvature = values[2];
    quality_f = values[3];
    quality = (gint)(quality_f < 1.0f ? 1 : (quality_f > 5.0f ? 5 : quality_f));
    edge_index = (gint)values[4];
    if (edge_index < 0 || (guint)edge_index >= GLASS_TILES_EDGE_MAP_SIZE) {
        edge_index = 1; /* default reflect */
    }
    edge_mode = GLASS_TILES_EDGE_MAP[edge_index];

    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3;
    rgb_input = (guchar*)g_malloc((size_t)(height * stride));
    rgb_output = (guchar*)g_malloc((size_t)(height * stride));

    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Glass Tiles filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Glass Tiles filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    status = ocularGlassTilesFilter(rgb_input, rgb_output, width, height, stride,
                                    angle, tile_size, curvature, quality, edge_mode);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Glass Tiles filter: Ocular returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Glass Tiles filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);
    cairo_surface_mark_dirty(surface);
    return TRUE;
}
