#ifndef PLUGIN_BMP_H
#define PLUGIN_BMP_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize BMP plugin
 */
bool plugin_init_bmp(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_BMP_H */
