#ifndef PLUGIN_DEEP_H
#define PLUGIN_DEEP_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize DEEP (TVPaint IFF DEEP) plugin
 */
bool plugin_init_deep(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_DEEP_H */
