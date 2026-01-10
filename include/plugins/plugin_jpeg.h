#ifndef PLUGIN_JPEG_H
#define PLUGIN_JPEG_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize JPEG plugin using libjpeg
 */
bool plugin_init_jpeg(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_JPEG_H */
