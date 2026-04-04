#include "ui/filters/filter_marble.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include "debug_logger.h"

/* Map combo index (clamp=0, reflect=1, wrap=2, erase=3, ignore=4) to OcEdgeMode */
static const OcEdgeMode MARBLE_EDGE_MAP[] = {
    OC_EDGE_CLAMP,  /* 0: clamp */
    OC_EDGE_MIRROR, /* 1: reflect */
    OC_EDGE_WRAP,   /* 2: wrap */
    OC_EDGE_ERASE,  /* 3: erase */
    OC_EDGE_IGNORE  /* 4: ignore */
};
#define MARBLE_EDGE_MAP_SIZE (sizeof(MARBLE_EDGE_MAP) / sizeof(MARBLE_EDGE_MAP[0]))

gboolean filter_marble_apply(ImageLayer* layer, const gfloat* values, gint num_values) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgb_input;
    guchar* rgb_output;
    gint stride;
    OC_STATUS status;
    gfloat scale;
    gfloat turbulence;
    gfloat quality_f;
    gint quality;
    gint edge_index;
    OcEdgeMode edge_mode;
    gdouble seed_d;
    unsigned int seed;

    if (!layer || !layer->surface || !values || num_values < 5) {
        return FALSE;
    }

    scale = values[0];
    turbulence = values[1];
    quality_f = values[2];
    quality = (gint)(quality_f < 1.0f ? 1 : (quality_f > 5.0f ? 5 : quality_f));
    edge_index = (gint)values[3];
    if (edge_index < 0 || (guint)edge_index >= MARBLE_EDGE_MAP_SIZE) {
        edge_index = 1; /* default reflect */
    }
    edge_mode = MARBLE_EDGE_MAP[edge_index];
    seed_d = (gdouble)values[4];
    if (seed_d < 0.0) {
        seed_d = 0.0;
    } else if (seed_d > (gdouble)G_MAXUINT32) {
        seed_d = (gdouble)G_MAXUINT32;
    }
    seed = (unsigned int)seed_d;

    surface = layer->surface;

    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    stride = width * 3;
    rgb_input = (guchar*)g_malloc((size_t)(height * stride));
    rgb_output = (guchar*)g_malloc((size_t)(height * stride));

    if (!rgb_input || !rgb_output) {
        debug_log("WRN", "Marble filter: Failed to allocate memory");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_cairo_to_rgb(surface, rgb_input)) {
        debug_log("WRN", "Marble filter: Failed to convert surface to RGB");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    status = ocularMarbleFilter(rgb_input, rgb_output, width, height, stride,
                                scale, turbulence, quality, edge_mode, seed);

    if (status != OC_STATUS_OK) {
        debug_log("WRN", "Marble filter: Ocular returned error %d", status);
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    if (!adjustments_rgb_to_cairo(surface, rgb_output)) {
        debug_log("WRN", "Marble filter: Failed to convert RGB to surface");
        g_free(rgb_input);
        g_free(rgb_output);
        return FALSE;
    }

    g_free(rgb_input);
    g_free(rgb_output);
    cairo_surface_mark_dirty(surface);
    return TRUE;
}
