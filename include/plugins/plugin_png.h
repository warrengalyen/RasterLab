#ifndef PLUGIN_PNG_H
#define PLUGIN_PNG_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize PNG plugin
 */
bool plugin_init_png(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_PNG_H */
