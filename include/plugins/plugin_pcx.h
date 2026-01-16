#ifndef PLUGIN_PCX_H
#define PLUGIN_PCX_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize PCX plugin
 */
bool plugin_init_pcx(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_PCX_H */
