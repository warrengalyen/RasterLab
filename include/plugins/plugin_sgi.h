#ifndef PLUGIN_SGI_H
#define PLUGIN_SGI_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize SGI (Silicon Graphics Image) plugin
 * Supports .rgb, .rgba, .sgi, .bw and related extensions.
 */
bool plugin_init_sgi(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_SGI_H */
