#ifndef PLUGIN_TIFF_H
#define PLUGIN_TIFF_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize TIFF plugin
 */
bool plugin_init_tiff(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_TIFF_H */
