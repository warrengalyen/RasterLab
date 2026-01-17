#ifndef PLUGIN_RAS_H
#define PLUGIN_RAS_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize RAS (Sun Raster) plugin
 */
bool plugin_init_ras(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_RAS_H */
