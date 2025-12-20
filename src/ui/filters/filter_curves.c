#include "ui/filters/filter_curves.h"
#include "filters.h"
#include "ocular.h"
#include <glib.h>
#include <string.h>

/**
 * Apply curves filter to a layer using Ocular library
 */
gboolean filter_curves_apply(ImageLayer* layer, CurvesWidget* curves) {
    cairo_surface_t* surface;
    gint width, height;
    guchar *input_data, *output_data;
    gint stride;
    OcCurve *curveR, *curveG, *curveB, *curveL;
    OC_STATUS status;
    Channel saved_channel;
    uint8_t rgb_lut[256];
    uint8_t channel_lut[256];
    gint i;

    if (!layer || !layer->surface || !curves) {
        return FALSE;
    }

    surface = layer->surface;
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);

    /* Allocate buffers */
    input_data = (guchar*)g_malloc(width * height * 4);
    output_data = (guchar*)g_malloc(width * height * 4);

    if (!input_data || !output_data) {
        g_warning("Curves filter: Failed to allocate memory");
        g_free(input_data);
        g_free(output_data);
        return FALSE;
    }

    /* Copy surface data to input buffer (ARGB32 format) */
    guchar* surface_data = cairo_image_surface_get_data(surface);
    memcpy(input_data, surface_data, width * height * 4);

    /* Create curves */
    curveR = createCurve();
    curveG = createCurve();
    curveB = createCurve();
    curveL = createCurve();

    if (!curveR || !curveG || !curveB || !curveL) {
        g_warning("Failed to create curves");
        if (curveR)
            destroyCurve(curveR);
        if (curveG)
            destroyCurve(curveG);
        if (curveB)
            destroyCurve(curveB);
        if (curveL)
            destroyCurve(curveL);
        g_free(input_data);
        g_free(output_data);
        return FALSE;
    }

    /* Save current active channel */
    saved_channel = curves->active_channel;

    /* Get lookup table for RGB curve (luminance curve) */
    curves->active_channel = CHANNEL_RGB;
    curves_widget_get_lut(curves, rgb_lut);

    /* Populate luminance curve */
    for (i = 0; i < 256; i++) {
        curveAddPoint(curveL, (uint8_t)i, rgb_lut[i]);
    }
    curveBuild(curveL);

    /* Get lookup tables from each channel and populate curves */
    for (int c = 0; c < 3; c++) {
        OcCurve* curve = (c == 0) ? curveR : (c == 1) ? curveG
                                                      : curveB;

        /* If RGB curve is active, use it for all channels (luminance adjustment) */
        if (saved_channel == CHANNEL_RGB) {
            memcpy(channel_lut, rgb_lut, 256);
        } else {
            /* Otherwise, get individual channel LUT */
            Channel channel = (c == 0) ? CHANNEL_RED : (c == 1) ? CHANNEL_GREEN
                                                                : CHANNEL_BLUE;
            curves->active_channel = channel;
            curves_widget_get_lut(curves, channel_lut);
        }

        /* Populate curve from lookup table */
        for (i = 0; i < 256; i++) {
            curveAddPoint(curve, (uint8_t)i, channel_lut[i]);
        }
        curveBuild(curve);
    }

    /* Restore the original active channel */
    curves->active_channel = saved_channel;

    /* Apply filter */
    status = ocularCurvesFilter(input_data, output_data, width, height, stride,
                                curveR, curveG, curveB, curveL);

    if (status == OC_STATUS_OK) {
        /* Copy output back to surface */
        memcpy(surface_data, output_data, width * height * 4);
        cairo_surface_mark_dirty(surface);
    } else {
        g_warning("ocularCurvesFilter failed with status %d", status);
    }

    /* Clean up */
    destroyCurve(curveR);
    destroyCurve(curveG);
    destroyCurve(curveB);
    destroyCurve(curveL);
    g_free(input_data);
    g_free(output_data);

    return (status == OC_STATUS_OK);
}
