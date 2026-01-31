#ifndef PLUGIN_XBM_H
#define PLUGIN_XBM_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize XBM (X Bitmap) plugin
 * Supports .xbm and .h (C header) X Bitmap images.
 */
bool plugin_init_xbm(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_XBM_H */
